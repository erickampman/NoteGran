#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Assigner.h"
#include "core/Pitch.h"

// NoteGran Buffer's logic (docs/design.md, "NoteGran Buffer"), after PMU's BufferGran: a whole generator
// whose input is a history. It captures the notes played on the set channels in the last Window,
// and its grains read that capture: when (Density, Onset random, as Cloud's), what pitch
// (Transpose, Scatter, Quantize) and where (Position, Scan, Spray).
//
// - The capture: a ring of the last 512 notes, each its start, end (open while held: its length
//   grows until the note-off), pitch, velocity and where it came from (group, channel, dialect).
//   Of those, the ones starting in the window [E - Window, E] are read, E the grain's time, or
//   with Freeze where it was frozen. Frozen, new notes aren't recorded (a held note's note-off
//   still fixes its length).
// - The read point, as BufferGran's: a fraction of the window from its oldest end, Position plus
//   Scan's drift plus Spray. Scan is windows per window length (1: across the window in Window's
//   time; negative, backwards), its phase worked from the absolute sample count, so it's the same
//   at any block size or start; Spray a uniform ± spread (Position draw 0). It wraps around the
//   window, except that 1 with no drift or spray is the newest end.
// - Grain Note: the captured note whose start is nearest the read point (the earlier on a tie),
//   with Octaves, Transpose, Scatter and Quantize, its velocity times Level, and Length (or, with
//   Length from Source, its own length, provisional while held).
// - Grain Fragment: the stretch of the capture Length long from the read point: every note
//   starting in it, replayed with its timing from the grain's time, each its own length (past the
//   stretch's end too).
//   One pitch draw for the whole fragment (a chord stays a chord), quantized per note. The notes
//   are copied when the grain starts (up to 64), so a later capture can't change a fragment
//   playing. Up to 32 fragments at once; a 33rd replaces the oldest, whose notes still to come are
//   dropped (those sounding end at their length). A fragment plays out whatever Gate does.
// - Gate on: a grain (not a fragment's notes) plays only while a note is held (or latched) on the
//   set channels. With a trigger channel, its note-ons are the grains and Gate doesn't apply.
// - All Notes Off, a reset and the transport stopping clear the capture.
namespace notegran {

enum class BufferGrain : std::uint8_t { Note, Fragment };

struct BufferParams {
    AssignerSettings settings{Clock::Mode::Free, 8, WhenFull::Skip, true};   // Dry on
    double           windowMs     = 4000.0;          // Free
    std::int64_t     windowBeats  = 8 * kBeat;       // Beats
    bool             freeze       = false;
    std::int32_t     position     = 32768;           // Q16, 0 oldest .. 1 newest
    std::int32_t     scan         = 0;               // Q16, -4 .. 4: windows per window length
    std::int32_t     spray        = 0;               // Q16, 0 .. 1: ± that much of the window
    double           density      = 8.0;             // Free: grains per second
    std::int64_t     divisionNum  = 4;               // Beats: divisionNum per divisionDen beats
    std::int64_t     divisionDen  = 1;
    std::int32_t     onsetRandom  = 0;               // Q16, 0 .. 1
    BufferGrain      grain        = BufferGrain::Note;
    double           lengthMs     = 100.0;           // Free
    std::int64_t     lengthBeats  = kBeat / 4;       // Beats
    bool             sourceLength = false;           // Note: the captured note's own length
    PitchParams      pitch;
    std::int32_t     level        = 65536;           // Q16, 0 .. 2
    std::int32_t     velSpread    = 0;               // Q16
    std::int32_t     lenSpread    = 0;               // Q16
    bool             gate         = false;
};

class BufferCore : public Assigner {
public:
    static constexpr int kCapacity     = 512;   // captured notes
    static constexpr int kMaxFragments = 32;
    static constexpr int kMaxNotes     = 64;    // per fragment

    // Applied from the next block (Freeze at its start).
    void setParams(const BufferParams& params) noexcept;

    bool busy() const noexcept override;

    // The capture as it is now, for an editor and the tests: the notes in the window at the last
    // block's end, oldest first (start and length relative to the window's oldest end, in samples).
    struct Captured {
        std::int64_t start  = 0;
        std::int64_t length = 0;
        std::int32_t pitch  = 0;
        std::uint16_t velocity = 0;
        bool         held   = false;
    };
    int  capture(std::array<Captured, kCapacity>& notes) const noexcept;
    bool frozen() const noexcept { return frozen_; }

    // For the editor's view (docs/design.md, "NoteGran Buffer"), at the last block's end: the
    // window and Length in samples, the read point without Spray (Q16 of the window from its oldest
    // end, 65536 the newest), and the read points the grains took lately (with Spray; a ring,
    // numbered so the editor can tell which are new).
    static constexpr int kReads = 16;
    std::int64_t  windowSamples() const noexcept { return window(); }
    std::int64_t  lengthSamples() const noexcept { return baseLength(); }
    std::int64_t  sampleRate() const noexcept { return clock_.sampleRate(); }
    std::uint32_t readPointNow() const noexcept {
        return static_cast<std::uint32_t>(readFraction(clock_.blockEnd(), 0, false) >> 16);
    }
    std::int64_t  readsMade() const noexcept { return readsMade_; }
    std::uint32_t readAt(std::int64_t k) const noexcept { return reads_[static_cast<std::size_t>(k % kReads)]; }

private:
    struct Note {
        std::int64_t start = 0;
        std::int64_t end   = 0;   // kOpen while held
        Member       member;      // where it came from, its pitch and velocity
    };
    struct FragmentNote {
        std::int64_t offset = 0;   // from the fragment's start
        std::int64_t length = 1;
        Member       member;
    };
    struct Fragment {
        bool          active  = false;
        std::int64_t  serial  = 0;
        std::int64_t  start   = 0;        // the grain's time
        std::int64_t  index   = 0;        // the grain's draw index
        std::uint16_t trigger = 0xFFFF;
        std::int64_t  lastTime = 0;       // the last note collected
        int           next    = 0;        // the next note to collect
        int           count   = 0;
        std::array<FragmentNote, kMaxNotes> notes{};
    };

    static constexpr std::int64_t kOpen = INT64_MAX;

    void reapply() noexcept override { setParams(params_); }
    void collectGrains(std::size_t& count) noexcept override;
    void play(const Grain& g, NoteOut& out) noexcept override;
    bool playsWithoutSet() const noexcept override { return true; }
    void heard(const Heard& h, std::int64_t time, std::size_t next, std::size_t& count) noexcept override;
    void cleared() noexcept override;

    std::int64_t window() const noexcept;
    std::int64_t anchor(std::int64_t time) const noexcept { return frozen_ ? freezeTime_ : time; }
    std::int64_t readFraction(std::int64_t time, std::int64_t index, bool spray) const noexcept;   // Q32
    std::int64_t readPoint(std::int64_t time, std::int64_t index) const noexcept;
    void         noteRead(std::int64_t fraction) noexcept;
    std::int64_t baseLength() const noexcept;
    const Note&  noteAt(std::int64_t serial) const noexcept { return notes_[static_cast<std::size_t>(serial % kCapacity)]; }
    std::int64_t oldest() const noexcept { return written_ > kCapacity ? written_ - kCapacity : 0; }
    void         startFragment(const Grain& g) noexcept;
    void         collectFragment(int slot, std::size_t* count) noexcept;
    void         dropFragment(int slot) noexcept;
    void         sortGrains(std::size_t count) noexcept;

    BufferParams                          params_;
    std::array<Note, kCapacity>           notes_{};
    std::int64_t                          written_    = 0;   // notes captured ever: the next's serial
    bool                                  frozen_     = false;
    std::int64_t                          freezeTime_ = 0;
    std::array<Fragment, kMaxFragments>   fragments_{};
    std::int64_t                          fragmentsStarted_ = 0;
    std::array<std::uint32_t, kReads>     reads_{};   // Q16, for the editor
    std::int64_t                          readsMade_ = 0;
};

} // namespace notegran
