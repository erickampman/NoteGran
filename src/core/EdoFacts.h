#pragma once

#include <cstddef>

// EdoFacts: what an N-EDO does with chords, for Chords' This EDO panel and its chord read-out
// (docs/design.md, "NoteGran Chords": This EDO). Pure math on N, no CLAP or UMP. The diatonic and
// comma part is EDOSpread12's src/EdoFacts (the same struct and rules), with the chord intervals
// added; both move to uw-core, as one, after the release.
//
// The EDO's patent val is the nearest whole number of steps to each prime: v2 = N, v3 =
// round(N log2 3), v5 = round(N log2 5), v7 = round(N log2 7). Its best step for a ratio is the
// val's: 3/2 is v3 - v2, 5/4 is v5 - 2 v2, 6/5 is v2 + v3 - v5, 7/4 is v7 - 2 v2. Its fifth F
// makes the 5L2s (diatonic) scale by stacking: large step L = 2F - N, small step s = 3N - 5F. The
// scale's own intervals, built from the fifth as Map By fifths builds them, are the major third
// 2L (four fifths up), the minor third L + s (three fifths down) and the minor seventh N - L (two
// fifths down). Where they differ from the best steps, a chord stacked in the scale misses the
// EDO's best thirds or seventh. A comma, as prime exponents (a2, a3, a5), is tempered out (counts
// as no steps) when a2 v2 + a3 v3 + a5 v5 = 0.

namespace notegran {

// What the chain of fifths gives as a 7-note scale.
enum class Diatonic : int {
    Proper,       // L > s > 0: a diatonic scale (12-EDO's 2:1, 19's 3:2, 17's 3:1, ...)
    Equal,        // L = s: seven equal steps (7-EDO and its multiples)
    Pentatonic,   // s = 0: the half steps vanish, five equal steps (5-, 10-, 15-EDO ...)
    Mavila,       // s > L > 0: the fifth is flat of 7-EDO's, so large and small swap (9, 16, 23 ...)
    None,         // L <= 0 or s < 0: a fifth of 600 cents or less, or sharp of 720
};

// A chord interval in the EDO: the EDO's best step for its ratio, and the step the diatonic scale
// gives it. Errors in cents from the just ratio, + sharp.
struct ChordInterval {
    int    best       = 0;
    double bestError  = 0.0;
    int    scale      = 0;      // 0 or less: the scale has none (its steps are degenerate)
    double scaleError = 0.0;
};

struct EdoFacts {
    int      edo        = 12;
    int      fifth      = 7;      // steps
    double   fifthCents = 700.0;
    double   fifthError = 0.0;    // cents from 3/2 (701.955), + sharp
    int      large      = 2;      // the 5L2s steps, L and s; meaningful as Diatonic says
    int      small      = 1;
    Diatonic diatonic   = Diatonic::Proper;
    bool     meantone   = true;   // tempers out 81/80, the syntonic comma
    bool     augmented  = true;   // 128/125, the diesis: three major thirds make an octave
    bool     diminished = true;   // 648/625: four minor thirds make an octave

    ChordInterval majorThird;     // 5/4
    ChordInterval minorThird;     // 6/5
    ChordInterval seventh;        // 7/4, against the scale's minor seventh
};

// The facts for N-EDO, N clamped to 1 … 96 (Chords' range).
EdoFacts edoFacts(int edo) noexcept;

// --- naming an interval -------------------------------------------------------------------------

// A just ratio an interval is near, and how far: for the chord read-out ("5/4 -4.5¢").
struct JustNear {
    int    num   = 1;
    int    den   = 1;
    double error = 0.0;   // cents, + sharp of the ratio
};

// The ratio from a short list of chord intervals (1/1, 16/15, 9/8, 8/7, 7/6, 6/5, 11/9, 5/4, 9/7,
// 4/3, 11/8, 7/5, 10/7, 3/2, 14/9, 8/5, 13/8, 5/3, 12/7, 7/4, 16/9, 9/5, 11/6, 15/8, 2/1) nearest
// `cents`, taken within the octave first (so a 10th is named as a 3rd). A tie goes to the earlier.
JustNear nearestJust(double cents) noexcept;

// --- the panel ----------------------------------------------------------------------------------

// The This EDO panel's rows: a label, then the EDO's best and the scale's, as text, UTF-8. `lit`
// marks the scale's value when it's the EDO's best (a chord stacked in the scale gets it), or a
// comma tempered out. For 22-EDO:
//   Fifth 3/2        13 · +7.1¢     13 · +7.1¢       (steps · cents from the ratio)
//   Major 3rd 5/4     7 · -4.5¢      8 · +50.0¢
//   Minor 3rd 6/5     6 · +11.6¢     5 · -42.9¢
//   7th 7/4          18 · +13.0¢    18 · +13.0¢
//   Diatonic          4:1
//   Meantone 81/80    no                  (then Augmented, Diminished)
struct FactRow {
    char label[32];
    char best[32];
    char scale[32];   // "" where the row has one value (in `best`)
    bool lit;
};
inline constexpr std::size_t kFactRows = 8;
void factRows(const EdoFacts& f, FactRow (&rows)[kFactRows]) noexcept;

} // namespace notegran
