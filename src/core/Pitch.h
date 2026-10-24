#pragma once

#include <cstdint>

#include "core/NoteEvents.h"
#include "core/NoteSet.h"
#include "core/Random.h"

// Pitch (docs/design.md, "Pitch"): from a picked member's pitch to a grain's, and from that to the key it
// goes out on. In 1/512 semitone (Pitch 7.9 units) throughout: 6144 is an octave, 0 .. 65535 the
// range a Pitch 7.9 can carry (MIDI note 0 to 127 + 511/512).
namespace notegran {

inline constexpr std::int32_t kSemitone = 512;
inline constexpr std::int32_t kOctave   = 12 * kSemitone;
inline constexpr std::int32_t kMaxPitch = 0xFFFF;

enum class Quantize : std::uint8_t {
    Off,
    Edo,   // the nearest step of `edo` equal steps per octave, from `root`
    Set,   // the nearest member of the set, in any octave: a scatter stays in its harmony
};

struct PitchParams {
    std::int32_t transpose = 0;             // 1/512 semitone, either way
    int          octaves   = 0;             // 0..4: a grain may move up or down by up to this many
    std::int32_t scatter   = 0;             // 1/512 semitone: the widest a grain moves, triangular
    Quantize     quantize  = Quantize::Off;
    int          edo       = 12;            // 1..96
    int          root      = 9;             // pitch class the EDO steps start from: A, as KeyboardEDO's origin

    // Semitones (as the parameters show them) to 1/512 semitone.
    static std::int32_t fromSemitones(double semitones) noexcept;
};

// A grain's pitch from its member's. In order: Octaves (Pitch draw 0), Transpose, Scatter (Pitch
// draw 1), then Quantize, then folded by octaves into 0 .. kMaxPitch. `index` is the grain's (its
// clock slot, or its trigger's count); `set` is used by Quantize Set and may be empty.
std::int32_t grainPitch(std::int32_t memberPitch, const PitchParams& params, const Random& random,
                        std::int64_t index, const NoteSet& set) noexcept;

// Rounding and snapping, also used alone (Chords' scale, Buffer).
std::int32_t quantizeEdo(std::int32_t pitch, int edo, int root) noexcept;
std::int32_t quantizeToSet(std::int32_t pitch, const NoteSet& set) noexcept;   // unchanged if empty
std::int32_t foldIntoRange(std::int32_t pitch) noexcept;

// The key a grain goes out on, and the pitch it carries.
struct KeyChoice {
    std::uint8_t  key     = 0;
    std::uint16_t pitch79 = 0;   // MIDI 1.0: key << 9; otherwise the grain's exact pitch
};

// MIDI 1.0: the nearest key (a key already sounding is struck again: the same-key rule). MIDI 2.0
// and CLAP: the nearest key that isn't sounding on that group and channel (`sounding(key)`), so
// two grains a few cents apart don't share one; the nearest key if all are; the receiver plays
// the exact pitch (the Pitch 7.9 attribute, or CLAP's tuning expression for the difference).
template <class Sounding>
KeyChoice chooseKey(std::int32_t pitch, Dialect dialect, Sounding&& sounding) noexcept;

// --- inline --------------------------------------------------------------------------------

namespace detail {
inline int nearestKey(std::int32_t pitch) noexcept {   // pitch in 0 .. kMaxPitch
    const int k = (pitch + kSemitone / 2) / kSemitone;
    return k > 127 ? 127 : k;
}
} // namespace detail

template <class Sounding>
KeyChoice chooseKey(std::int32_t pitch, Dialect dialect, Sounding&& sounding) noexcept {
    pitch = pitch < 0 ? 0 : pitch > kMaxPitch ? kMaxPitch : pitch;
    const int near = detail::nearestKey(pitch);
    KeyChoice c;
    if (dialect == Dialect::Midi1) {
        c.key     = static_cast<std::uint8_t>(near);
        c.pitch79 = static_cast<std::uint16_t>(near << 9);
        return c;
    }
    c.key     = static_cast<std::uint8_t>(near);
    c.pitch79 = static_cast<std::uint16_t>(pitch);
    // Outward from the nearest, the closer side first (below on a tie).
    for (int d = 0; d <= 127; ++d) {
        const int lo = near - d, hi = near + d;
        const bool loFirst = pitch - lo * kSemitone <= hi * kSemitone - pitch;
        const int first = loFirst ? lo : hi, second = loFirst ? hi : lo;
        for (int k : {first, second}) {
            if (k < 0 || k > 127 || sounding(static_cast<std::uint8_t>(k))) continue;
            c.key = static_cast<std::uint8_t>(k);
            return c;
        }
    }
    return c;   // every key sounding: the nearest, struck again
}

} // namespace notegran
