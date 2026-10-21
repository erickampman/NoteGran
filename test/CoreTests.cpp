// NoteGran core tests (PLAN.md, phase 1). Silent unless a check fails.

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "core/Clock.h"
#include "core/Cloud.h"
#include "core/NoteSet.h"
#include "core/OwnedNotes.h"
#include "core/Pitch.h"
#include "core/Random.h"
#include "core/Shape.h"
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
    t.setWhenFull(WhenFull::Steal);
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
    t.setWhenFull(WhenFull::Steal);
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
    u.setWhenFull(WhenFull::Steal);
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

void testSkip() {
    OwnedNotes t;
    CHECK(t.whenFull() == WhenFull::Skip && t.voices() == 8);   // the plugins' defaults
    t.setVoices(2);
    Out o(8);
    t.start(note(0, 1000, 60), o.out);
    t.start(note(1, 1000, 62), o.out);
    o.out.clear();
    CHECK(t.start(note(2, 1000, 64), o.out) == 0);  // full: skipped, nothing sent, both whole
    CHECK(o.out.size() == 0 && t.size() == 2 && t.sounding(0, 0, 60) && t.sounding(0, 0, 62));

    // A sounding key struck again is skipped too, even under the limit: nothing is cut short.
    t.setVoices(4);
    CHECK(t.start(note(3, 1000, 62), o.out) == 0);
    CHECK(o.out.size() == 0 && t.size() == 2);
    t.setVoices(2);

    // A note that ends at the new one's time makes room for it.
    OwnedNotes u;
    u.setVoices(1);
    u.start(note(0, 10, 60), o.out);
    o.out.clear();
    CHECK(u.start(note(10, 20, 62), o.out) != 0);
    CHECK(o.out.size() == 2 && isOff(o.out[0], 10, 60) && isOn(o.out[1], 10, 62));

    // Lowering Voices: nothing ends, new notes wait until enough have.
    OwnedNotes w;
    w.setVoices(4);
    for (std::uint8_t k = 0; k < 4; ++k) w.start(note(0, 100 + k, static_cast<std::uint8_t>(60 + k)), o.out);
    w.setVoices(2);
    o.out.clear();
    CHECK(w.start(note(50, 500, 70), o.out) == 0 && o.out.size() == 0);
    CHECK(w.start(note(101, 500, 70), o.out) == 0);  // 60 and 61 ended: still 2 sounding
    CHECK(o.out.size() == 2 && w.size() == 2);
    o.out.clear();
    CHECK(w.start(note(102, 500, 70), o.out) != 0);  // 62 ended too: room
    CHECK(o.out.size() == 2 && isOff(o.out[0], 102, 62) && isOn(o.out[1], 102, 70));
}

void testNoRoom() {
    OwnedNotes t;
    t.setWhenFull(WhenFull::Steal);
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
    t.setWhenFull(WhenFull::Steal);
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
            t.setWhenFull(rnd(2) == 0 ? WhenFull::Skip : WhenFull::Steal);
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


// --- the clock ----------------------------------------------------------------------------

using Slots = std::vector<std::pair<std::int64_t, std::int64_t>>;   // (n, sample)

// Block sizes from 1 to 2048, the same each run.
struct Sizes {
    std::uint32_t lcg = 777;
    std::int64_t  next() {
        lcg = lcg * 1664525u + 1013904223u;
        return 1 + (lcg >> 8) % 2048;
    }
};

// Runs [0, length) in blocks (one block if `oneBlock`), with `transport` from a function of the
// block's start sample (null for none), collecting every slot of `grid`. Each run gets its own copy
// of the clock: an absolute sample count never goes back.
template <class T>
Slots runClock(Clock clock, const Grid& grid, std::int64_t length, bool oneBlock, T transportAt) {
    Slots  out;
    Sizes  sizes;
    for (std::int64_t start = 0; start < length;) {
        const std::int64_t frames = oneBlock ? length : std::min(sizes.next(), length - start);
        Transport          t;
        const Transport*   tp = transportAt(start, t);
        clock.begin(start, frames, tp);
        clock.slots(grid, [&](std::int64_t n, std::int64_t sample) {
            CHECK(sample >= start && sample < start + frames);
            out.emplace_back(n, sample);
        });
        start += frames;
    }
    return out;
}

const Transport* noTransport(std::int64_t, Transport&) { return nullptr; }

void testClockFree() {
    Clock c;
    c.setSampleRate(48000.0);
    const Grid g10 = Grid::perSecond(10.0, 48000);
    const Slots one = runClock(c, g10, 48000, true, noTransport);
    CHECK(one.size() == 10);
    for (std::size_t i = 0; i < one.size(); ++i)
        CHECK(one[i].first == static_cast<std::int64_t>(i) && one[i].second == static_cast<std::int64_t>(i) * 4800);
    CHECK(runClock(c, g10, 48000, false, noTransport) == one);   // any block sizes, the same

    const Grid g7 = Grid::perSecond(7.0, 48000);
    const Slots seven = runClock(c, g7, 480000, true, noTransport);
    CHECK(seven.size() == 70);
    CHECK(seven[1].second == 6858);              // ceil(48000 / 7)
    CHECK(seven[69].second == 69 * 48000 / 7 + 1);
    CHECK(runClock(c, g7, 480000, false, noTransport) == seven);

    // A fractional rate, any block sizes.
    const Grid odd = Grid::perSecond(13.37, 48000);
    CHECK(runClock(c, odd, 200000, false, noTransport) == runClock(c, odd, 200000, true, noTransport));

    CHECK(runClock(c, Grid::perSecond(0.0, 48000), 48000, false, noTransport).empty());

    // Free mode reports no transport changes.
    Transport playing;
    playing.hasBeats = playing.playing = true;
    c.begin(0, 64, &playing);
    CHECK(!c.begin(64, 64, nullptr).stopped);
}

void testClockOwnCount() {
    Clock c;
    c.setSampleRate(48000.0);
    c.setMode(Clock::Mode::Beats);
    // No transport: 120 BPM, a beat every 24000 samples from sample 0.
    const Slots beats = runClock(c, Grid::perBeat(1, 1), 96001, true, noTransport);
    CHECK(beats.size() == 5);
    for (std::size_t i = 0; i < beats.size(); ++i) CHECK(beats[i].second == static_cast<std::int64_t>(i) * 24000);

    for (auto [num, den, step] : {std::tuple{3, 1, 8000}, std::tuple{1, 4, 96000}, std::tuple{8, 1, 3000}}) {
        const Grid  g    = Grid::perBeat(num, den);
        const Slots one  = runClock(c, g, 960000, true, noTransport);
        const Slots many = runClock(c, g, 960000, false, noTransport);
        CHECK(one == many);
        CHECK(!one.empty());
        for (std::size_t i = 0; i < one.size(); ++i) CHECK(one[i].second == static_cast<std::int64_t>(i) * step);
    }

    // A stopped transport with a tempo: its own count, at that tempo.
    auto stopped90 = [](std::int64_t, Transport& t) -> const Transport* {
        t.hasTempo = true;
        t.tempo    = 90.0;
        t.hasBeats = true;
        t.playing  = false;
        t.beats    = 123 * kBeat;   // ignored while stopped
        return &t;
    };
    const Slots s90 = runClock(c, Grid::perBeat(1, 1), 96000, false, stopped90);
    CHECK(s90.size() == 3 && s90[1].second == 32000 && s90[2].second == 64000);

    // A tempo change carries on from the beat it's on.
    Clock d;
    d.setSampleRate(48000.0);
    d.setMode(Clock::Mode::Beats);
    Slots got;
    Transport t;
    t.hasTempo = true;
    for (std::int64_t start = 0; start < 180000; start += 1000) {
        t.tempo = start < 60000 ? 120.0 : 60.0;   // 2.5 beats at 120, then 60
        d.begin(start, 1000, &t);
        d.slots(Grid::perBeat(1, 1), [&](std::int64_t n, std::int64_t sample) { got.emplace_back(n, sample); });
    }
    CHECK(got.size() == 5);
    CHECK(got[2].second == 48000 && got[3].first == 3 && got[3].second == 60000 + 24000);
    CHECK(got[4].second == 60000 + 72000);
}

// The host's timeline, at 120 BPM: its beat position rounded (and, with `wobble`, off by a little)
// at each block's start, as a host's double positions are.
const Transport* host(std::int64_t start, Transport& t, std::int64_t wobble) {
    t.hasTempo = t.hasBeats = t.playing = true;
    t.tempo = 120.0;
    t.beats = std::llround(static_cast<double>(start) / 24000.0 * static_cast<double>(kBeat));
    if (wobble != 0 && start > 0) t.beats += (start / 7) % 2 == 0 ? wobble : -wobble;
    return &t;
}

void testClockHost() {
    Clock c;
    c.setSampleRate(48000.0);
    c.setMode(Clock::Mode::Beats);
    const Grid g = Grid::perBeat(4, 1);

    for (std::int64_t wobble : {0, 3000, 300000}) {
        const Slots got = runClock(c, g, 480000, false,
                                   [wobble](std::int64_t s, Transport& t) { return host(s, t, wobble); });
        // Every slot once, in order, within a sample or two of where it belongs, or of where the
        // host says it is (300000 beat units is 3.4 samples, which can bring slot 80 into the run).
        const std::int64_t slack = 2 + wobble * 24000 / kBeat;
        CHECK(got.size() == 80 || (wobble > 0 && got.size() == 81));
        for (std::size_t i = 0; i < got.size(); ++i) {
            CHECK(got[i].first == static_cast<std::int64_t>(i));
            const std::int64_t off = got[i].second - static_cast<std::int64_t>(i) * 6000;
            CHECK(off >= -slack && off <= slack);
        }
    }

    // Pre-roll: the host starts half a beat before 0.
    Clock d;
    d.setSampleRate(48000.0);
    d.setMode(Clock::Mode::Beats);
    Transport t;
    t.hasTempo = t.hasBeats = t.playing = true;
    t.beats = -kBeat / 2;
    d.begin(1000, 24000, &t);
    Slots pre;
    d.slots(Grid::perBeat(1, 1), [&](std::int64_t n, std::int64_t sample) { pre.emplace_back(n, sample); });
    CHECK(pre.size() == 1 && pre[0].first == 0 && pre[0].second == 13000);

    // A loop back to beat 0 is a jump, and its slots play again; stopping is reported once.
    Clock e;
    e.setSampleRate(48000.0);
    e.setMode(Clock::Mode::Beats);
    Transport u;
    CHECK(!e.begin(0, 24000, host(0, u, 0)).jumped);
    CHECK(!e.begin(24000, 24000, host(24000, u, 0)).jumped);
    Clock::Change ch = e.begin(48000, 24000, host(0, u, 0));
    CHECK(ch.jumped && !ch.stopped);
    Slots again;
    e.slots(Grid::perBeat(1, 1), [&](std::int64_t n, std::int64_t sample) { again.emplace_back(n, sample); });
    CHECK(again.size() == 1 && again[0].first == 0 && again[0].second == 48000);
    u.playing = false;
    ch = e.begin(72000, 24000, &u);
    CHECK(ch.stopped && !ch.jumped);
    CHECK(!e.begin(96000, 24000, &u).stopped);
}

void testClockLengths() {
    Clock c;
    c.setSampleRate(48000.0);
    CHECK(c.samplesForMs(20.0) == 960);
    CHECK(c.samplesForMs(0.0) == 1);
    c.begin(0, 64, nullptr);
    CHECK(c.samplesForBeats(kBeat) == 24000);
    CHECK(c.samplesForBeats(kBeat / 3) == 8000);
    CHECK(c.samplesForBeats(0) == 1);

    Clock d;
    d.setSampleRate(44100.0);
    Transport t;
    t.hasTempo = true;
    t.tempo    = 140.0;
    d.begin(0, 64, &t);
    CHECK(d.samplesForBeats(kBeat) == 18900);   // 44100 * 60 / 140
    CHECK(d.tempo() == 140.0);
}


// --- random draws -------------------------------------------------------------------------

void testRandomPinned() {
    // These values are part of what a fixed seed sounds like. If one changes, so does every saved
    // song's output: version it (PLAN.md, "Integration"), don't just update the numbers.
    const Random r(1234);
    CHECK(r.bits(Stream::Timing, 0) == 0x5caceb7b9e631c50ull);
    CHECK(r.bits(Stream::Pick, 0) == 0xf09d263a1ae25cd8ull);
    CHECK(r.bits(Stream::Timing, 1) == 0x4cb30a9c2572de08ull);
    CHECK(r.bits(Stream::Timing, 0, 1) == 0xfcc281fd6bbcd27eull);
    const Random z(0);
    CHECK(z.bits(Stream::Pitch, -5, 3) == 0x472a5d44be438723ull);
    CHECK(z.below(12, Stream::Pick, 1000) == 9);
}

void testRandomIndependent() {
    // The same arguments give the same draw, in any order; any argument changed gives another.
    const Random a(7), b(7), c(8);
    CHECK(a.bits(Stream::Pick, 99, 2) == b.bits(Stream::Pick, 99, 2));
    CHECK(a.bits(Stream::Pick, 99, 2) != c.bits(Stream::Pick, 99, 2));
    std::vector<std::uint64_t> seen;
    for (std::int64_t i = -500; i < 500; ++i)
        for (std::uint32_t s = 1; s <= 7; ++s)
            for (std::uint32_t d = 0; d < 4; ++d) seen.push_back(a.bits(static_cast<Stream>(s), i, d));
    std::sort(seen.begin(), seen.end());
    CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());   // 28000 draws, no repeats

    // Streams are uncorrelated: a pick stream's coin doesn't follow the timing stream's.
    int agree = 0;
    for (std::int64_t i = 0; i < 100000; ++i)
        agree += (a.bits(Stream::Timing, i) >> 63) == (a.bits(Stream::Pick, i) >> 63);
    CHECK(agree > 49000 && agree < 51000);
}

void testRandomDistributions() {
    const Random r(42);
    constexpr int kN = 200000;

    // below: in range, every value about equally often.
    int counts[12] = {};
    for (int i = 0; i < kN; ++i) {
        const std::uint32_t v = r.below(12, Stream::Pick, i);
        CHECK(v < 12);
        if (v < 12) ++counts[v];
    }
    for (int c : counts) CHECK(c > kN / 12 * 95 / 100 && c < kN / 12 * 105 / 100);
    CHECK(r.below(0, Stream::Pick, 1) == 0);
    CHECK(r.below(1, Stream::Pick, 1) == 0);

    // unit: in [0, 1), mean a half.
    std::int64_t sum = 0;
    for (int i = 0; i < kN; ++i) {
        const std::int64_t u = r.unit(Stream::Velocity, i);
        CHECK(u >= 0 && u < kOne32);
        sum += u >> 16;
    }
    const double mean = static_cast<double>(sum) / kN / 65536.0;
    CHECK(mean > 0.49 && mean < 0.51);

    // triangular: in [-1, 1), mean 0, and half of it within ±0.29 (1 - 1/sqrt 2).
    std::int64_t tsum = 0;
    int          near = 0;
    for (int i = 0; i < kN; ++i) {
        const std::int64_t t = r.triangular(Stream::Pitch, i);
        CHECK(t >= -kOne32 && t < kOne32);
        tsum += t >> 16;
        if (t > -kOne32 * 29 / 100 && t < kOne32 * 29 / 100) ++near;
    }
    const double tmean = static_cast<double>(tsum) / kN / 65536.0;
    CHECK(tmean > -0.01 && tmean < 0.01);
    CHECK(near > kN * 48 / 100 && near < kN * 52 / 100);

    // chance: never, always, and about right between.
    int hits = 0;
    for (int i = 0; i < kN; ++i) {
        CHECK(!r.chance(0, Stream::Chance, i));
        CHECK(r.chance(kOne32, Stream::Chance, i));
        hits += r.chance(kOne32 / 4, Stream::Chance, i);
    }
    CHECK(hits > kN / 4 * 97 / 100 && hits < kN / 4 * 103 / 100);

    // poisson1: mean 1, P(0) = P(1) = e^-1, P(2) = e^-1 / 2.
    int          byCount[14] = {};
    std::int64_t total       = 0;
    for (int i = 0; i < kN; ++i) {
        const std::uint32_t k = r.poisson1(Stream::Timing, i);
        CHECK(k <= 12);
        if (k <= 12) ++byCount[k];
        total += k;
    }
    const double pmean = static_cast<double>(total) / kN;
    CHECK(pmean > 0.98 && pmean < 1.02);
    const double e1 = std::exp(-1.0) * kN;
    CHECK(byCount[0] > e1 * 0.97 && byCount[0] < e1 * 1.03);
    CHECK(byCount[1] > e1 * 0.97 && byCount[1] < e1 * 1.03);
    CHECK(byCount[2] > e1 / 2 * 0.95 && byCount[2] < e1 / 2 * 1.05);
}


// --- pitch --------------------------------------------------------------------------------

constexpr std::int32_t st(double semitones) { return static_cast<std::int32_t>(semitones * 512); }

void testPitchParams() {
    CHECK(PitchParams::fromSemitones(1.5) == 768);
    CHECK(PitchParams::fromSemitones(-0.25) == -128);
    CHECK(PitchParams::fromSemitones(500.0) == 127 * 512);
    CHECK(PitchParams::fromSemitones(std::nan("")) == 0);
}

void testGrainPitch() {
    const Random r(5);
    NoteSet      empty;
    PitchParams  p;
    CHECK(grainPitch(st(60.25), p, r, 0, empty) == st(60.25));   // nothing set: unchanged
    p.transpose = st(-7);
    CHECK(grainPitch(st(60.25), p, r, 0, empty) == st(53.25));

    // Octaves: every move from -2 to +2 octaves, about equally, and the same for the same index.
    p           = PitchParams{};
    p.octaves   = 2;
    int moves[5] = {};
    for (std::int64_t i = 0; i < 50000; ++i) {
        const std::int32_t d = grainPitch(st(60), p, r, i, empty) - st(60);
        CHECK(d % kOctave == 0 && d >= -2 * kOctave && d <= 2 * kOctave);
        ++moves[d / kOctave + 2];
        CHECK(grainPitch(st(60), p, r, i, empty) == st(60) + d);
    }
    for (int m : moves) CHECK(m > 9500 && m < 10500);

    // Scatter: within ± its width, centred, and on its own draw: adding it doesn't change any
    // grain's octave.
    PitchParams s = p;
    s.scatter     = st(2);
    std::int64_t sum = 0;
    for (std::int64_t i = 0; i < 50000; ++i) {
        const std::int32_t plain = grainPitch(st(60), p, r, i, empty);
        const std::int32_t moved = grainPitch(st(60), s, r, i, empty);
        CHECK(moved - plain >= -st(2) && moved - plain <= st(2));
        sum += moved - plain;
    }
    CHECK(sum / 50000 > -20 && sum / 50000 < 20);

    // Folded into range by octaves, not clamped.
    PitchParams up;
    up.transpose = st(24);
    CHECK(grainPitch(st(120), up, r, 0, empty) == st(144 - 24));
    up.transpose = st(-24);
    CHECK(grainPitch(st(10), up, r, 0, empty) == st(10 - 24 + 24));
    CHECK(foldIntoRange(-100) == kOctave - 100);
    CHECK(foldIntoRange(70000) == 70000 - kOctave);
    CHECK(foldIntoRange(kMaxPitch) == kMaxPitch);
}

void testQuantizeEdo() {
    // 12-EDO: the nearest semitone, wherever the root.
    CHECK(quantizeEdo(st(60) + 200, 12, 9) == st(60));
    CHECK(quantizeEdo(st(60) + 300, 12, 0) == st(61));
    CHECK(quantizeEdo(st(60) + 256, 12, 0) == st(61));      // a half rounds up
    // 24-EDO: quarter tones.
    CHECK(quantizeEdo(st(60) + 200, 24, 9) == st(60) + 256);
    // 31-EDO from A: A is a step, the next is 6144 / 31 = 198.19 above it.
    CHECK(quantizeEdo(st(69), 31, 9) == st(69));
    CHECK(quantizeEdo(st(69) + 100, 31, 9) == st(69) + 198);
    CHECK(quantizeEdo(st(69) + 99, 31, 9) == st(69));
    CHECK(quantizeEdo(st(69) - 100, 31, 9) == st(69) - 198);
    CHECK(quantizeEdo(st(69) + kOctave + 5, 31, 9) == st(81));
    // From C instead, A4 isn't a step of 31-EDO.
    CHECK(quantizeEdo(st(69), 31, 0) == 35278);
    // 1-EDO: octaves of the root. Out of range is clamped to 1 .. 96.
    CHECK(quantizeEdo(st(64), 1, 9) == st(69));
    CHECK(quantizeEdo(st(62.75), 1, 9) == st(57));
    CHECK(quantizeEdo(st(63), 1, 9) == st(69));                 // halfway: up
    CHECK(quantizeEdo(st(64), 0, 9) == st(69));
    CHECK(quantizeEdo(st(60) + 40, 500, 0) == st(60) + 64); // 96-EDO: steps of 64
    CHECK(quantizeEdo(st(60) + 30, 500, 0) == st(60));
    // Below MIDI note 0 too.
    CHECK(quantizeEdo(-300, 12, 0) == -512);
}

void testQuantizeSet() {
    NoteSet set;
    CHECK(quantizeToSet(st(61.3), set) == st(61.3));        // empty: unchanged
    set.input(0, on2(60, 0x8000));                           // C
    set.input(0, on2(64, 0x8000, st(64.5)));                 // a quarter-sharp E
    CHECK(quantizeToSet(st(61), set) == st(60));
    CHECK(quantizeToSet(st(63.5), set) == st(64.5));
    CHECK(quantizeToSet(st(74), set) == st(72));             // any octave
    CHECK(quantizeToSet(st(40), set) == st(40.5));
    CHECK(quantizeToSet(st(66.5), set) == st(64.5));          // 2 to E-ish, 5.5 to C: E-ish
    NoteSet two;
    two.input(0, on1(60));
    two.input(0, on1(64));
    CHECK(quantizeToSet(st(62), two) == st(60));             // a tie goes down
    CHECK(quantizeToSet(st(66), two) == st(64));             // 2 to E, 6 to C
    NoteSet one;
    one.input(0, on1(60));
    CHECK(quantizeToSet(st(66), one) == st(60));             // a tritone either way: down

    // Scatter with Quantize Set stays in the set's pitch classes.
    const Random r(11);
    PitchParams  p;
    p.octaves  = 1;
    p.scatter  = st(7);
    p.quantize = Quantize::Set;
    for (std::int64_t i = 0; i < 5000; ++i) {
        const std::int32_t g  = grainPitch(st(60), p, r, i, set);
        const std::int32_t pc = g % kOctave;
        CHECK(pc == 0 || pc == st(4.5));
    }
}

void testChooseKey() {
    auto none = [](std::uint8_t) { return false; };
    KeyChoice c = chooseKey(st(60) + 300, Dialect::Midi1, none);
    CHECK(c.key == 61 && c.pitch79 == 61 << 9);
    c = chooseKey(st(60) + 300, Dialect::Midi2, none);
    CHECK(c.key == 61 && c.pitch79 == st(60) + 300);
    c = chooseKey(st(60) + 300, Dialect::Clap, none);
    CHECK(c.key == 61 && c.pitch79 == st(60) + 300);

    // The nearest free key, the closer side first.
    OwnedNotes table;
    table.setVoices(32);
    Out o(8);
    table.start(note(0, kHeld, 61), o.out);
    auto sounding = [&table](std::uint8_t k) { return table.sounding(0, 0, k); };
    c = chooseKey(st(60) + 300, Dialect::Midi2, sounding);   // 61 sounding: 60 is 300 away, 62 is 724
    CHECK(c.key == 60 && c.pitch79 == st(60) + 300);
    c = chooseKey(st(61) + 300, Dialect::Midi2, sounding);   // nearest is 62, free
    CHECK(c.key == 62);
    c = chooseKey(st(60) + 256, Dialect::Midi2, sounding);   // rounds to 61; 60 and 62 tie: below
    CHECK(c.key == 60);
    table.start(note(0, kHeld, 60), o.out);
    c = chooseKey(st(60) + 300, Dialect::Midi2, sounding);
    CHECK(c.key == 62);
    // MIDI 1.0 strikes the nearest again, sounding or not (the same-key rule ends the old one).
    CHECK(chooseKey(st(60) + 300, Dialect::Midi1, sounding).key == 61);
    // Another channel's keys don't count.
    auto other = [&table](std::uint8_t k) { return table.sounding(0, 1, k); };
    CHECK(chooseKey(st(60) + 300, Dialect::Midi2, other).key == 61);

    // Every key sounding: the nearest. The top and bottom of the range.
    auto all = [](std::uint8_t) { return true; };
    CHECK(chooseKey(st(60) + 300, Dialect::Midi2, all).key == 61);
    CHECK(chooseKey(kMaxPitch, Dialect::Midi2, none).key == 127);
    CHECK(chooseKey(kMaxPitch, Dialect::Midi1, none).pitch79 == 127 << 9);
    auto top = [](std::uint8_t k) { return k == 127; };
    CHECK(chooseKey(kMaxPitch, Dialect::Midi2, top).key == 126);
    auto bottom = [](std::uint8_t k) { return k == 0; };
    CHECK(chooseKey(0, Dialect::Midi2, bottom).key == 1);
    CHECK(chooseKey(-500, Dialect::Midi2, none).pitch79 == 0);
}


// --- velocity and length ------------------------------------------------------------------

void testShape() {
    const Random r(3);
    CHECK(toQ16(1.0) == kOne16 && toQ16(0.5) == kOne16 / 2 && toQ16(-1.0) == 0 && toQ16(9.0) == 4 * kOne16);
    CHECK(grainVelocity(0x8000, kOne16, 0, 0xFFFF, r, 0) == 0x8000);
    CHECK(grainVelocity(0x8000, 2 * kOne16, 0, 0xFFFF, r, 0) == 0xFFFF);   // clamped
    CHECK(grainVelocity(0x8000, 0, 0, 0xFFFF, r, 0) == 1);                 // never 0
    CHECK(grainVelocity(0x8000, kOne16, 0, 0x8000, r, 0) == 0x4000);       // a trigger's velocity
    std::int64_t sum = 0;
    for (std::int64_t i = 0; i < 20000; ++i) {
        const std::uint16_t v = grainVelocity(0x8000, kOne16, kOne16 / 2, 0xFFFF, r, i);
        CHECK(v >= 0x4000 && v <= 0xC000);
        sum += v;
    }
    CHECK(sum / 20000 > 0x8000 - 300 && sum / 20000 < 0x8000 + 300);
    for (std::int64_t i = 0; i < 20000; ++i) {
        const std::int64_t len = grainLength(1000, kOne16 / 2, r, i);
        CHECK(len >= 500 && len <= 1500);
    }
    CHECK(grainLength(1000, 0, r, 0) == 1000);
    CHECK(grainLength(0, 0, r, 0) == 1);
    CHECK(velocity7(100) == 1 && velocity7(0xFFFF) == 127 && velocity7(0x8000) == 64);
}

// --- Cloud --------------------------------------------------------------------------------

using TransportFn = std::function<const Transport*(std::int64_t, Transport&)>;

struct CloudRun {
    std::vector<NoteEvent> events;
    std::vector<bool>      passed;   // per input
};

CloudInput input(std::int64_t time, const uw::MidiMessage& m, Dialect d = Dialect::Midi2) {
    CloudInput in;
    in.time    = time;
    in.message = m;
    in.dialect = uw::ump::messageType(m) == uw::ump::MessageType::Midi1ChannelVoice ? Dialect::Midi1 : d;
    return in;
}

// Runs a Cloud over [0, length) in blocks of random sizes (or one block), feeding each block its
// inputs, and collects everything it sends.
CloudRun runCloud(const CloudParams& params, std::vector<CloudInput> inputs, std::int64_t length, bool oneBlock,
                  const TransportFn& transport = nullptr, std::uint32_t sizesSeed = 777) {
    auto core = std::make_unique<CloudCore>();
    core->setSampleRate(48000.0);
    core->setParams(params);
    CloudRun               run;
    std::vector<NoteEvent> buffer(8192);
    Sizes                  sizes;
    sizes.lcg      = sizesSeed;
    std::size_t in = 0;
    for (std::int64_t start = 0; start < length;) {
        const std::int64_t frames = oneBlock ? length : std::min(sizes.next(), length - start);
        std::size_t        first  = in;
        while (in < inputs.size() && inputs[in].time < start + frames) ++in;
        NoteOut          out(buffer.data(), buffer.size());
        Transport        t;
        const Transport* tp = transport ? transport(start, t) : nullptr;
        core->process(start, frames, tp, inputs.data() + first, in - first, out);
        for (std::size_t i = 0; i < out.size(); ++i) {
            CHECK(out[i].time >= start && out[i].time < start + frames);
            run.events.push_back(out[i]);
        }
        start += frames;
    }
    for (const CloudInput& i : inputs) run.passed.push_back(i.pass);
    return run;
}

bool sameEvents(const std::vector<NoteEvent>& a, const std::vector<NoteEvent>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const NoteEvent& x = a[i];
        const NoteEvent& y = b[i];
        if (x.time != y.time || x.on != y.on || x.group != y.group || x.channel != y.channel || x.key != y.key ||
            x.dialect != y.dialect || x.pitch79 != y.pitch79 || x.velocity != y.velocity)
            return false;
    }
    return true;
}

std::vector<NoteEvent> ons(const std::vector<NoteEvent>& events) {
    std::vector<NoteEvent> v;
    for (const NoteEvent& e : events)
        if (e.on) v.push_back(e);
    return v;
}

// What a synth sees: no key struck while it sounds, no off for a key that isn't, never more than
// `voices` at once, and (if `ended`) nothing left sounding.
void checkPlayable(const std::vector<NoteEvent>& events, int voices, bool ended) {
    std::vector<bool> on(16 * 16 * 128, false);
    int               sounding = 0;
    std::int64_t      last     = 0;
    for (const NoteEvent& e : events) {
        CHECK(e.time >= last);
        last = e.time;
        const std::size_t k = (static_cast<std::size_t>(e.group) * 16 + e.channel) * 128 + e.key;
        if (e.on) {
            CHECK(!on[k]);
            on[k] = true;
            CHECK(++sounding <= voices);
        } else {
            CHECK(on[k]);
            on[k] = false;
            --sounding;
        }
    }
    if (ended) CHECK(sounding == 0);
}

CloudParams arp(Pick pick) {
    CloudParams p;
    p.sync        = Clock::Mode::Beats;
    p.divisionNum = 4;              // sixteenths at 120 BPM: 6000 samples
    p.lengthBeats = kBeat / 8;      // 3000 samples
    p.pick        = pick;
    return p;
}

void testCloudArpeggio() {
    // Played in E, C, G order.
    const std::vector<CloudInput> chord = {input(0, on1(64)), input(0, on1(60)), input(0, on1(67))};
    struct Case { Pick pick; std::vector<int> keys; };
    const Case cases[] = {
        {Pick::Up, {60, 64, 67, 60, 64, 67, 60, 64}},
        {Pick::Down, {67, 64, 60, 67, 64, 60, 67, 64}},
        {Pick::UpDown, {60, 64, 67, 64, 60, 64, 67, 64}},
        {Pick::Played, {64, 60, 67, 64, 60, 67, 64, 60}},
    };
    for (const Case& c : cases) {
        const CloudRun run = runCloud(arp(c.pick), chord, 48000, false);
        const auto     on  = ons(run.events);
        CHECK(on.size() == 8);
        for (std::size_t i = 0; i < on.size() && i < c.keys.size(); ++i) {
            CHECK(on[i].time == static_cast<std::int64_t>(i) * 6000);
            CHECK(on[i].key == c.keys[i] && on[i].dialect == Dialect::Midi1);
        }
        checkPlayable(run.events, 8, false);
        for (const NoteEvent& e : run.events)
            if (!e.on) CHECK(e.time % 6000 == 3000);
    }

    // The ordered picks keep their place when the set changes: after 64, with 62 added, 67 is next.
    std::vector<CloudInput> changing = chord;
    changing.push_back(input(9000, on1(62)));
    const auto on = ons(runCloud(arp(Pick::Up), changing, 30000, false).events);
    CHECK(on.size() == 5 && on[0].key == 60 && on[1].key == 64 && on[2].key == 67 && on[3].key == 60 &&
          on[4].key == 62);
}

std::vector<CloudInput> busyInput() {
    // A changing set, MIDI 2.0 with fractions, a per-note bend and a release.
    return {input(0, on2(60, 0x9000, st(60.25))),
            input(0, on2(63, 0x5000, st(63.4))),
            input(1000, on2(67, 0xF000)),
            input(30000, m2(0x6, 0, 63, 0, 0xA0000000u)),
            input(48000, off2(60)),
            input(96000, on2(70, 0x7000, st(70.1))),
            input(150000, off2(63)),
            input(200000, off2(67)),
            input(240000, off2(70))};
}

CloudParams busyParams(Clock::Mode sync) {
    CloudParams p;
    p.sync           = sync;
    p.density        = 25.0;
    p.divisionNum    = 6;
    p.lengthMs       = 80.0;
    p.lengthBeats    = kBeat / 5;
    p.onsetRandom    = toQ16(0.6);
    p.pick           = Pick::Random;
    p.weight         = toQ16(0.5);
    p.pitch.octaves  = 1;
    p.pitch.scatter  = st(3);
    p.velSpread      = toQ16(0.3);
    p.lenSpread      = toQ16(0.5);
    p.voices         = 6;
    p.seed           = 4242;
    return p;
}

void testCloudRepeatable() {
    const TransportFn hostAt = [](std::int64_t s, Transport& t) { return host(s, t, 0); };
    for (Clock::Mode sync : {Clock::Mode::Free, Clock::Mode::Beats}) {
        for (bool withHost : {false, true}) {
            if (withHost && sync == Clock::Mode::Free) continue;
            const CloudParams p   = busyParams(sync);
            const TransportFn tf  = withHost ? hostAt : nullptr;
            const CloudRun    one = runCloud(p, busyInput(), 288000, true, tf);
            const CloudRun    a   = runCloud(p, busyInput(), 288000, false, tf, 777);
            const CloudRun    b   = runCloud(p, busyInput(), 288000, false, tf, 31337);
            CHECK(ons(one.events).size() > 50);
            // Any block sizes, the same notes. (With the host's rounded positions, only notes sent
            // from the host's position can differ by a sample; this host is exact at 0 wobble.)
            CHECK(sameEvents(one.events, a.events));
            CHECK(sameEvents(one.events, b.events));
            checkPlayable(one.events, p.voices, true);

            // The same seed, the same notes; another seed, others.
            CloudParams q = p;
            q.seed        = 4243;
            CHECK(sameEvents(runCloud(p, busyInput(), 288000, false, tf).events, one.events));
            CHECK(!sameEvents(runCloud(q, busyInput(), 288000, true, tf).events, one.events));
        }
    }
}

void testCloudOnsets() {
    CloudParams p;
    p.density  = 20.0;              // a slot every 2400 samples
    p.lengthMs = 1.0;
    p.voices   = 32;
    const std::vector<CloudInput> held = {input(0, on1(60))};

    // Onset random 0: exactly on the grid.
    auto on = ons(runCloud(p, held, 48000, false).events);
    CHECK(on.size() == 20);
    for (std::size_t i = 0; i < on.size(); ++i) CHECK(on[i].time == static_cast<std::int64_t>(i) * 2400);

    // 1: dust, at the same mean rate (2000 in 100 s), some slots empty and some with several.
    p.onsetRandom = kOne16;
    on            = ons(runCloud(p, held, 4800000, false).events);
    CHECK(on.size() > 1900 && on.size() < 2100);
    // Halfway: still the same mean rate.
    p.onsetRandom = kOne16 / 2;
    on            = ons(runCloud(p, held, 4800000, false).events);
    CHECK(on.size() > 1900 && on.size() < 2100);
}

void testCloudSetChangeFirst() {
    CloudParams p;
    p.density  = 20.0;
    p.lengthMs = 1.0;
    // A note-on at a slot's sample plays at once; a note-off at one stops it (hold-over 0).
    p.holdOverMs = 0.0;
    auto on = ons(runCloud(p, {input(2400, on1(60)), input(7200, off1(60))}, 12000, false).events);
    CHECK(on.size() == 2 && on[0].time == 2400 && on[1].time == 4800);
    // With hold-over, a sequence's re-strike at the slot doesn't drop a grain.
    p.holdOverMs = 20.0;
    on = ons(runCloud(p, {input(0, on1(60)), input(4800, off1(60)), input(4800, on1(60))}, 12000, false).events);
    CHECK(on.size() == 5);
}

void testCloudTriggers() {
    CloudParams p;
    p.triggerChannel = 9;
    p.lengthMs       = 10.0;
    const CloudRun run = runCloud(p,
                                  {input(0, on2(60, 0x8000)), input(100, on2(36, 0x8000, 0, 9)),
                                   input(200, off2(36, 9)), input(5000, on2(36, 0xFFFF, 0, 9)),
                                   input(6000, m1(0xB, 0, 7, 100))},
                                  48000, false);
    const auto on = ons(run.events);
    CHECK(on.size() == 2);   // one grain per trigger strike, none from Density
    CHECK(on.size() == 2 && on[0].time == 100 && on[1].time == 5000);
    CHECK(on.size() == 2 && on[0].velocity == 0x4000 && on[1].velocity == 0x8000);
    CHECK(run.passed == std::vector<bool>({false, false, false, false, true}));
}

void testCloudDialects() {
    CloudParams p;
    p.density         = 10.0;
    p.lengthMs        = 1.0;
    p.pitch.transpose = st(0.3);
    auto on = ons(runCloud(p, {input(0, on1(60))}, 4800, true).events);
    CHECK(on.size() == 1 && on[0].dialect == Dialect::Midi1 && on[0].key == 60 && on[0].pitch79 == st(60));
    on = ons(runCloud(p, {input(0, on2(60, 0x8000, st(60.25)))}, 4800, true).events);
    CHECK(on.size() == 1 && on[0].dialect == Dialect::Midi2 && on[0].key == 61 && on[0].pitch79 == st(60.55));
    on = ons(runCloud(p, {input(0, on2(60, 0x8000), Dialect::Clap)}, 4800, true).events);
    CHECK(on.size() == 1 && on[0].dialect == Dialect::Clap && on[0].pitch79 == st(60.3));
    // Output goes on the group and channel of its member.
    on = ons(runCloud(p, {input(0, on2(60, 0x8000, 0, 5, 3))}, 4800, true).events);
    CHECK(on.size() == 1 && on[0].group == 3 && on[0].channel == 5);
}

void testCloudEnds() {
    CloudParams p;
    p.density  = 50.0;
    p.lengthMs = 300.0;              // long notes, still sounding when things stop
    p.voices   = 8;
    p.whenFull = WhenFull::Steal;

    // The set released: the notes play out and end.
    CloudRun run = runCloud(p, {input(0, on1(60)), input(0, on1(64)), input(24000, off1(60)), input(24000, off1(64))},
                            96000, false);
    checkPlayable(run.events, 8, true);
    CHECK(run.passed == std::vector<bool>({false, false, false, false}));   // not Dry

    // All Notes Off ends them at once, and passes through.
    run = runCloud(p, {input(0, on1(60)), input(20000, m1(0xB, 0, 123, 0))}, 30000, false);
    checkPlayable(run.events, 8, true);
    CHECK(run.events.back().time == 20000 && run.passed[1]);

    // The transport stopping ends them at the next block, and clears the set.
    const TransportFn stops = [](std::int64_t s, Transport& t) -> const Transport* {
        host(s, t, 0);
        t.playing = s < 24000;
        return &t;
    };
    CloudParams b = p;
    b.sync        = Clock::Mode::Beats;
    run           = runCloud(b, {input(0, on1(60))}, 96000, false, stops);
    checkPlayable(run.events, 8, true);
    for (const NoteEvent& e : run.events) CHECK(!e.on || e.time < 24000 + 2048);

    // Dry: set notes pass through, and a MIDI 2.0 grain avoids their keys.
    CloudParams d = p;
    d.dry         = true;
    d.voices      = 1;
    d.density     = 10.0;
    d.lengthMs    = 1.0;
    run           = runCloud(d, {input(0, on2(60, 0x8000)), input(2000, m1(0xB, 0, 7, 1))}, 4800, true);
    CHECK(run.passed == std::vector<bool>({true, true}));
    const auto on = ons(run.events);
    CHECK(on.size() == 1 && on[0].key != 60 && on[0].pitch79 == st(60));

    // Dry in MIDI 1.0: a grain on the passing note's own key is skipped.
    run = runCloud(d, {input(0, on1(60))}, 48000, false);
    CHECK(run.passed == std::vector<bool>({true}));
    CHECK(ons(run.events).empty());
    d.pitch.transpose = st(7);   // on another key, it plays
    CHECK(ons(runCloud(d, {input(0, on1(60))}, 4800, true).events).size() == 1);

    // Busy while notes sound or a set is held.
    CloudCore core;
    core.setSampleRate(48000.0);
    core.setParams(p);
    CHECK(!core.busy());
}

void testCloudVoices() {
    CloudParams p;
    p.density  = 100.0;
    p.lengthMs = 200.0;              // 20 grains' worth sounding at once, if they could
    p.voices   = 3;
    for (WhenFull w : {WhenFull::Skip, WhenFull::Steal}) {
        p.whenFull       = w;
        const CloudRun r = runCloud(p, {input(0, on1(60)), input(0, on1(64)), input(0, on1(67))}, 96000, false);
        checkPlayable(r.events, 3, false);
        const auto on = ons(r.events);
        // Skip: whole grains, so few; Steal: a grain every slot.
        if (w == WhenFull::Skip) CHECK(on.size() < 40);
        if (w == WhenFull::Steal) CHECK(on.size() >= 199);
    }
}

} // namespace

int main() {
    testStartAndEnd();
    testEndsInOrder();
    testShortestLength();
    testHeld();
    testSameKey();
    testVoices();
    testSkip();
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
    testClockFree();
    testClockOwnCount();
    testClockHost();
    testClockLengths();
    testRandomPinned();
    testRandomIndependent();
    testRandomDistributions();
    testPitchParams();
    testGrainPitch();
    testQuantizeEdo();
    testQuantizeSet();
    testChooseKey();
    testShape();
    testCloudArpeggio();
    testCloudRepeatable();
    testCloudOnsets();
    testCloudSetChangeFirst();
    testCloudTriggers();
    testCloudDialects();
    testCloudEnds();
    testCloudVoices();
    if (failures != 0) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
