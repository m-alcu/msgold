#pragma once
// Shared UI state, palette and widget helpers for ui.cpp / chart.cpp.
// main.cpp only needs App, drawUI, applyStyle, loadFonts, startFetch, pollFetch.
#include <cstdint>
#include <future>
#include <string>
#include <vector>
#include "gold.h"
#include "imgui.h"

// ---- palette ---------------------------------------------------------------
inline ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
inline ImU32 col32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }
inline const ImVec4 C_BG = rgb(20, 24, 29);
inline const ImVec4 C_PANEL = rgb(29, 35, 43);
inline const ImVec4 C_PANEL2 = rgb(38, 46, 56);
inline const ImVec4 C_BORDER = rgb(55, 65, 78);
inline const ImVec4 C_GRID = rgb(40, 47, 57);
inline const ImVec4 C_TEXT = rgb(232, 234, 237);
inline const ImVec4 C_DIM = rgb(145, 155, 167);
inline const ImVec4 C_GREEN = rgb(46, 204, 113);
inline const ImVec4 C_RED = rgb(235, 87, 70);
inline const ImVec4 C_GOLD = rgb(222, 184, 65);
inline const ImVec4 C_DARK = rgb(15, 18, 22);

extern ImFont* fBody;
extern ImFont* fBig;
extern ImFont* fHuge;

// ---- periods -----------------------------------------------------------------
struct Period {
    const char* label;
    int months;
};
inline const Period kPeriods[] = {
    {"10 años", 120}, {"5 años", 60}, {"1 año", 12},
    {"6 meses", 6},   {"3 meses", 3}, {"1 mes", 1},
};
constexpr int kNumPeriods = (int)(sizeof kPeriods / sizeof kPeriods[0]);
inline const char* kFxSymbol = "EURUSD=X";  // USD per EUR on Yahoo Finance
// years of daily history to download: the longest period plus a year of
// warm-up so the SAR is already settled at the left edge of the 10-year view
constexpr int kHistoryYears = 11;

// ---- app state ---------------------------------------------------------------
struct FetchResult {
    std::vector<Candle> candles, fx;
    std::string error, fxError;
};

struct App {
    std::string symbol = "GC=F";       // COMEX gold futures on Yahoo Finance
    std::string cachePath = "msgold.csv";
    std::string fxCachePath = "msgold_eurusd.csv";
    int refreshMinutes = 15;           // automatic re-download; 0 disables
    std::vector<Candle> daily;         // full daily history in USD, oldest first
    std::vector<Candle> fx;            // EUR/USD daily history (for the EUR view)
    std::string updatedAt;             // last successful download ("" = cache only)
    std::future<FetchResult> fetch;    // in-flight download, if valid()
    uint64_t lastFetchAt = 0;          // SDL ticks of the last download attempt

    // view settings
    int period = 2;     // index into kPeriods
    int interval = 0;   // 0 auto, 1 daily, 2 weekly, 3 monthly
    int style = 0;      // 0 auto, 1 candles, 2 line
    float sarStep = 0.02f, sarMax = 0.20f;
    int currency = 0;   // 0 USD, 1 EUR
    // zoom/pan: visible window as dates, so it survives data refreshes and
    // interval changes; zoomFrom empty = whole period, zoomTo empty = up to
    // the latest bar (the view keeps following new data)
    std::string zoomFrom, zoomTo;
    float panAccum = 0;  // sub-bar drag distance carried between frames

    // derived from the above (rebuilt by recompute when dirty)
    bool dirty = true;
    Interval effInterval = Interval::Daily;
    bool eur = false;              // prices below are in EUR
    std::vector<Candle> dailyView; // daily history in the displayed currency
    std::vector<Candle> bars;      // full history at effInterval
    std::vector<SarPoint> sar;     // one per bar
    int first = 0;                 // first bar inside the selected period

    std::string status;
    bool statusError = false;
    uint64_t statusAt = 0;
};

// ---- ui.cpp --------------------------------------------------------------------
void setStatus(App& a, const std::string& s, bool error = false);
bool AccentButton(const char* label, const ImVec4& bg, const ImVec4& fg,
                  ImVec2 size = ImVec2(0, 0));
void startFetch(App& a);  // background download (no-op if one is running)
void pollFetch(App& a);   // picks up a finished download; call every frame
bool fetching(const App& a);
void recompute(App& a);   // rebuilds bars/sar/first when dirty
void visibleRange(const App& a, int& first, int& last);  // bars shown (zoom aware)
void setZoom(App& a, int first, int last);                // zoom to bar indices
void resetZoom(App& a);
const char* unitLabel(const App& a);  // "USD/onza" or "EUR/onza"
void drawUI(App& a);      // full-window UI
void applyStyle();
void loadFonts();

// ---- chart.cpp -------------------------------------------------------------------
void drawChart(App& a);   // price + SAR chart filling the current child window
