#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/NoteEvents.h"
#include "core/NoteSet.h"
#include "core/OwnedNotes.h"
#include "core/Scales.h"

// NoteGran Chords' logic (PLAN.md, "NoteGran Chords"), with no CLAP: each note held becomes a chord
// built on it in a scale, held as long as the note. The scale is a familiar one mapped into an
// N-EDO, or the notes held on a scale channel. A note in the scale sounds on its EDO step with its
// chord; one outside it passes through as played (or goes to the nearest degree, or is dropped).
//
// Each input note remembers its chord (latching, as Retune's), so a change to the scale or the
// settings while it's held can't leave a chord note sounding. Overlapping chords share their
// notes: one already sounding isn't struck again, and ends with the last chord using it.
namespace notegran {

enum class InputMatch : std::uint8_t {
    Familiar,   // a 12-EDO keyboard: its keys matched against the familiar scale
    Edo,        // pitches already in the EDO (KeyboardEDO): matched against the mapped steps
};

enum class OffScale : std::uint8_t { Pass, Nearest, Drop };

enum class Voicing : std::uint8_t {
    Close,    // as stacked (after the inversion)
    Open,     // the 2nd, 4th and 6th notes from the bottom an octave up
    Drop2,    // the second-highest note an octave down
    Spread,   // note i up by i / 2 octaves
};

struct ChordsParams {
    std::size_t scale        = 0;              // index into familiarScales()
    int         keyFifths    = 0;              // the key on the chain of fifths: C 0, G 1, F −1, ...
    MapBy       map          = MapBy::Fifths;
    int         edo          = 12;             // 1 .. 96
    int         origin       = 9;              // pitch class of the EDO's step 0: A
    InputMatch  input        = InputMatch::Familiar;
    int         scaleChannel = -1;             // 0..15: the scale is the notes held there; -1: the menu
    int         size         = 3;              // 1 .. 6 notes
    int         stack        = 2;              // degrees between chord notes: 2 thirds, 3 fourths, 4 fifths
    int         inversion    = 0;              // 0 .. size - 1: that many lowest notes an octave up
    Voicing     voicing      = Voicing::Close;
    int         octave       = 0;              // -2 .. 2
    OffScale    offScale     = OffScale::Pass;
    std::int32_t level       = 65536;          // Q16, 0 .. 2: times the input's velocity
    int         voices       = 8;
    WhenFull    whenFull     = WhenFull::Skip;
};

class ChordsCore {
public:
    static constexpr std::size_t kMaxHeld   = 256;   // input notes held at once
    static constexpr std::size_t kMaxShared = 512;   // distinct chord notes sounding
    static constexpr int         kMaxSize   = 6;

    ChordsCore() noexcept;

    void setParams(const ChordsParams& params) noexcept;

    // Reads `inputs` (sorted by time), writes chord notes to `out`, sets each input's `pass`.
    void process(InputEvent* inputs, std::size_t inputCount, NoteOut& out) noexcept;

    // Ends every chord at `time` and forgets the held notes and the scale channel's scale.
    void reset(std::int64_t time, NoteOut& out) noexcept;

    bool busy() const noexcept { return !owned_.empty(); }

    const EdoScale&   scale() const noexcept { return scale_; }
    const OwnedNotes& owned() const noexcept { return owned_; }

    // The chord for a note at `pitch` (MIDI key `key` when it's 12-EDO), as pitches, lowest first;
    // 0 notes if it's off the scale (and OffScale isn't Nearest). For tests, and a window later.
    std::size_t chordFor(std::int32_t pitch, std::array<std::int32_t, kMaxSize>& pitches) const noexcept;

private:
    struct Held {
        bool          used    = false;
        std::uint8_t  group   = 0;
        std::uint8_t  channel = 0;
        std::uint8_t  key     = 0;
        bool          passed  = false;   // played through as it came: its off passes too
        std::uint8_t  count   = 0;
        std::array<std::uint16_t, kMaxSize> notes{};   // indices into shared_
    };
    struct Shared {
        bool          used    = false;
        std::uint8_t  group   = 0;
        std::uint8_t  channel = 0;
        Dialect       dialect = Dialect::Midi2;
        std::int32_t  pitch   = 0;
        NoteId        id      = 0;
        std::uint16_t refs    = 0;
    };

    void   rebuildScale() noexcept;
    bool   locate(std::int32_t pitch, int& degree, std::int64_t& rootStep) const noexcept;
    Held*  find(std::uint8_t group, std::uint8_t channel, std::uint8_t key) noexcept;
    Held*  freeHeld() noexcept;
    void   release(Held& h, std::int64_t time, NoteOut& out) noexcept;
    void   strike(Held& h, std::int64_t time, Dialect dialect, std::uint16_t velocity,
                  const std::array<std::int32_t, kMaxSize>& pitches, std::size_t n, NoteOut& out) noexcept;

    ChordsParams params_;
    EdoScale     scale_;
    NoteSet      scaleSet_;   // the scale channel's notes, latched
    OwnedNotes   owned_;
    std::array<Held, kMaxHeld>     held_{};
    std::array<Shared, kMaxShared> shared_{};
};

} // namespace notegran
