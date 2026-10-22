#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Assigner.h"
#include "core/Pitch.h"

// NoteGran Grid's logic (PLAN.md, "NoteGran Grid"), after PMU's ClockGran: picks on a beat grid
// whose positions have tiers. A rate's slots are grouped into a repeating cycle (a meter), and
// each slot belongs to one of five tiers, each with its own Probability, Level, Offset, Chaos and
// Pick. So a bar's downbeat can always take the lowest note, and the spare sixteenths a random
// one, sometimes.
//
// Locked to the beat timeline: slot n is n's place in the cycle (n mod its length), so a Grid on
// one row and a sequence on another stay together. Always Beats (its own count when the host has
// no timeline). Each slot plays with its tier's Probability (Chance draw 0), then Density sub-picks
// (a fraction by Chance draw 1), each up to Smear × Chaos of the way into the slot (Timing draw j).
namespace notegran {

inline constexpr int kTiers = 5;

// Meters, as ClockGran's: a cycle of slot ticks, counted 2 to an eighth.
enum class Meter : std::uint8_t {
    Straight,   // 16 ticks: bar, half, quarters, eighths, sixteenths
    Five32,     // 5 as 3 + 2
    Five23,     // 5 as 2 + 3
    Seven223,
    Seven322,
    Seven232,
    Nine333,
};

int meterCycle(Meter meter) noexcept;
// A tick's tier (0 .. 4) in its cycle. Straight: 0 the bar's downbeat, 1 the half, 2 the quarters,
// 3 the eighths, 4 the sixteenths. Grouped: the group's index for a group's downbeat (0 the bar's),
// then the other beats (even ticks), then the off-beats.
int tierOf(Meter meter, std::int64_t tick) noexcept;

enum class TierPick : std::uint8_t { Lowest, Highest, Next, Random };

struct GridTier {
    std::int32_t probability = 65536;   // Q16, 0 .. 1
    std::int32_t level       = 65536;   // Q16, 0 .. 2: times the member's velocity
    std::int32_t offset      = 0;       // 1/512 semitone, from the pick
    std::int32_t chaos       = 0;       // Q16, 0 .. 1: how much of Smear and Scatter this tier gets
    TierPick     pick        = TierPick::Next;
};

struct GridParams {
    AssignerSettings settings;                 // its sync is ignored: Grid is always Beats
    std::int64_t     rateNum   = 4;            // slots per rateDen beats
    std::int64_t     rateDen   = 1;
    Meter            meter     = Meter::Straight;
    std::array<GridTier, kTiers> tiers = defaultTiers();
    std::int32_t     density   = 65536;        // Q16, 1 .. 4: notes per slot
    std::int32_t     smear     = 0;            // Q16, 0 .. 1: of the slot, times the tier's Chaos
    std::int32_t     scatter   = 12 * 512;     // 1/512 semitone at Chaos 1, triangular
    PitchParams      pitch;                    // Transpose and Quantize (its Scatter is the tiers')
    std::int64_t     lengthBeats = kBeat / 8;
    std::int32_t     lenSpread = 0;            // Q16
    std::int32_t     velSpread = 0;            // Q16

    static std::array<GridTier, kTiers> defaultTiers() noexcept;
};

class GridCore : public Assigner {
public:
    void setParams(const GridParams& params) noexcept;

private:
    void reapply() noexcept override { setParams(params_); }
    void collectGrains(std::size_t& count) noexcept override;
    void play(const Grain& g, NoteOut& out) noexcept override;

    GridParams params_;
};

} // namespace notegran
