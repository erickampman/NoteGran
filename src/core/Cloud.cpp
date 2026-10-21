#include "core/Cloud.h"

#include <algorithm>
#include <cmath>

#include "core/Shape.h"

namespace notegran {

void CloudCore::setSampleRate(double sampleRate) noexcept {
    clock_.setSampleRate(sampleRate);
    setParams(params_);
}

void CloudCore::setParams(const CloudParams& params) noexcept {
    params_ = params;
    clock_.setMode(params.sync);
    owned_.setVoices(params.voices);
    owned_.setWhenFull(params.whenFull);
    set_.setChannels(params.setChannel, params.triggerChannel);
    set_.setLatch(params.latch);
    const double ms = std::isfinite(params.holdOverMs) ? std::max(params.holdOverMs, 0.0) : 0.0;
    set_.setHoldOver(std::llround(ms * static_cast<double>(clock_.sampleRate()) / 1000.0));
    if (random_.seed() != params.seed) random_.setSeed(params.seed);
}

void CloudCore::reset(std::int64_t time, NoteOut& out) noexcept {
    owned_.endAll(time, out);
    set_.clear();
    hasLast_ = false;
}

void CloudCore::process(std::int64_t start, std::int64_t frames, const Transport* transport, CloudInput* inputs,
                        std::size_t inputCount, NoteOut& out) noexcept {
    if (clock_.begin(start, frames, transport).stopped) reset(start, out);

    std::size_t grains = 0;
    if (params_.triggerChannel == NoteSet::kNone) collectGrains(grains);

    // In time order: the inputs at a sample before the grains at it (a set change comes first).
    std::size_t next = 0;
    auto playUntil = [&](std::int64_t before) {
        for (; next < grains && grains_[next].time < before; ++next) play(grains_[next], out);
    };
    for (std::size_t i = 0; i < inputCount; ++i) {
        CloudInput& in = inputs[i];
        playUntil(in.time);
        const Heard h = set_.input(in.time, in.message, in.dialect);
        switch (h.kind) {
        case Heard::Kind::Other: in.pass = true; break;
        case Heard::Kind::SetNote: in.pass = params_.dry; break;
        case Heard::Kind::Reset:
            in.pass = true;
            owned_.endAll(in.time, out);
            hasLast_ = false;
            break;
        case Heard::Kind::Trigger:
            in.pass = false;
            // Triggers only come with a trigger channel, which has no slot grains: appending keeps
            // the grains in time order.
            if (h.strike && grains < grains_.size()) grains_[grains++] = Grain{in.time, triggers_++, h.velocity};
            break;
        }
    }
    playUntil(clock_.blockEnd());
    owned_.endUntil(clock_.blockEnd() - 1, out);
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
                grains_[count++] = Grain{sample, n * 16 + j, 0xFFFF};
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
    set_.settle(g.time);
    // Notes due by now end first, so whether a key is free doesn't depend on where blocks fall.
    if (!owned_.endUntil(g.time, out)) return;
    if (set_.empty()) return;
    const std::size_t i = pick(g.index);
    if (i >= set_.size()) return;
    const Member m = set_[i];

    const std::int32_t pitch = grainPitch(m.pitch, params_.pitch, random_, g.index, set_);
    const KeyChoice    key   = chooseKey(pitch, m.dialect, [&](std::uint8_t k) {
        return owned_.sounding(m.group, m.channel, k) || (params_.dry && set_.holds(m.group, m.channel, k));
    });
    // Dry: a grain can't share a key with a set note passing through, which the plugin doesn't
    // own and can't end first. MIDI 2.0 and CLAP grains move to a free key above; a MIDI 1.0
    // grain (or any, with every key taken) is skipped (PLAN.md, open question 9).
    if (params_.dry && set_.holds(m.group, m.channel, key.key)) return;

    NoteStart n;
    n.time     = g.time;
    n.end      = g.time + grainLength(baseLength(), params_.lenSpread, random_, g.index);
    n.group    = m.group;
    n.channel  = m.channel;
    n.key      = key.key;
    n.dialect  = m.dialect;
    n.pitch79  = key.pitch79;
    n.velocity = grainVelocity(m.velocity, params_.level, params_.velSpread, g.trigger, random_, g.index);
    owned_.start(n, out);
}

std::size_t CloudCore::sortedMembers(bool byArrival) noexcept {
    const std::size_t n = set_.size();
    for (std::size_t i = 0; i < n; ++i) order_[i] = static_cast<std::uint16_t>(i);
    std::sort(order_.begin(), order_.begin() + static_cast<std::ptrdiff_t>(n), [&](std::uint16_t a, std::uint16_t b) {
        const Member& x = set_[a];
        const Member& y = set_[b];
        if (!byArrival && x.pitch != y.pitch) return x.pitch < y.pitch;
        return x.arrival < y.arrival;
    });
    return n;
}

// Members in a fixed order (by pitch, then arrival; Played by arrival), so the set's own storage
// order never shows. The ordered picks go on from the last member picked, so they keep their place
// when the set changes.
std::size_t CloudCore::pick(std::int64_t index) noexcept {
    const bool        played = params_.pick == Pick::Played;
    const std::size_t n      = sortedMembers(played);
    auto at = [&](std::size_t k) -> const Member& { return set_[order_[k]]; };
    auto after = [&](const Member& m) {   // after the last pick, in this order
        if (played) return m.arrival > lastArrival_;
        return m.pitch > lastPitch_ || (m.pitch == lastPitch_ && m.arrival > lastArrival_);
    };
    auto before = [&](const Member& m) {
        return m.pitch < lastPitch_ || (m.pitch == lastPitch_ && m.arrival < lastArrival_);
    };

    std::size_t k = n;
    switch (params_.pick) {
    case Pick::Random: {
        const std::int64_t w = std::clamp<std::int64_t>(params_.weight, 0, kOne16);
        std::uint64_t total = 0;
        for (std::size_t j = 0; j < n; ++j) total += static_cast<std::uint64_t>(((kOne16 - w) * 0xFFFF + w * at(j).velocity) >> 16);
        if (total == 0) {
            k = random_.below(static_cast<std::uint32_t>(n), Stream::Pick, index, 0);
        } else {
            std::uint64_t r = ((random_.bits(Stream::Pick, index, 0) >> 32) * total) >> 32;
            for (k = 0; k + 1 < n; ++k) {
                const auto wk = static_cast<std::uint64_t>(((kOne16 - w) * 0xFFFF + w * at(k).velocity) >> 16);
                if (r < wk) break;
                r -= wk;
            }
        }
        return order_[k];   // Random doesn't move the ordered picks' place
    }
    case Pick::Up:
    case Pick::Played:
        k = 0;
        if (hasLast_)
            for (std::size_t j = 0; j < n; ++j)
                if (after(at(j))) { k = j; break; }
        break;
    case Pick::Down:
        k = n - 1;
        if (hasLast_)
            for (std::size_t j = n; j-- > 0;)
                if (before(at(j))) { k = j; break; }
        break;
    case Pick::UpDown: {
        auto up = [&]() -> std::size_t {
            for (std::size_t j = 0; j < n; ++j) if (after(at(j))) return j;
            return n;
        };
        auto down = [&]() -> std::size_t {
            for (std::size_t j = n; j-- > 0;) if (before(at(j))) return j;
            return n;
        };
        if (!hasLast_) {
            k          = 0;
            goingDown_ = false;
        } else if (!goingDown_) {
            k = up();
            if (k == n) { goingDown_ = true; k = down(); }
        } else {
            k = down();
            if (k == n) { goingDown_ = false; k = up(); }
        }
        if (k == n) k = 0;   // one member: it again
        break;
    }
    }
    hasLast_     = true;
    lastPitch_   = at(k).pitch;
    lastArrival_ = at(k).arrival;
    return order_[k];
}

} // namespace notegran
