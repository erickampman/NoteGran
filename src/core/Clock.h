#pragma once

#include <cstdint>

// The clock (PLAN.md, "Time"): where a block is, in samples (Free) or in beats (Beats), and which
// slots of a rate start in it. A slot is one tick of a rate: slot n of a grid has a fixed position,
// so nothing is carried from block to block. Output can't depend on block size, a host can start
// anywhere (Umpire playing a clip from bar 5), and n is the index a counter-based random draw is
// keyed on.
//
// Integers throughout, exact (rational, 128-bit intermediates). The only floating point is turning
// the host's tempo (to 1/65536 BPM), the sample rate and a rate into integers with llround, which
// gives the same result on every machine.
namespace notegran {

// CLAP's beat time: 2^31 per beat (CLAP_BEATTIME_FACTOR).
inline constexpr std::int64_t kBeat = std::int64_t{1} << 31;

// `count` slots every `span` position units: samples in Free, beat units in Beats. Slot n is at
// ceil(n * span / count). count 0 is no slots.
struct Grid {
    std::int64_t count = 0;
    std::int64_t span  = 1;

    // Free: grains per second (fractional), at an integer sample rate.
    static Grid perSecond(double rate, std::int64_t sampleRate) noexcept;
    // Beats: `num` slots every `den` beats (1/4 per beat is perBeat(1, 4); 3 per beat, perBeat(3, 1)).
    static Grid perBeat(std::int64_t num, std::int64_t den) noexcept;

    std::int64_t position(std::int64_t n) const noexcept;   // slot n's position
};

// What the host says about its transport at the block's first sample (CLAP's clap_event_transport).
struct Transport {
    bool         hasTempo = false;
    double       tempo    = 120.0;   // BPM
    bool         hasBeats = false;   // a beat timeline
    bool         playing  = false;
    std::int64_t beats    = 0;       // beat position, in kBeat units
};

class Clock {
public:
    enum class Mode : std::uint8_t { Free, Beats };

    // What changed at this block's start, for the caller.
    struct Change {
        bool stopped = false;   // Beats: the host's transport stopped (end every note, PLAN.md)
        bool jumped  = false;   // Beats: the host's beat position moved other than by playing (loop, seek)
    };

    void setSampleRate(double sampleRate) noexcept;
    void setMode(Mode mode) noexcept { mode_ = mode; }
    Mode mode() const noexcept { return mode_; }
    std::int64_t sampleRate() const noexcept { return sampleRate_; }

    // The block [start, start + frames), start an absolute sample count; `transport` is null when
    // the host sends none (then Beats runs at 120 BPM on its own count).
    Change begin(std::int64_t start, std::int64_t frames, const Transport* transport) noexcept;

    std::int64_t blockStart() const noexcept { return start_; }
    std::int64_t blockEnd() const noexcept { return start_ + frames_; }

    // The position at an absolute sample in this block: the sample itself in Free, beat units in
    // Beats. And its inverse: the first sample at or after a position (may be outside the block).
    std::int64_t positionAt(std::int64_t sample) const noexcept;
    std::int64_t sampleAt(std::int64_t position) const noexcept;

    // Calls f(n, sample) for each slot of `grid` starting in this block, in order. A slot's sample
    // is the first one at or after its exact position.
    template <class F>
    void slots(const Grid& grid, F&& f) const {
        if (grid.count <= 0 || frames_ <= 0) return;
        for (std::int64_t n = firstSlot(grid);; ++n) {
            const std::int64_t s = slotSample(grid, n);
            if (s >= blockEnd()) break;
            f(n, s < start_ ? start_ : s);
        }
    }

    // Lengths: milliseconds, or beat units at the current tempo, as a whole number of samples
    // (at least 1).
    std::int64_t samplesForMs(double ms) const noexcept;
    std::int64_t samplesForBeats(std::int64_t beatUnits) const noexcept;

    double tempo() const noexcept { return tempo_; }

private:
    std::int64_t        firstSlot(const Grid& grid) const noexcept;
    std::int64_t        slotSample(const Grid& grid, std::int64_t n) const noexcept;
    static double       clampTempo(double bpm) noexcept;
    void                setTempo(double bpm) noexcept;
    std::int64_t        beatAt(std::int64_t sample) const noexcept;
    __int128            exactBeatAt(std::int64_t sample) const noexcept;   // times samplesPerMinuteQ()
    std::int64_t        sampleAtBeat(std::int64_t beat) const noexcept;
    std::int64_t        samplesPerMinuteQ() const noexcept;

    Mode         mode_       = Mode::Free;
    std::int64_t sampleRate_ = 48000;
    std::int64_t start_      = 0;
    std::int64_t frames_     = 0;

    // Beats: position(s) = anchorPos_ + floor((s - anchorSample_) * tempoQ_ * kBeat / (60 * sr * 65536)),
    // tempoQ_ the tempo in 1/65536 BPM.
    double       tempo_        = 0.0;
    std::int64_t tempoQ_       = 0;
    std::int64_t anchorSample_ = 0;
    std::int64_t anchorPos_    = 0;
    // Beats: where this block's slots start, exactly: a beat position times samplesPerMinuteQ().
    // Playing on, it's where the last block ended, so a slot is never played twice or skipped.
    __int128     from_         = 0;
    bool         started_      = false;   // a block has been seen
    bool         following_    = false;   // the last block followed the host's timeline
};

} // namespace notegran
