// Price chart with the Parabolic SAR overlay, drawn straight onto the window's
// ImDrawList. Bars are spaced by index (no weekend gaps), price axis on the
// right as in most trading platforms.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "ui.h"

static double niceStep(double range, int maxTicks) {
    if (range <= 0) return 1.0;
    double raw = range / std::max(1, maxTicks);
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 2.5, 5.0})
        if (mag * m >= raw) return mag * m;
    return mag * 10.0;
}

static const char* kMonths[] = {"ene", "feb", "mar", "abr", "may", "jun",
                                "jul", "ago", "sep", "oct", "nov", "dic"};

static int monthOf(const std::string& d) { return atoi(d.c_str() + 5); }
static int dayOf(const std::string& d) { return atoi(d.c_str() + 8); }

static void dashedHLine(ImDrawList* dl, float x0, float x1, float y, ImU32 col) {
    for (float x = x0; x < x1; x += 9.0f) dl->AddLine(ImVec2(x, y), ImVec2(std::min(x + 5, x1), y), col);
}

// value label in a filled box on the price axis
static void axisTag(ImDrawList* dl, float x, float y, const std::string& text, ImU32 bg,
                    ImU32 fg) {
    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    ImVec2 a(x, y - ts.y * 0.5f - 2), b(x + ts.x + 10, y + ts.y * 0.5f + 2);
    dl->AddRectFilled(a, b, bg, 3.0f);
    dl->AddText(ImVec2(x + 5, a.y + 2), fg, text.c_str());
}

void drawChart(App& a) {
    const auto& b = a.bars;
    const auto& s = a.sar;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (b.size() < 2) {
        ImGui::TextColored(C_DIM, fetching(a) ? "Descargando el histórico del oro..."
                                              : "Sin datos. Pulsa Actualizar para descargarlos.");
        return;
    }

    const float axisW = 84.0f, lineH = ImGui::GetTextLineHeight();
    ImVec2 pl0(p0.x + 4, p0.y + lineH + 14);  // room for the legend on top
    ImVec2 pl1(p0.x + avail.x - axisW, p0.y + avail.y - lineH - 12);
    if (pl1.x - pl0.x < 80 || pl1.y - pl0.y < 80) return;

    // visible bars (the period, or the zoomed window); `end` is the latest bar
    int first, last, end = (int)b.size() - 1;
    visibleRange(a, first, last);
    int n = last - first + 1;
    double lo = b[first].low, hi = b[first].high;
    for (int i = first; i <= last; i++) {
        lo = std::min(lo, b[i].low);
        hi = std::max(hi, b[i].high);
        if (s[i].valid) {
            lo = std::min(lo, s[i].sar);
            hi = std::max(hi, s[i].sar);
        }
    }
    double pad = (hi - lo) * 0.05 + 1e-9;
    lo -= pad;
    hi += pad;

    float slot = (pl1.x - pl0.x) / (float)n;
    auto px = [&](int i) { return pl0.x + ((float)(i - first) + 0.5f) * slot; };
    auto py = [&](double v) { return pl1.y - (float)((v - lo) / (hi - lo)) * (pl1.y - pl0.y); };
    bool candles = a.style == 1 || (a.style == 0 && slot >= 4.0f);

    ImU32 cGrid = col32(C_GRID), cDim = col32(C_DIM), cUp = col32(C_GREEN),
          cDown = col32(C_RED), cGold = col32(C_GOLD);

    // horizontal grid + price labels on the right
    int ticks = std::max(3, (int)((pl1.y - pl0.y) / 55.0f));
    double step = niceStep(hi - lo, ticks);
    int dec = step < 1 ? 2 : 0;
    for (double v = std::ceil(lo / step) * step; v <= hi; v += step) {
        float y = py(v);
        dl->AddLine(ImVec2(pl0.x, y), ImVec2(pl1.x, y), cGrid);
        dl->AddText(ImVec2(pl1.x + 8, y - lineH * 0.5f), cDim, fmtPrice(v, dec).c_str());
    }

    // vertical grid + date labels: years for long views, months for medium
    // ones, weeks for the short ones; labels that would overlap are skipped
    long span = dateToDays(b[last].date) - dateToDays(b[first].date);
    float lastEnd = -1e9f;
    for (int i = first; i <= last; i++) {
        const std::string& d = b[i].date;
        bool mark = false;
        char text[32];
        if (i == first) {
            mark = false;
        } else if (span > 3 * 365) {
            mark = d.compare(0, 4, b[i - 1].date, 0, 4) != 0;
            snprintf(text, sizeof text, "%.4s", d.c_str());
        } else if (span > 100) {
            mark = monthOf(d) != monthOf(b[i - 1].date);
            if (monthOf(d) == 1) snprintf(text, sizeof text, "%s %.4s", kMonths[0], d.c_str());
            else snprintf(text, sizeof text, "%s", kMonths[monthOf(d) - 1]);
        } else {
            mark = (dateToDays(d) + 3) / 7 != (dateToDays(b[i - 1].date) + 3) / 7;
            snprintf(text, sizeof text, "%d %s", dayOf(d), kMonths[monthOf(d) - 1]);
        }
        if (!mark) continue;
        float x = px(i) - slot * 0.5f;
        float tw = ImGui::CalcTextSize(text).x;
        if (x - tw * 0.5f < lastEnd + 16 || x + tw * 0.5f > pl1.x + axisW - 4) continue;
        dl->AddLine(ImVec2(x, pl0.y), ImVec2(x, pl1.y), cGrid);
        dl->AddText(ImVec2(x - tw * 0.5f, pl1.y + 6), cDim, text);
        lastEnd = x + tw * 0.5f;
    }

    // price series
    dl->PushClipRect(pl0, pl1, true);
    if (candles) {
        float bw = std::max(1.0f, std::floor(slot * 0.65f));
        for (int i = first; i <= last; i++) {
            const Candle& k = b[i];
            ImU32 c = k.close >= k.open ? cUp : cDown;
            float x = std::floor(px(i)) + 0.5f;
            dl->AddLine(ImVec2(x, py(k.high)), ImVec2(x, py(k.low)), c);
            float y0 = py(std::max(k.open, k.close)), y1 = py(std::min(k.open, k.close));
            if (y1 - y0 < 1.0f) y1 = y0 + 1.0f;
            dl->AddRectFilled(ImVec2(x - bw * 0.5f, y0), ImVec2(x + bw * 0.5f, y1), c);
        }
    } else {
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (int i = first; i <= last; i++) pts.emplace_back(px(i), py(b[i].close));
        dl->AddPolyline(pts.data(), (int)pts.size(), cGold, 0, 1.8f);
    }

    // SAR dots: green under the price in an uptrend, red above it in a
    // downtrend; a small triangle marks every reversal
    float r = std::clamp(slot * 0.28f, 1.3f, 3.2f);
    for (int i = first; i <= last; i++) {
        if (!s[i].valid) continue;
        ImU32 c = s[i].up ? cUp : cDown;
        ImVec2 p(px(i), py(s[i].sar));
        dl->AddCircleFilled(p, r, c, 10);
        if (s[i].reversal) {
            float t = 5.0f, x = px(i);
            if (s[i].up) {
                float y = py(b[i].low) + 8;
                dl->AddTriangleFilled(ImVec2(x, y), ImVec2(x - t, y + t * 1.4f),
                                      ImVec2(x + t, y + t * 1.4f), cUp);
            } else {
                float y = py(b[i].high) - 8;
                dl->AddTriangleFilled(ImVec2(x, y), ImVec2(x - t, y - t * 1.4f),
                                      ImVec2(x + t, y - t * 1.4f), cDown);
            }
        }
    }
    dl->PopClipRect();
    dl->AddRect(pl0, pl1, col32(C_BORDER));

    // latest close and current SAR tagged on the price axis (skipped when a
    // zoom into the past puts them outside the visible price range)
    auto inY = [&](float y) { return y >= pl0.y && y <= pl1.y; };
    float yLast = py(b[end].close), ySar = py(s[end].sar);
    if (inY(yLast)) dashedHLine(dl, pl0.x, pl1.x, yLast, col32(rgb(222, 184, 65, 0.55f)));
    if (inY(ySar) && (!inY(yLast) || std::fabs(ySar - yLast) > lineH + 4))
        axisTag(dl, pl1.x + 2, ySar, fmtPrice(s[end].sar, 0), s[end].up ? cUp : cDown,
                col32(C_DARK));
    if (inY(yLast)) axisTag(dl, pl1.x + 2, yLast, fmtPrice(b[end].close, 0), cGold, col32(C_DARK));

    // legend
    {
        const char* ivName[] = {"Velas diarias", "Velas semanales", "Velas mensuales"};
        float x = pl0.x + 2, y = p0.y + 4;
        auto item = [&](ImU32 c, const char* t, bool dot) {
            if (dot) dl->AddCircleFilled(ImVec2(x + 5, y + lineH * 0.5f), 4.0f, c);
            else dl->AddLine(ImVec2(x, y + lineH * 0.5f), ImVec2(x + 14, y + lineH * 0.5f), c, 2.0f);
            dl->AddText(ImVec2(x + 18, y), cDim, t);
            x += 18 + ImGui::CalcTextSize(t).x + 22;
        };
        item(candles ? cUp : cGold, candles ? "Precio (velas)" : "Precio (cierre)", false);
        item(cUp, "SAR alcista", true);
        item(cDown, "SAR bajista", true);
        dl->AddText(ImVec2(x, y), cDim, ivName[(int)a.effInterval]);
        x += ImGui::CalcTextSize(ivName[(int)a.effInterval]).x + 22;
        char buf[64];
        snprintf(buf, sizeof buf, "%d barras", n);
        dl->AddText(ImVec2(x, y), cDim, buf);
        x += ImGui::CalcTextSize(buf).x + 22;
        dl->AddText(ImVec2(x, y), cDim, a.eur ? "EUR/onza" : "USD/onza");
        if (!a.zoomFrom.empty()) {
            const char* z = "ZOOM  ·  doble clic o Esc para quitarlo";
            dl->AddText(ImVec2(pl1.x - ImGui::CalcTextSize(z).x, y), cGold, z);
        }
    }

    // zoom & pan. Changes apply from the next frame (main renders one extra
    // frame after every input event, so there is no visible lag).
    //   wheel: zoom around the bar under the mouse (min 10 bars, up to the
    //          whole history); drag: pan; double click: back to the period
    ImGui::InvisibleButton("##plot", avail);
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsItemHovered() && io.MouseWheel != 0) {
        float mx = std::clamp(io.MousePos.x, pl0.x, pl1.x);
        double anchor = first + (mx - pl0.x) / slot;  // fractional bar index
        double t = (anchor - first) / n;
        int newN = std::clamp((int)std::lround(n * std::pow(0.85, io.MouseWheel)), 10, end + 1);
        int nf = (int)std::lround(anchor - t * newN), nl = nf + newN - 1;
        if (nf < 0) nl -= nf, nf = 0;
        if (nl > end) nf = std::max(0, nf - (nl - end)), nl = end;
        setZoom(a, nf, nl);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        a.panAccum += io.MouseDelta.x;
        int shift = (int)(a.panAccum / slot);
        if (shift != 0) {
            a.panAccum -= shift * slot;
            int nf = first - shift, nl = last - shift;  // drag right = go back in time
            if (nf < 0) nl -= nf, nf = 0;
            if (nl > end) nf = std::max(0, nf - (nl - end)), nl = end;
            setZoom(a, nf, nl);
        }
    } else if (!ImGui::IsItemActive()) {
        a.panAccum = 0;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        resetZoom(a);

    // hover: crosshair + OHLC / SAR tooltip of the bar under the mouse
    if (ImGui::IsMouseHoveringRect(pl0, pl1) && ImGui::IsWindowHovered()) {
        ImVec2 m = io.MousePos;
        int i = std::clamp(first + (int)((m.x - pl0.x) / slot), first, last);
        float x = px(i);
        ImU32 cCross = col32(rgb(145, 155, 167, 0.6f));
        dl->AddLine(ImVec2(x, pl0.y), ImVec2(x, pl1.y), cCross);
        dl->AddLine(ImVec2(pl0.x, m.y), ImVec2(pl1.x, m.y), cCross);
        double vy = lo + (pl1.y - m.y) / (pl1.y - pl0.y) * (hi - lo);
        axisTag(dl, pl1.x + 2, m.y, fmtPrice(vy, dec), col32(C_PANEL2), col32(C_TEXT));

        const Candle& k = b[i];
        ImGui::BeginTooltip();
        const char* prefix = a.effInterval == Interval::Weekly    ? "Semana del "
                             : a.effInterval == Interval::Monthly ? "Mes desde "
                                                                  : "";
        ImGui::TextColored(C_GOLD, "%s%s", prefix, fmtDateShort(k.date).c_str());
        ImGui::Separator();
        if (ImGui::BeginTable("tt", 2)) {
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            auto row = [](const char* key, const std::string& v, const ImVec4& c) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(C_DIM, "%s", key);
                ImGui::TableNextColumn();
                ImGui::TextColored(c, "%s", v.c_str());
            };
            row("Apertura", fmtPrice(k.open), C_TEXT);
            row("Máximo", fmtPrice(k.high), C_TEXT);
            row("Mínimo", fmtPrice(k.low), C_TEXT);
            row("Cierre", fmtPrice(k.close), C_TEXT);
            if (i > 0) {
                double pct = (k.close - b[i - 1].close) / b[i - 1].close * 100.0;
                row("Variación", fmtPct(pct), pct >= 0 ? C_GREEN : C_RED);
            }
            if (s[i].valid) {
                row("SAR", fmtPrice(s[i].sar), s[i].up ? C_GREEN : C_RED);
                row("Tendencia", s[i].up ? "alcista" : "bajista", s[i].up ? C_GREEN : C_RED);
            }
            ImGui::EndTable();
        }
        if (s[i].reversal)
            ImGui::TextColored(s[i].up ? C_GREEN : C_RED, "Cambio de tendencia: señal de %s",
                               s[i].up ? "compra" : "venta");
        ImGui::EndTooltip();
    }
}
