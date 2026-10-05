#include "gold.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

// ---- dates -----------------------------------------------------------------
// Howard Hinnant's civil <-> days algorithms (proleptic Gregorian)
long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

static void civilFromDays(long z, int& y, int& m, int& d) {
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned)(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yoe + era * 400) + (m <= 2);
}

static std::string ymdStr(int y, int m, int d) {
    char buf[16];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
    return buf;
}

static bool parseYmd(const std::string& s, int& y, int& m, int& d) {
    return sscanf(s.c_str(), "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 &&
           d <= 31;
}

long dateToDays(const std::string& ymd) {
    int y, m, d;
    if (!parseYmd(ymd, y, m, d)) return -1;
    return daysFromCivil(y, m, d);
}

std::string addMonths(const std::string& ymd, int months) {
    int y, m, d;
    if (!parseYmd(ymd, y, m, d)) return ymd;
    int total = y * 12 + (m - 1) + months;
    y = total / 12;
    m = total % 12 + 1;
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    int last = mdays[m - 1] + (m == 2 && leap);
    return ymdStr(y, m, std::min(d, last));
}

static tm localNow() {
    time_t t = time(nullptr);
    tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    return lt;
}

std::string todayStr() {
    tm lt = localNow();
    char buf[16];
    strftime(buf, sizeof buf, "%Y-%m-%d", &lt);
    return buf;
}

std::string nowStr() {
    tm lt = localNow();
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &lt);
    return buf;
}

// ---- formatting --------------------------------------------------------------
std::string fmtPrice(double v, int decimals) {
    if (!std::isfinite(v)) return "-";
    bool neg = v < 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%.*f", decimals, std::fabs(v));
    std::string s(buf);
    size_t dot = s.find('.');
    if (dot != std::string::npos) s[dot] = ',';
    else dot = s.size();
    for (int i = (int)dot - 3; i > 0; i -= 3) s.insert((size_t)i, ".");
    return (neg ? "-" : "") + s;
}

std::string fmtPct(double v) {
    if (!std::isfinite(v)) return "-";
    return (v >= 0 ? "+" : "-") + fmtPrice(std::fabs(v), 2) + " %";
}

std::string fmtDateShort(const std::string& ymd) {
    static const char* mon[] = {"ene", "feb", "mar", "abr", "may", "jun",
                                "jul", "ago", "sep", "oct", "nov", "dic"};
    int y, m, d;
    if (!parseYmd(ymd, y, m, d)) return ymd;
    char buf[32];
    snprintf(buf, sizeof buf, "%02d %s %04d", d, mon[m - 1], y);
    return buf;
}

// ---- download ------------------------------------------------------------------
namespace {

// Runs a command and returns its stdout. On Windows the process is started
// directly with CREATE_NO_WINDOW: going through popen/cmd.exe from a GUI
// (-mwindows) app would flash a console window on every download.
std::string runCommand(const std::string& cmd) {
    std::string out;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return out;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::string line = cmd;  // CreateProcessA may modify its buffer
    BOOL ok = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return out;
    }
    std::array<char, 8192> buf{};
    DWORD n = 0;
    while (ReadFile(rd, buf.data(), (DWORD)buf.size(), &n, nullptr) && n > 0)
        out.append(buf.data(), n);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
#else
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return out;
    std::array<char, 8192> buf{};
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), pipe.get())) > 0) out.append(buf.data(), n);
#endif
    return out;
}

// first "key":"value" or "key":123 occurrence; enough for the flat fields we
// need without pulling in a JSON library
std::string extractField(const std::string& json, const std::string& key) {
    std::string marker = "\"" + key + "\":";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return "";
    pos += marker.size();
    if (pos < json.size() && json[pos] == '"') {
        size_t end = json.find('"', pos + 1);
        return end == std::string::npos ? "" : json.substr(pos + 1, end - pos - 1);
    }
    size_t end = pos;
    while (end < json.size() && (std::isdigit((unsigned char)json[end]) || json[end] == '.' ||
                                 json[end] == '-' || json[end] == 'e' || json[end] == '+'))
        end++;
    return json.substr(pos, end - pos);
}

// "key":[1.5,null,2,...] -> values, NaN for null; empty if missing/malformed
std::vector<double> numberArray(const std::string& json, const std::string& key) {
    std::vector<double> out;
    std::string marker = "\"" + key + "\":[";
    size_t pos = json.find(marker);
    if (pos == std::string::npos) return out;
    pos += marker.size();
    const char* base = json.c_str();
    while (pos < json.size() && json[pos] != ']') {
        if (json.compare(pos, 4, "null") == 0) {
            out.push_back(NAN);
            pos += 4;
        } else {
            char* end = nullptr;
            double v = strtod(base + pos, &end);
            if (end == base + pos) return {};
            out.push_back(v);
            pos = (size_t)(end - base);
        }
        if (pos < json.size() && json[pos] == ',') pos++;
    }
    return out;
}

// keep only characters that are valid in a ticker and safe inside quotes
std::string sanitizeSymbol(const std::string& raw) {
    std::string out;
    for (char c : raw)
        if (std::isalnum((unsigned char)c) || strchr(".-^=", c)) out += c;
    return out;
}

}  // namespace

std::vector<Candle> parseYahooChart(const std::string& json, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return std::vector<Candle>{};
    };
    if (json.empty()) return fail("sin respuesta del servidor (¿sin conexión o curl no disponible?)");
    if (json.find("\"result\":null") != std::string::npos) {
        std::string desc = extractField(json, "description");
        return fail("Yahoo: " + (desc.empty() ? std::string("sin datos") : desc));
    }
    auto ts = numberArray(json, "timestamp");
    auto o = numberArray(json, "open"), h = numberArray(json, "high");
    auto l = numberArray(json, "low"), c = numberArray(json, "close");
    if (ts.empty()) {
        std::string head = json.substr(0, 80);
        for (char& ch : head)
            if (ch == '\n' || ch == '\r') ch = ' ';
        return fail("respuesta inesperada: " + head);
    }
    if (o.size() != ts.size() || h.size() != ts.size() || l.size() != ts.size() ||
        c.size() != ts.size())
        return fail("respuesta incompleta (series de distinta longitud)");

    // Daily bars are stamped at midnight exchange time. Shift by the reported
    // UTC offset plus a 3h margin so a DST mismatch between the bar and the
    // (current) offset can never push a bar onto the previous day.
    long gmtoff = atol(extractField(json, "gmtoffset").c_str());
    std::vector<Candle> out;
    out.reserve(ts.size());
    for (size_t i = 0; i < ts.size(); i++) {
        if (std::isnan(c[i]) || std::isnan(h[i]) || std::isnan(l[i]) || c[i] <= 0) continue;
        double op = (std::isnan(o[i]) || o[i] <= 0) ? c[i] : o[i];
        long long t = (long long)ts[i] + gmtoff + 3 * 3600;
        long days = (long)((t >= 0 ? t : t - 86399) / 86400);
        int y, m, d;
        civilFromDays(days, y, m, d);
        Candle k{ymdStr(y, m, d), op, std::max({h[i], op, c[i]}), std::min({l[i], op, c[i]}),
                 c[i]};
        if (!out.empty() && out.back().date == k.date) out.back() = k;  // live bar repeat
        else if (out.empty() || out.back().date < k.date) out.push_back(k);
    }
    if (out.empty()) return fail("la respuesta no contiene cotizaciones");
    return out;
}

std::vector<Candle> fetchHistory(const std::string& rawSymbol, int years, std::string* error) {
    std::string symbol = sanitizeSymbol(rawSymbol);
    if (symbol.empty()) {
        if (error) *error = "símbolo vacío";
        return {};
    }
    long long now = (long long)time(nullptr);
    long long from = now - (long long)(years * 365.25 * 86400.0);
    std::string url = "https://query1.finance.yahoo.com/v8/finance/chart/" + symbol +
                      "?period1=" + std::to_string(from) + "&period2=" + std::to_string(now) +
                      "&interval=1d&includePrePost=false&events=";
#ifdef _WIN32
    const char q = '"';  // CreateProcess: curl parses double-quoted arguments
#else
    const char q = '\'';  // popen goes through /bin/sh
#endif
    std::string cmd = std::string("curl -s -L --max-time 25 -A ") + q + "Mozilla/5.0" + q + " " +
                      q + url + q;
    return parseYahooChart(runCommand(cmd), error);
}

// ---- cache ---------------------------------------------------------------------
bool saveCsv(const std::string& path, const std::vector<Candle>& candles) {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << "date,open,high,low,close\n";
        char buf[160];
        for (const auto& k : candles) {
            snprintf(buf, sizeof buf, "%s,%.4f,%.4f,%.4f,%.4f\n", k.date.c_str(), k.open, k.high,
                     k.low, k.close);
            f << buf;
        }
        if (!f) return false;
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::vector<Candle> loadCsv(const std::string& path) {
    std::vector<Candle> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        char date[16];
        Candle k;
        if (sscanf(line.c_str(), "%10[0-9-],%lf,%lf,%lf,%lf", date, &k.open, &k.high, &k.low,
                   &k.close) != 5)
            continue;  // header or junk
        k.date = date;
        if (dateToDays(k.date) < 0 || k.close <= 0) continue;
        if (out.empty() || out.back().date < k.date) out.push_back(k);
    }
    return out;
}

// ---- resampling ------------------------------------------------------------------
std::vector<Candle> resample(const std::vector<Candle>& daily, Interval iv) {
    if (iv == Interval::Daily) return daily;
    auto key = [iv](const Candle& k) -> long {
        if (iv == Interval::Monthly) {
            int y, m, d;
            parseYmd(k.date, y, m, d);
            return y * 12L + m;
        }
        // 1970-01-01 was a Thursday: +3 makes weeks start on Monday
        long days = dateToDays(k.date);
        return (days + 3) / 7;
    };
    std::vector<Candle> out;
    long cur = 0;
    for (const auto& k : daily) {
        long g = key(k);
        if (out.empty() || g != cur) {
            out.push_back(k);
            cur = g;
        } else {
            Candle& b = out.back();
            b.high = std::max(b.high, k.high);
            b.low = std::min(b.low, k.low);
            b.close = k.close;
        }
    }
    return out;
}

int firstIndexFrom(const std::vector<Candle>& bars, const std::string& ymd) {
    auto it = std::lower_bound(bars.begin(), bars.end(), ymd,
                               [](const Candle& k, const std::string& d) { return k.date < d; });
    return (int)(it - bars.begin());
}

// ---- currency --------------------------------------------------------------------
double fxRateOn(const std::vector<Candle>& fx, const std::string& ymd) {
    if (fx.empty()) return 0;
    // first bar strictly after ymd, then step back to the one in force
    auto it = std::upper_bound(fx.begin(), fx.end(), ymd,
                               [](const std::string& d, const Candle& k) { return d < k.date; });
    return it == fx.begin() ? fx.front().close : std::prev(it)->close;
}

std::vector<Candle> convertToEur(const std::vector<Candle>& usd, const std::vector<Candle>& fx) {
    if (fx.empty()) return usd;
    std::vector<Candle> out;
    out.reserve(usd.size());
    size_t j = 0;  // both series are sorted: walk the FX history alongside
    for (const auto& k : usd) {
        while (j + 1 < fx.size() && fx[j + 1].date <= k.date) j++;
        double r = fx[j].close;
        out.push_back({k.date, k.open / r, k.high / r, k.low / r, k.close / r});
    }
    return out;
}

// ---- Parabolic SAR -----------------------------------------------------------------
std::vector<SarPoint> parabolicSar(const std::vector<Candle>& c, double step, double maxStep) {
    std::vector<SarPoint> out(c.size());
    if (c.size() < 2) return out;

    // initial trend from the first two closes; the SAR starts at the
    // opposite extreme of the first bar
    bool up = c[1].close >= c[0].close;
    double sar = up ? c[0].low : c[0].high;
    double ep = up ? c[0].high : c[0].low;
    double af = step;

    for (size_t i = 1; i < c.size(); i++) {
        double cur = i == 1 ? sar : sar + af * (ep - sar);
        bool rev = false;
        if (up) {
            // never above the lows of the two previous bars
            cur = std::min(cur, c[i - 1].low);
            if (i >= 2) cur = std::min(cur, c[i - 2].low);
            if (c[i].low < cur) {  // stop hit: flip to short at the prior extreme
                rev = true;
                up = false;
                cur = std::max(ep, c[i].high);
                ep = c[i].low;
                af = step;
            } else if (c[i].high > ep) {
                ep = c[i].high;
                af = std::min(af + step, maxStep);
            }
        } else {
            cur = std::max(cur, c[i - 1].high);
            if (i >= 2) cur = std::max(cur, c[i - 2].high);
            if (c[i].high > cur) {
                rev = true;
                up = true;
                cur = std::min(ep, c[i].low);
                ep = c[i].high;
                af = step;
            } else if (c[i].low < ep) {
                ep = c[i].low;
                af = std::min(af + step, maxStep);
            }
        }
        sar = cur;
        out[i] = {sar, up, rev, true};
    }
    return out;
}
