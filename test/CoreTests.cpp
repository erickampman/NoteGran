// NoteGran core tests (PLAN.md, phase 1). Silent unless a check fails.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/NoteSet.h"
#include "core/OwnedNotes.h"
#include "uw/ump/Ump.h"

using namespace notegran;

namespace {

int failures = 0;

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

// An output buffer with its storage.
struct Out {
    explicit Out(std::size_t capacity) : events(capacity), out(events.data(), capacity) {}
    std::vector<NoteEvent> events;
    NoteOut                out;
};

NoteStart note(std::int64_t time, std::int64_t end, std::uint8_t key, std::uint8_t channel = 0,
               std::uint8_t group = 0) {
    NoteStart n;
    n.time     = time;
    n.end      = end;
    n.group    = group;
    n.channel  = channel;
    n.key      = key;
    n.pitch79  = static_cast<std::uint16_t>(key << 9);
    n.velocity = 0x8000;
    return n;
}

bool isOn(const NoteEvent& e, std::int64_t time, std::uint8_t key, std::uint8_t channel = 0) {
    return e.on && e.time == time && e.key == key && e.channel == channel;
}

bool isOff(const NoteEvent& e, std::int64_t time, std::uint8_t key, std::uint8_t channel = 0) {
    return !e.on && e.time == time && e.key == key && e.channel == channel && e.velocity == 0;
}

// --- the owned-note table -----------------------------------------------------------------

void testStartAndEnd() {
    OwnedNotes t;
    Out o(16);
    NoteStart n = note(100, 150, 60);
    n.dialect   = Dialect::Midi1;
    n.pitch79   = (60 << 9) + 100;
    const NoteId id = t.start(n, o.out);
    CHECK(id != 0);
    CHECK(o.out.size() == 1);
    CHECK(isOn(o.out[0], 100, 60));
    CHECK(o.out[0].dialect == Dialect::Midi1 && o.out[0].pitch79 == (60 << 9) + 100 && o.out[0].velocity == 0x8000);
    CHECK(t.sounding(0, 0, 60) && t.sounding(id));
    CHECK(t.nextEnd() == 150);

    o.out.clear();
    CHECK(t.endUntil(149, o.out));
    CHECK(o.out.size() == 0);
    CHECK(t.endUntil(150, o.out));
    CHECK(o.out.size() == 1);
    CHECK(isOff(o.out[0], 150, 60));
    CHECK(o.out[0].dialect == Dialect::Midi1);   // the off leaves in the note's dialect
    CHECK(!t.sounding(0, 0, 60) && !t.sounding(id) && t.empty());
    CHECK(t.nextEnd() == kHeld);
}

void testEndsInOrder() {
    OwnedNotes t;
    t.setVoices(8);
    Out o(16);
    t.start(note(0, 300, 60), o.out);
    t.start(note(1, 200, 61), o.out);
    t.start(note(2, 300, 62), o.out);   // ties with 60: 60 started first
    t.start(note(3, 250, 63), o.out);
    o.out.clear();
    CHECK(t.endUntil(1000, o.out));
    CHECK(o.out.size() == 4);
    CHECK(isOff(o.out[0], 200, 61));
    CHECK(isOff(o.out[1], 250, 63));
    CHECK(isOff(o.out[2], 300, 60));
    CHECK(isOff(o.out[3], 300, 62));
}

void testShortestLength() {
    OwnedNotes t;
    Out o(4);
    t.start(note(50, 50, 60), o.out);    // zero length
    t.start(note(50, 10, 61), o.out);    // ends before it starts
    CHECK(t.nextEnd() == 51);
    o.out.clear();
    t.endUntil(51, o.out);
    CHECK(o.out.size() == 2 && isOff(o.out[0], 51, 60) && isOff(o.out[1], 51, 61));
}

void testHeld() {
    OwnedNotes t;
    Out o(8);
    const NoteId a = t.start(note(0, kHeld, 60), o.out);
    const NoteId b = t.start(note(0, kHeld, 64), o.out);
    CHECK(t.nextEnd() == kHeld);
    o.out.clear();
    CHECK(t.endUntil(1'000'000'000, o.out) && o.out.size() == 0);   // held notes aren't due
    CHECK(t.end(b, 500, o.out));
    CHECK(o.out.size() == 1 && isOff(o.out[0], 500, 64));
    CHECK(!t.end(b, 600, o.out));            // already ended
    CHECK(!t.end(0, 600, o.out));            // no note
    CHECK(o.out.size() == 1);
    CHECK(t.sounding(a) && t.size() == 1);
}

void testSameKey() {
    OwnedNotes t;
    Out o(8);
    const NoteId a = t.start(note(0, 1000, 60), o.out);
    o.out.clear();
    const NoteId b = t.start(note(10, 500, 60), o.out);
    CHECK(o.out.size() == 2);
    CHECK(isOff(o.out[0], 10, 60));          // the old note's off goes just before
    CHECK(isOn(o.out[1], 10, 60));
    CHECK(!t.sounding(a) && t.sounding(b) && t.size() == 1);

    // The first note's owner ending it later can't end the second.
    o.out.clear();
    CHECK(!t.end(a, 20, o.out));
    CHECK(o.out.size() == 0 && t.sounding(b));

    // Another channel or group is another key.
    t.start(note(30, 500, 60, 1), o.out);
    t.start(note(30, 500, 60, 0, 1), o.out);
    CHECK(t.size() == 3);
    CHECK(t.sounding(0, 1, 60) && t.sounding(1, 0, 60) && !t.sounding(1, 1, 60));
}

void testVoices() {
    OwnedNotes t;
    t.setVoices(2);
    Out o(8);
    t.start(note(0, 1000, 60), o.out);
    t.start(note(1, 1000, 62), o.out);
    o.out.clear();
    t.start(note(2, 1000, 64), o.out);       // over the limit: 60, the oldest, ends first
    CHECK(o.out.size() == 2);
    CHECK(isOff(o.out[0], 2, 60) && isOn(o.out[1], 2, 64));
    CHECK(t.size() == 2);

    // A note that ends at the new one's time ends on its own, and isn't a steal.
    o.out.clear();
    OwnedNotes u;
    u.setVoices(1);
    u.start(note(0, 10, 60), o.out);
    o.out.clear();
    u.start(note(10, 20, 62), o.out);
    CHECK(o.out.size() == 2 && isOff(o.out[0], 10, 60) && isOn(o.out[1], 10, 62));

    // The same key, at the limit, isn't also a steal.
    o.out.clear();
    t.start(note(3, 1000, 64), o.out);
    CHECK(o.out.size() == 2 && isOff(o.out[0], 3, 64) && isOn(o.out[1], 3, 64));
    CHECK(t.sounding(0, 0, 62));

    // Lowering Voices ends what's over at the next start, oldest first.
    t.setVoices(8);
    t.start(note(4, 1000, 66), o.out);
    t.start(note(5, 1000, 67), o.out);
    CHECK(t.size() == 4);                    // 62, 64, 66, 67
    t.setVoices(2);
    CHECK(t.size() == 4);
    o.out.clear();
    t.start(note(6, 1000, 69), o.out);
    CHECK(o.out.size() == 4);
    CHECK(isOff(o.out[0], 6, 62) && isOff(o.out[1], 6, 64) && isOff(o.out[2], 6, 66));
    CHECK(isOn(o.out[3], 6, 69));
    CHECK(t.size() == 2);

    t.setVoices(0);
    CHECK(t.voices() == 1);
    t.setVoices(1'000'000);
    CHECK(t.voices() == static_cast<int>(OwnedNotes::kCapacity));
}

void testNoRoom() {
    OwnedNotes t;
    t.setVoices(1);
    Out big(8);
    t.start(note(0, 1000, 60), big.out);

    // A start that would steal needs two places; with one it does nothing.
    Out one(1);
    CHECK(t.start(note(1, 1000, 62), one.out) == 0);
    CHECK(one.out.size() == 0);
    CHECK(t.sounding(0, 0, 60) && !t.sounding(0, 0, 62));

    // endUntil stops when the buffer fills; the rest end next time, late but not stuck.
    OwnedNotes u;
    u.setVoices(8);
    for (std::uint8_t k = 0; k < 5; ++k) u.start(note(0, 100 + k, static_cast<std::uint8_t>(60 + k)), big.out);
    Out two(2);
    CHECK(!u.endUntil(200, two.out));
    CHECK(two.out.size() == 2 && isOff(two.out[0], 100, 60) && isOff(two.out[1], 101, 61));
    two.out.clear();
    CHECK(!u.endUntil(300, two.out));
    CHECK(isOff(two.out[0], 102, 62) && isOff(two.out[1], 103, 63));
    two.out.clear();
    CHECK(u.endUntil(400, two.out));
    CHECK(two.out.size() == 1 && isOff(two.out[0], 104, 64));
    CHECK(u.empty());

    // endAll the same way.
    big.out.clear();
    for (std::uint8_t k = 0; k < 3; ++k) u.start(note(500, kHeld, static_cast<std::uint8_t>(60 + k)), big.out);
    two.out.clear();
    CHECK(!u.endAll(600, two.out));
    CHECK(u.size() == 1);
    two.out.clear();
    CHECK(u.endAll(601, two.out));
    CHECK(two.out.size() == 1 && isOff(two.out[0], 601, 62) && u.empty());
}

void testEndAll() {
    OwnedNotes t;
    t.setVoices(8);
    Out o(16);
    t.start(note(0, 1000, 64), o.out);
    t.start(note(1, kHeld, 60), o.out);
    t.start(note(2, 50, 67, 3, 2), o.out);
    o.out.clear();
    CHECK(t.endAll(40, o.out));
    CHECK(o.out.size() == 3);
    CHECK(isOff(o.out[0], 40, 64) && isOff(o.out[1], 40, 60));
    CHECK(isOff(o.out[2], 40, 67, 3) && o.out[2].group == 2);
    CHECK(t.empty() && !t.sounding(2, 3, 67) && !t.sounding(0, 0, 60));
}

void testFull() {
    // Every slot in use: a new note ends the oldest, as Voices does.
    OwnedNotes t;
    t.setVoices(static_cast<int>(OwnedNotes::kCapacity));
    Out o(OwnedNotes::kCapacity + 4);
    for (std::size_t i = 0; i < OwnedNotes::kCapacity; ++i) {
        const auto key     = static_cast<std::uint8_t>(i % 128);
        const auto channel = static_cast<std::uint8_t>(i / 128 % 16);
        const auto group   = static_cast<std::uint8_t>(i / 2048);
        CHECK(t.start(note(static_cast<std::int64_t>(i), kHeld, key, channel, group), o.out) != 0);
    }
    CHECK(t.size() == OwnedNotes::kCapacity);
    o.out.clear();
    CHECK(t.start(note(5000, kHeld, 0, 0, 1), o.out) != 0);
    CHECK(o.out.size() == 2 && isOff(o.out[0], 5000, 0, 0) && o.out[0].group == 0);
    CHECK(t.size() == OwnedNotes::kCapacity);
    CHECK(!t.sounding(0, 0, 0) && t.sounding(1, 0, 0) && t.sounding(0, 0, 1));
}

// Random starts, ends and resets, checking the output as a synth would see it: every off ends a
// key that's on, no key is struck while it's on, and at the end nothing is left on.
void testNothingSticks() {
    std::uint32_t lcg = 12345;
    auto rnd = [&lcg](std::uint32_t n) {
        lcg = lcg * 1664525u + 1013904223u;
        return (lcg >> 8) % n;
    };

    OwnedNotes t;
    Out o(64);
    std::vector<bool>   on(2 * 16 * 128, false);
    std::vector<NoteId> held;
    std::int64_t        now = 0;
    long                ons = 0, offs = 0;

    auto drain = [&] {
        for (std::size_t i = 0; i < o.out.size(); ++i) {
            const NoteEvent& e = o.out[i];
            const std::size_t k = (static_cast<std::size_t>(e.group) * 16 + e.channel) * 128 + e.key;
            if (e.on) {
                CHECK(!on[k]);
                on[k] = true;
                ++ons;
            } else {
                CHECK(on[k]);
                on[k] = false;
                ++offs;
            }
        }
        o.out.clear();
    };

    for (int step = 0; step < 20000; ++step) {
        now += rnd(40);
        switch (rnd(20)) {
        case 0:
            t.setVoices(1 + static_cast<int>(rnd(24)));
            break;
        case 1:
            if (!held.empty()) {
                const std::size_t i = rnd(static_cast<std::uint32_t>(held.size()));
                t.end(held[i], now, o.out);
                held[i] = held.back();
                held.pop_back();
            }
            break;
        case 2:
            if (rnd(10) == 0) {
                t.endAll(now, o.out);
                held.clear();
            }
            break;
        default: {
            const bool hold = rnd(8) == 0;
            const NoteStart n = note(now, hold ? kHeld : now + rnd(300), static_cast<std::uint8_t>(48 + rnd(12)),
                                     static_cast<std::uint8_t>(rnd(2)), static_cast<std::uint8_t>(rnd(2)));
            const NoteId id = t.start(n, o.out);
            if (id != 0) CHECK(static_cast<int>(t.size()) <= t.voices());
            if (hold && id != 0) held.push_back(id);
            break;
        }
        }
        drain();
    }
    while (!t.endAll(now, o.out)) drain();
    drain();
    CHECK(t.empty());
    CHECK(ons == offs && ons > 10000);
    for (bool b : on) CHECK(!b);
}

// --- the set ------------------------------------------------------------------------------

uw::MidiMessage msg(std::uint32_t w0, std::uint32_t w1 = 0) {
    uw::MidiMessage m;
    m.words[0]  = w0;
    m.words[1]  = w1;
    m.wordCount = uw::ump::wordCountForType(static_cast<std::uint8_t>(w0 >> 28));
    return m;
}

uw::MidiMessage m1(std::uint8_t status, std::uint8_t channel, std::uint8_t data1, std::uint8_t data2) {
    return msg(0x20000000u | std::uint32_t(status << 4 | channel) << 16 | std::uint32_t(data1) << 8 | data2);
}

uw::MidiMessage m2(std::uint8_t status, std::uint8_t channel, std::uint8_t index1, std::uint8_t index2,
                   std::uint32_t data, std::uint8_t group = 0) {
    return msg(0x40000000u | std::uint32_t(group) << 24 | std::uint32_t(status) << 20 | std::uint32_t(channel) << 16 |
                   std::uint32_t(index1) << 8 | index2,
               data);
}

uw::MidiMessage on1(std::uint8_t key, std::uint8_t velocity = 100, std::uint8_t channel = 0) {
    return m1(0x9, channel, key, velocity);
}
uw::MidiMessage off1(std::uint8_t key, std::uint8_t channel = 0) { return m1(0x8, channel, key, 0); }

uw::MidiMessage on2(std::uint8_t key, std::uint16_t velocity, std::uint16_t pitch79 = 0, std::uint8_t channel = 0,
                    std::uint8_t group = 0) {
    return pitch79 == 0 ? m2(0x9, channel, key, 0, std::uint32_t(velocity) << 16, group)
                        : m2(0x9, channel, key, 3, std::uint32_t(velocity) << 16 | pitch79, group);
}
uw::MidiMessage off2(std::uint8_t key, std::uint8_t channel = 0, std::uint8_t group = 0) {
    return m2(0x8, channel, key, 0, 0, group);
}

// The member with this key, or null.
const Member* member(const NoteSet& s, std::uint8_t key, std::uint8_t channel = 0) {
    for (std::size_t i = 0; i < s.size(); ++i)
        if (s[i].key == key && s[i].channel == channel) return &s[i];
    return nullptr;
}

void testSetBasics() {
    NoteSet s;
    Heard h = s.input(0, on1(60, 100));
    CHECK(h.kind == Heard::Kind::SetNote && !h.strike && h.key == 60);
    s.input(0, on2(64, 0x4000, (64 << 9) + 171));
    s.settle(0);
    CHECK(s.size() == 2);
    const Member* a = member(s, 60);
    const Member* b = member(s, 64);
    CHECK(a && a->pitch == 60 << 9 && a->dialect == Dialect::Midi1 && a->velocity == uw::ump::scaleUp(100, 7, 16));
    CHECK(b && b->pitch == (64 << 9) + 171 && b->dialect == Dialect::Midi2 && b->velocity == 0x4000);
    CHECK(a && b && a->arrival < b->arrival);

    // A set change at the same sample as a pick is applied first.
    s.input(100, off1(60));
    s.settle(100);
    CHECK(s.size() == 1 && member(s, 64));

    // MIDI 1.0 velocity 0 is a note-off; MIDI 2.0 velocity 0 is a note-on.
    s.input(200, on1(64, 0));
    s.input(200, on2(67, 0));
    s.settle(200);
    CHECK(s.size() == 1 && member(s, 67) && member(s, 67)->velocity == 0);   // 64 was MIDI 2.0: still ended

    // Not a note: passes through, set unchanged.
    CHECK(s.input(300, m1(0xE, 0, 0, 0x50)).kind == Heard::Kind::Other);   // channel pitch bend
    CHECK(s.input(300, m1(0xB, 0, 7, 100)).kind == Heard::Kind::Other);    // CC 7
    CHECK(s.input(300, msg(0x10F80000u)).kind == Heard::Kind::Other);      // clock
    CHECK(s.size() == 1);

    // A note-off for a note not in the set is still a set note, and changes nothing.
    CHECK(s.input(300, off1(50)).kind == Heard::Kind::SetNote && s.size() == 1);

    // The plugin layer names the CLAP dialect.
    s.input(400, on2(70, 0x8000), Dialect::Clap);
    CHECK(member(s, 70) && member(s, 70)->dialect == Dialect::Clap);
}

void testHoldOver() {
    NoteSet s;
    s.setHoldOver(960);   // 20 ms at 48 kHz
    s.input(0, on1(60, 100));
    s.settle(0);
    const std::uint64_t arrival = member(s, 60)->arrival;

    s.input(1000, off1(60));
    s.settle(1959);
    CHECK(member(s, 60));                    // released, but still in for the hold-over
    s.input(1959, on1(60, 50));              // struck again: no change, new velocity
    s.settle(5000);
    CHECK(s.size() == 1 && member(s, 60)->arrival == arrival);
    CHECK(member(s, 60)->velocity == uw::ump::scaleUp(50, 7, 16));

    // Struck again exactly at the end of the hold-over still counts...
    s.input(6000, off1(60));
    s.input(6960, on1(60, 100));
    s.settle(6960);
    CHECK(s.size() == 1 && member(s, 60)->arrival == arrival);

    // ...one sample later it's a new note; a pick at the end sees it gone.
    s.input(8000, off1(60));
    s.settle(8959);
    CHECK(s.size() == 1);
    s.settle(8960);
    CHECK(s.empty());
    s.input(9000, off1(61));
    s.input(9100, on1(60, 100));
    CHECK(s.size() == 1 && member(s, 60)->arrival != arrival);

    // Without a settle between: the input itself drops what ended before it.
    s.input(10000, off1(60));
    s.input(10961, on1(60, 100));
    s.settle(10961);
    CHECK(s.size() == 1 && member(s, 60)->arrival > arrival);
}

// A one-slot sequence on Repeat: the chord's offs and ons at the same sample every pass, or a few
// samples apart. The set never empties.
void testRepeatedStrikes() {
    for (std::int64_t gap : {0, 5}) {
        NoteSet s;
        s.setHoldOver(gap == 0 ? 0 : 960);
        const std::uint8_t chord[] = {60, 64, 67};
        for (std::uint8_t k : chord) s.input(0, on2(k, 0x8000));
        s.settle(0);
        std::uint64_t arrivals[3];
        for (int i = 0; i < 3; ++i) arrivals[i] = member(s, chord[i])->arrival;

        for (std::int64_t pass = 1; pass <= 10; ++pass) {
            const std::int64_t t = pass * 24000;
            for (std::uint8_t k : chord) s.input(t, off2(k));
            if (gap > 0) {
                s.settle(t);
                CHECK(s.size() == 3);
            }
            for (std::uint8_t k : chord) s.input(t + gap, on2(k, 0x8000));
            s.settle(t + gap);
            CHECK(s.size() == 3);
            for (int i = 0; i < 3; ++i) CHECK(member(s, chord[i]) && member(s, chord[i])->arrival == arrivals[i]);
        }
    }
}

void testPitchMoves() {
    NoteSet s;
    const std::uint16_t p = (60 << 9) + 100;
    s.input(0, on2(60, 0x8000, p));
    s.input(0, on2(64, 0x8000));
    auto pitch = [&s] { return member(s, 60)->pitch; };
    CHECK(pitch() == p);

    // Per-note bend, over the default ±2.
    s.input(10, m2(0x6, 0, 60, 0, 0xFFFFFFFFu));
    CHECK(pitch() == p + 1024);
    s.input(10, m2(0x6, 0, 60, 0, 0));
    CHECK(pitch() == p - 1024);
    s.input(10, m2(0x6, 0, 60, 0, 0xC0000000u));
    CHECK(pitch() == p + 512);
    CHECK(member(s, 64)->pitch == 64 << 9);  // other notes don't move

    // The channel's range (registered controller 0), then the per-note one (7), which wins.
    s.input(20, m2(0x2, 0, 0, 0, 12u << 25));
    CHECK(pitch() == p + 6 * 512);
    s.input(20, m2(0x2, 0, 0, 7, 48u << 25 | 50u << 18));   // 48.5 semitones
    CHECK(pitch() == p + 24 * 512 + 128);
    s.input(20, m2(0x2, 1, 0, 0, 1u << 25));                // another channel: no change
    CHECK(pitch() == p + 24 * 512 + 128);

    // Pitch 7.25 replaces the note-on's pitch; the bend still adds to it.
    s.input(30, m2(0x0, 0, 60, 3, 62u << 25 | 1u << 24));   // 62.5
    CHECK(pitch() == 62 * 512 + 256 + 24 * 512 + 128);
    s.input(30, m2(0x6, 0, 60, 0, 0x80000000u));
    CHECK(pitch() == 62 * 512 + 256);
    s.input(30, m2(0x0, 0, 60, 4, 0));                      // another per-note controller: no change
    CHECK(pitch() == 62 * 512 + 256);

    // Per-Note Management: Detach alone changes nothing; Reset clears 7.25 and the bend.
    s.input(40, m2(0x6, 0, 60, 0, 0xFFFFFFFFu));
    s.input(40, m2(0xF, 0, 60, 0x02, 0));
    CHECK(pitch() == 62 * 512 + 256 + 48 * 512 + 256);
    s.input(40, m2(0xF, 0, 60, 0x01, 0));
    CHECK(pitch() == p);
}

void testChannelSplit() {
    NoteSet s;
    s.setChannels(NoteSet::kAll, 9);
    Heard h = s.input(0, on1(36, 127, 9));
    CHECK(h.kind == Heard::Kind::Trigger && h.strike && h.channel == 9 && h.velocity == 0xFFFF);
    h = s.input(10, off1(36, 9));
    CHECK(h.kind == Heard::Kind::Trigger && !h.strike);
    CHECK(s.input(0, on1(60, 100, 3)).kind == Heard::Kind::SetNote);
    s.settle(10);
    CHECK(s.size() == 1 && member(s, 60, 3));

    // One set channel: the others pass through.
    s.setChannels(2, NoteSet::kNone);
    CHECK(s.input(20, on1(62, 100, 0)).kind == Heard::Kind::Other);
    CHECK(s.input(20, on1(64, 100, 9)).kind == Heard::Kind::Other);
    CHECK(s.input(20, on1(65, 100, 2)).kind == Heard::Kind::SetNote);
    s.settle(20);
    CHECK(s.size() == 2 && member(s, 65, 2) && !member(s, 62));

    // A channel that's both is the trigger channel.
    s.setChannels(5, 5);
    CHECK(s.input(30, on1(40, 100, 5)).kind == Heard::Kind::Trigger);
}

void testLatch() {
    NoteSet s;
    s.setLatch(true);
    s.input(0, on1(60));
    s.input(0, on1(64));
    s.input(100, off1(60));
    s.input(100, off1(64));
    s.settle(100);
    CHECK(s.size() == 2);                    // let go: the set stays

    s.input(200, on1(67));                   // nothing held: a new set
    s.settle(200);
    CHECK(s.size() == 1 && member(s, 67));

    // The same key as a latched one, with nothing held, starts a new set too.
    s.input(300, off1(67));
    s.settle(300);
    const std::uint64_t old = member(s, 67)->arrival;
    s.input(400, on1(67));
    s.settle(400);
    CHECK(s.size() == 1 && member(s, 67)->arrival != old);

    // While a note is held, notes add, and a latched one struck again keeps its place.
    s.input(500, on1(70));
    s.input(600, off1(67));
    s.settle(600);
    const std::uint64_t a67 = member(s, 67)->arrival;
    s.input(700, on1(67));
    s.settle(700);
    CHECK(s.size() == 2 && member(s, 67)->arrival == a67);

    // Turning latch off drops the latched members.
    s.input(800, off1(70));
    s.settle(800);
    CHECK(s.size() == 2);
    s.setLatch(false);
    CHECK(s.size() == 1 && member(s, 67));

    // Latch with hold-over: a released note latches once its hold-over is up.
    NoteSet h;
    h.setLatch(true);
    h.setHoldOver(50);
    h.input(0, on1(60));
    h.input(10, off1(60));
    h.settle(100);
    h.input(100, on1(62));                   // past the hold-over, nothing held: a new set
    h.settle(100);
    CHECK(h.size() == 1 && member(h, 62));
}

void testSetReset() {
    NoteSet s;
    s.input(0, on1(60));
    s.input(0, on2(64, 0x8000, 0, 4, 2));
    CHECK(s.input(10, m1(0xB, 3, 123, 0)).kind == Heard::Kind::Reset);   // All Notes Off, any channel
    CHECK(s.empty());
    s.input(20, on1(60));
    CHECK(s.input(30, m2(0xB, 0, 120, 0, 0)).kind == Heard::Kind::Reset); // All Sound Off, MIDI 2.0
    CHECK(s.empty());
}

void testSetFull() {
    NoteSet s;
    for (std::size_t i = 0; i < NoteSet::kCapacity; ++i)
        s.input(0, on2(static_cast<std::uint8_t>(i % 128), 0x8000, 0, static_cast<std::uint8_t>(i / 128)));
    CHECK(s.size() == NoteSet::kCapacity);
    CHECK(s.input(0, on2(0, 0x8000, 0, 15)).kind == Heard::Kind::SetNote);   // classed, not kept
    CHECK(s.size() == NoteSet::kCapacity);
    s.input(1, off2(0, 0));
    s.settle(1);
    s.input(2, on2(0, 0x8000, 0, 15));
    CHECK(s.size() == NoteSet::kCapacity && member(s, 0, 15));
}

} // namespace

int main() {
    testStartAndEnd();
    testEndsInOrder();
    testShortestLength();
    testHeld();
    testSameKey();
    testVoices();
    testNoRoom();
    testEndAll();
    testFull();
    testNothingSticks();
    testSetBasics();
    testHoldOver();
    testRepeatedStrikes();
    testPitchMoves();
    testChannelSplit();
    testLatch();
    testSetReset();
    testSetFull();
    if (failures != 0) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
