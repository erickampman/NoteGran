#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "core/NoteEvents.h"

// The owned-note table (PLAN.md, "Notes always end"): every note the plugin has started and not
// yet ended, with the time it ends. Its note-offs are the only ones the plugin sends, so none can
// stick. It also applies `Voices` with `When full` (over the limit, a new note is skipped, or ends
// the oldest first) and the same-key rule (a key already sounding on that channel is ended just
// before it's struck again).
//
// Fixed size, no allocation: the table is a member array, sized far past what any plugin asks
// for. Voices are what's scarce (a synth has at most 32, and a part usually wants far fewer), not
// slots. When it's full, a new note is skipped or steals, as with Voices. Only the notes sounding
// are scanned, so the size costs memory, not time. There's one output port, so no port field.
namespace notegran {

// The end time of a note that has none yet: it sounds until end() or endAll().
inline constexpr std::int64_t kHeld = std::numeric_limits<std::int64_t>::max();

// Names one started note, so a later end() can't end a different note on the same key. 0 is none.
using NoteId = std::uint64_t;

struct NoteStart {
    std::int64_t  time     = 0;      // absolute sample of the note-on
    std::int64_t  end      = kHeld;  // absolute sample of the note-off; at least time + 1
    std::uint8_t  group    = 0;
    std::uint8_t  channel  = 0;
    std::uint8_t  key      = 0;
    Dialect       dialect  = Dialect::Midi2;
    std::uint16_t pitch79  = 0;
    std::uint16_t velocity = 0;
};

// What a new note does when Voices are all sounding (PLAN.md, "When full").
enum class WhenFull : std::uint8_t {
    Skip,    // it isn't played: grains stay whole, a dense cloud thins out
    Steal,   // the oldest note ends first: the newest notes sound, cut short at high density
};

class OwnedNotes {
public:
    static constexpr std::size_t kCapacity = 1024;

    OwnedNotes() noexcept;

    // 1..kCapacity. Lowering it ends nothing at once: with Steal, the next start() ends what's
    // over; with Skip, new notes are skipped until enough have ended.
    void setVoices(int voices) noexcept;
    int  voices() const noexcept { return voices_; }

    void     setWhenFull(WhenFull w) noexcept { whenFull_ = w; }
    WhenFull whenFull() const noexcept { return whenFull_; }

    std::size_t size() const noexcept { return count_; }
    bool        empty() const noexcept { return count_ == 0; }

    bool sounding(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept;
    bool sounding(NoteId id) const noexcept;

    // The earliest end time of any note, or kHeld if none has one.
    std::int64_t nextEnd() const noexcept;

    // Starts a note, sending first: the notes that end at or before its time, the note on its
    // key (the same-key rule), and, with Steal, the oldest notes over Voices; then its note-on.
    // Striking a sounding key again replaces it, so it's never skipped. Returns its id, or 0 if
    // it isn't started: skipped (Voices full, with Skip), or `out` hasn't room for all of that.
    // Notes ended before then stay ended (their offs were sent).
    NoteId start(const NoteStart& note, NoteOut& out) noexcept;

    // Ends one note at `time`, if it's still sounding (Voices or the same-key rule may have ended
    // it already). Returns false if it wasn't, or if `out` is full (it then still sounds).
    bool end(NoteId id, std::int64_t time, NoteOut& out) noexcept;

    // Ends every note whose end time is at or before `time`, each at its own end time, earliest
    // first (ties in the order they started). Stops early if `out` fills; the rest end on the
    // next call, late but not stuck. Returns true if none is left due.
    bool endUntil(std::int64_t time, NoteOut& out) noexcept;

    // Ends every note at `time`, oldest first: reset, All Notes Off, deactivation, a transport
    // stop. Returns true if the table is empty after; false if `out` filled first.
    bool endAll(std::int64_t time, NoteOut& out) noexcept;

private:
    struct Slot {
        std::int64_t end;
        NoteId       id;
        std::uint8_t group;
        std::uint8_t channel;
        std::uint8_t key;
        Dialect      dialect;
    };

    std::size_t findKey(std::uint8_t group, std::uint8_t channel, std::uint8_t key) const noexcept;
    std::size_t findId(NoteId id) const noexcept;
    std::size_t oldest() const noexcept;
    void        endSlot(std::size_t i, std::int64_t time, NoteOut& out) noexcept;

    // Slots 0..count_-1 are the notes sounding, in no order (removal swaps the last in).
    std::array<Slot, kCapacity> slots_{};
    std::size_t                 count_ = 0;
    // One bit per key, per group and channel, for sounding() without a scan.
    std::array<std::array<std::uint64_t, 2>, 256> keyBits_{};
    NoteId   nextId_   = 1;
    int      voices_   = 8;               // the plugins' default; their parameter goes to 32 (PLAN.md)
    WhenFull whenFull_ = WhenFull::Skip;
};

} // namespace notegran
