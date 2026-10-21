// NoteGran core tests (PLAN.md, phase 1). Silent unless a check fails.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "core/OwnedNotes.h"

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
    if (failures != 0) std::fprintf(stderr, "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
