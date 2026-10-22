#include "core/Cloud.h"

#include <algorithm>
#include <cmath>

#include "core/Shape.h"

namespace notegran {

void CloudCore::setParams(const CloudParams& params) noexcept {
    params_ = params;
    AssignerSettings a;
    a.sync           = params.sync;
    a.voices         = params.voices;
    a.whenFull       = params.whenFull;
    a.dry            = params.dry;
    a.holdOverMs     = params.holdOverMs;
    a.setChannel     = params.setChannel;
    a.triggerChannel = params.triggerChannel;
    a.latch          = params.latch;
    a.seed           = params.seed;
    applySettings(a);
}

Grid CloudCore::grid() const noexcept {
    if (params_.sync == Clock::Mode::Free) return Grid::perSecond(params_.density, clock_.sampleRate());
    return Grid::perBeat(params_.divisionNum, params_.divisionDen);
}

std::int64_t CloudCore::baseLength() const noexcept {
    return params_.sync == Clock::Mode::Free ? clock_.samplesForMs(params_.lengthMs)
                                             : clock_.samplesForBeats(params_.lengthBeats);
}

// Onset random r: each slot is, with chance r, Poisson dust (a Poisson(1) count of grains, each at
// a uniform place in the slot), and otherwise one grain, up to r of the way into the slot. At 0,
// one grain on each slot; at 1, a Poisson process at the same mean rate. All keyed on the slot n
// (Timing draws 0, 1, 2 + j), and grain j of slot n is index n * 16 + j for the other draws.
void CloudCore::collectGrains(std::size_t& count) noexcept {
    const Grid         g = grid();
    const std::int64_t r = std::clamp<std::int64_t>(params_.onsetRandom, 0, kOne16);
    clock_.slotsAround(g, [&](std::int64_t n) {
        auto add = [&](std::uint32_t j, std::int64_t frac) {
            std::int64_t sample = 0;
            if (count < grains_.size() && clock_.placeInSlot(g, n, frac, sample))
                grains_[count++] = Grain{sample, n * 16 + j, 0xFFFF, 0};
        };
        if (random_.chance(r << 16, Stream::Timing, n, 0)) {
            const std::uint32_t k = random_.poisson1(Stream::Timing, n, 1);
            for (std::uint32_t j = 0; j < k; ++j) add(j, random_.unit(Stream::Timing, n, 2 + j) >> 16);
        } else {
            add(0, ((random_.unit(Stream::Timing, n, 2) >> 16) * r) >> 16);
        }
    });
    std::sort(grains_.begin(), grains_.begin() + static_cast<std::ptrdiff_t>(count),
              [](const Grain& a, const Grain& b) { return a.time != b.time ? a.time < b.time : a.index < b.index; });
}

void CloudCore::play(const Grain& g, NoteOut& out) noexcept {
    const std::size_t i = pick(params_.pick, params_.weight, g.index);
    if (i >= set_.size()) return;
    const Member m = set_[i];
    start(m, grainPitch(m.pitch, params_.pitch, random_, g.index, set_), g.time,
          grainLength(baseLength(), params_.lenSpread, random_, g.index),
          grainVelocity(m.velocity, params_.level, params_.velSpread, g.trigger, random_, g.index), out);
}

} // namespace notegran
