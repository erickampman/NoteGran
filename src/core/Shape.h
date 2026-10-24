#pragma once

#include <cstdint>

#include "core/Random.h"

// Velocity and length (docs/design.md, "Velocity and length"). Settings in Q16 fixed point (65536 is 1),
// turned from the parameters' doubles once, as the clock's are.
namespace notegran {

inline constexpr std::int32_t kOne16 = 65536;

std::int32_t toQ16(double v) noexcept;   // clamped to 0 .. 4

// A grain's velocity (16-bit): the member's, times `level` (0 .. 2), times the trigger's (with a
// trigger channel; 0xFFFF otherwise), then moved by up to ± `spread` of itself (0 .. 1,
// triangular, Velocity draw 0). At least 1: a MIDI 1.0 writer still has to keep 7 bits above 0.
std::uint16_t grainVelocity(std::uint16_t member, std::int32_t level, std::int32_t spread, std::uint16_t trigger,
                            const Random& random, std::int64_t index) noexcept;

// A grain's length in samples: `base`, moved by up to ± `spread` of itself (0 .. 1, triangular,
// Length draw 0). At least 1.
std::int64_t grainLength(std::int64_t base, std::int32_t spread, const Random& random, std::int64_t index) noexcept;

// A 16-bit velocity as MIDI 1.0's 7 bits, never 0 (which would be a note-off).
inline std::uint8_t velocity7(std::uint16_t v) noexcept {
    const auto v7 = static_cast<std::uint8_t>(v >> 9);
    return v7 == 0 ? 1 : v7;
}

} // namespace notegran
