#pragma once

#include <cstdint>

// Seeded random draws (PLAN.md, "Randomness and repeatability"). Counter-based: a draw is a fixed
// integer hash of the seed, its stream, its index and its draw number, not the next value of a
// running generator. So any draw can be worked out alone, in any order: a host can start anywhere
// (Umpire playing a clip from bar 5) and get what it would have got playing from the start, and
// output can't depend on block size.
//
// - Stream: what the draw is for. Each knob's randomness has its own, so turning one doesn't
//   reshuffle the others.
// - Index: whose draw it is, usually a clock slot n (Clock::slots), or a trigger's count.
// - Draw: which of that index's draws in the stream, when it needs several (0, 1, 2, ...).
//
// A draw is one random value: the one fixed by (seed, stream, index, draw number). Nothing is used
// up: asking again gives the same value. Each plugin gives every value it needs a fixed (stream,
// draw number), e.g. a Cloud grain's octave is Pitch draw 0 and its scatter Pitch draw 1, and that
// assignment never changes between versions, any more than the hash does.
//
// Integer arithmetic only, so every machine gives the same values. The values are part of what a
// saved song sounds like with a fixed seed: the tests pin some of them, and a change to the hash or a
// distribution is a change of output, to be versioned (PLAN.md, "Integration").
namespace notegran {

enum class Stream : std::uint32_t {
    Timing   = 1,   // onsets: Onset random, Burst's attractor
    Pick     = 2,   // which member of the set
    Pitch    = 3,   // Octaves, Scatter
    Velocity = 4,   // Velocity spread
    Length   = 5,   // Length spread
    Position = 6,   // Buffer's read point
    Chance   = 7,   // Grid's per-tier Probability
};

// 1.0 in the Q32 fixed point the distributions use.
inline constexpr std::int64_t kOne32 = std::int64_t{1} << 32;

class Random {
public:
    explicit Random(std::uint64_t seed = 0) noexcept : seed_(seed) {}

    void          setSeed(std::uint64_t seed) noexcept { seed_ = seed; }
    std::uint64_t seed() const noexcept { return seed_; }

    // 64 uniform bits.
    std::uint64_t bits(Stream stream, std::int64_t index, std::uint32_t draw = 0) const noexcept;

    // Uniform in [0, n); 0 when n is 0. (Multiply-high: each value's chance is off by under
    // n / 2^32, far below hearing.)
    std::uint32_t below(std::uint32_t n, Stream stream, std::int64_t index, std::uint32_t draw = 0) const noexcept;

    // Uniform in [0, 1), as Q32: 0 .. kOne32 - 1.
    std::int64_t unit(Stream stream, std::int64_t index, std::uint32_t draw = 0) const noexcept;

    // Triangular in [-1, 1), as Q32, peaked at 0: the sum of two uniforms, less one (PMU's
    // triangular draws). For Scatter and the spreads: small offsets often, large ones rarely.
    std::int64_t triangular(Stream stream, std::int64_t index, std::uint32_t draw = 0) const noexcept;

    // True with probability p, a Q32 fraction: 0 never, kOne32 (or more) always.
    bool chance(std::int64_t p, Stream stream, std::int64_t index, std::uint32_t draw = 0) const noexcept;

    // A count with a Poisson distribution of mean 1 (0 .. 12): how many grains fall in one slot of
    // a rate when Onset random is 1, so the grains make a Poisson process ("dust").
    std::uint32_t poisson1(Stream stream, std::int64_t index, std::uint32_t draw = 0) const noexcept;

private:
    std::uint64_t seed_;
};

} // namespace notegran
