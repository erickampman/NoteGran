#include "core/Burst.h"

#include <algorithm>
#include <cmath>

#include "core/Shape.h"

namespace notegran {

namespace {

constexpr int          kWarmup = 30;               // IFSGran's GRAN_WARMUP
constexpr std::int64_t kOne30  = std::int64_t{1} << 30;
constexpr double       kPi     = 3.14159265358979323846;

std::int64_t q30(double v) noexcept { return std::llround(v * static_cast<double>(kOne30)); }

double clamped(double v, double lo, double hi, double fallback) noexcept {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

// A grain's draw index: its phrase, pass and point.
std::int64_t grainIndex(std::int64_t serial, std::int64_t pass, int point) noexcept {
    return serial * 65536 + (pass & 1023) * BurstCore::kMaxPoints + point;
}

} // namespace

void BurstCore::setParams(const BurstParams& params) noexcept {
    params_ = params;
    applySettings(params.settings);
    const BurstTransform* t[2] = {&params.t1, &params.t2};
    for (int i = 0; i < 2; ++i) {
        const double c   = clamped(t[i]->contraction, 0.01, 0.99, 0.5);
        const double rad = clamped(t[i]->rotation, -180.0, 180.0, 0.0) * kPi / 180.0;
        maps_[i]         = Map{q30(clamped(t[i]->fixedX, 0.0, 1.0, 0.5)), q30(clamped(t[i]->fixedY, 0.0, 1.0, 0.5)),
                               q30(c * std::cos(rad)), q30(c * std::sin(rad))};
    }
}

bool BurstCore::busy() const noexcept {
    if (Assigner::busy()) return true;
    for (const Phrase& p : phrases_)
        if (p.active && !p.ended) return true;
    return false;
}

int BurstCore::phrasePoints(std::int64_t key, std::array<Point, kMaxPoints>& points) const noexcept {
    const int          n    = std::clamp(params_.count, 1, kMaxPoints);
    const std::int64_t prob = std::int64_t{std::clamp(params_.probability, 0, kOne16)} << 16;
    std::int64_t       x = kOne30 / 2, y = kOne30 / 2;
    std::array<std::int64_t, kMaxPoints> xs{}, ys{};
    for (int i = 0; i < kWarmup + n; ++i) {
        const Map&         m  = maps_[random_.chance(prob, Stream::Timing, key, static_cast<std::uint32_t>(i)) ? 0 : 1];
        const std::int64_t dx = x - m.fx, dy = y - m.fy;
        x = std::clamp(m.fx + ((m.a * dx - m.b * dy) >> 30), std::int64_t{0}, kOne30);
        y = std::clamp(m.fy + ((m.b * dx + m.a * dy) >> 30), std::int64_t{0}, kOne30);
        if (i >= kWarmup) {
            xs[static_cast<std::size_t>(i - kWarmup)] = x;
            ys[static_cast<std::size_t>(i - kWarmup)] = y;
        }
    }
    std::array<int, kMaxPoints> order{};
    for (int i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
    std::stable_sort(order.begin(), order.begin() + n, [&](int a, int b) {
        return xs[static_cast<std::size_t>(a)] < xs[static_cast<std::size_t>(b)];
    });

    const std::int64_t x0   = xs[static_cast<std::size_t>(order[0])];
    const std::int64_t span = xs[static_cast<std::size_t>(order[static_cast<std::size_t>(n - 1)])] - x0;
    const std::int64_t range = std::max(params_.range, 0);
    for (int i = 0; i < n; ++i) {
        const auto k = static_cast<std::size_t>(order[static_cast<std::size_t>(i)]);
        // (x - x0) / span of (n - 1) / n of the phrase, in Q16.
        const std::int64_t t = span == 0 ? 0 : (((xs[k] - x0) << 16) / span) * (n - 1) / n;
        points[static_cast<std::size_t>(i)] =
            Point{static_cast<std::int32_t>(t), static_cast<std::int32_t>(((ys[k] - kOne30 / 2) * 2 * range) >> 30)};
    }
    return n;
}

void BurstCore::cleared() noexcept {
    for (Phrase& p : phrases_) p.active = false;
}

void BurstCore::collectGrains(std::size_t& count) noexcept {
    for (int s = 0; s < kMaxPhrases; ++s) {
        Phrase& p = phrases_[static_cast<std::size_t>(s)];
        if (p.active && p.ended) p.active = false;   // its last grain played in an earlier block
        if (p.active) collect(s, count);
    }
    sortGrains(0, count);
}

// The phrase's grains starting before the block's end, from where it got to.
void BurstCore::collect(int slot, std::size_t& count) noexcept {
    Phrase& p = phrases_[static_cast<std::size_t>(slot)];
    while (!p.ended) {
        if (p.next == p.count) {
            if (p.pass >= p.lastPass) {
                p.ended = true;
                break;
            }
            ++p.pass;
            p.next = 0;
        }
        const Point&       pt = p.points[static_cast<std::size_t>(p.next)];
        const std::int64_t t  = p.start + p.pass * p.duration + ((p.duration * pt.time) >> 16);
        if (t >= clock_.blockEnd() || count >= grains_.size()) break;
        const std::int64_t at = std::max(t, clock_.blockStart());
        grains_[count++]      = Grain{at, grainIndex(p.serial, p.pass, p.next), p.trigger, static_cast<std::uint8_t>(slot)};
        p.lastTime            = at;
        ++p.next;
    }
}

// Drops the phrase's grains at `from` or later from the block's grains not yet played.
void BurstCore::drop(int slot, std::int64_t from, std::size_t next, std::size_t& count) noexcept {
    std::size_t kept = next;
    for (std::size_t i = next; i < count; ++i)
        if (grains_[i].tag != slot || grains_[i].time < from) grains_[kept++] = grains_[i];
    count = kept;
}

void BurstCore::sortGrains(std::size_t from, std::size_t count) noexcept {
    std::sort(grains_.begin() + static_cast<std::ptrdiff_t>(from), grains_.begin() + static_cast<std::ptrdiff_t>(count),
              [](const Grain& a, const Grain& b) { return a.time != b.time ? a.time < b.time : a.index < b.index; });
}

void BurstCore::heard(const Heard& h, std::int64_t time, std::size_t next, std::size_t& count) noexcept {
    const bool byTrigger = h.kind == Heard::Kind::Trigger;
    if (h.release) {
        for (int s = 0; s < kMaxPhrases; ++s) {
            Phrase& p = phrases_[static_cast<std::size_t>(s)];
            if (!p.active || !p.held || p.byTrigger != byTrigger || p.group != h.group || p.channel != h.channel ||
                p.key != h.key)
                continue;
            p.held = false;
            if (params_.release == Release::Stop) {
                drop(s, time, next, count);
                p.ended    = true;
                p.lastTime = time - 1;   // what's left of it has played
            } else if (params_.release == Release::Loop) {
                // Finish the pass it's in.
                p.lastPass = std::max<std::int64_t>((time - p.start) / p.duration, 0);
                drop(s, p.start + (p.lastPass + 1) * p.duration, next, count);
                if (p.pass > p.lastPass) p.ended = true;
            }
        }
        return;
    }
    // With a trigger channel, only its note-ons start phrases.
    if (!h.strike || byTrigger != (params_.settings.triggerChannel != NoteSet::kNone)) return;

    // The root: the note struck, or for a trigger the newest member of the set.
    std::size_t root = set_.size();
    for (std::size_t i = 0; i < set_.size(); ++i) {
        const Member& m = set_[i];
        if (byTrigger ? (root == set_.size() || m.arrival > set_[root].arrival)
                      : (m.group == h.group && m.channel == h.channel && m.key == h.key))
            root = i;
    }
    if (root == set_.size()) return;
    startPhrase(set_[root], h, time, next, count);
}

void BurstCore::startPhrase(const Member& root, const Heard& h, std::int64_t time, std::size_t next,
                            std::size_t& count) noexcept {
    // A free slot (none with grains still to come), else the oldest phrase's.
    int slot = -1;
    for (int s = 0; s < kMaxPhrases && slot < 0; ++s) {
        const Phrase& p = phrases_[static_cast<std::size_t>(s)];
        if (!p.active || (p.ended && p.lastTime < time)) slot = s;
    }
    if (slot < 0) {
        slot = 0;
        for (int s = 1; s < kMaxPhrases; ++s)
            if (phrases_[static_cast<std::size_t>(s)].serial < phrases_[static_cast<std::size_t>(slot)].serial) slot = s;
        drop(slot, time, next, count);
    }

    Phrase& p   = phrases_[static_cast<std::size_t>(slot)];
    p           = Phrase{};
    p.active    = true;
    p.byTrigger = h.kind == Heard::Kind::Trigger;
    p.group     = h.group;
    p.channel   = h.channel;
    p.key       = h.key;
    p.root      = root;
    p.trigger   = p.byTrigger ? h.velocity : 0xFFFF;
    p.serial    = strikes_++;
    p.start     = time;
    p.duration  = std::max<std::int64_t>(clock_.mode() == Clock::Mode::Free ? clock_.samplesForMs(params_.durationMs)
                                                                            : clock_.samplesForBeats(params_.durationBeats),
                                         1);
    p.lastPass  = params_.release == Release::Loop ? INT64_MAX : 0;
    p.count     = phrasePoints(params_.vary ? p.serial : 0, p.points);

    const std::size_t from = count;
    collect(slot, count);
    if (count > from) sortGrains(next, count);
}

void BurstCore::play(const Grain& g, NoteOut& out) noexcept {
    const Phrase& p  = phrases_[std::min<std::size_t>(g.tag, kMaxPhrases - 1)];
    const Point&  pt = p.points[static_cast<std::size_t>(g.index % kMaxPoints)];

    PitchParams pitch = params_.pitch;
    pitch.octaves     = 0;
    pitch.scatter     = 0;
    const std::int64_t fade  = kOne16 - ((std::int64_t{std::clamp(params_.decay, 0, kOne16)} * pt.time) >> 16);
    const auto         level = static_cast<std::int32_t>((std::int64_t{std::max(params_.level, 0)} * fade) >> 16);
    const std::int64_t base  = clock_.mode() == Clock::Mode::Free ? clock_.samplesForMs(params_.lengthMs)
                                                                  : clock_.samplesForBeats(params_.lengthBeats);
    start(p.root, grainPitch(p.root.pitch + pt.offset, pitch, random_, g.index, set_), g.time,
          grainLength(base, params_.lenSpread, random_, g.index),
          grainVelocity(p.root.velocity, level, params_.velSpread, g.trigger, random_, g.index), out);
}

} // namespace notegran
