#include "core/EdoFacts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace notegran {

namespace {

constexpr int kMinEdo = 1, kMaxEdo = 96;   // Chords' EDO range

double cents(double ratio) noexcept { return 1200.0 * std::log2(ratio); }

// Steps nearest `ratio` in N-EDO (the patent val's entry for one prime).
int nearestSteps(int edo, double ratio) noexcept {
    return static_cast<int>(std::lround(edo * std::log2(ratio)));
}

// Does the val (v2, v3, v5) send the comma with exponents (a2, a3, a5) to no steps?
bool tempers(int v2, int v3, int v5, int a2, int a3, int a5) noexcept {
    return a2 * v2 + a3 * v3 + a5 * v5 == 0;
}

ChordInterval interval(int edo, double ratio, int best, int scale) noexcept {
    ChordInterval i;
    const double just = cents(ratio), step = 1200.0 / edo;
    i.best      = best;
    i.bestError = best * step - just;
    i.scale     = scale;
    i.scaleError = scale * step - just;
    return i;
}

// Rounded to a tenth first, so a value within 0.05 never reads "-0.0".
double tenth(double v) noexcept {
    const double r = std::round(v * 10.0) / 10.0;
    return r == 0.0 ? 0.0 : r;
}

void stepsText(char* out, std::size_t size, int steps, double error) noexcept {
    std::snprintf(out, size, "%d \xc2\xb7 %+.1f\xc2\xa2", steps, tenth(error));   // "7 · -4.5¢"
}

struct Just {
    int num, den;
};
constexpr Just kJust[] = {{1, 1},  {16, 15}, {9, 8},  {8, 7},  {7, 6},   {6, 5},  {11, 9}, {5, 4}, {9, 7},
                          {4, 3},  {11, 8},  {7, 5},  {10, 7}, {3, 2},   {14, 9}, {8, 5},  {13, 8}, {5, 3},
                          {12, 7}, {7, 4},   {16, 9}, {9, 5},  {11, 6},  {15, 8}, {2, 1}};

} // namespace

EdoFacts edoFacts(int edo) noexcept {
    EdoFacts f;
    const int n = std::clamp(edo, kMinEdo, kMaxEdo);
    const int v2 = n, v3 = nearestSteps(n, 3.0), v5 = nearestSteps(n, 5.0), v7 = nearestSteps(n, 7.0);
    f.edo        = n;
    f.fifth      = v3 - v2;
    f.fifthCents = 1200.0 * f.fifth / n;
    f.fifthError = f.fifthCents - cents(1.5);
    f.large      = 2 * f.fifth - n;
    f.small      = 3 * n - 5 * f.fifth;
    if (f.large <= 0 || f.small < 0)  f.diatonic = Diatonic::None;
    else if (f.small == 0)            f.diatonic = Diatonic::Pentatonic;
    else if (f.large == f.small)      f.diatonic = Diatonic::Equal;
    else if (f.large > f.small)       f.diatonic = Diatonic::Proper;
    else                              f.diatonic = Diatonic::Mavila;
    f.meantone   = tempers(v2, v3, v5, -4, 4, -1);   // 81/80 = 3^4 / (2^4 · 5)
    f.augmented  = tempers(v2, v3, v5, 7, 0, -3);    // 128/125 = 2^7 / 5^3
    f.diminished = tempers(v2, v3, v5, 3, 4, -4);    // 648/625 = 2^3 · 3^4 / 5^4

    // The scale's intervals only where it is one (a degenerate scale has none to stack).
    const bool scale = f.diatonic != Diatonic::None;
    f.majorThird = interval(n, 5.0 / 4.0, v5 - 2 * v2, scale ? 2 * f.large : 0);
    f.minorThird = interval(n, 6.0 / 5.0, v2 + v3 - v5, scale ? f.large + f.small : 0);
    f.seventh    = interval(n, 7.0 / 4.0, v7 - 2 * v2, scale ? n - f.large : 0);
    return f;
}

JustNear nearestJust(double c) noexcept {
    c -= 1200.0 * std::floor(c / 1200.0);   // 0 … 1200
    JustNear best;
    double   bestDistance = 1e9;
    for (const Just& j : kJust) {
        const double error = c - cents(static_cast<double>(j.num) / j.den);
        if (std::abs(error) < bestDistance) {
            bestDistance = std::abs(error);
            best         = {j.num, j.den, error};
        }
    }
    return best;
}

void factRows(const EdoFacts& f, FactRow (&rows)[kFactRows]) noexcept {
    constexpr std::size_t kText = sizeof(FactRow::best);
    auto row = [&rows](std::size_t i, const char* label, bool lit) -> FactRow& {
        std::snprintf(rows[i].label, sizeof(rows[i].label), "%s", label);
        rows[i].best[0] = rows[i].scale[0] = '\0';
        rows[i].lit     = lit;
        return rows[i];
    };
    auto pair = [&](std::size_t i, const char* label, const ChordInterval& c) {
        FactRow& r = row(i, label, c.scale > 0 && c.scale == c.best);
        stepsText(r.best, kText, c.best, c.bestError);
        if (c.scale > 0) stepsText(r.scale, kText, c.scale, c.scaleError);
        else std::snprintf(r.scale, kText, "\xe2\x80\x94");   // an em dash: no scale
    };

    // The fifth is the scale's by definition: both columns.
    pair(0, "Fifth 3/2", {f.fifth, f.fifthError, f.diatonic != Diatonic::None ? f.fifth : 0, f.fifthError});
    pair(1, "Major 3rd 5/4", f.majorThird);
    pair(2, "Minor 3rd 6/5", f.minorThird);
    pair(3, "7th 7/4", f.seventh);

    char* d = row(4, "Diatonic", false).best;
    switch (f.diatonic) {
    case Diatonic::Proper:     std::snprintf(d, kText, "%d:%d", f.large, f.small); break;
    case Diatonic::Equal:      std::snprintf(d, kText, "7 equal steps"); break;
    case Diatonic::Pentatonic: std::snprintf(d, kText, "5 equal steps"); break;
    case Diatonic::Mavila:     std::snprintf(d, kText, "mavila, %d:%d", f.large, f.small); break;
    case Diatonic::None:       std::snprintf(d, kText, "none"); break;
    }

    std::snprintf(row(5, "Meantone 81/80", f.meantone).best, kText, "%s", f.meantone ? "yes" : "no");
    std::snprintf(row(6, "Augmented 128/125", f.augmented).best, kText, "%s", f.augmented ? "yes" : "no");
    std::snprintf(row(7, "Diminished 648/625", f.diminished).best, kText, "%s", f.diminished ? "yes" : "no");
}

} // namespace notegran
