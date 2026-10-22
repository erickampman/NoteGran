#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Assigner.h"
#include "core/Pitch.h"

// NoteGran Burst's logic (PLAN.md, "NoteGran Burst"), after PMU's IFSGran: each note-on in the set
// (or on the trigger channel) starts a phrase of grains rooted on that note, its points in time and
// pitch drawn from a two-transform attractor.
//
// - The attractor (IFSGran's scatter, PMU src/Module/IFSGranModule.cpp, computeScatter): two maps
//   on [0, 1]², each a fixed point, a contraction and a rotation; each step takes T1 with chance
//   Probability. After 30 warm-up steps, Count points, sorted by x (time). Worked in Q30 fixed
//   point, the draws Timing draws of the phrase (its strike count with Vary, else 0), so a phrase
//   is the same on every machine.
// - Time: the first point is at the note-on, and the points spread so that Duration is one pass
//   with the last gap included (point i at (x - x0) / (xLast - x0) × (Count - 1) / Count). So Loop
//   repeats every Duration. Pitch: (y - 0.5) × 2 × Range from the root.
// - Positions are samples: Duration and Length in beats are turned into samples at the strike and
//   at the grain, so a phrase doesn't move when the host loops or seeks.
// - Release (the root's note-off; with a trigger channel, the trigger note's): Finish plays the
//   phrase out, Stop drops its grains still to come, Loop repeats it while held and then finishes
//   the pass it's in.
// - Up to 16 phrases; a 17th replaces the oldest, whose grains still to come are dropped.
namespace notegran {

enum class Release : std::uint8_t { Finish, Stop, Loop };

struct BurstTransform {
    double fixedX      = 0.5;    // 0 .. 1: time
    double fixedY      = 0.5;    // 0 .. 1: pitch
    double contraction = 0.6;    // 0.01 .. 0.99
    double rotation    = 0.0;    // degrees
};

struct BurstParams {
    AssignerSettings settings;
    BurstTransform   t1{0.2, 0.4, 0.6, 25.0};
    BurstTransform   t2{0.8, 0.6, 0.6, -25.0};
    std::int32_t     probability    = 32768;          // Q16: the share of T1
    int              count          = 8;              // 1 .. 64 points
    bool             vary           = true;           // each phrase its own points
    double           durationMs     = 800.0;          // Free
    std::int64_t     durationBeats  = 2 * kBeat;      // Beats
    std::int32_t     range          = 12 * 512;       // 1/512 semitone: the attractor's pitch axis, ±
    Release          release        = Release::Finish;
    double           lengthMs       = 100.0;          // Free
    std::int64_t     lengthBeats    = kBeat / 4;      // Beats
    std::int32_t     level          = 65536;          // Q16, 0 .. 2
    std::int32_t     decay          = 32768;          // Q16, 0 .. 1: velocity's fall across the phrase
    std::int32_t     velSpread      = 0;              // Q16
    std::int32_t     lenSpread      = 0;              // Q16
    PitchParams      pitch;                           // Transpose and Quantize (no Octaves or Scatter)
};

class BurstCore : public Assigner {
public:
    static constexpr int kMaxPhrases = 16;
    static constexpr int kMaxPoints  = 64;

    // Applied from the next block; a phrase keeps the points and duration it started with.
    void setParams(const BurstParams& params) noexcept;

    bool busy() const noexcept override;

    struct Point {
        std::int32_t time;     // Q16, 0 .. 1: of Duration
        std::int32_t offset;   // 1/512 semitone from the root
    };
    // A phrase's points (sorted by time), into `points`; returns how many. `key` is the phrase's
    // draw index.
    int phrasePoints(std::int64_t key, std::array<Point, kMaxPoints>& points) const noexcept;

    // The attractor's points behind a phrase, as drawn (Q30, 0 .. 1 each way, not sorted), and
    // whether each came from T1: what an editor draws in the attractor's own space.
    struct RawPoint {
        std::int64_t x, y;
        bool         t1;
    };
    int attractor(std::int64_t key, std::array<RawPoint, kMaxPoints>& points) const noexcept;

private:
    struct Phrase {
        bool          active   = false;   // has grains to come, or in this block
        bool          ended    = false;   // every grain collected
        bool          held     = true;
        bool          byTrigger = false;
        std::uint8_t  group = 0, channel = 0, key = 0;   // the note whose release releases it
        Member        root;
        std::uint16_t trigger  = 0xFFFF;
        std::int64_t  serial   = 0;
        std::int64_t  start    = 0;       // sample
        std::int64_t  duration = 1;       // samples
        std::int64_t  pass     = 0;
        std::int64_t  lastPass = 0;
        std::int64_t  lastTime = 0;       // the last grain collected
        int           next     = 0;
        int           count    = 0;
        std::array<Point, kMaxPoints> points{};
    };

    void reapply() noexcept override { setParams(params_); }
    void collectGrains(std::size_t& count) noexcept override;
    void play(const Grain& g, NoteOut& out) noexcept override;
    bool startsPhrases() const noexcept override { return true; }
    void heard(const Heard& h, std::int64_t time, std::size_t next, std::size_t& count) noexcept override;
    void cleared() noexcept override;

    void startPhrase(const Member& root, const Heard& h, std::int64_t time, std::size_t next,
                     std::size_t& count) noexcept;
    void collect(int slot, std::size_t& count) noexcept;
    void drop(int slot, std::int64_t from, std::size_t next, std::size_t& count) noexcept;
    void sortGrains(std::size_t from, std::size_t count) noexcept;

    BurstParams params_;
    // The maps as Q30: the fixed point, and contraction × (cos, sin) of the rotation.
    struct Map {
        std::int64_t fx, fy, a, b;
    };
    Map                               maps_[2]{};
    std::array<Phrase, kMaxPhrases>   phrases_{};
    std::int64_t                      strikes_ = 0;
};

} // namespace notegran
