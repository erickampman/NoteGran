#include "core/Shape.h"

#include <algorithm>
#include <cmath>

namespace notegran {

namespace {

// Moves v by `spread` (Q16) times a triangular draw in [-1, 1): v * (1 + spread * t).
std::int64_t spreadBy(std::int64_t v, std::int32_t spread, std::int64_t triangular) noexcept {
    if (spread <= 0) return v;
    const std::int64_t factor = kOne16 + ((triangular * spread) >> 32);   // Q16
    return (v * factor + kOne16 / 2) >> 16;
}

} // namespace

std::int32_t toQ16(double v) noexcept {
    if (!std::isfinite(v)) return 0;
    return static_cast<std::int32_t>(std::llround(std::clamp(v, 0.0, 4.0) * kOne16));
}

std::uint16_t grainVelocity(std::uint16_t member, std::int32_t level, std::int32_t spread, std::uint16_t trigger,
                            const Random& random, std::int64_t index) noexcept {
    std::int64_t v = (std::int64_t{member} * std::max(level, 0) + kOne16 / 2) >> 16;
    v              = (v * trigger + 0x7FFF) / 0xFFFF;
    if (spread > 0) v = spreadBy(v, std::min(spread, kOne16), random.triangular(Stream::Velocity, index, 0));
    return static_cast<std::uint16_t>(std::clamp<std::int64_t>(v, 1, 0xFFFF));
}

std::int64_t grainLength(std::int64_t base, std::int32_t spread, const Random& random, std::int64_t index) noexcept {
    std::int64_t len = base;
    if (spread > 0) len = spreadBy(len, std::min(spread, kOne16), random.triangular(Stream::Length, index, 0));
    return std::max<std::int64_t>(len, 1);
}

} // namespace notegran
