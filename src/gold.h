#pragma once
// Domain layer: price history, download, cache and the Parabolic SAR.
// No SDL / ImGui here, so it can be unit tested on its own.
#include <string>
#include <vector>

// one OHLC bar; date is YYYY-MM-DD (for resampled bars, the first trading
// day of the week / month)
struct Candle {
    std::string date;
    double open = 0, high = 0, low = 0, close = 0;
};

enum class Interval { Daily, Weekly, Monthly };

// ---- dates -----------------------------------------------------------------
long daysFromCivil(int y, int m, int d);         // days since 1970-01-01
long dateToDays(const std::string& ymd);         // -1 if unparseable
std::string addMonths(const std::string& ymd, int months);  // day clamped to month end
std::string todayStr();
std::string nowStr();                            // YYYY-MM-DD HH:MM, local time

// ---- formatting (Spanish style: 4.192,30) ------------------------------------
std::string fmtPrice(double v, int decimals = 2);
std::string fmtPct(double v);                    // signed, e.g. "+1,25 %"
std::string fmtDateShort(const std::string& ymd);  // "05 oct 2026"

// ---- data --------------------------------------------------------------------
// Parses a Yahoo Finance v8 "chart" response into daily candles, oldest first.
// Bars with missing values are skipped and same-day duplicates collapsed.
// Returns an empty vector and fills *error when the response has no data.
std::vector<Candle> parseYahooChart(const std::string& json, std::string* error);

// Downloads `years` of daily history for `symbol` (e.g. "GC=F", COMEX gold
// futures) from Yahoo Finance's public chart endpoint through the system
// `curl` binary. Blocking: call it from a worker thread.
std::vector<Candle> fetchHistory(const std::string& symbol, int years, std::string* error);

// plain CSV cache: date,open,high,low,close
bool saveCsv(const std::string& path, const std::vector<Candle>& candles);
std::vector<Candle> loadCsv(const std::string& path);

// groups daily bars into weekly (Monday-based) or monthly bars
std::vector<Candle> resample(const std::vector<Candle>& daily, Interval iv);

// index of the first bar dated on/after ymd (bars.size() if none)
int firstIndexFrom(const std::vector<Candle>& bars, const std::string& ymd);

// Converts USD-priced bars to EUR using a EUR/USD history (USD per EUR, as
// Yahoo's "EURUSD=X" quotes it): every price of a bar is divided by that
// day's EUR/USD close, or the latest earlier one when FX has no bar that day
// (bars older than the whole FX history use its first close). Returns the
// input unchanged when fx is empty.
std::vector<Candle> convertToEur(const std::vector<Candle>& usd, const std::vector<Candle>& fx);

// EUR/USD close in force on ymd (latest bar on/before it); 0 if fx is empty
double fxRateOn(const std::vector<Candle>& fx, const std::string& ymd);

// ---- Parabolic SAR (Wilder) --------------------------------------------------
struct SarPoint {
    double sar = 0;        // stop-and-reverse level for this bar
    bool up = true;        // trend: true = long (SAR below price)
    bool reversal = false; // the trend flipped on this bar
    bool valid = false;    // false for the first bar (no SAR yet)
};

// step: acceleration factor start and increment (classic 0.02);
// maxStep: acceleration cap (classic 0.20)
std::vector<SarPoint> parabolicSar(const std::vector<Candle>& c, double step = 0.02,
                                   double maxStep = 0.2);
