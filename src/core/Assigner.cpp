#include "core/Assigner.h"

#include <algorithm>
#include <cmath>

#include "core/Pitch.h"
#include "core/Shape.h"

namespace notegran {

void Assigner::setSampleRate(double sampleRate) noexcept {
    clock_.setSampleRate(sampleRate);
    reapply();
}

void Assigner::applySettings(const AssignerSettings& s) noexcept {
    clock_.setMode(s.sync);
    owned_.setVoices(s.voices);
    owned_.setWhenFull(s.whenFull);
    set_.setChannels(s.setChannel, s.triggerChannel);
    set_.setLatch(s.latch);
    const double ms = std::isfinite(s.holdOverMs) ? std::max(s.holdOverMs, 0.0) : 0.0;
    set_.setHoldOver(std::llround(ms * static_cast<double>(clock_.sampleRate()) / 1000.0));
    if (random_.seed() != s.seed) random_.setSeed(s.seed);
    dry_            = s.dry;
    triggerChannel_ = s.triggerChannel;
}

std::int64_t Assigner::beatPosition() const noexcept {
    if (clock_.mode() != Clock::Mode::Beats || clock_.blockEnd() <= clock_.blockStart()) return -1;
    return clock_.positionAt(clock_.blockEnd() - 1);
}

void Assigner::reset(std::int64_t time, NoteOut& out) noexcept {
    owned_.endAll(time, out);
    set_.clear();
    hasLast_ = false;
    cleared();
}

void Assigner::process(std::int64_t start, std::int64_t frames, const Transport* transport, InputEvent* inputs,
                       std::size_t inputCount, NoteOut& out) noexcept {
    if (clock_.begin(start, frames, transport).stopped) reset(start, out);

    const bool  phrases = startsPhrases();
    std::size_t grains  = 0;
    if (triggerChannel_ == NoteSet::kNone || phrases) collectGrains(grains);

    // In time order: the inputs at a sample before the grains at it (a set change comes first).
    std::size_t next = 0;
    auto playUntil = [&](std::int64_t before) {
        for (; next < grains && grains_[next].time < before; ++next) playOne(grains_[next], out);
    };
    for (std::size_t i = 0; i < inputCount; ++i) {
        InputEvent& in = inputs[i];
        playUntil(in.time);
        const Heard h = set_.input(in.time, in.message, in.dialect);
        switch (h.kind) {
        case Heard::Kind::Other: in.pass = true; break;
        case Heard::Kind::SetNote:
            in.pass = dry_;
            if (phrases) heard(h, in.time, next, grains);
            break;
        case Heard::Kind::Reset:
            in.pass = true;
            owned_.endAll(in.time, out);
            hasLast_ = false;
            cleared();
            if (phrases) grains = next;   // the phrases' grains still to come
            break;
        case Heard::Kind::Trigger:
            in.pass = false;
            if (phrases) {
                heard(h, in.time, next, grains);
                break;
            }
            // Triggers only come with a trigger channel, which has no collected grains: appending
            // keeps the grains in time order.
            if (h.strike && grains < grains_.size()) grains_[grains++] = Grain{in.time, triggers_++, h.velocity, 0};
            break;
        }
    }
    playUntil(clock_.blockEnd());
    owned_.endUntil(clock_.blockEnd() - 1, out);
}

void Assigner::playOne(const Grain& g, NoteOut& out) noexcept {
    set_.settle(g.time);
    // Notes due by now end first, so whether a key is free doesn't depend on where blocks fall.
    if (!owned_.endUntil(g.time, out)) return;
    if (set_.empty() && !startsPhrases()) return;
    play(g, out);
}

void Assigner::start(const Member& m, std::int32_t pitch, std::int64_t time, std::int64_t length,
                     std::uint16_t velocity, NoteOut& out) noexcept {
    const KeyChoice key = chooseKey(pitch, m.dialect, [&](std::uint8_t k) {
        return owned_.sounding(m.group, m.channel, k) || (dry_ && set_.holds(m.group, m.channel, k));
    });
    // Dry: a grain can't share a key with a set note passing through, which the plugin doesn't
    // own and can't end first. MIDI 2.0 and CLAP grains move to a free key; a MIDI 1.0 grain (or
    // any, with every key taken) is skipped (PLAN.md, open question 9).
    if (dry_ && set_.holds(m.group, m.channel, key.key)) return;

    NoteStart n;
    n.time     = time;
    n.end      = time + length;
    n.group    = m.group;
    n.channel  = m.channel;
    n.key      = key.key;
    n.dialect  = m.dialect;
    n.pitch79  = key.pitch79;
    n.velocity = velocity;
    owned_.start(n, out);
}

std::size_t Assigner::sortedMembers(bool byArrival) noexcept {
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
std::size_t Assigner::pick(Pick mode, std::int32_t weight, std::int64_t index) noexcept {
    const bool        played = mode == Pick::Played;
    const std::size_t n      = sortedMembers(played);
    if (n == 0) return 0;
    auto at = [&](std::size_t k) -> const Member& { return set_[order_[k]]; };
    auto after = [&](const Member& m) {   // after the last pick, in this order
        if (played) return m.arrival > lastArrival_;
        return m.pitch > lastPitch_ || (m.pitch == lastPitch_ && m.arrival > lastArrival_);
    };
    auto before = [&](const Member& m) {
        return m.pitch < lastPitch_ || (m.pitch == lastPitch_ && m.arrival < lastArrival_);
    };

    std::size_t k = n;
    switch (mode) {
    case Pick::Random: {
        const std::int64_t w = std::clamp<std::int64_t>(weight, 0, kOne16);
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
    case Pick::Lowest: return order_[0];          // nor do these
    case Pick::Highest: return order_[n - 1];
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
