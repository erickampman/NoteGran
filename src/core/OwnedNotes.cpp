#include "core/OwnedNotes.h"

#include <algorithm>

namespace notegran {

namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

std::size_t channelIndex(std::uint8_t group, std::uint8_t channel) noexcept {
    return static_cast<std::size_t>((group & 0xF) << 4 | (channel & 0xF));
}

std::uint64_t keyBit(std::uint8_t key) noexcept { return std::uint64_t{1} << (key & 63); }

} // namespace

OwnedNotes::OwnedNotes() noexcept = default;

void OwnedNotes::setVoices(int voices) noexcept {
    voices_ = std::clamp(voices, 1, static_cast<int>(kCapacity));
}

bool OwnedNotes::sounding(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept {
    if (key > 127) return false;
    return (keyBits_[channelIndex(group, channel)][key >> 6] & keyBit(key)) != 0;
}

bool OwnedNotes::sounding(NoteId id) const noexcept { return findId(id) != kNone; }

std::int64_t OwnedNotes::nextEnd() const noexcept {
    std::int64_t t = kHeld;
    for (std::size_t i = 0; i < count_; ++i) t = std::min(t, slots_[i].end);
    return t;
}

NoteId OwnedNotes::start(const NoteStart& note, NoteOut& out) noexcept {
    if (note.key > 127) return 0;
    if (!endUntil(note.time, out)) return 0;

    const std::uint8_t group   = note.group & 0xF;
    const std::uint8_t channel = note.channel & 0xF;

    // Everything this start sends, counted before any of it is, so it's all or nothing.
    const std::size_t same     = findKey(group, channel, note.key);
    const std::size_t left     = count_ - (same == kNone ? 0 : 1);
    const std::size_t voices   = static_cast<std::size_t>(voices_);
    if (whenFull_ == WhenFull::Skip && left >= voices) return 0;
    const std::size_t steals   = left + 1 > voices ? left + 1 - voices : 0;
    const std::size_t needed   = (same == kNone ? 0 : 1) + steals + 1;
    if (out.room() < needed) return 0;

    if (same != kNone) endSlot(same, note.time, out);
    while (count_ >= voices) endSlot(oldest(), note.time, out);

    const NoteId id = nextId_++;
    slots_[count_++] = Slot{std::max(note.end, note.time + 1), id, group, channel, note.key, note.dialect};
    keyBits_[channelIndex(group, channel)][note.key >> 6] |= keyBit(note.key);

    out.push(NoteEvent{note.time, true, group, channel, note.key, note.dialect, note.pitch79, note.velocity});
    return id;
}

bool OwnedNotes::end(NoteId id, std::int64_t time, NoteOut& out) noexcept {
    const std::size_t i = findId(id);
    if (i == kNone || out.room() == 0) return false;
    endSlot(i, time, out);
    return true;
}

bool OwnedNotes::endUntil(std::int64_t time, NoteOut& out) noexcept {
    for (;;) {
        std::size_t due = kNone;
        for (std::size_t i = 0; i < count_; ++i) {
            const Slot& s = slots_[i];
            if (s.end > time) continue;
            if (due == kNone || s.end < slots_[due].end || (s.end == slots_[due].end && s.id < slots_[due].id))
                due = i;
        }
        if (due == kNone) return true;
        if (out.room() == 0) return false;
        endSlot(due, slots_[due].end, out);
    }
}

bool OwnedNotes::endAll(std::int64_t time, NoteOut& out) noexcept {
    while (count_ > 0) {
        if (out.room() == 0) return false;
        endSlot(oldest(), time, out);
    }
    return true;
}

std::size_t OwnedNotes::findKey(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept {
    if (!sounding(group, channel, key)) return kNone;
    for (std::size_t i = 0; i < count_; ++i) {
        const Slot& s = slots_[i];
        if (s.key == key && s.channel == channel && s.group == group) return i;
    }
    return kNone;
}

std::size_t OwnedNotes::findId(NoteId id) const noexcept {
    if (id == 0) return kNone;
    for (std::size_t i = 0; i < count_; ++i)
        if (slots_[i].id == id) return i;
    return kNone;
}

std::size_t OwnedNotes::oldest() const noexcept {
    std::size_t o = kNone;
    for (std::size_t i = 0; i < count_; ++i)
        if (o == kNone || slots_[i].id < slots_[o].id) o = i;
    return o;
}

// The caller has checked out.room().
void OwnedNotes::endSlot(std::size_t i, std::int64_t time, NoteOut& out) noexcept {
    const Slot s = slots_[i];
    out.push(NoteEvent{time, false, s.group, s.channel, s.key, s.dialect,
                       static_cast<std::uint16_t>(s.key << 9), 0});
    keyBits_[channelIndex(s.group, s.channel)][s.key >> 6] &= ~keyBit(s.key);
    slots_[i] = slots_[--count_];
}

} // namespace notegran
