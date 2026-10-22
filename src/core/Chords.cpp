#include "core/Chords.h"

#include <algorithm>

#include "core/Pitch.h"
#include "core/Shape.h"
#include "uw/ump/Ump.h"

namespace notegran {

namespace ump = uw::ump;

namespace {

constexpr std::uint8_t kAllSoundOff = 120;
constexpr std::uint8_t kAllNotesOff = 123;

std::int64_t mod(std::int64_t a, std::int64_t n) noexcept { return ((a % n) + n) % n; }

} // namespace

ChordsCore::ChordsCore() noexcept {
    scaleSet_.setLatch(true);   // a scale played and let go stays, until the next one starts
    setParams(params_);
}

void ChordsCore::setParams(const ChordsParams& params) noexcept {
    params_ = params;
    owned_.setVoices(params.voices);
    owned_.setWhenFull(params.whenFull);
    scaleSet_.setChannels(params.scaleChannel < 0 ? NoteSet::kAll : params.scaleChannel, NoteSet::kNone);
    rebuildScale();
}

void ChordsCore::rebuildScale() noexcept {
    if (params_.scaleChannel < 0) {
        const std::size_t i = std::min(params_.scale, familiarScaleCount() - 1);
        scale_ = mapFamiliar(familiarScales()[i], params_.keyFifths, params_.edo, params_.origin, params_.map);
        return;
    }
    // The scale channel's notes, each at its nearest step of the EDO.
    EdoScale grid;
    grid.edo    = std::clamp(params_.edo, 1, 96);
    grid.origin = static_cast<int>(mod(params_.origin, 12));
    std::array<std::int64_t, NoteSet::kCapacity> steps{};
    for (std::size_t i = 0; i < scaleSet_.size(); ++i) steps[i] = grid.stepNear(scaleSet_[i].pitch);
    scale_ = scaleFromSteps(steps.data(), scaleSet_.size(), params_.edo, params_.origin);
}

// The degree a note at `pitch` is, and the absolute step its chord's root goes on.
bool ChordsCore::locate(std::int32_t pitch, int& degree, std::int64_t& rootStep) const noexcept {
    if (scale_.count == 0) return false;
    const std::int64_t near = scale_.stepNear(pitch);

    if (params_.input == InputMatch::Familiar && scale_.familiar()) {
        // Its nearest 12-EDO key against the familiar degrees; Nearest looks outward (down first).
        const int key = (pitch + kSemitone / 2) / kSemitone;
        degree        = scale_.degreeOfSemitone(key);
        for (int d = 1; degree < 0 && params_.offScale == OffScale::Nearest && d <= 6; ++d) {
            degree = scale_.degreeOfSemitone(key - d);
            if (degree < 0) degree = scale_.degreeOfSemitone(key + d);
        }
        if (degree < 0) return false;
        // That degree's step, in the octave nearest the note.
        const std::int64_t target = scale_.keyStep + scale_.steps[static_cast<std::size_t>(degree)];
        std::int64_t       move   = mod(target - near, scale_.edo);
        if (2 * move > scale_.edo) move -= scale_.edo;
        rootStep = near + move;
        return true;
    }

    // Its nearest EDO step against the mapped steps.
    degree   = scale_.degreeOfStep(near);
    rootStep = near;
    for (int d = 1; degree < 0 && params_.offScale == OffScale::Nearest && d <= scale_.edo; ++d) {
        if ((degree = scale_.degreeOfStep(near - d)) >= 0) rootStep = near - d;
        else if ((degree = scale_.degreeOfStep(near + d)) >= 0) rootStep = near + d;
    }
    return degree >= 0;
}

std::size_t ChordsCore::chordFor(std::int32_t pitch, std::array<std::int32_t, kMaxSize>& pitches) const noexcept {
    int          degree   = 0;
    std::int64_t rootStep = 0;
    if (!locate(pitch, degree, rootStep)) return 0;

    // Stack degrees from the root's: every `stack`th.
    const int          size  = std::clamp(params_.size, 1, kMaxSize);
    const int          stack = std::clamp(params_.stack, 1, 6);
    const std::int64_t base  = rootStep - scale_.steps[static_cast<std::size_t>(degree)];   // degree 0's step
    std::array<std::int64_t, kMaxSize> steps{};
    for (int i = 0; i < size; ++i) steps[static_cast<std::size_t>(i)] = scale_.stepOfDegree(degree + i * stack, base);

    const std::int64_t n   = scale_.edo;
    auto               end = steps.begin() + size;
    // Inversion: the lowest notes an octave up.
    const int inversion = std::clamp(params_.inversion, 0, size - 1);
    for (int i = 0; i < inversion; ++i) steps[static_cast<std::size_t>(i)] += n;
    std::sort(steps.begin(), end);
    switch (params_.voicing) {
    case Voicing::Close: break;
    case Voicing::Open:
        for (int i = 1; i < size; i += 2) steps[static_cast<std::size_t>(i)] += n;
        break;
    case Voicing::Drop2:
        if (size >= 2) steps[static_cast<std::size_t>(size - 2)] -= n;
        break;
    case Voicing::Spread:
        for (int i = 0; i < size; ++i) steps[static_cast<std::size_t>(i)] += (i / 2) * n;
        break;
    }
    std::sort(steps.begin(), end);

    std::size_t count = 0;
    for (int i = 0; i < size; ++i) {
        const std::int64_t s = steps[static_cast<std::size_t>(i)] + std::clamp(params_.octave, -2, 2) * n;
        const std::int32_t p = scale_.pitchOfStep(s);
        if (p >= 0 && p <= kMaxPitch) pitches[count++] = p;   // past MIDI's range: left out
    }
    return count;
}

ChordsCore::Held* ChordsCore::find(std::uint8_t group, std::uint8_t channel, std::uint8_t key) noexcept {
    for (Held& h : held_)
        if (h.used && h.key == key && h.channel == channel && h.group == group) return &h;
    return nullptr;
}

ChordsCore::Held* ChordsCore::freeHeld() noexcept {
    for (Held& h : held_)
        if (!h.used) return &h;
    return nullptr;
}

void ChordsCore::release(Held& h, std::int64_t time, NoteOut& out) noexcept {
    for (std::uint8_t i = 0; i < h.count; ++i) {
        Shared& s = shared_[h.notes[i]];
        if (--s.refs == 0) {
            owned_.end(s.id, time, out);
            s = Shared{};
        }
    }
    h = Held{};
}

void ChordsCore::strike(Held& h, std::int64_t time, Dialect dialect, std::uint16_t velocity,
                        const std::array<std::int32_t, kMaxSize>& pitches, std::size_t n, NoteOut& out) noexcept {
    const auto v = static_cast<std::uint16_t>(
        std::clamp<std::int64_t>((std::int64_t{velocity} * std::max(params_.level, 0) + kOne16 / 2) >> 16, 1, 0xFFFF));
    for (std::size_t i = 0; i < n; ++i) {
        // MIDI 1.0 can't carry a fraction: such a note goes out as MIDI 2.0 (PLAN.md, question 10).
        const Dialect d = dialect == Dialect::Midi1 && pitches[i] % kSemitone != 0 ? Dialect::Midi2 : dialect;

        // Already sounding from another chord: share it.
        std::size_t k = kMaxShared;
        for (std::size_t j = 0; j < kMaxShared && k == kMaxShared; ++j) {
            const Shared& s = shared_[j];
            if (s.used && s.pitch == pitches[i] && s.channel == h.channel && s.group == h.group && s.dialect == d) k = j;
        }
        if (k == kMaxShared) {
            for (std::size_t j = 0; j < kMaxShared && k == kMaxShared; ++j)
                if (!shared_[j].used) k = j;
            if (k == kMaxShared) continue;
            const KeyChoice key = chooseKey(pitches[i], d, [&](std::uint8_t kk) { return owned_.sounding(h.group, h.channel, kk); });
            NoteStart       note;
            note.time     = time;
            note.end      = kHeld;
            note.group    = h.group;
            note.channel  = h.channel;
            note.key      = key.key;
            note.dialect  = d;
            note.pitch79  = key.pitch79;
            note.velocity = v;
            const NoteId id = owned_.start(note, out);
            if (id == 0) continue;   // Voices full (Skip), or no room
            shared_[k] = Shared{true, h.group, h.channel, d, pitches[i], id, 0};
        }
        ++shared_[k].refs;
        h.notes[h.count++] = static_cast<std::uint16_t>(k);
    }
}

void ChordsCore::process(InputEvent* inputs, std::size_t inputCount, NoteOut& out) noexcept {
    for (std::size_t i = 0; i < inputCount; ++i) {
        InputEvent&            in = inputs[i];
        const uw::MidiMessage& m  = in.message;
        in.pass                   = true;
        if (!ump::isChannelVoice(m)) continue;
        const std::uint8_t group   = ump::group(m);
        const std::uint8_t channel = ump::channel(m);
        const std::uint8_t index   = static_cast<std::uint8_t>((m.words[0] >> 8) & 0x7F);
        const ump::Status  status  = ump::status(m);

        if (status == ump::Status::ControlChange && (index == kAllNotesOff || index == kAllSoundOff)) {
            for (Held& h : held_)
                if (h.used) release(h, in.time, out);
            owned_.endAll(in.time, out);
            continue;
        }

        // The scale channel: its notes are the scale, not played.
        if (params_.scaleChannel >= 0 && channel == params_.scaleChannel) {
            const Heard heard = scaleSet_.input(in.time, m, in.dialect);
            if (heard.kind == Heard::Kind::SetNote) {
                in.pass = false;
                scaleSet_.settle(in.time);
                rebuildScale();
            }
            continue;
        }

        const bool on  = ump::isNoteOn(m);
        const bool off = ump::isNoteOff(m);
        Held*      h   = find(group, channel, index);
        if (on) {
            if (h != nullptr) {   // struck again while held: the old chord ends first
                release(*h, in.time, out);
            } else {
                h = freeHeld();
                if (h == nullptr) continue;   // too many held: passes through
            }
            *h = Held{true, group, channel, index, false, 0, {}};
            std::array<std::int32_t, kMaxSize> pitches{};
            const std::size_t                  n = chordFor(ump::notePitch79(m), pitches);
            if (n == 0) {
                // Off the scale: Pass plays it as it came; Drop doesn't (Nearest found a degree).
                h->passed = params_.offScale == OffScale::Pass;
                in.pass   = h->passed;
                continue;
            }
            in.pass = false;
            strike(*h, in.time, in.dialect, ump::velocity16(m), pitches, n, out);
            continue;
        }
        if (off) {
            if (h == nullptr) continue;   // not ours (from before a reset): passes
            in.pass = h->passed;
            release(*h, in.time, out);
            continue;
        }
        // Per-note messages: they follow a note that passed; a chorded note's are dropped.
        const bool perNote = status == ump::Status::PolyPressure || status == ump::Status::PerNotePitchBend ||
                             status == ump::Status::RegisteredPerNoteController ||
                             status == ump::Status::AssignablePerNoteController ||
                             status == ump::Status::PerNoteManagement;
        if (perNote && h != nullptr) in.pass = h->passed;
    }
}

void ChordsCore::reset(std::int64_t time, NoteOut& out) noexcept {
    for (Held& h : held_)
        if (h.used) release(h, time, out);
    owned_.endAll(time, out);
    for (Shared& s : shared_) s = Shared{};
    scaleSet_.clear();
    if (params_.scaleChannel >= 0) rebuildScale();
}

} // namespace notegran
