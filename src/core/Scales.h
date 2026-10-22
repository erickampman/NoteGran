#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Familiar scales, and mapping them into an N-EDO (PLAN.md, "NoteGran Chords").
//
// A familiar scale is stored spelled: each degree as its place on the chain of fifths from the key
// (C 0, G 1, D 2, ... F −1, Bb −2, ...), so F# (6) and Gb (−6) are different degrees. A place p
// is (7p mod 12) semitones above the key in 12-EDO, and in an N-EDO either:
//   By fifths: p × F mod N steps, F the EDO's own fifth (its nearest to 702¢). Never ties.
//   Nearest:   the step nearest its 12-EDO cents; a tie goes up for a sharp-side place (p > 0), down
//              for a flat-side one (p < 0).
// The grid's step 0 is the origin pitch class (default A, as KeyboardEDO's), and the key is placed
// on it by the same mapping, so with an A origin in 19-EDO, C is 5 steps up (316¢), not 300¢.
namespace notegran {

struct FamiliarScale {
    const char*  name;
    std::uint8_t count;
    std::int8_t  fifths[7];   // ascending in 12-EDO pitch from the key
};

// The menu, in order (the parameter's steps). Appended, never inserted: saved songs hold the index.
const FamiliarScale* familiarScales() noexcept;
std::size_t          familiarScaleCount() noexcept;

enum class MapBy : std::uint8_t { Fifths, Nearest };

// The EDO's own fifth: its step count nearest 701.955 cents.
int edoFifth(int edo) noexcept;

// A pitch class (0..11, C = 0) as a place on the chain of fifths, spelled as a pitch-class menu
// shows it: F (−1) .. B (5) and F# (6) as they are, the others as flats, Bb (−2) .. Db (−5).
int fifthsOfPitchClass(int pitchClass) noexcept;

// A scale on an N-EDO grid: its degrees as steps above the key, ascending and distinct, and where
// the key is.
struct EdoScale {
    static constexpr std::size_t kMaxDegrees = 96;

    int          edo     = 12;
    int          origin  = 9;     // pitch class of step 0
    int          keyStep = 0;     // the key's step above the origin, 0 .. edo - 1
    std::size_t  count   = 0;
    std::array<std::int32_t, kMaxDegrees> steps{};      // above the key, 0 .. edo - 1, ascending
    // A familiar scale's degrees as 12-EDO semitones above the key (for Input: Familiar), and its
    // key as a 12-EDO pitch class; -1 when the scale came from a channel.
    std::array<std::int8_t, kMaxDegrees>  semitones{};
    int                                   keyPitchClass = -1;

    bool familiar() const noexcept { return keyPitchClass >= 0; }

    // 1/512-semitone pitch of an absolute step (from the origin in MIDI octave −1).
    std::int32_t pitchOfStep(std::int64_t step) const noexcept;
    // The step nearest a pitch.
    std::int64_t stepNear(std::int32_t pitch) const noexcept;
    // The degree whose pitch class is this absolute step's, or -1.
    int degreeOfStep(std::int64_t step) const noexcept;
    // The degree whose familiar 12-EDO pitch class is this key's, or -1 (familiar scales only).
    int degreeOfSemitone(int midiKey) const noexcept;
    // The absolute step of degree d above the key at absolute step `base` (degree 0's step). Any
    // integer d: count is the next octave's degree 0, -1 the octave below's last degree.
    std::int64_t stepOfDegree(int degree, std::int64_t base) const noexcept;
};

// A familiar scale in key `keyFifths` (C 0, G 1, F −1, ...) mapped into `edo` steps from `origin`.
EdoScale mapFamiliar(const FamiliarScale& scale, int keyFifths, int edo, int origin, MapBy map) noexcept;

// A scale from absolute steps (a scale channel's notes, already in the EDO): their pitch classes.
EdoScale scaleFromSteps(const std::int64_t* steps, std::size_t n, int edo, int origin) noexcept;

} // namespace notegran
