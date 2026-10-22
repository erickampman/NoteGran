#include "core/Grid.h"

#include <algorithm>

#include "core/Shape.h"

namespace notegran {

namespace {

// ClockGran's meter table: cycle length in ticks and the ticks its groups start on.
struct MeterInfo {
    int cycle;
    int groups;
    int groupStart[3];
};
constexpr MeterInfo kMeters[] = {
    {16, 0, {0, 0, 0}},    // Straight (its own hierarchy)
    {10, 2, {0, 6, 0}},    // 5: 3 + 2
    {10, 2, {0, 4, 0}},    // 5: 2 + 3
    {14, 3, {0, 4, 8}},    // 7: 2 + 2 + 3
    {14, 3, {0, 6, 10}},   // 7: 3 + 2 + 2
    {14, 3, {0, 4, 10}},   // 7: 2 + 3 + 2
    {18, 3, {0, 6, 12}},   // 9: 3 + 3 + 3
};

const MeterInfo& info(Meter m) noexcept {
    return kMeters[std::min<std::size_t>(static_cast<std::size_t>(m), sizeof(kMeters) / sizeof(kMeters[0]) - 1)];
}

Pick toPick(TierPick p) noexcept {
    switch (p) {
    case TierPick::Lowest: return Pick::Lowest;
    case TierPick::Highest: return Pick::Highest;
    case TierPick::Next: return Pick::Up;
    case TierPick::Random: return Pick::Random;
    }
    return Pick::Up;
}

} // namespace

int meterCycle(Meter meter) noexcept { return info(meter).cycle; }

int tierOf(Meter meter, std::int64_t tick) noexcept {
    const MeterInfo& m = info(meter);
    const auto       t = static_cast<int>(((tick % m.cycle) + m.cycle) % m.cycle);
    if (m.groups == 0) {
        if (t == 0) return 0;
        if (t == 8) return 1;
        if (t == 4 || t == 12) return 2;
        if (t % 2 == 0) return 3;
        return 4;
    }
    for (int g = 0; g < m.groups; ++g)
        if (t == m.groupStart[g]) return g;
    return t % 2 == 0 ? m.groups : m.groups + 1;
}

std::array<GridTier, kTiers> GridParams::defaultTiers() noexcept {
    // The bar's downbeat always, the lowest note; the spare positions half the time, any note.
    std::array<GridTier, kTiers> t{};
    const std::int32_t probability[kTiers] = {65536, 65536, 65536, 49152, 32768};
    const std::int32_t level[kTiers]       = {65536, 55705, 49152, 42598, 36045};   // 1, .85, .75, .65, .55
    const TierPick     pick[kTiers]        = {TierPick::Lowest, TierPick::Next, TierPick::Next, TierPick::Next,
                                              TierPick::Random};
    for (int i = 0; i < kTiers; ++i) t[static_cast<std::size_t>(i)] = GridTier{probability[i], level[i], 0, 0, pick[i]};
    return t;
}

void GridCore::setParams(const GridParams& params) noexcept {
    params_ = params;
    AssignerSettings a = params.settings;
    a.sync             = Clock::Mode::Beats;
    applySettings(a);
}

void GridCore::collectGrains(std::size_t& count) noexcept {
    const Grid g = Grid::perBeat(params_.rateNum, params_.rateDen);
    const auto density = std::clamp<std::int64_t>(params_.density, kOne16, 4 * kOne16);
    clock_.slotsAround(g, [&](std::int64_t n) {
        const int       tier = tierOf(params_.meter, n);
        const GridTier& t    = params_.tiers[static_cast<std::size_t>(tier)];
        if (!random_.chance(std::int64_t{std::clamp(t.probability, 0, kOne16)} << 16, Stream::Chance, n, 0)) return;
        // Density's whole part, and its fraction by chance, so a fraction hits the right mean.
        std::int64_t k = density >> 16;
        if (random_.chance((density & 0xFFFF) << 16, Stream::Chance, n, 1)) ++k;
        const std::int64_t spread = (std::int64_t{std::clamp(params_.smear, 0, kOne16)} * std::clamp(t.chaos, 0, kOne16)) >> 16;
        for (std::int64_t j = 0; j < k; ++j) {
            const std::int64_t frac = ((random_.unit(Stream::Timing, n, static_cast<std::uint32_t>(j)) >> 16) * spread) >> 16;
            std::int64_t       sample = 0;
            if (count < grains_.size() && clock_.placeInSlot(g, n, frac, sample))
                grains_[count++] = Grain{sample, n * 16 + j, 0xFFFF, static_cast<std::uint8_t>(tier)};
        }
    });
    std::sort(grains_.begin(), grains_.begin() + static_cast<std::ptrdiff_t>(count),
              [](const Grain& a, const Grain& b) { return a.time != b.time ? a.time < b.time : a.index < b.index; });
}

void GridCore::play(const Grain& g, NoteOut& out) noexcept {
    const GridTier&   t = params_.tiers[std::min<std::size_t>(g.tag, kTiers - 1)];
    const std::size_t i = pick(toPick(t.pick), 0, g.index);
    if (i >= set_.size()) return;
    const Member m = set_[i];

    PitchParams p = params_.pitch;
    p.transpose += t.offset;
    p.scatter = static_cast<std::int32_t>((std::int64_t{std::max(params_.scatter, 0)} * std::clamp(t.chaos, 0, kOne16)) >> 16);
    start(m, grainPitch(m.pitch, p, random_, g.index, set_), g.time,
          grainLength(clock_.samplesForBeats(params_.lengthBeats), params_.lenSpread, random_, g.index),
          grainVelocity(m.velocity, t.level, params_.velSpread, g.trigger, random_, g.index), out);
}

} // namespace notegran
