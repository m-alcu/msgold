// Window shell (header, controls, side panel, status bar), download handling,
// style and fonts. The chart itself lives in chart.cpp.
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include "ui.h"

ImFont* fBody = nullptr;
ImFont* fBig = nullptr;
ImFont* fHuge = nullptr;

void setStatus(App& a, const std::string& s, bool error) {
    a.status = s;
    a.statusError = error;
    a.statusAt = SDL_GetTicks();
}

bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(bg.x * 1.15f, bg.y * 1.15f, bg.z * 1.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          ImVec4(bg.x * 0.9f, bg.y * 0.9f, bg.z * 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return r;
}

// ---- download ------------------------------------------------------------------
bool fetching(const App& a) { return a.fetch.valid(); }

void startFetch(App& a) {
    if (fetching(a)) return;
    a.lastFetchAt = SDL_GetTicks();
    std::string symbol = a.symbol;
    a.fetch = std::async(std::launch::async, [symbol] {
        FetchResult r;
        r.candles = fetchHistory(symbol, kHistoryYears, &r.error);
        if (!r.candles.empty()) r.fx = fetchHistory(kFxSymbol, kHistoryYears, &r.fxError);
        return r;
    });
}

void pollFetch(App& a) {
    if (!fetching(a) ||
        a.fetch.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    FetchResult r = a.fetch.get();  // leaves the future invalid
    if (r.candles.empty()) {
        setStatus(a, "Error al descargar " + a.symbol + ": " + r.error, true);
        return;
    }
    a.daily = std::move(r.candles);
    a.updatedAt = nowStr();
    a.dirty = true;
    bool saved = saveCsv(a.cachePath, a.daily);
    // a failed EUR/USD download keeps the previous (cached) rates
    bool fxOk = !r.fx.empty();
    if (fxOk) {
        a.fx = std::move(r.fx);
        saved &= saveCsv(a.fxCachePath, a.fx);
    }
    std::string msg = "Descargados " + std::to_string(a.daily.size()) + " cierres diarios de " +
                      a.symbol + (fxOk ? " y " + std::string(kFxSymbol) : "") +
                      " (Yahoo Finance)";
    if (!fxOk) msg += "  -  EUR/USD no disponible: " + r.fxError;
    if (!saved) msg += "  -  no se pudo guardar la caché";
    setStatus(a, msg, !saved || !fxOk);
}

// ---- derived data ----------------------------------------------------------------
void recompute(App& a) {
    if (!a.dirty) return;
    a.dirty = false;
    const Period& p = kPeriods[a.period];
    a.effInterval = a.interval == 0 ? (p.months >= 60 ? Interval::Weekly : Interval::Daily)
                                    : (Interval)(a.interval - 1);
    // the SAR runs over the whole history so it is already settled at the
    // left edge of the visible period, exactly as a trader's chart would be
    a.eur = a.currency == 1 && !a.fx.empty();
    a.dailyView = a.eur ? convertToEur(a.daily, a.fx) : a.daily;
    a.bars = resample(a.dailyView, a.effInterval);
    a.sar = parabolicSar(a.bars, a.sarStep, a.sarMax);
    a.first = 0;
    if (a.bars.size() >= 2) {
        a.first = firstIndexFrom(a.bars, addMonths(a.daily.back().date, -p.months));
        a.first = std::clamp(a.first, 0, (int)a.bars.size() - 2);
    }
}

void visibleRange(const App& a, int& first, int& last) {
    int end = (int)a.bars.size() - 1;
    first = a.first;
    last = end;
    if (a.zoomFrom.empty() || end < 1) return;
    first = std::clamp(firstIndexFrom(a.bars, a.zoomFrom), 0, end - 1);
    if (!a.zoomTo.empty()) last = std::clamp(firstIndexFrom(a.bars, a.zoomTo), first + 1, end);
}

void setZoom(App& a, int first, int last) {
    int end = (int)a.bars.size() - 1;
    if (end < 1) return;
    first = std::clamp(first, 0, end - 1);
    last = std::clamp(last, first + 1, end);
    if (first == a.first && last == end) {  // back to the plain period view
        resetZoom(a);
        return;
    }
    a.zoomFrom = a.bars[first].date;
    a.zoomTo = last == end ? "" : a.bars[last].date;
}

void resetZoom(App& a) {
    a.zoomFrom.clear();
    a.zoomTo.clear();
    a.panAccum = 0;
}

const char* unitLabel(const App& a) { return a.eur ? "EUR/onza" : "USD/onza"; }

// ---- side panel --------------------------------------------------------------------
static void label(const char* k) { ImGui::TextColored(C_DIM, "%s", k); }

static void sidePanel(App& a) {
    const auto& b = a.bars;
    const auto& d = a.dailyView;
    if (b.size() < 2) {
        ImGui::TextColored(C_DIM, "Sin datos");
        return;
    }
    int last = (int)b.size() - 1;
    int vf, vl;  // visible range (period, or the zoomed window)
    visibleRange(a, vf, vl);
    bool zoomed = !a.zoomFrom.empty();
    const Candle& lc = d.back();

    // price block
    ImGui::TextColored(C_DIM, "Último cierre (%s)", unitLabel(a));
    ImGui::PushFont(fHuge);
    ImGui::TextColored(C_GOLD, "%s", fmtPrice(lc.close).c_str());
    ImGui::PopFont();
    ImGui::TextColored(C_DIM, "%s", fmtDateShort(lc.date).c_str());
    if (d.size() >= 2) {
        double prev = d[d.size() - 2].close;
        double chg = lc.close - prev, pct = chg / prev * 100.0;
        ImGui::SameLine();
        ImGui::TextColored(chg >= 0 ? C_GREEN : C_RED, "%s%s  (%s)", chg >= 0 ? "+" : "",
                           fmtPrice(chg).c_str(), fmtPct(pct).c_str());
    }
    if (!a.fx.empty()) {
        double r = fxRateOn(a.fx, lc.date);
        ImGui::TextColored(C_DIM, "EUR/USD %s  ·  %s %s", fmtPrice(r, 4).c_str(),
                           a.eur ? "en USD" : "en EUR",
                           fmtPrice(a.eur ? lc.close * r : lc.close / r).c_str());
    }
    ImGui::Spacing();

    // period stats
    double hi = b[vf].high, lo = b[vf].low;
    int hiI = vf, loI = vf;
    for (int i = vf; i <= vl; i++) {
        if (b[i].high > hi) hi = b[i].high, hiI = i;
        if (b[i].low < lo) lo = b[i].low, loI = i;
    }
    double base = b[vf].open;
    double pchg = (b[vl].close - base) / base * 100.0;
    if (ImGui::BeginTable("stats", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* k, const std::string& v, const ImVec4& c,
                      const std::string& sub = "") {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "%s", k);
            ImGui::TableNextColumn();
            ImGui::TextColored(c, "%s", v.c_str());
            if (!sub.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(C_DIM, "%s", sub.c_str());
            }
        };
        row(zoomed ? "Var. zoom" : ("Var. " + std::string(kPeriods[a.period].label)).c_str(),
            fmtPct(pchg), pchg >= 0 ? C_GREEN : C_RED);
        row("Máximo", fmtPrice(hi), C_TEXT, fmtDateShort(b[hiI].date));
        row("Mínimo", fmtPrice(lo), C_TEXT, fmtDateShort(b[loI].date));
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // SAR block
    ImGui::PushFont(fBig);
    ImGui::TextUnformatted("Parabolic SAR");
    ImGui::PopFont();
    const char* ivName[] = {"diario", "semanal", "mensual"};
    ImGui::TextColored(C_DIM, "%s  ·  paso %.3f  ·  máx %.2f", ivName[(int)a.effInterval],
                       a.sarStep, a.sarMax);
    const SarPoint& s = a.sar[last];
    int since = last;
    while (since > 1 && !a.sar[since].reversal) since--;
    {
        ImVec4 c = s.up ? C_GREEN : C_RED;
        ImGui::Spacing();
        AccentButton(s.up ? "  ALCISTA  " : "  BAJISTA  ", c, C_DARK);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(C_DIM, "SAR %s", fmtPrice(s.sar).c_str());
        double dist = (b[last].close - s.sar) / b[last].close * 100.0;
        ImGui::TextColored(C_DIM, "distancia %s", fmtPct(dist).c_str());
        ImGui::EndGroup();
        int ago = last - since;
        const char* unit = a.effInterval == Interval::Daily    ? (ago == 1 ? "sesión" : "sesiones")
                           : a.effInterval == Interval::Weekly ? (ago == 1 ? "semana" : "semanas")
                                                               : (ago == 1 ? "mes" : "meses");
        ImGui::TextColored(C_DIM, "Desde %s (hace %d %s), a %s",
                           fmtDateShort(b[since].date).c_str(), ago, unit,
                           fmtPrice(b[since].close).c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // reversals inside the visible range, newest first, with the move each
    // signal captured until the next one (or until today for the open one)
    std::vector<int> revs;
    for (int i = vf; i <= vl; i++)
        if (a.sar[i].reversal) revs.push_back(i);
    ImGui::Text("Señales en %s: %d", zoomed ? "el zoom" : "el periodo", (int)revs.size());
    if (ImGui::BeginTable("signals", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Fecha", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Señal", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Precio", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Result.", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::PushStyleColor(ImGuiCol_Text, C_DIM);
        ImGui::TableHeadersRow();
        ImGui::PopStyleColor();
        for (int k = (int)revs.size() - 1; k >= 0; k--) {
            int i = revs[k];
            // the move ends at the next reversal, which may lie past the
            // zoomed window, or at the latest bar for the open signal
            int next = i + 1;
            while (next < last && !a.sar[next].reversal) next++;
            next = std::min(next, last);
            bool open = !a.sar[next].reversal || next == i;
            bool buy = a.sar[i].up;
            double entry = b[i].close, exit = b[next].close;
            double res = (buy ? exit - entry : entry - exit) / entry * 100.0;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(C_DIM, "%s", fmtDateShort(b[i].date).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(buy ? C_GREEN : C_RED, "%s", buy ? "Compra" : "Venta");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmtPrice(entry).c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(res >= 0 ? C_GREEN : C_RED, "%s", fmtPct(res).c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(open ? "Movimiento desde la señal hasta el último cierre"
                                       : "Movimiento a favor de la señal hasta la siguiente");
        }
        ImGui::EndTable();
    }
}

// ---- main UI -------------------------------------------------------------------------
void drawUI(App& a) {
    recompute(a);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

    // keyboard: 1-6 period, R refresh, E currency, Esc reset zoom
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
        for (int i = 0; i < kNumPeriods; i++)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false) && a.period != i) {
                a.period = i;
                a.dirty = true;
                resetZoom(a);
            }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) startFetch(a);
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            a.currency ^= 1;
            a.dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) resetZoom(a);
    }

    // header: title, subtitle, USD|EUR toggle, data info, refresh button.
    // Every element is placed with SetCursorPos at an absolute Y centred on
    // the title: relative nudges after SameLine() drift, because SameLine
    // restores the previous item's Y and text also picks up the line's
    // frame-padding baseline. Without SameLine each item starts a fresh line.
    float rowY = ImGui::GetCursorPosY();
    ImGui::PushFont(fHuge);
    float midY = rowY + ImGui::GetTextLineHeight() * 0.5f;
    ImGui::TextColored(C_GOLD, "msgold");
    ImGui::PopFont();
    float rowEndY = ImGui::GetCursorPosY();
    float x = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 12;
    float textY = midY - ImGui::GetTextLineHeight() * 0.5f;
    float frameY = midY - ImGui::GetFrameHeight() * 0.5f;
    ImGui::SetCursorPos(ImVec2(x, textY));
    ImGui::TextColored(C_DIM, "Oro  ·  %s (futuros COMEX)  ·  %s  ·  Yahoo Finance",
                       a.symbol.c_str(), unitLabel(a));
    {
        const float btnW = 130.0f, curBtnW = 58.0f, gap = 4.0f;
        std::string info = fetching(a)       ? "Descargando..."
                           : a.updatedAt.empty() ? "Datos de caché local"
                                                 : "Actualizado " + a.updatedAt;
        float infoW = ImGui::CalcTextSize(info.c_str()).x;
        // right to left: refresh button, info text, EUR, USD
        float right = ImGui::GetWindowContentRegionMax().x;
        float btnX = right - btnW;
        float infoX = btnX - 16 - infoW;
        float eurX = infoX - 24 - curBtnW;
        float usdX = eurX - gap - curBtnW;
        for (int c = 0; c < 2; c++) {
            ImGui::SetCursorPos(ImVec2(c ? eurX : usdX, frameY));
            const char* name = c ? "EUR" : "USD";
            bool sel = (a.eur ? 1 : 0) == c;
            ImGui::BeginDisabled(c == 1 && a.fx.empty());  // no rates yet
            if (sel ? AccentButton(name, C_GOLD, C_DARK, ImVec2(curBtnW, 0))
                    : ImGui::Button(name, ImVec2(curBtnW, 0))) {
                a.currency = c;
                a.dirty = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(c == 0         ? "Precio en dólares por onza (E)"
                                  : a.fx.empty() ? "Sin datos de EUR/USD: pulsa Actualizar"
                                                 : "Precio en euros por onza, convertido con "
                                                   "el cierre EUR/USD de cada día (E)");
        }
        ImGui::SetCursorPos(ImVec2(infoX, textY));
        ImGui::TextColored(fetching(a) ? C_GOLD : C_DIM, "%s", info.c_str());
        ImGui::SetCursorPos(ImVec2(btnX, frameY));
        ImGui::BeginDisabled(fetching(a));
        if (AccentButton("Actualizar", C_GOLD, C_DARK, ImVec2(btnW, 0))) startFetch(a);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Descargar de nuevo el histórico (R)");
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x,
                               std::max(rowEndY, frameY + ImGui::GetFrameHeight() +
                                                     ImGui::GetStyle().ItemSpacing.y)));

    // controls: periods, bar interval, chart style, SAR parameters
    for (int i = 0; i < kNumPeriods; i++) {
        if (i) ImGui::SameLine(0, 6);
        bool sel = a.period == i;
        if (sel ? AccentButton(kPeriods[i].label, C_GOLD, C_DARK, ImVec2(86, 0))
                : ImGui::Button(kPeriods[i].label, ImVec2(86, 0))) {
            a.period = i;
            a.dirty = true;
            resetZoom(a);
        }
    }
    ImGui::SameLine(0, 28);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(C_DIM, "Velas");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    if (ImGui::Combo("##interval", &a.interval, "Auto\0Diarias\0Semanales\0Mensuales\0"))
        a.dirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Auto: semanales para 5 y 10 años, diarias para el resto");
    ImGui::SameLine(0, 18);
    ImGui::TextColored(C_DIM, "Gráfico");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##style", &a.style, "Auto\0Velas\0Línea\0");
    ImGui::SameLine(0, 18);
    ImGui::TextColored(C_DIM, "SAR paso");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::DragFloat("##step", &a.sarStep, 0.001f, 0.001f, 0.2f, "%.3f",
                         ImGuiSliderFlags_AlwaysClamp))
        a.dirty = true;
    ImGui::SameLine();
    ImGui::TextColored(C_DIM, "máx");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (ImGui::DragFloat("##max", &a.sarMax, 0.005f, 0.01f, 1.0f, "%.2f",
                         ImGuiSliderFlags_AlwaysClamp))
        a.dirty = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset")) {
        a.sarStep = 0.02f;
        a.sarMax = 0.20f;
        a.dirty = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Valores clásicos de Wilder: 0,02 / 0,20");
    if (a.sarMax < a.sarStep) {
        a.sarMax = a.sarStep;
        a.dirty = true;
    }
    recompute(a);  // controls above may have changed the view

    // chart + side panel
    float statusH = ImGui::GetTextLineHeightWithSpacing() + 6;
    float sideW = 360.0f;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::BeginChild("chart", ImVec2(ImGui::GetContentRegionAvail().x - sideW - gap, -statusH),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    drawChart(a);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, C_PANEL);
    ImGui::BeginChild("side", ImVec2(sideW, -statusH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    sidePanel(a);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // status bar
    ImGui::Separator();
    if (!a.status.empty() && SDL_GetTicks() - a.statusAt < (a.statusError ? 15000u : 6000u))
        ImGui::TextColored(a.statusError ? C_RED : C_GREEN, "%s", a.status.c_str());
    else
        ImGui::TextUnformatted(" ");
    ImGui::SameLine();
    {
        const char* help = "1-6: periodo  |  E: USD/EUR  |  R: actualizar  |  rueda: zoom, arrastrar: mover, "
                           "doble clic/Esc: quitar zoom";
        float off = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(help).x;
        if (off > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
        ImGui::TextColored(C_DIM, "%s", help);
    }

    ImGui::End();
}

void applyStyle() {
    ImGuiStyle& st = ImGui::GetStyle();
    ImGui::StyleColorsDark();
    st.WindowRounding = 0;
    st.ChildRounding = 6;
    st.FrameRounding = 4;
    st.PopupRounding = 6;
    st.GrabRounding = 4;
    st.ScrollbarSize = 12;
    st.WindowPadding = ImVec2(16, 12);
    st.FramePadding = ImVec2(10, 6);
    st.ItemSpacing = ImVec2(10, 8);
    st.CellPadding = ImVec2(6, 4);

    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = C_BG;
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = C_PANEL;
    c[ImGuiCol_Border] = C_BORDER;
    c[ImGuiCol_Text] = C_TEXT;
    c[ImGuiCol_TextDisabled] = C_DIM;
    c[ImGuiCol_FrameBg] = C_DARK;
    c[ImGuiCol_FrameBgHovered] = C_PANEL2;
    c[ImGuiCol_FrameBgActive] = C_PANEL2;
    c[ImGuiCol_Button] = C_PANEL2;
    c[ImGuiCol_ButtonHovered] = rgb(52, 62, 74);
    c[ImGuiCol_ButtonActive] = rgb(46, 56, 68);
    c[ImGuiCol_Header] = C_PANEL2;
    c[ImGuiCol_HeaderHovered] = rgb(44, 53, 64);
    c[ImGuiCol_HeaderActive] = rgb(50, 60, 72);
    c[ImGuiCol_TableHeaderBg] = C_PANEL2;
    c[ImGuiCol_TableRowBg] = rgb(33, 40, 49);
    c[ImGuiCol_TableRowBgAlt] = C_PANEL;
    c[ImGuiCol_TableBorderLight] = rgb(44, 52, 62);
    c[ImGuiCol_TableBorderStrong] = C_BORDER;
    c[ImGuiCol_Separator] = C_BORDER;
    c[ImGuiCol_CheckMark] = C_GOLD;
    c[ImGuiCol_SliderGrab] = C_GOLD;
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = C_BORDER;
    c[ImGuiCol_NavCursor] = C_GOLD;
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    std::string reg, bold;
#ifdef _WIN32
    const char* win = getenv("WINDIR");
    std::string fonts = std::string(win ? win : "C:\\Windows") + "\\Fonts\\";
    reg = fonts + "segoeui.ttf";
    bold = fonts + "segoeuib.ttf";
#else
    reg = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    bold = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
#endif
    auto exists = [](const std::string& p) {
        if (FILE* f = fopen(p.c_str(), "rb")) {
            fclose(f);
            return true;
        }
        return false;
    };
    if (exists(reg) && exists(bold)) {
        fBody = io.Fonts->AddFontFromFileTTF(reg.c_str(), 17.0f);
        fBig = io.Fonts->AddFontFromFileTTF(bold.c_str(), 23.0f);
        fHuge = io.Fonts->AddFontFromFileTTF(bold.c_str(), 34.0f);
    }
    if (!fBody) fBody = io.Fonts->AddFontDefault();
    if (!fBig) fBig = fBody;
    if (!fHuge) fHuge = fBody;
}
