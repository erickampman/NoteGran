#include "core/Buffer.h"

#include <algorithm>
#include <cstdlib>

#include "core/Shape.h"

namespace notegran {

void BufferCore::setParams(const BufferParams& params) noexcept {
    params_ = params;
    applySettings(params.settings);
}

std::int64_t BufferCore::window() const noexcept {
    return std::max<std::int64_t>(params_.settings.sync == Clock::Mode::Free ? clock_.samplesForMs(params_.windowMs)
                                                                             : clock_.samplesForBeats(params_.windowBeats),
                                  1);
}

std::int64_t BufferCore::baseLength() const noexcept {
    return params_.settings.sync == Clock::Mode::Free ? clock_.samplesForMs(params_.lengthMs)
                                                      : clock_.samplesForBeats(params_.lengthBeats);
}

// Position + Scan's phase + Spray, as a fraction of the window (Q32), from its oldest end.
std::int64_t BufferCore::readPoint(std::int64_t time, std::int64_t index) const noexcept {
    const std::int64_t w     = window();
    const std::int64_t scan  = std::clamp(params_.scan, -4 * kOne16, 4 * kOne16);
    const std::int64_t spray = std::clamp(params_.spray, 0, kOne16);
    const auto         phase = static_cast<std::int64_t>((static_cast<__int128>(scan) * time * kOne16 / w) % kOne32);
    std::int64_t       v     = (std::int64_t{std::clamp(params_.position, 0, kOne16)} << 16) + phase +
                     (((random_.unit(Stream::Position, index, 0) * 2 - kOne32) * spray) >> 16);
    if (v != kOne32) v = ((v % kOne32) + kOne32) % kOne32;   // 1, unmoved, is the newest end
    return anchor(time) - w + static_cast<std::int64_t>((static_cast<__int128>(w) * v + (kOne32 >> 1)) >> 32);   // nearest
}

bool BufferCore::busy() const noexcept {
    if (Assigner::busy()) return true;
    for (const Fragment& f : fragments_)
        if (f.active && f.next < f.count) return true;
    // Grains to come: a note in the window (the newest is the latest to start).
    if (triggered() || (params_.gate && set_.empty()) || written_ == 0) return false;
    return noteAt(written_ - 1).start >= anchor(clock_.blockEnd()) - window();
}

int BufferCore::capture(std::array<Captured, kCapacity>& out) const noexcept {
    const std::int64_t e    = anchor(clock_.blockEnd());
    const std::int64_t from = e - window();
    int                n    = 0;
    for (std::int64_t s = oldest(); s < written_; ++s) {
        const Note& note = noteAt(s);
        if (note.start < from || note.start > e) continue;
        const bool held = note.end == kOpen;
        out[static_cast<std::size_t>(n++)] =
            Captured{note.start - from, (held ? e : note.end) - note.start, note.member.pitch, note.member.velocity, held};
    }
    return n;
}

void BufferCore::cleared() noexcept {
    written_ = 0;
    for (Fragment& f : fragments_) f.active = false;
}

void BufferCore::heard(const Heard& h, std::int64_t time, std::size_t, std::size_t&) noexcept {
    if (h.kind != Heard::Kind::SetNote || (!h.strike && !h.release)) return;
    // A note-off fixes the length of the newest note held on that key; so does a second note-on.
    for (std::int64_t s = written_; s-- > oldest();) {
        Note& n = notes_[static_cast<std::size_t>(s % kCapacity)];
        if (n.end == kOpen && n.member.group == h.group && n.member.channel == h.channel && n.member.key == h.key) {
            n.end = std::max(time, n.start + 1);
            break;
        }
    }
    if (!h.strike || frozen_) return;
    for (std::size_t i = 0; i < set_.size(); ++i) {
        const Member& m = set_[i];
        if (m.group != h.group || m.channel != h.channel || m.key != h.key) continue;
        notes_[static_cast<std::size_t>(written_ % kCapacity)] = Note{time, kOpen, m};
        ++written_;
        return;
    }
}

void BufferCore::collectGrains(std::size_t& count) noexcept {
    if (params_.freeze != frozen_) {
        frozen_ = params_.freeze;
        if (frozen_) freezeTime_ = clock_.blockStart();
    }
    if (!triggered()) {
        const Grid g = params_.settings.sync == Clock::Mode::Free ? Grid::perSecond(params_.density, clock_.sampleRate())
                                                                  : Grid::perBeat(params_.divisionNum, params_.divisionDen);
        collectOnsets(g, params_.onsetRandom, count);
    }
    for (int s = 0; s < kMaxFragments; ++s) {
        Fragment& f = fragments_[static_cast<std::size_t>(s)];
        if (f.active && f.next == f.count && f.lastTime < clock_.blockStart()) f.active = false;
        if (f.active) collectFragment(s, &count);
    }
    sortGrains(count);
}

// The onsets first at a sample, then the fragments' notes, in the order the fragments started.
void BufferCore::sortGrains(std::size_t count) noexcept {
    auto key = [&](const Grain& g) {
        return g.tag == 0 ? std::int64_t{-1} : fragments_[static_cast<std::size_t>(g.tag - 1)].serial;
    };
    std::sort(grains_.begin(), grains_.begin() + static_cast<std::ptrdiff_t>(count), [&](const Grain& a, const Grain& b) {
        if (a.time != b.time) return a.time < b.time;
        if (key(a) != key(b)) return key(a) < key(b);
        return a.index < b.index;
    });
}

// The fragment's notes starting before the block's end, from where it got to: appended to the
// block's grains (collectGrains), or with no `count`, put among those still to play (play()).
void BufferCore::collectFragment(int slot, std::size_t* count) noexcept {
    Fragment& f = fragments_[static_cast<std::size_t>(slot)];
    while (f.next < f.count) {
        const std::int64_t t = f.start + f.notes[static_cast<std::size_t>(f.next)].offset;
        const Grain        g{t, f.next, f.trigger, static_cast<std::uint8_t>(slot + 1)};
        if (t >= clock_.blockEnd()) break;
        if (count != nullptr) {
            if (*count >= grains_.size()) break;
            grains_[(*count)++] = g;
        } else if (!addGrain(g)) {
            break;
        }
        f.lastTime = t;
        ++f.next;
    }
}

void BufferCore::startFragment(const Grain& g) noexcept {
    const std::int64_t e    = anchor(g.time);
    const std::int64_t from = e - window();
    const std::int64_t r    = readPoint(g.time, g.index);
    const std::int64_t len  = grainLength(baseLength(), params_.lenSpread, random_, g.index);

    // A free slot (every note collected and played), else the oldest fragment's.
    int slot = -1;
    for (int s = 0; s < kMaxFragments && slot < 0; ++s) {
        const Fragment& f = fragments_[static_cast<std::size_t>(s)];
        if (!f.active || (f.next == f.count && f.lastTime < g.time)) slot = s;
    }
    if (slot < 0) {
        slot = 0;
        for (int s = 1; s < kMaxFragments; ++s)
            if (fragments_[static_cast<std::size_t>(s)].serial < fragments_[static_cast<std::size_t>(slot)].serial) slot = s;
        dropGrains(static_cast<std::uint8_t>(slot + 1));
    }

    Fragment& f = fragments_[static_cast<std::size_t>(slot)];
    f           = Fragment{};
    f.serial    = fragmentsStarted_++;
    f.start     = g.time;
    f.index     = g.index;
    f.trigger   = g.trigger;
    for (std::int64_t s = oldest(); s < written_ && f.count < kMaxNotes; ++s) {
        const Note& n = noteAt(s);
        if (n.start < from || n.start > e || n.start < r || n.start >= r + len) continue;
        // Its own length, past the fragment's end too (so far, if held).
        const std::int64_t own = n.end == kOpen ? g.time - n.start : n.end - n.start;
        f.notes[static_cast<std::size_t>(f.count++)] = FragmentNote{n.start - r, std::max<std::int64_t>(own, 1), n.member};
    }
    f.active = f.count > 0;
    if (f.active) collectFragment(slot, nullptr);
}

void BufferCore::play(const Grain& g, NoteOut& out) noexcept {
    if (g.tag > 0) {   // one of a fragment's notes
        const Fragment&     f = fragments_[std::min<std::size_t>(g.tag - 1u, kMaxFragments - 1)];
        const FragmentNote& n = f.notes[static_cast<std::size_t>(g.index % kMaxNotes)];
        // The fragment's pitch draws for every note (a chord stays a chord); its own velocity draws.
        start(n.member, grainPitch(n.member.pitch, params_.pitch, random_, f.index, set_), g.time, n.length,
              grainVelocity(n.member.velocity, params_.level, params_.velSpread, f.trigger, random_,
                            f.index * kMaxNotes + g.index),
              out);
        return;
    }
    if (params_.gate && !triggered() && set_.empty()) return;
    if (params_.grain == BufferGrain::Fragment) {
        startFragment(g);
        return;
    }

    // Note: the captured note starting nearest the read point.
    const std::int64_t e    = anchor(g.time);
    const std::int64_t from = e - window();
    const std::int64_t r    = readPoint(g.time, g.index);
    const Note*        best = nullptr;
    for (std::int64_t s = oldest(); s < written_; ++s) {
        const Note& n = noteAt(s);
        if (n.start < from || n.start > e) continue;
        if (best == nullptr || std::llabs(n.start - r) < std::llabs(best->start - r)) best = &n;
    }
    if (best == nullptr) return;
    const Member&      m    = best->member;
    const std::int64_t base = params_.sourceLength
                                  ? std::max<std::int64_t>((best->end == kOpen ? g.time : best->end) - best->start, 1)
                                  : baseLength();
    start(m, grainPitch(m.pitch, params_.pitch, random_, g.index, set_), g.time,
          grainLength(base, params_.lenSpread, random_, g.index),
          grainVelocity(m.velocity, params_.level, params_.velSpread, g.trigger, random_, g.index), out);
}

} // namespace notegran
