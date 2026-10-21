#include "core/Clock.h"

#include <algorithm>
#include <cmath>

namespace notegran {

namespace {

// 128-bit intermediates (GCC and Clang, the two compilers NoteGran builds with), so a product of a
// position and a rate can't overflow.
using i128 = __int128;

std::int64_t floorDiv(i128 a, i128 b) noexcept {   // b > 0
    i128 q = a / b;
    if (a % b != 0 && a < 0) --q;
    return static_cast<std::int64_t>(q);
}

std::int64_t ceilDiv(i128 a, i128 b) noexcept {    // b > 0
    i128 q = a / b;
    if (a % b != 0 && a > 0) ++q;
    return static_cast<std::int64_t>(q);
}

constexpr double       kMinTempo  = 1.0;
constexpr double       kMaxTempo  = 999.0;
constexpr double       kNoTempo   = 120.0;          // no transport, or one without a tempo
constexpr std::int64_t kJumpSlack = kBeat / 64;     // host rounding and tempo ramps within a block

} // namespace

Grid Grid::perSecond(double rate, std::int64_t sampleRate) noexcept {
    Grid g;
    if (!(rate > 0.0) || sampleRate <= 0) return g;
    g.count = std::llround(rate * 65536.0);
    g.span  = sampleRate * 65536;
    return g;
}

Grid Grid::perBeat(std::int64_t num, std::int64_t den) noexcept {
    Grid g;
    if (num <= 0 || den <= 0) return g;
    g.count = num;
    g.span  = den * kBeat;
    return g;
}

std::int64_t Grid::position(std::int64_t n) const noexcept { return ceilDiv(i128{n} * span, count); }

void Clock::setSampleRate(double sampleRate) noexcept {
    sampleRate_   = std::max<std::int64_t>(1, std::llround(sampleRate));
    started_      = false;
    following_    = false;
    anchorSample_ = 0;
    anchorPos_    = 0;
    setTempo(tempo_ > 0.0 ? tempo_ : kNoTempo);
}

double Clock::clampTempo(double bpm) noexcept {
    return std::clamp(std::isfinite(bpm) ? bpm : kNoTempo, kMinTempo, kMaxTempo);
}

void Clock::setTempo(double bpm) noexcept {
    tempo_  = clampTempo(bpm);
    tempoQ_ = std::llround(tempo_ * 65536.0);
}

Clock::Change Clock::begin(std::int64_t start, std::int64_t frames, const Transport* transport) noexcept {
    if (tempoQ_ == 0) setTempo(kNoTempo);   // setSampleRate() not called yet
    start_  = start;
    frames_ = std::max<std::int64_t>(frames, 0);

    Change c;
    const double bpm    = transport && transport->hasTempo ? transport->tempo : kNoTempo;
    const bool   follow = transport && transport->hasBeats && transport->playing;

    if (!started_) {
        // The first block: on its own count, beat 0 is sample 0.
        anchorSample_ = 0;
        anchorPos_    = 0;
        setTempo(bpm);
    }
    if (follow && transport->continues && started_ && following_) {
        from_ = exactBeatAt(start);   // the host's block goes on: nothing new to anchor to
    } else if (follow) {
        bool carryOn = false;
        std::int64_t predicted = 0;
        i128         predictedExact = 0;
        if (started_ && following_) {
            predicted              = beatAt(start);
            predictedExact         = exactBeatAt(start);
            const std::int64_t off = transport->beats - predicted;
            c.jumped               = off > kJumpSlack || off < -kJumpSlack;
            carryOn                = !c.jumped;
        }
        setTempo(bpm);
        anchorSample_ = start;
        anchorPos_    = transport->beats;
        // Playing on: slots continue from where the last block ended, not from the host's rounded
        // position, so none is played twice or skipped at a block boundary.
        // Starting or jumping: from the host's position, less one sample for its rounding.
        from_ = carryOn ? predictedExact : i128{anchorPos_} * samplesPerMinuteQ() - i128{tempoQ_} * kBeat;
    } else {
        c.stopped = started_ && following_;
        // Own count: carry on from where the beat is now, at the new tempo if it changed. Between
        // changes the position is worked out from the anchor, so blocks don't add up rounding.
        if (clampTempo(bpm) != tempo_ || following_) {
            const std::int64_t here = beatAt(start);
            anchorSample_           = start;
            anchorPos_              = here;
            setTempo(bpm);
        }
        from_ = exactBeatAt(start);
    }
    following_ = follow;
    started_   = true;

    if (mode_ == Mode::Free) c = Change{};
    return c;
}

std::int64_t Clock::positionAt(std::int64_t sample) const noexcept {
    return mode_ == Mode::Free ? sample : beatAt(sample);
}

std::int64_t Clock::sampleAt(std::int64_t position) const noexcept {
    return mode_ == Mode::Free ? position : sampleAtBeat(position);
}

i128 Clock::exactBeatAt(std::int64_t sample) const noexcept {
    return i128{anchorPos_} * samplesPerMinuteQ() + i128{sample - anchorSample_} * tempoQ_ * kBeat;
}

std::int64_t Clock::beatAt(std::int64_t sample) const noexcept {
    return anchorPos_ + floorDiv(i128{sample - anchorSample_} * tempoQ_ * kBeat, samplesPerMinuteQ());
}

std::int64_t Clock::sampleAtBeat(std::int64_t position) const noexcept {
    return anchorSample_ + ceilDiv(i128{position - anchorPos_} * samplesPerMinuteQ(), i128{tempoQ_} * kBeat);
}

// Samples per minute, in the tempo's units: beat(s) = s * tempoQ * kBeat / this.
std::int64_t Clock::samplesPerMinuteQ() const noexcept { return 60 * sampleRate_ * 65536; }

std::int64_t Clock::firstSlot(const Grid& grid) const noexcept {
    if (mode_ == Mode::Free)   // the smallest n with ceil(n * span / count) >= start
        return floorDiv((i128{start_} - 1) * grid.count, grid.span) + 1;
    // The smallest n with n * span / count >= from_ / samplesPerMinuteQ().
    return ceilDiv(from_ * grid.count, i128{grid.span} * samplesPerMinuteQ());
}

std::int64_t Clock::slotSample(const Grid& grid, std::int64_t n) const noexcept {
    if (mode_ == Mode::Free) return grid.position(n);
    // The first sample s with beat(s) >= n * span / count, all exact.
    const i128 beats = i128{n} * grid.span - i128{anchorPos_} * grid.count;   // over count
    return anchorSample_ + ceilDiv(beats * samplesPerMinuteQ(), i128{grid.count} * tempoQ_ * kBeat);
}

bool Clock::placeInSlot(const Grid& grid, std::int64_t n, std::int64_t frac, std::int64_t& sample) const noexcept {
    if (grid.count <= 0) return false;
    // The position is num / den: in samples (Free) or beat units (Beats).
    const i128 num = (i128{n} * 65536 + frac) * grid.span;
    const i128 den = i128{grid.count} * 65536;
    if (mode_ == Mode::Free) {
        sample = ceilDiv(num, den);
        return sample >= start_ && sample < blockEnd();
    }
    if (num * samplesPerMinuteQ() < from_ * den) return false;   // before where this block's slots start
    const i128 beats = num - i128{anchorPos_} * den;               // from the anchor, over den
    sample           = anchorSample_ + ceilDiv(beats * samplesPerMinuteQ(), den * tempoQ_ * kBeat);
    if (sample >= blockEnd()) return false;
    if (sample < start_) sample = start_;
    return true;
}

std::int64_t Clock::samplesForMs(double ms) const noexcept {
    if (!(ms > 0.0)) return 1;
    return std::max<std::int64_t>(1, std::llround(ms * static_cast<double>(sampleRate_) / 1000.0));
}

std::int64_t Clock::samplesForBeats(std::int64_t beatUnits) const noexcept {
    if (beatUnits <= 0) return 1;
    return std::max<std::int64_t>(1, ceilDiv(i128{beatUnits} * samplesPerMinuteQ(), i128{tempoQ_} * kBeat));
}

} // namespace notegran
