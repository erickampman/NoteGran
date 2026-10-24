#pragma once

#include <cstddef>
#include <cstdint>

#include "uw/ump/MidiMessage.h"

// What the core sends: note starts and ends, in absolute sample time, before they're written as
// UMP or CLAP events. The plugin layer turns each into the dialect it names, at the offset of
// `time` in its block (docs/design.md, "Ports and dialects").
namespace notegran {

// The dialect a note leaves in: that of the note it came from.
enum class Dialect : std::uint8_t {
    Midi1,   // MIDI 1.0 channel voice, the pitch rounded to the key
    Midi2,   // MIDI 2.0 channel voice, with a Pitch 7.9 attribute
    Clap,    // a CLAP note, the fraction as a CLAP_NOTE_EXPRESSION_TUNING
};

struct NoteEvent {
    std::int64_t  time     = 0;      // absolute sample
    bool          on       = false;
    std::uint8_t  group    = 0;      // 0..15
    std::uint8_t  channel  = 0;      // 0..15
    std::uint8_t  key      = 0;      // 0..127: the note number it's addressed by
    Dialect       dialect  = Dialect::Midi2;
    std::uint16_t pitch79  = 0;      // note-on: the pitch in 1/512 semitone; note-off: key << 9
    std::uint16_t velocity = 0;      // note-on: 16-bit; note-off: 0
};

// One input message, in time order, and whether it goes out unchanged (set by the core).
struct InputEvent {
    std::int64_t    time    = 0;                 // absolute sample
    uw::MidiMessage message;
    Dialect         dialect = Dialect::Midi2;    // Clap for a CLAP note, converted to MIDI 2.0
    bool            pass    = false;             // out: whether it goes out unchanged
};

// A caller's output buffer: fixed capacity, never grows. Whatever pushes into it checks room()
// first, so a full buffer delays a note-off rather than losing it.
class NoteOut {
public:
    NoteOut(NoteEvent* data, std::size_t capacity) noexcept : data_(data), capacity_(capacity) {}

    std::size_t size() const noexcept { return size_; }
    std::size_t room() const noexcept { return capacity_ - size_; }
    const NoteEvent& operator[](std::size_t i) const noexcept { return data_[i]; }
    void clear() noexcept { size_ = 0; }

    bool push(const NoteEvent& e) noexcept {
        if (size_ == capacity_) return false;
        data_[size_++] = e;
        return true;
    }

private:
    NoteEvent*  data_;
    std::size_t capacity_;
    std::size_t size_ = 0;
};

} // namespace notegran
