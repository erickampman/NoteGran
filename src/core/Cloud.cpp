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

void CloudCore::collectGrains(std::size_t& count) noexcept {
    if (!triggered()) collectOnsets(grid(), params_.onsetRandom, count);
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
