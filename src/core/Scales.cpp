#include "core/Scales.h"

#include <algorithm>
#include <cmath>

#include "core/Pitch.h"

namespace notegran {

namespace {

// Places on the chain of fifths from the key: C 0, D 2, E 4, F −1, G 1, A 3, B 5; a sharp adds 7, a
// flat takes 7 away.
constexpr FamiliarScale kScales[] = {
    {"Major (Ionian)", 7, {0, 2, 4, -1, 1, 3, 5}},
    {"Dorian", 7, {0, 2, -3, -1, 1, 3, -2}},
    {"Phrygian", 7, {0, -5, -3, -1, 1, -4, -2}},
    {"Lydian", 7, {0, 2, 4, 6, 1, 3, 5}},
    {"Mixolydian", 7, {0, 2, 4, -1, 1, 3, -2}},
    {"Minor (Aeolian)", 7, {0, 2, -3, -1, 1, -4, -2}},
    {"Locrian", 7, {0, -5, -3, -1, -6, -4, -2}},
    {"Melodic minor", 7, {0, 2, -3, -1, 1, 3, 5}},
    {"Dorian b2", 7, {0, -5, -3, -1, 1, 3, -2}},
    {"Lydian augmented", 7, {0, 2, 4, 6, 8, 3, 5}},
    {"Lydian dominant", 7, {0, 2, 4, 6, 1, 3, -2}},
    {"Mixolydian b6", 7, {0, 2, 4, -1, 1, -4, -2}},
    {"Locrian #2", 7, {0, 2, -3, -1, -6, -4, -2}},
    {"Altered", 7, {0, -5, -3, -8, -6, -4, -2}},
    {"Harmonic minor", 7, {0, 2, -3, -1, 1, -4, 5}},
    {"Locrian #6", 7, {0, -5, -3, -1, -6, 3, -2}},
    {"Ionian #5", 7, {0, 2, 4, -1, 8, 3, 5}},
    {"Dorian #4", 7, {0, 2, -3, 6, 1, 3, -2}},
    {"Phrygian dominant", 7, {0, -5, 4, -1, 1, -4, -2}},
    {"Lydian #2", 7, {0, 9, 4, 6, 1, 3, 5}},
    {"Ultralocrian", 7, {0, -5, -3, -8, -6, -4, -9}},
    {"Harmonic major", 7, {0, 2, 4, -1, 1, -4, 5}},
    {"Major pentatonic", 5, {0, 2, 4, 1, 3}},
    {"Minor pentatonic", 5, {0, -3, -1, 1, -2}},
    {"Whole tone", 6, {0, 2, 4, 6, 8, 10}},
    // The diminished scale's three 8-note sets, a half step apart, each as two diminished sevenths.
    {"Diminished (whole-half)", 8, {0, 2, -3, -1, -6, -4, 3, 5}},     // C D Eb F Gb Ab A B
    {"Diminished (half-whole)", 8, {0, -5, -3, 4, 6, 1, 3, -2}},      // C Db Eb E F# G A Bb
    {"Diminished (from the 2nd)", 8, {-5, 2, 4, -1, 1, -4, -2, 5}},   // Db D E F G Ab Bb B: no C
    {"Blues", 6, {0, -3, -1, -6, 1, -2}},                             // C Eb F Gb G Bb
};

std::int64_t mod(std::int64_t a, std::int64_t n) noexcept { return ((a % n) + n) % n; }

int semitonesOf(int fifths) noexcept { return static_cast<int>(mod(std::int64_t{fifths} * 7, 12)); }

// The step nearest `cents` (0 .. 1199) of an N-EDO; a tie up if `up`.
std::int64_t nearestStep(std::int64_t cents, int edo, bool up) noexcept {
    const std::int64_t twice = 2 * cents * edo;   // 2 × the exact step, over 1200
    const std::int64_t whole = twice / 2400;
    const std::int64_t rest  = twice % 2400;
    if (rest > 1200 || (rest == 1200 && up)) return whole + 1;
    return whole;
}

// A place on the chain of fifths as steps above the key, 0 .. edo - 1.
std::int64_t stepsOf(int fifths, int edo, MapBy map) noexcept {
    if (map == MapBy::Fifths) return mod(std::int64_t{fifths} * edoFifth(edo), edo);
    return mod(nearestStep(std::int64_t{semitonesOf(fifths)} * 100, edo, fifths > 0), edo);
}

} // namespace

const FamiliarScale* familiarScales() noexcept { return kScales; }
std::size_t          familiarScaleCount() noexcept { return sizeof(kScales) / sizeof(kScales[0]); }

int edoFifth(int edo) noexcept {
    // round(edo × 701.955 / 1200), in integers.
    return static_cast<int>((std::int64_t{edo} * 701955 + 600000) / 1200000);
}

int fifthsOfPitchClass(int pitchClass) noexcept {
    static constexpr int kFifths[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};   // C Db D Eb E F F# G Ab A Bb B
    return kFifths[mod(pitchClass, 12)];
}

std::int32_t EdoScale::pitchOfStep(std::int64_t step) const noexcept {
    const std::int64_t num = step * kOctave;
    const std::int64_t q   = num >= 0 ? (2 * num + edo) / (2 * edo) : -((-2 * num + edo) / (2 * edo));
    return static_cast<std::int32_t>(std::int64_t{origin} * kSemitone + q);
}

std::int64_t EdoScale::stepNear(std::int32_t pitch) const noexcept {
    const std::int64_t num = (std::int64_t{pitch} - std::int64_t{origin} * kSemitone) * edo;
    // num / kOctave, to the nearest (halves up)
    const std::int64_t twice = 2 * num + kOctave;
    const std::int64_t den   = 2 * kOctave;
    return twice >= 0 ? twice / den : -((-twice + den - 1) / den);
}

int EdoScale::degreeOfStep(std::int64_t step) const noexcept {
    const std::int64_t rel = mod(step - keyStep, edo);
    for (std::size_t d = 0; d < count; ++d)
        if (steps[d] == rel) return static_cast<int>(d);
    return -1;
}

int EdoScale::degreeOfSemitone(int midiKey) const noexcept {
    if (!familiar()) return -1;
    const auto rel = static_cast<std::int8_t>(mod(midiKey - keyPitchClass, 12));
    for (std::size_t d = 0; d < count; ++d)
        if (semitones[d] == rel) return static_cast<int>(d);
    return -1;
}

std::int64_t EdoScale::stepOfDegree(int degree, std::int64_t base) const noexcept {
    const auto         n      = static_cast<std::int64_t>(count);
    const std::int64_t octave = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    const std::int64_t d      = degree - octave * n;
    return base + steps[static_cast<std::size_t>(d)] + octave * edo;
}

EdoScale mapFamiliar(const FamiliarScale& scale, int keyFifths, int edo, int origin, MapBy map) noexcept {
    EdoScale s;
    s.edo           = std::clamp(edo, 1, 96);
    s.origin        = static_cast<int>(mod(origin, 12));
    s.keyStep       = static_cast<int>(stepsOf(keyFifths - fifthsOfPitchClass(s.origin), s.edo, map));
    s.keyPitchClass = semitonesOf(keyFifths);

    // Each degree's steps above the key; ascending, and a degree landing on a step already taken
    // merges into it. The key itself needn't be among them (Diminished (from the 2nd)).
    struct Degree { std::int32_t step; std::int8_t semis; };
    std::array<Degree, FamiliarScale::kMaxDegrees> d{};
    std::size_t           n = 0;
    for (std::uint8_t i = 0; i < scale.count && i < FamiliarScale::kMaxDegrees; ++i) {
        const auto step = static_cast<std::int32_t>(stepsOf(scale.fifths[i], s.edo, map));
        bool       seen = false;
        for (std::size_t j = 0; j < n; ++j) seen = seen || d[j].step == step;
        if (!seen) d[n++] = Degree{step, static_cast<std::int8_t>(semitonesOf(scale.fifths[i]))};
    }
    std::sort(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(n),
              [](const Degree& a, const Degree& b) { return a.step < b.step; });
    s.count = n;
    for (std::size_t i = 0; i < n; ++i) {
        s.steps[i]     = d[i].step;
        s.semitones[i] = d[i].semis;
    }
    return s;
}

EdoScale scaleFromSteps(const std::int64_t* steps, std::size_t n, int edo, int origin) noexcept {
    EdoScale s;
    s.edo    = std::clamp(edo, 1, 96);
    s.origin = static_cast<int>(mod(origin, 12));
    std::array<bool, EdoScale::kMaxDegrees> has{};
    for (std::size_t i = 0; i < n; ++i) has[static_cast<std::size_t>(mod(steps[i], s.edo))] = true;
    for (int step = 0; step < s.edo; ++step)
        if (has[static_cast<std::size_t>(step)]) s.steps[s.count++] = step;
    return s;   // keyStep 0: degrees are the pitch classes above the origin
}

} // namespace notegran
