#include "core/Random.h"

namespace notegran {

namespace {

// SplitMix64's finaliser (Stafford's variant 13): every input bit reaches every output bit.
constexpr std::uint64_t mix(std::uint64_t z) noexcept {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// The golden ratio's 64-bit fraction: steps each field apart, so that no field of zero is a fixed
// point of mix().
constexpr std::uint64_t kGolden = 0x9E3779B97F4A7C15ull;

// Poisson(1)'s cumulative probabilities P(X <= k), k = 0..11, as Q32: e^-1 * sum(1/i!, i <= k).
constexpr std::uint32_t kPoisson1[] = {
    1580030169u, 3160060337u, 3950075422u, 4213413783u, 4279248374u, 4292415292u,
    4294609778u, 4294923276u, 4294962463u, 4294966817u, 4294967253u, 4294967292u,
};

} // namespace

std::uint64_t Random::bits(Stream stream, std::int64_t index, std::uint32_t draw) const noexcept {
    std::uint64_t h = mix(seed_ + kGolden);
    h               = mix(h + static_cast<std::uint64_t>(stream) * kGolden);
    h               = mix(h + static_cast<std::uint64_t>(index) * kGolden);
    return mix(h + (static_cast<std::uint64_t>(draw) + 1) * kGolden);
}

std::uint32_t Random::below(std::uint32_t n, Stream stream, std::int64_t index, std::uint32_t draw) const noexcept {
    return static_cast<std::uint32_t>(((bits(stream, index, draw) >> 32) * n) >> 32);
}

std::int64_t Random::unit(Stream stream, std::int64_t index, std::uint32_t draw) const noexcept {
    return static_cast<std::int64_t>(bits(stream, index, draw) >> 32);
}

std::int64_t Random::triangular(Stream stream, std::int64_t index, std::uint32_t draw) const noexcept {
    const std::uint64_t b = bits(stream, index, draw);   // two uniforms: its halves
    return static_cast<std::int64_t>(b >> 32) + static_cast<std::int64_t>(b & 0xFFFFFFFFu) - kOne32;
}

bool Random::chance(std::int64_t p, Stream stream, std::int64_t index, std::uint32_t draw) const noexcept {
    if (p <= 0) return false;
    if (p >= kOne32) return true;
    return unit(stream, index, draw) < p;
}

std::uint32_t Random::poisson1(Stream stream, std::int64_t index, std::uint32_t draw) const noexcept {
    const auto u = static_cast<std::uint32_t>(bits(stream, index, draw) >> 32);
    std::uint32_t k = 0;
    for (const std::uint32_t c : kPoisson1) {
        if (u < c) return k;
        ++k;
    }
    return k;
}

} // namespace notegran
