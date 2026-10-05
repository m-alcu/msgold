// Unit tests for the domain layer (gold.h/gold.cpp). No SDL, no ImGui:
// build with just these two files and run; exit code 0 means all checks pass.
#include <cmath>
#include <cstdio>
#include <string>
#include "gold.h"

static int failed = 0;

static void check(bool ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failed++;
    }
}

static void checkNear(double got, double want, const char* what) {
    if (std::fabs(got - want) > 1e-9) {
        printf("FAIL: %s (got %.6f, want %.6f)\n", what, got, want);
        failed++;
    }
}

static Candle bar(const char* date, double h, double l, double c) {
    return Candle{date, c, h, l, c};
}

static void testDates() {
    check(daysFromCivil(1970, 1, 1) == 0, "epoch is day 0");
    check(dateToDays("2024-03-01") - dateToDays("2024-02-28") == 2, "2024 is a leap year");
    check(dateToDays("garbage") == -1, "unparseable date -> -1");
    check(addMonths("2026-10-05", -12) == "2025-10-05", "minus one year");
    check(addMonths("2026-03-31", -1) == "2026-02-28", "clamped to month end");
    check(addMonths("2024-03-31", -1) == "2024-02-29", "leap february");
    check(addMonths("2026-01-15", -120) == "2016-01-15", "minus ten years");
    check(fmtDateShort("2026-10-05") == "05 oct 2026", "short spanish date");
}

static void testFormat() {
    check(fmtPrice(4192.3) == "4.192,30", "thousands dot, decimal comma");
    check(fmtPrice(1234567.891, 1) == "1.234.567,9", "millions");
    check(fmtPrice(-12.5, 0) == "-13" || fmtPrice(-12.5, 0) == "-12", "negative, no decimals");
    check(fmtPct(1.256) == "+1,26 %", "positive percent");
    check(fmtPct(-0.5) == "-0,50 %", "negative percent");
}

// hand-computed Wilder SAR, step 0.02 / max 0.2
static void testSarHandComputed() {
    std::vector<Candle> c = {
        bar("2026-01-01", 10, 9, 9.5),  bar("2026-01-02", 11, 10, 10.5),
        bar("2026-01-03", 12, 11, 11.5), bar("2026-01-04", 13, 12, 12.5),
        bar("2026-01-05", 9.5, 8, 8.5),  // breaks below the SAR -> reversal
        bar("2026-01-06", 9, 7, 7.5),
    };
    auto s = parabolicSar(c);
    check(!s[0].valid, "no SAR on the first bar");
    check(s[1].valid && s[1].up, "uptrend from rising closes");
    checkNear(s[1].sar, 9.0, "bar1: starts at first low");
    checkNear(s[2].sar, 9.0, "bar2: 9.08 clamped to the low two bars back");
    checkNear(s[3].sar, 9.18, "bar3: 9 + 0.06*(12-9)");
    check(s[4].reversal && !s[4].up, "bar4: reversal to downtrend");
    checkNear(s[4].sar, 13.0, "bar4: SAR jumps to the prior extreme point");
    // downtrend: ep=8, af=0.02 -> 13 + 0.02*(8-13) = 12.9, max with highs 13/9.5 -> 13
    checkNear(s[5].sar, 13.0, "bar5: not below the previous two highs");
    check(!s[5].reversal && !s[5].up, "bar5: still down");
}

static void testSarTrendFollowing() {
    // steady uptrend: SAR always below the low, accelerating up to the cap
    std::vector<Candle> c;
    for (int i = 0; i < 60; i++) {
        char d[16];
        snprintf(d, sizeof d, "2026-%02d-%02d", 1 + i / 28, 1 + i % 28);
        c.push_back(bar(d, 100 + i + 1, 100 + i - 1, 100 + i));
    }
    auto s = parabolicSar(c);
    bool allBelow = true, anyRev = false;
    for (size_t i = 1; i < c.size(); i++) {
        allBelow &= s[i].up && s[i].sar <= c[i].low;
        anyRev |= s[i].reversal;
    }
    check(allBelow, "uptrend: SAR stays under every low");
    check(!anyRev, "uptrend: no reversals");
    check(s.back().sar > s[10].sar, "uptrend: SAR rises");
    check(parabolicSar({}).empty(), "empty input");
    check(parabolicSar({bar("2026-01-01", 1, 1, 1)}).size() == 1, "single bar");
}

static void testResample() {
    std::vector<Candle> d = {
        {"2026-09-28", 10, 12, 9, 11},   // Monday
        {"2026-09-30", 11, 15, 10, 14},  // Wednesday
        {"2026-10-02", 14, 14, 8, 9},    // Friday
        {"2026-10-05", 9, 10, 7, 8},     // next Monday
    };
    auto w = resample(d, Interval::Weekly);
    check(w.size() == 2, "two weeks");
    check(w[0].date == "2026-09-28", "week dated by its first bar");
    checkNear(w[0].open, 10, "week open = first open");
    checkNear(w[0].high, 15, "week high");
    checkNear(w[0].low, 8, "week low");
    checkNear(w[0].close, 9, "week close = last close");
    auto m = resample(d, Interval::Monthly);
    check(m.size() == 2 && m[1].date == "2026-10-02", "months split on the 1st");
    check(firstIndexFrom(d, "2026-09-29") == 1, "first bar on/after date");
    check(firstIndexFrom(d, "2027-01-01") == 4, "past the end");
}

static void testConvertToEur() {
    std::vector<Candle> usd = {
        {"2026-09-30", 2.0, 4.0, 2.0, 3.0},  // before any FX bar -> first rate
        {"2026-10-02", 2.2, 4.4, 2.2, 3.3},  // exact FX date
        {"2026-10-03", 2.4, 4.8, 2.4, 3.6},  // FX gap (weekend) -> previous rate
    };
    std::vector<Candle> fx = {{"2026-10-01", 0, 0, 0, 1.0}, {"2026-10-02", 0, 0, 0, 1.1}};
    auto e = convertToEur(usd, fx);
    check(e.size() == 3, "one EUR bar per USD bar");
    checkNear(e[0].close, 3.0, "older than FX history: first rate");
    checkNear(e[1].close, 3.0, "same-day rate: 3.3 / 1.1");
    checkNear(e[1].high, 4.0, "every price converted");
    checkNear(e[2].close, 3.6 / 1.1, "gap uses the latest earlier rate");
    check(convertToEur(usd, {}).size() == 3 && convertToEur(usd, {})[1].close == 3.3,
          "no FX: unchanged");
    checkNear(fxRateOn(fx, "2026-10-05"), 1.1, "rate after the last FX bar");
    checkNear(fxRateOn(fx, "2026-09-01"), 1.0, "rate before the FX history");
    check(fxRateOn({}, "2026-10-01") == 0, "no FX: rate 0");
}

static void testParseYahoo() {
    // 2026-10-01 and 2026-10-02 00:00 New York (EDT, UTC-4), one null bar,
    // and a repeated live bar for the last day
    std::string json =
        R"({"chart":{"result":[{"meta":{"currency":"USD","symbol":"GC=F","gmtoffset":-14400},)"
        R"("timestamp":[1790827200,1790913600,1791000000,1791010000],"indicators":{"quote":[{)"
        R"("high":[4200.5,null,4250,4260],"close":[4190,null,4240,4245.5],)"
        R"("open":[4180,null,4200,4200],"low":[4170,null,4190,4185],"volume":[1,2,3,4]}],)"
        R"("adjclose":[{"adjclose":[4190,null,4240,4245.5]}]}}],"error":null}})";
    std::string err;
    auto c = parseYahooChart(json, &err);
    check(c.size() == 2, "null bar skipped, live repeat collapsed");
    if (c.size() == 2) {
        check(c[0].date == "2026-10-01", "first bar date in exchange time");
        check(c[1].date == "2026-10-03", "repeat of the same day replaces the bar");
        checkNear(c[1].close, 4245.5, "latest close kept");
        checkNear(c[0].high, 4200.5, "high parsed");
    }
    std::string bad = R"({"chart":{"result":null,"error":{"code":"Not Found","description":"No data found"}}})";
    check(parseYahooChart(bad, &err).empty() && err.find("No data found") != std::string::npos,
          "yahoo error is reported");
    check(parseYahooChart("", &err).empty() && !err.empty(), "empty response is an error");
}

static void testCsvRoundTrip() {
    std::vector<Candle> d = {{"2026-10-01", 1.5, 2.25, 1.0, 2.0}, {"2026-10-02", 2, 3, 1.5, 2.5}};
    const char* path = "gold_tests_tmp.csv";
    check(saveCsv(path, d), "csv saved");
    auto r = loadCsv(path);
    check(r.size() == 2 && r[1].date == "2026-10-02", "csv reloaded");
    if (r.size() == 2) checkNear(r[0].high, 2.25, "csv keeps values");
    std::remove(path);
    check(loadCsv("does-not-exist.csv").empty(), "missing cache -> empty");
}

int main() {
    testDates();
    testFormat();
    testSarHandComputed();
    testSarTrendFollowing();
    testResample();
    testConvertToEur();
    testParseYahoo();
    testCsvRoundTrip();
    if (failed) {
        printf("%d check(s) failed\n", failed);
        return 1;
    }
    printf("all gold tests passed\n");
    return 0;
}
