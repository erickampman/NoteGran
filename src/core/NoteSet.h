#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/NoteEvents.h"
#include "uw/ump/MidiMessage.h"

// The set (PLAN.md, "The set"): the notes held on the input, which an assigner picks from and
// Chords builds on. Each member has its pitch in 1/512 semitone (EDO pitches survive), its velocity
// and its arrival order.
//
// - Struck again isn't a change: a note-off followed within `holdOver` samples by a note-on of the
//   same note (group, channel, key) leaves the member in, with the new velocity. Until then a
//   released member is still in the set, so a sequence on Repeat never empties it.
// - Pitch moves are followed, as the author's synths read them: Pitch 7.25 (registered per-note
//   controller 3) replaces the note-on's pitch, and per-note pitch bend adds to either, over the
//   per-note bend range (registered controller 7, else the channel's 0, else ±2). Per-Note
//   Management with Reset clears both.
// - Latch: a released member stays until the next note-on with nothing held starts a new set.
// - Channel split: `setChannel` (-1 all) are members, `triggerChannel` (-1 none) are rhythm.
//
// Time is the caller's absolute sample count, never going back. Inputs at a time are applied
// before picks at that time: call input() for each, then settle(time), then pick. Fixed size, no
// allocation; a note over kCapacity is classed as a set note but not kept.
namespace notegran {

struct Member {
    std::uint8_t  group    = 0;
    std::uint8_t  channel  = 0;
    std::uint8_t  key      = 0;
    Dialect       dialect  = Dialect::Midi2;
    std::uint16_t velocity = 0;      // 16-bit
    std::int32_t  pitch    = 0;      // 1/512 semitone, after 7.25 and bend; may leave 0..127
    std::uint64_t arrival  = 0;      // order of arrival: lower came first
};

// What one input message was, for the caller: whether it passes through (Other always; SetNote
// only if Dry), asks for a pick (a Trigger strike), or ends everything (Reset).
struct Heard {
    enum class Kind : std::uint8_t {
        Other,     // not about a set or trigger note: passes through
        SetNote,   // a note or per-note message on a set channel
        Trigger,   // a note or per-note message on the trigger channel
        Reset,     // All Notes Off or All Sound Off: the set is cleared
    };
    Kind          kind     = Kind::Other;
    bool          strike   = false;  // Trigger: a note-on, asking for one pick
    std::uint8_t  group    = 0;
    std::uint8_t  channel  = 0;
    std::uint8_t  key      = 0;
    Dialect       dialect  = Dialect::Midi2;
    std::uint16_t velocity = 0;      // a note-on's, 16-bit
};

class NoteSet {
public:
    static constexpr std::size_t kCapacity = 512;
    static constexpr int         kAll      = -1;   // setChannel: every channel
    static constexpr int         kNone     = -1;   // triggerChannel: none

    void setHoldOver(std::int64_t samples) noexcept { holdOver_ = samples < 0 ? 0 : samples; }
    // -1 or 0..15. A channel that's both is the trigger channel.
    void setChannels(int setChannel, int triggerChannel) noexcept;
    // Turning latch off drops the latched members at once.
    void setLatch(bool on) noexcept;

    // Reads one input message at `time`. A MIDI 1.0 note is Midi1, a MIDI 2.0 note Midi2; the
    // plugin layer passes Clap for a note that came as a CLAP note, converted to MIDI 2.0.
    Heard input(std::int64_t time, const uw::MidiMessage& m) noexcept;
    Heard input(std::int64_t time, const uw::MidiMessage& m, Dialect dialect) noexcept;

    // Drops the released members whose hold-over has run out by `time` (latches them, with Latch
    // on). Call before picking at `time`.
    void settle(std::int64_t time) noexcept;

    // Whether a member is this note (Dry: its key is sounding, not as one of the plugin's own).
    bool holds(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept {
        return find(group, channel, key) != static_cast<std::size_t>(-1);
    }

    std::size_t   size() const noexcept { return count_; }
    bool          empty() const noexcept { return count_ == 0; }
    const Member& operator[](std::size_t i) const noexcept { return slots_[i].member; }   // no order
    void          clear() noexcept { count_ = 0; }

private:
    enum class State : std::uint8_t { Held, Releasing, Latched };

    struct Slot {
        Member       member;
        State        state      = State::Held;
        std::int64_t releasedAt = 0;
        std::int32_t base       = 0;       // the note-on's pitch
        std::int32_t pitch725   = -1;      // -1: none
        std::uint32_t bend      = 0x80000000u;
    };

    struct Ranges {
        std::int32_t channel = 2 * 512;    // registered controller 0, in 1/512 semitone
        std::int32_t perNote = -1;         // registered controller 7; -1: the channel's
    };

    bool        isSetChannel(std::uint8_t channel) const noexcept;
    std::size_t find(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept;
    void        expire(std::int64_t time, bool inclusive) noexcept;
    void        removeAt(std::size_t i) noexcept;
    void        repitch(Slot& s) const noexcept;
    void        noteOn(std::int64_t time, const uw::MidiMessage& m, Dialect dialect) noexcept;
    void        noteOff(std::int64_t time, const uw::MidiMessage& m) noexcept;

    std::array<Slot, kCapacity> slots_{};
    std::size_t                 count_ = 0;
    std::array<Ranges, 256>     ranges_{};       // [group * 16 + channel]
    std::uint64_t               nextArrival_ = 1;
    std::int64_t                holdOver_    = 0;
    int                         setChannel_     = kAll;
    int                         triggerChannel_ = kNone;
    bool                        latch_          = false;
};

} // namespace notegran
