#include "core/Pitch.h"

#include <algorithm>
#include <cmath>

namespace notegran {

namespace {

std::int64_t floorDiv(std::int64_t a, std::int64_t b) noexcept {   // b > 0
    const std::int64_t q = a / b;
    return (a % b != 0 && a < 0) ? q - 1 : q;
}

// a / b to the nearest, halves up; b > 0.
std::int64_t roundDiv(std::int64_t a, std::int64_t b) noexcept { return floorDiv(2 * a + b, 2 * b); }

} // namespace

std::int32_t PitchParams::fromSemitones(double semitones) noexcept {
    if (!std::isfinite(semitones)) return 0;
    const double limit = 127.0;
    return static_cast<std::int32_t>(std::llround(std::clamp(semitones, -limit, limit) * kSemitone));
}

std::int32_t grainPitch(std::int32_t memberPitch, const PitchParams& params, const Random& random,
                        std::int64_t index, const NoteSet& set) noexcept {
    std::int64_t p = memberPitch;

    const int octaves = std::clamp(params.octaves, 0, 4);
    if (octaves > 0) {
        const auto span = static_cast<std::uint32_t>(2 * octaves + 1);
        p += (static_cast<std::int64_t>(random.below(span, Stream::Pitch, index, 0)) - octaves) * kOctave;
    }
    p += params.transpose;
    if (params.scatter > 0)
        p += (random.triangular(Stream::Pitch, index, 1) * params.scatter + (kOne32 / 2)) >> 32;

    p = std::clamp<std::int64_t>(p, -16 * kOctave, 32 * kOctave);   // so the steps below can't overflow
    auto q = static_cast<std::int32_t>(p);
    switch (params.quantize) {
    case Quantize::Off: break;
    case Quantize::Edo: q = quantizeEdo(q, params.edo, params.root); break;
    case Quantize::Set: q = quantizeToSet(q, set); break;
    }
    return foldIntoRange(q);
}

std::int32_t quantizeEdo(std::int32_t pitch, int edo, int root) noexcept {
    const std::int64_t n      = std::clamp(edo, 1, 96);
    const std::int64_t anchor = static_cast<std::int64_t>(((root % 12) + 12) % 12) * kSemitone;
    // The nearest step k (each kOctave / n), then its pitch, both rounded to the nearest.
    const std::int64_t k = roundDiv((pitch - anchor) * n, kOctave);
    return static_cast<std::int32_t>(anchor + roundDiv(k * kOctave, n));
}

std::int32_t quantizeToSet(std::int32_t pitch, const NoteSet& set) noexcept {
    if (set.empty()) return pitch;
    // The smallest move to any member's pitch class; on a tie, down.
    std::int32_t best = kOctave;
    for (std::size_t i = 0; i < set.size(); ++i) {
        std::int32_t r = ((pitch - set[i].pitch) % kOctave + kOctave) % kOctave;   // 0 .. kOctave - 1
        if (r > kOctave / 2) r -= kOctave;                                        // -kOctave/2 < r <= kOctave/2
        if (std::abs(r) < std::abs(best) || (std::abs(r) == std::abs(best) && r > best)) best = r;
    }
    return pitch - best;
}

std::int32_t foldIntoRange(std::int32_t pitch) noexcept {
    while (pitch < 0) pitch += kOctave;
    while (pitch > kMaxPitch) pitch -= kOctave;
    return pitch;
}

} // namespace notegran
