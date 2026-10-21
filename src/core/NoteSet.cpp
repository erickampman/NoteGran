#include "core/NoteSet.h"

#include <algorithm>

#include "uw/ump/Ump.h"

namespace notegran {

namespace ump = uw::ump;

namespace {

constexpr std::size_t   kNotFound          = static_cast<std::size_t>(-1);
constexpr std::uint32_t kBendCentre        = 0x80000000u;
constexpr std::uint8_t  kPitch7_25         = 3;      // registered per-note controller
constexpr std::uint8_t  kPerNoteReset      = 0x01;   // Per-Note Management "S"
constexpr std::uint8_t  kAllSoundOff       = 120;
constexpr std::uint8_t  kAllNotesOff       = 123;

std::size_t channelIndex(std::uint8_t group, std::uint8_t channel) noexcept {
    return static_cast<std::size_t>((group & 0xF) << 4 | (channel & 0xF));
}

// n / d rounded to nearest, halves away from zero; d > 0.
std::int64_t roundDiv(std::int64_t n, std::int64_t d) noexcept {
    return n >= 0 ? (n + d / 2) / d : -((-n + d / 2) / d);
}

// A bend range's data (registered controller 0 or 7): semitones in bits 31-25, cents in 24-18.
std::int32_t rangeFrom(std::uint32_t data) noexcept {
    const std::int64_t semitones = data >> 25;
    const std::int64_t cents     = std::min<std::int64_t>((data >> 18) & 0x7F, 99);
    return static_cast<std::int32_t>(semitones * 512 + roundDiv(cents * 512, 100));
}

// Pitch 7.25 to 7.9: drop 16 bits of fraction, rounding.
std::int32_t pitch79From725(std::uint32_t data) noexcept {
    return static_cast<std::int32_t>(std::min<std::int64_t>((std::int64_t{data} + 0x8000) >> 16, 0xFFFF));
}

bool isPerNote(ump::Status s) noexcept {
    switch (s) {
    case ump::Status::NoteOn:
    case ump::Status::NoteOff:
    case ump::Status::PolyPressure:
    case ump::Status::RegisteredPerNoteController:
    case ump::Status::AssignablePerNoteController:
    case ump::Status::PerNotePitchBend:
    case ump::Status::PerNoteManagement:
        return true;
    default:
        return false;
    }
}

} // namespace

void NoteSet::setChannels(int setChannel, int triggerChannel) noexcept {
    setChannel_     = std::clamp(setChannel, kAll, 15);
    triggerChannel_ = std::clamp(triggerChannel, kNone, 15);
}

void NoteSet::setLatch(bool on) noexcept {
    latch_ = on;
    if (on) return;
    for (std::size_t i = 0; i < count_;) {
        if (slots_[i].state == State::Latched) removeAt(i);
        else ++i;
    }
}

Heard NoteSet::input(std::int64_t time, const uw::MidiMessage& m) noexcept {
    const Dialect d = ump::messageType(m) == ump::MessageType::Midi1ChannelVoice ? Dialect::Midi1 : Dialect::Midi2;
    return input(time, m, d);
}

Heard NoteSet::input(std::int64_t time, const uw::MidiMessage& m, Dialect dialect) noexcept {
    // A re-strike exactly at the end of the hold-over still counts, so only what ended before.
    expire(time, false);

    Heard h;
    if (!ump::isChannelVoice(m)) return h;

    const ump::Status  status  = ump::status(m);
    const std::uint8_t group   = ump::group(m);
    const std::uint8_t channel = ump::channel(m);
    const std::uint8_t index   = static_cast<std::uint8_t>((m.words[0] >> 8) & 0x7F);   // key or CC number

    if (status == ump::Status::ControlChange && (index == kAllNotesOff || index == kAllSoundOff)) {
        clear();
        h.kind = Heard::Kind::Reset;
        return h;
    }
    if (status == ump::Status::RegisteredController && ump::messageType(m) == ump::MessageType::Midi2ChannelVoice) {
        const std::uint8_t controller = static_cast<std::uint8_t>(m.words[0] & 0x7F);
        Ranges&            r          = ranges_[channelIndex(group, channel)];
        if (index == 0 && controller == 0) r.channel = rangeFrom(m.words[1]);
        else if (index == 0 && controller == 7) r.perNote = rangeFrom(m.words[1]);
        else return h;
        for (std::size_t i = 0; i < count_; ++i)
            if (slots_[i].member.group == group && slots_[i].member.channel == channel) repitch(slots_[i]);
        return h;
    }
    if (!isPerNote(status)) return h;

    h.group   = group;
    h.channel = channel;
    h.key     = index;
    h.dialect = dialect;
    const bool on  = ump::isNoteOn(m);
    const bool off = ump::isNoteOff(m);
    if (on) h.velocity = ump::velocity16(m);

    if (triggerChannel_ != kNone && channel == triggerChannel_) {
        h.kind   = Heard::Kind::Trigger;
        h.strike = on;
        return h;
    }
    if (!isSetChannel(channel)) return h;
    h.kind = Heard::Kind::SetNote;

    if (on) {
        noteOn(time, m, dialect);
        return h;
    }
    if (off) {
        noteOff(time, m);
        return h;
    }

    const std::size_t i = find(group, channel, index);
    if (i == kNotFound) return h;
    Slot& s = slots_[i];
    switch (status) {
    case ump::Status::PerNotePitchBend:
        s.bend = m.words[1];
        break;
    case ump::Status::RegisteredPerNoteController:
        if ((m.words[0] & 0xFF) != kPitch7_25) return h;
        s.pitch725 = pitch79From725(m.words[1]);
        break;
    case ump::Status::PerNoteManagement:
        if ((m.words[0] & kPerNoteReset) == 0) return h;
        s.pitch725 = -1;
        s.bend     = kBendCentre;
        break;
    default:
        return h;
    }
    repitch(s);
    return h;
}

void NoteSet::settle(std::int64_t time) noexcept { expire(time, true); }

bool NoteSet::isSetChannel(std::uint8_t channel) const noexcept {
    if (triggerChannel_ != kNone && channel == triggerChannel_) return false;
    return setChannel_ == kAll || channel == setChannel_;
}

std::size_t NoteSet::find(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
        const Member& m = slots_[i].member;
        if (m.key == key && m.channel == channel && m.group == group) return i;
    }
    return kNotFound;
}

void NoteSet::expire(std::int64_t time, bool inclusive) noexcept {
    for (std::size_t i = 0; i < count_;) {
        Slot& s = slots_[i];
        const std::int64_t ends = s.releasedAt + holdOver_;
        if (s.state == State::Releasing && (inclusive ? ends <= time : ends < time)) {
            if (latch_) {
                s.state = State::Latched;
            } else {
                removeAt(i);
                continue;
            }
        }
        ++i;
    }
}

void NoteSet::removeAt(std::size_t i) noexcept { slots_[i] = slots_[--count_]; }

void NoteSet::repitch(Slot& s) const noexcept {
    const Ranges&      r     = ranges_[channelIndex(s.member.group, s.member.channel)];
    const std::int64_t range = r.perNote >= 0 ? r.perNote : r.channel;
    const std::int64_t d     = static_cast<std::int64_t>(s.bend) - kBendCentre;
    const std::int64_t bend  = roundDiv(d * range, d >= 0 ? 0x7FFFFFFF : 0x80000000LL);
    s.member.pitch = static_cast<std::int32_t>((s.pitch725 >= 0 ? s.pitch725 : s.base) + bend);
}

void NoteSet::noteOn(std::int64_t, const uw::MidiMessage& m, Dialect dialect) noexcept {
    const std::uint8_t group   = ump::group(m);
    const std::uint8_t channel = ump::channel(m);
    const std::uint8_t key     = ump::noteNumber(m);

    std::size_t i = find(group, channel, key);
    if (latch_ && (i == kNotFound || slots_[i].state == State::Latched)) {
        // A note-on with nothing held starts a new set.
        const bool held = std::any_of(slots_.begin(), slots_.begin() + static_cast<std::ptrdiff_t>(count_),
                                      [](const Slot& s) { return s.state != State::Latched; });
        if (!held) {
            count_ = 0;
            i      = kNotFound;
        }
    }

    if (i == kNotFound) {
        if (count_ == kCapacity) return;
        i                 = count_++;
        slots_[i]         = Slot{};
        slots_[i].member  = Member{group, channel, key, dialect, 0, 0, nextArrival_++};
    }
    // Struck again (or new): the arrival stays; per-note bend and 7.25 stay too, as in a synth,
    // until a Per-Note Management Reset.
    Slot& s          = slots_[i];
    s.state          = State::Held;
    s.member.dialect = dialect;
    s.member.velocity = ump::velocity16(m);
    s.base           = ump::notePitch79(m);
    repitch(s);
}

void NoteSet::noteOff(std::int64_t time, const uw::MidiMessage& m) noexcept {
    const std::size_t i = find(ump::group(m), ump::channel(m), ump::noteNumber(m));
    if (i == kNotFound || slots_[i].state != State::Held) return;
    slots_[i].state      = State::Releasing;
    slots_[i].releasedAt = time;
}

} // namespace notegran
