// NoteGran CLAP tests: drives the built NoteGran.clap through the CLAP C API, with no DAW: dlopen,
// clap_entry, the factory, then events in over many blocks and the output checked. CoreTests
// covers the rules; this covers the CLAP side: descriptors, ports, parameters (events mid-block,
// flush, text), the three dialects in and out, pass-through and Dry, transport, reset, sleep and
// state. As Retune's.
//
// Usage: notegran-clap-tests /path/to/NoteGran.clap. Silent unless a check fails.

#include <clap/clap.h>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(expr)                                                                       \
    do {                                                                                  \
        if (!(expr)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #expr); \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

constexpr const char* kCloudId  = "com.unlikelyware.notegran.cloud";
constexpr const char* kChordsId = "com.unlikelyware.notegran.chords";
constexpr const char* kGridId   = "com.unlikelyware.notegran.grid";
constexpr const char* kBurstId  = "com.unlikelyware.notegran.burst";
constexpr const char* kBufferId = "com.unlikelyware.notegran.buffer";

// Parameter ids (docs/design.md; plugin.cpp's ParamId).
enum : clap_id {
    pSync, pDensity, pRate, pLengthMs, pLengthBeats, pOnsetRandom, pPick, pWeight, pOctaves,
    pTranspose, pScatter, pQuantize, pEdo, pRoot, pLevel, pVelSpread, pLenSpread, pVoices,
    pWhenFull, pDry, pHoldOver, pSetChannel, pTriggerChannel, pLatch, pSeed, pCount
};
enum : clap_id {
    cScale, cKey, cMap, cEdo, cOrigin, cInput, cScaleChannel, cSize, cStack, cInversion, cVoicing, cOctave,
    cOffScale, cLevel, cVoices, cWhenFull, cCount
};

// --- host ------------------------------------------------------------------------------------

int  gRescans = 0;
void hostRescan(const clap_host_t*, clap_param_rescan_flags) { ++gRescans; }
void hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hostRequestFlush(const clap_host_t*) {}
const clap_host_params_t kHostParams = {hostRescan, hostClear, hostRequestFlush};

const void* hostGetExtension(const clap_host_t*, const char* id) {
    return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &kHostParams : nullptr;
}
void hostNoop(const clap_host_t*) {}

const clap_host_t kHost = {
    .clap_version     = CLAP_VERSION_INIT,
    .host_data        = nullptr,
    .name             = "notegran-clap-tests",
    .vendor           = "Unlikelyware",
    .url              = "",
    .version          = "0",
    .get_extension    = hostGetExtension,
    .request_restart  = hostNoop,
    .request_process  = hostNoop,
    .request_callback = hostNoop,
};

// --- events ----------------------------------------------------------------------------------

// Input events for one block, in order, as owned copies.
struct Input {
    std::vector<std::vector<uint8_t>> storage;

    void add(const clap_event_header_t& h) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&h);
        storage.emplace_back(bytes, bytes + h.size);
    }
    void midi1(uint32_t time, uint8_t s, uint8_t d1, uint8_t d2) {
        clap_event_midi_t ev{};
        ev.header  = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        ev.data[0] = s;
        ev.data[1] = d1;
        ev.data[2] = d2;
        add(ev.header);
    }
    void midi2(uint32_t time, uint32_t w0, uint32_t w1 = 0) {
        clap_event_midi2_t ev{};
        ev.header  = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI2, 0};
        ev.data[0] = w0;
        ev.data[1] = w1;
        add(ev.header);
    }
    void note(uint16_t type, uint32_t time, int16_t channel, int16_t key, double velocity = 0.8) {
        clap_event_note_t ev{};
        ev.header   = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
        ev.note_id  = -1;
        ev.channel  = channel;
        ev.key      = key;
        ev.velocity = velocity;
        add(ev.header);
    }
    void tuning(uint32_t time, int16_t channel, int16_t key, double semitones) {
        clap_event_note_expression_t ev{};
        ev.header        = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
        ev.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
        ev.note_id       = -1;
        ev.channel       = channel;
        ev.key           = key;
        ev.value         = semitones;
        add(ev.header);
    }
    void param(uint32_t time, clap_id id, double value) {
        clap_event_param_value_t ev{};
        ev.header   = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        ev.param_id = id;
        ev.note_id  = -1;
        ev.port_index = -1;
        ev.channel  = -1;
        ev.key      = -1;
        ev.value    = value;
        add(ev.header);
    }

    static uint32_t size(const clap_input_events_t* list) {
        return uint32_t(static_cast<const Input*>(list->ctx)->storage.size());
    }
    static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t i) {
        return reinterpret_cast<const clap_event_header_t*>(static_cast<const Input*>(list->ctx)->storage[i].data());
    }
};

// An output event, with its time made absolute.
struct Event {
    int64_t  time;
    uint16_t type;
    uint32_t words[4];
    uint8_t  bytes[3];
    int16_t  channel, key;
    int32_t  expressionId;
    double   value;   // CLAP: velocity or expression value
};

struct Output {
    std::vector<Event> events;
    int64_t            base = 0;

    static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* h) {
        auto* self = static_cast<Output*>(list->ctx);
        Event e{self->base + h->time, h->type, {}, {}, -1, -1, -1, 0};
        if (h->type == CLAP_EVENT_MIDI2) {
            std::memcpy(e.words, reinterpret_cast<const clap_event_midi2_t*>(h)->data, sizeof(e.words));
        } else if (h->type == CLAP_EVENT_MIDI) {
            std::memcpy(e.bytes, reinterpret_cast<const clap_event_midi_t*>(h)->data, 3);
        } else if (h->type == CLAP_EVENT_NOTE_EXPRESSION) {
            const auto* x  = reinterpret_cast<const clap_event_note_expression_t*>(h);
            e.channel      = x->channel;
            e.key          = x->key;
            e.expressionId = x->expression_id;
            e.value        = x->value;
        } else if (h->type <= CLAP_EVENT_NOTE_END) {
            const auto* n = reinterpret_cast<const clap_event_note_t*>(h);
            e.channel     = n->channel;
            e.key         = n->key;
            e.value       = n->velocity;
        }
        self->events.push_back(e);
        return true;
    }
};

bool sameEvents(const std::vector<Event>& a, const std::vector<Event>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Event& x = a[i];
        const Event& y = b[i];
        if (x.time != y.time || x.type != y.type || std::memcmp(x.words, y.words, sizeof(x.words)) != 0 ||
            std::memcmp(x.bytes, y.bytes, 3) != 0 || x.channel != y.channel || x.key != y.key || x.value != y.value)
            return false;
    }
    return true;
}

bool isMidi1NoteOn(const Event& e) { return e.type == CLAP_EVENT_MIDI && (e.bytes[0] & 0xF0) == 0x90 && e.bytes[2] != 0; }
bool isMidi2NoteOn(const Event& e) { return e.type == CLAP_EVENT_MIDI2 && (e.words[0] >> 20) == 0x409; }

// --- the plugin under test -------------------------------------------------------------------

const clap_plugin_entry_t*   gEntry   = nullptr;
const clap_plugin_factory_t* gFactory = nullptr;

struct Instance {
    const clap_plugin_t*            plugin    = nullptr;
    const clap_plugin_params_t*     params    = nullptr;
    const clap_plugin_state_t*      state     = nullptr;
    const clap_plugin_note_ports_t* notePorts = nullptr;
    int64_t                         now       = 0;   // the host's sample count
    clap_process_status             lastStatus = CLAP_PROCESS_CONTINUE;

    explicit Instance(const char* id = kCloudId) {
        plugin = gFactory->create_plugin(gFactory, &kHost, id);
        if (plugin == nullptr || !plugin->init(plugin)) return;
        params    = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state     = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        notePorts = static_cast<const clap_plugin_note_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_NOTE_PORTS));
        plugin->activate(plugin, 48000.0, 1, 4096);
        plugin->start_processing(plugin);
    }
    ~Instance() {
        if (plugin == nullptr) return;
        plugin->stop_processing(plugin);
        plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }
    bool ok() const { return plugin != nullptr && params != nullptr && state != nullptr && notePorts != nullptr; }

    // One block of `frames`, from the host's current sample.
    std::vector<Event> process(Input& in, uint32_t frames, const clap_event_transport_t* transport = nullptr) {
        Output out;
        out.base                           = now;
        const clap_input_events_t  inList  = {&in, Input::size, Input::get};
        const clap_output_events_t outList = {&out, Output::tryPush};
        clap_process_t             p{};
        p.steady_time  = now;
        p.frames_count = frames;
        p.transport    = transport;
        p.in_events    = &inList;
        p.out_events   = &outList;
        lastStatus     = plugin->process(plugin, &p);
        now += frames;
        for (size_t i = 1; i < out.events.size(); ++i) CHECK(out.events[i].time >= out.events[i - 1].time);
        return out.events;
    }

    double value(clap_id id) {
        double v = -1000;
        params->get_value(plugin, id, &v);
        return v;
    }
    void set(clap_id id, double v) {
        Input in;
        in.param(0, id, v);
        const clap_input_events_t  inList  = {&in, Input::size, Input::get};
        Output                     out;
        const clap_output_events_t outList = {&out, Output::tryPush};
        params->flush(plugin, &inList, &outList);
    }
};

bool openLibrary(const std::string& path) {
    std::string       binary = path;
    const std::string name   = path.substr(path.find_last_of('/') + 1);
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".clap") == 0)
        binary = path + "/Contents/MacOS/" + name.substr(0, name.size() - 5);
    void* handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) return false;
    gEntry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
    if (gEntry == nullptr || !gEntry->init(path.c_str())) return false;
    gFactory = static_cast<const clap_plugin_factory_t*>(gEntry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    return gFactory != nullptr;
}

// An arpeggiator: Beats (120 BPM, no transport), sixteenths (6000 samples), on the grid, Up,
// eighth-of-a-beat notes (3000 samples), a fixed seed.
void makeArp(Instance& r) {
    r.set(pSync, 1);
    r.set(pRate, 5);
    r.set(pOnsetRandom, 0);
    r.set(pPick, 1);
    r.set(pLengthBeats, 0.125);
    r.set(pSeed, 7);
}

// Runs blocks of `frames` until `until`, the first block with `first`'s events; returns everything.
std::vector<Event> run(Instance& r, Input& first, int64_t until, uint32_t frames) {
    std::vector<Event> all = r.process(first, frames);
    Input              none;
    while (r.now < until) {
        auto ev = r.process(none, frames);
        all.insert(all.end(), ev.begin(), ev.end());
    }
    return all;
}

// --- tests -----------------------------------------------------------------------------------

void testDescriptor() {
    CHECK(gFactory->get_plugin_count(gFactory) == 5);
    const clap_plugin_descriptor_t* u = gFactory->get_plugin_descriptor(gFactory, 4);
    CHECK(u != nullptr && std::strcmp(u->id, kBufferId) == 0 && std::strcmp(u->name, "Buffer") == 0);
    const clap_plugin_descriptor_t* b = gFactory->get_plugin_descriptor(gFactory, 3);
    CHECK(b != nullptr && std::strcmp(b->id, kBurstId) == 0 && std::strcmp(b->name, "Burst") == 0);
    const clap_plugin_descriptor_t* g = gFactory->get_plugin_descriptor(gFactory, 2);
    CHECK(g != nullptr && std::strcmp(g->id, kGridId) == 0 && std::strcmp(g->name, "Grid") == 0);
    const clap_plugin_descriptor_t* c = gFactory->get_plugin_descriptor(gFactory, 1);
    CHECK(c != nullptr && std::strcmp(c->id, kChordsId) == 0 && std::strcmp(c->name, "Chords") == 0);
    const clap_plugin_descriptor_t* d = gFactory->get_plugin_descriptor(gFactory, 0);
    CHECK(d != nullptr && std::strcmp(d->id, kCloudId) == 0 && std::strcmp(d->name, "Cloud") == 0);
    bool noteEffect = false;
    for (const char* const* f = d->features; *f != nullptr; ++f)
        noteEffect |= std::strcmp(*f, CLAP_PLUGIN_FEATURE_NOTE_EFFECT) == 0;
    CHECK(noteEffect);
    CHECK(gFactory->create_plugin(gFactory, &kHost, "com.unlikelyware.nothing") == nullptr);
}

void testPorts() {
    Instance r;
    CHECK(r.ok());
    if (!r.ok()) return;
    for (bool input : {true, false}) {
        CHECK(r.notePorts->count(r.plugin, input) == 1);
        clap_note_port_info_t info{};
        CHECK(r.notePorts->get(r.plugin, 0, input, &info));
        CHECK(info.supported_dialects == (CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI2));
        CHECK(info.preferred_dialect == CLAP_NOTE_DIALECT_MIDI2);
        CHECK(info.id == 0);
    }
    const auto* audio =
        static_cast<const clap_plugin_audio_ports_t*>(r.plugin->get_extension(r.plugin, CLAP_EXT_AUDIO_PORTS));
    CHECK(audio != nullptr && audio->count(r.plugin, true) == 0 && audio->count(r.plugin, false) == 0);
}

void testParams() {
    Instance r;
    if (!r.ok()) return;
    CHECK(r.params->count(r.plugin) == pCount);
    clap_param_info_t info{};
    CHECK(r.params->get_info(r.plugin, pVoices, &info));
    CHECK(std::strcmp(info.name, "Voices") == 0 && info.min_value == 1 && info.max_value == 32 &&
          info.default_value == 8 && (info.flags & CLAP_PARAM_IS_STEPPED));
    CHECK(r.params->get_info(r.plugin, pWhenFull, &info) && (info.flags & CLAP_PARAM_IS_ENUM) && info.default_value == 0);
    CHECK(r.params->get_info(r.plugin, pRoot, &info) && info.default_value == 9);
    CHECK(!r.params->get_info(r.plugin, pCount, &info));
    CHECK(r.value(pVoices) == 8 && r.value(pSeed) == 0);

    char text[64];
    CHECK(r.params->value_to_text(r.plugin, pPick, 3, text, sizeof(text)) && std::strcmp(text, "Up-down") == 0);
    CHECK(r.params->value_to_text(r.plugin, pRoot, 9, text, sizeof(text)) && std::strcmp(text, "A") == 0);
    CHECK(r.params->value_to_text(r.plugin, pRate, 0, text, sizeof(text)) && std::strcmp(text, "1/4") == 0);
    CHECK(r.params->value_to_text(r.plugin, pSetChannel, 0, text, sizeof(text)) && std::strcmp(text, "All") == 0);
    CHECK(r.params->value_to_text(r.plugin, pTriggerChannel, 10, text, sizeof(text)) && std::strcmp(text, "10") == 0);
    CHECK(r.params->value_to_text(r.plugin, pSeed, 0, text, sizeof(text)) && std::strcmp(text, "New each start") == 0);
    CHECK(r.params->value_to_text(r.plugin, pLengthMs, 250, text, sizeof(text)) && std::strcmp(text, "250 ms") == 0);
    double v = -1;
    CHECK(r.params->text_to_value(r.plugin, pPick, "Played", &v) && v == 4);
    CHECK(r.params->text_to_value(r.plugin, pTriggerChannel, "None", &v) && v == 0);
    CHECK(r.params->text_to_value(r.plugin, pVoices, "99", &v) && v == 32);
    CHECK(!r.params->text_to_value(r.plugin, pVoices, "lots", &v));

    r.set(pVoices, 3.4);
    CHECK(r.value(pVoices) == 3);   // stepped: rounded
    r.set(pDensity, 1000);
    CHECK(r.value(pDensity) == 200);
}

void testMidi1Arpeggio() {
    Instance r;
    if (!r.ok()) return;
    makeArp(r);
    Input in;
    in.midi1(0, 0x90, 64, 100);
    in.midi1(0, 0x90, 60, 100);
    in.midi1(0, 0x90, 67, 100);
    in.midi1(10, 0xB0, 7, 90);   // a CC: passes through, in its place
    const auto out = run(r, in, 23552, 512);   // 46 blocks: slots 0 .. 18000
    std::vector<Event> ons;
    bool               cc = false;
    for (const Event& e : out) {
        if (isMidi1NoteOn(e)) ons.push_back(e);
        if (e.type == CLAP_EVENT_MIDI && e.bytes[0] == 0xB0) cc = e.time == 10 && e.bytes[1] == 7 && e.bytes[2] == 90;
    }
    CHECK(cc);
    CHECK(ons.size() == 4);
    const int keys[] = {60, 64, 67, 60};
    for (size_t i = 0; i < ons.size() && i < 4; ++i) {
        CHECK(ons[i].time == int64_t(i) * 6000 && ons[i].bytes[1] == keys[i] && ons[i].bytes[2] == 100);
    }
    // The set's own notes don't pass (Dry is off), and every grain ends 3000 samples on.
    int offs = 0;
    for (const Event& e : out) {
        if (e.type == CLAP_EVENT_MIDI && (e.bytes[0] & 0xF0) == 0x80) {
            CHECK(e.time % 6000 == 3000);
            ++offs;
        }
    }
    CHECK(offs == 4);
}

void testMidi2AndBlockSizes() {
    // MIDI 2.0 in: a quarter-tone note. Grains carry the exact pitch in a Pitch 7.9 attribute.
    // A busy cloud gives the same events for any block size, with a fixed seed.
    std::vector<Event> results[3];
    const uint32_t     sizes[] = {512, 64, 1777};
    for (int i = 0; i < 3; ++i) {
        Instance r;
        if (!r.ok()) return;
        r.set(pSeed, 99);
        r.set(pDensity, 40);
        r.set(pOnsetRandom, 0.7);
        r.set(pScatter, 2);
        r.set(pOctaves, 1);
        r.set(pLengthMs, 60);
        r.set(pLenSpread, 0.5);
        r.set(pVelSpread, 0.3);
        Input in;
        in.midi2(0, 0x40903C03u, 0x80000000u | (60 * 512 + 256));   // 60.5
        in.midi2(0, 0x40904000u, 0x60000000u);                      // 64
        results[i] = run(r, in, 96000, sizes[i]);
        // The runs end at different samples: compare up to a common one.
        results[i].erase(std::remove_if(results[i].begin(), results[i].end(),
                                        [](const Event& e) { return e.time >= 90000; }),
                         results[i].end());
    }
    CHECK(sameEvents(results[0], results[1]));
    CHECK(sameEvents(results[0], results[2]));
    int ons = 0, withAttribute = 0;
    for (const Event& e : results[0]) {
        if (!isMidi2NoteOn(e)) continue;
        ++ons;
        if ((e.words[0] & 0xFF) == 3) {
            ++withAttribute;
            // The key is the nearest to the pitch (or, with that one sounding, the next nearest).
            const int key = int((e.words[0] >> 8) & 0x7F), pitch = int(e.words[1] & 0xFFFF);
            CHECK(std::abs(pitch - key * 512) <= 3 * 512);
        }
    }
    CHECK(ons > 50 && withAttribute > 10);
}

void testClapNotes() {
    Instance r;
    if (!r.ok()) return;
    makeArp(r);
    r.set(pTranspose, 0.25);
    Input in;
    in.note(CLAP_EVENT_NOTE_ON, 0, 2, 60, 0.5);
    const auto out = run(r, in, 6000, 512);
    // A CLAP note on, then its tuning: the half semitone. On the member's channel.
    CHECK(out.size() >= 3);
    if (out.size() < 3) return;
    CHECK(out[0].type == CLAP_EVENT_NOTE_ON && out[0].time == 0 && out[0].channel == 2 && out[0].key == 60);
    CHECK(std::fabs(out[0].value - 0.5) < 1e-4);
    CHECK(out[1].type == CLAP_EVENT_NOTE_EXPRESSION && out[1].expressionId == CLAP_NOTE_EXPRESSION_TUNING &&
          out[1].key == 60 && std::fabs(out[1].value - 0.25) < 1e-9);
    CHECK(out[2].type == CLAP_EVENT_NOTE_OFF && out[2].time == 3000 && out[2].key == 60);

    // A tuning expression on the held note moves the set: grains follow it.
    Instance s;
    makeArp(s);
    Input in2;
    in2.note(CLAP_EVENT_NOTE_ON, 0, 0, 60);
    in2.tuning(100, 0, 60, 2.25);   // the note is now 62.25
    const auto out2 = run(s, in2, 12000, 512);
    bool moved = false;
    for (const Event& e : out2)
        if (e.type == CLAP_EVENT_NOTE_ON && e.time == 6000) moved = e.key == 62;
    for (const Event& e : out2)
        if (e.type == CLAP_EVENT_NOTE_EXPRESSION && e.time == 6000) moved = moved && std::fabs(e.value - 0.25) < 1e-9;
    CHECK(moved);
}

void testDryAndTriggers() {
    Instance r;
    if (!r.ok()) return;
    makeArp(r);
    r.set(pDry, 1);
    Input in;
    in.midi1(5, 0x90, 60, 100);
    const auto out = run(r, in, 6000, 512);
    // The set's note passes (at its time), and the grain at 6000 is the only other note.
    CHECK(!out.empty() && out[0].type == CLAP_EVENT_MIDI && out[0].time == 5 && out[0].bytes[1] == 60);

    // A trigger channel: each note-on there plays one grain, at its time; trigger notes don't pass.
    Instance t;
    makeArp(t);
    t.set(pTriggerChannel, 10);   // channel 10 is 9
    Input in2;
    in2.midi1(0, 0x90, 67, 100);
    in2.midi1(300, 0x99, 36, 127);
    in2.midi1(400, 0x89, 36, 0);
    const auto out2 = run(t, in2, 48000, 512);
    int ons = 0;
    for (const Event& e : out2) {
        CHECK(!(e.type == CLAP_EVENT_MIDI && (e.bytes[0] & 0x0F) == 9));
        if (isMidi1NoteOn(e)) {
            ++ons;
            CHECK(e.time == 300 && e.bytes[1] == 67);
        }
    }
    CHECK(ons == 1);
}

void testParamMidBlock() {
    // Transpose changes at sample 6000, inside a block of 8192: the grain there has it already.
    Instance r;
    if (!r.ok()) return;
    makeArp(r);
    Input in;
    in.midi1(0, 0x90, 60, 100);
    in.param(6000, pTranspose, 12);
    const auto out = r.process(in, 8192);
    std::vector<int> keys;
    for (const Event& e : out)
        if (isMidi1NoteOn(e)) keys.push_back(e.bytes[1]);
    CHECK(keys == std::vector<int>({60, 72}));
    CHECK(r.value(pTranspose) == 12);
}

void testTransport() {
    // Following a host's timeline: beat 1.5 at the block's start; slots fall on the sixteenths.
    Instance r;
    if (!r.ok()) return;
    makeArp(r);
    clap_event_transport_t t{};
    t.header         = {sizeof(t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    t.flags          = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    t.tempo          = 120.0;
    t.song_pos_beats = (int64_t(3) << 31) / 2 + (int64_t(1) << 31) / 8;   // 1.625 beats: 3000 samples past a slot
    Input in;
    in.midi1(0, 0x90, 60, 100);
    const auto out = r.process(in, 8192, &t);
    std::vector<int64_t> times;
    for (const Event& e : out)  // (one block of 8192: slots at 3000 and 9000 would need 9001)
        if (isMidi1NoteOn(e)) times.push_back(e.time);
    CHECK(times == std::vector<int64_t>({3000}));

    // Stopping ends what's sounding at the next block.
    Input none;
    t.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
    const auto stop = r.process(none, 512, &t);
    CHECK(stop.empty());   // the grain at 3000 ended at 6000, before the stop
}

void testResetAndSleep() {
    Instance r;
    if (!r.ok()) return;
    makeArp(r);
    r.set(pLengthBeats, 4);   // long notes
    Input in;
    in.midi1(0, 0x90, 60, 100);
    auto out = r.process(in, 512);
    CHECK(out.size() == 1 && isMidi1NoteOn(out[0]));
    CHECK(r.lastStatus == CLAP_PROCESS_CONTINUE);
    r.plugin->reset(r.plugin);
    Input none;
    out = r.process(none, 512);   // the note ends at once; the set is gone
    CHECK(out.size() == 1 && out[0].type == CLAP_EVENT_MIDI && (out[0].bytes[0] & 0xF0) == 0x80 &&
          out[0].time == 512);
    CHECK(r.lastStatus == CLAP_PROCESS_SLEEP);
    out = r.process(none, 48000);
    CHECK(out.empty());
}

struct Sink {
    std::string text;
    static int64_t write(const clap_ostream_t* s, const void* buffer, uint64_t size) {
        static_cast<Sink*>(s->ctx)->text.append(static_cast<const char*>(buffer), size);
        return int64_t(size);
    }
};
struct Source {
    std::string text;
    size_t      pos = 0;
    static int64_t read(const clap_istream_t* s, void* buffer, uint64_t size) {
        auto*        self = static_cast<Source*>(s->ctx);
        const size_t n    = std::min<size_t>(size, self->text.size() - self->pos);
        std::memcpy(buffer, self->text.data() + self->pos, n);
        self->pos += n;
        return int64_t(n);
    }
};

void testState() {
    Instance a;
    if (!a.ok()) return;
    a.set(pVoices, 5);
    a.set(pTranspose, -3.25);
    a.set(pPick, 2);
    Sink                 sink;
    const clap_ostream_t os = {&sink, Sink::write};
    CHECK(a.state->save(a.plugin, &os));
    CHECK(sink.text.rfind("NoteGran Cloud 1\n", 0) == 0);

    Instance b;
    gRescans = 0;
    Source               source{sink.text};
    const clap_istream_t is = {&source, Source::read};
    CHECK(b.state->load(b.plugin, &is));
    CHECK(gRescans == 1);
    CHECK(b.value(pVoices) == 5 && b.value(pTranspose) == -3.25 && b.value(pPick) == 2);

    // Unknown lines are ignored, missing ones keep their value; another header is refused.
    Source               older{"NoteGran Cloud 1\nvoices 12\nsomething 4\n"};
    const clap_istream_t olderStream = {&older, Source::read};
    CHECK(b.state->load(b.plugin, &olderStream));
    CHECK(b.value(pVoices) == 12 && b.value(pPick) == 2);
    Source               bad{"Retune 1\nsteps 3\n"};
    const clap_istream_t badStream = {&bad, Source::read};
    CHECK(!b.state->load(b.plugin, &badStream));
}

void testChords() {
    Instance r(kChordsId);
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK(r.params->count(r.plugin) == cCount);
    char text[64];
    CHECK(r.params->value_to_text(r.plugin, cKey, 7, text, sizeof(text)) && std::strcmp(text, "C") == 0);
    CHECK(r.params->value_to_text(r.plugin, cScale, 10, text, sizeof(text)) && std::strcmp(text, "Lydian dominant") == 0);
    CHECK(r.params->value_to_text(r.plugin, cInversion, 1, text, sizeof(text)) && std::strcmp(text, "1st") == 0);
    CHECK(r.params->value_to_text(r.plugin, cScaleChannel, 0, text, sizeof(text)) && std::strcmp(text, "None") == 0);
    CHECK(r.params->value_to_text(r.plugin, cOrigin, 9, text, sizeof(text)) && std::strcmp(text, "A") == 0);
    double v = -1;
    CHECK(r.params->text_to_value(r.plugin, cScale, "Lydian dominant", &v) && v == 10);
    clap_param_info_t info{};
    CHECK(r.params->get_info(r.plugin, cMap, &info) && info.default_value == 1);   // Nearest

    // Defaults: C major in 12-EDO, triads. MIDI 1.0 C gives C E G in MIDI 1.0; C# passes; a CC passes.
    Input in;
    in.midi1(0, 0x90, 60, 100);
    in.midi1(5, 0x90, 61, 90);
    in.midi1(7, 0xB0, 1, 64);
    in.midi1(100, 0x80, 60, 0);
    in.midi1(110, 0x80, 61, 0);
    auto out = r.process(in, 512);
    CHECK(out.size() == 9);
    if (out.size() == 9) {
        CHECK(isMidi1NoteOn(out[0]) && out[0].bytes[1] == 60 && out[0].bytes[2] == 100);
        CHECK(isMidi1NoteOn(out[1]) && out[1].bytes[1] == 64 && isMidi1NoteOn(out[2]) && out[2].bytes[1] == 67);
        CHECK(out[3].time == 5 && out[3].bytes[1] == 61 && out[3].bytes[2] == 90);   // passed, as played
        CHECK(out[4].time == 7 && out[4].bytes[0] == 0xB0);
        for (int i = 5; i < 8; ++i) CHECK(out[i].time == 100 && (out[i].bytes[0] & 0xF0) == 0x80);
        CHECK(out[8].time == 110 && out[8].bytes[1] == 61 && (out[8].bytes[0] & 0xF0) == 0x80);
    }
    CHECK(r.lastStatus == CLAP_PROCESS_SLEEP);

    // C lydian dominant in 19-EDO from C, mapped by fifths: MIDI 1.0 D in, MIDI 2.0 D major out, on
    // 19-EDO steps.
    r.set(cScale, 10);
    r.set(cMap, 0);
    r.set(cEdo, 19);
    r.set(cOrigin, 0);
    Input d;
    d.midi1(0, 0x90, 62, 100);
    out = r.process(d, 512);
    CHECK(out.size() == 3);
    const uint32_t steps[] = {5 * 19 + 3, 5 * 19 + 9, 5 * 19 + 14};
    for (size_t i = 0; i < out.size() && i < 3; ++i) {
        CHECK(isMidi2NoteOn(out[i]));
        const uint32_t pitch = (steps[i] * 6144 * 2 + 19) / (2 * 19);
        CHECK((out[i].words[1] & 0xFFFF) == pitch && (out[i].words[0] & 0xFF) == 3);
    }
    Input dOff;
    dOff.midi1(0, 0x80, 62, 0);
    out = r.process(dOff, 512);
    CHECK(out.size() == 3 && out[0].type == CLAP_EVENT_MIDI2 && (out[0].words[0] >> 20) == 0x408);

    // CLAP notes keep the CLAP dialect: C (the key, on its own pitch) needs no tuning; E and G are
    // 19-EDO's, 21 and 5 cents under 12-EDO's.
    Input c;
    c.note(CLAP_EVENT_NOTE_ON, 0, 0, 60);
    out = r.process(c, 512);
    CHECK(out.size() == 5);
    if (out.size() == 5) {
        CHECK(out[0].type == CLAP_EVENT_NOTE_ON && out[0].key == 60);
        CHECK(out[1].type == CLAP_EVENT_NOTE_ON && out[1].key == 64);
        CHECK(out[2].type == CLAP_EVENT_NOTE_EXPRESSION && out[2].expressionId == CLAP_NOTE_EXPRESSION_TUNING &&
              out[2].key == 64 && std::fabs(out[2].value - (379.0 - 400.0) / 100.0) < 0.005);
        CHECK(out[3].type == CLAP_EVENT_NOTE_ON && out[3].key == 67);
        CHECK(out[4].type == CLAP_EVENT_NOTE_EXPRESSION && std::fabs(out[4].value + 0.053) < 0.005);
    }

    // State: its own header.
    Sink                 sink;
    const clap_ostream_t os = {&sink, Sink::write};
    CHECK(r.state->save(r.plugin, &os));
    CHECK(sink.text.rfind("NoteGran Chords 1\n", 0) == 0 && sink.text.find("edo 19\n") != std::string::npos);
    Instance             cloud;
    Source               source{sink.text};
    const clap_istream_t is = {&source, Source::read};
    CHECK(!cloud.state->load(cloud.plugin, &is));   // a Chords state isn't a Cloud's
}

void testGrid() {
    Instance r(kGridId);
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK(r.params->count(r.plugin) == 45);
    clap_param_info_t info{};
    CHECK(r.params->get_info(r.plugin, 20, &info) && std::strcmp(info.name, "Tier 1 Probability") == 0);
    CHECK(r.params->get_info(r.plugin, 44, &info) && std::strcmp(info.name, "Tier 5 Pick") == 0 && info.default_value == 3);
    char text[64];
    CHECK(r.params->value_to_text(r.plugin, 24, 0, text, sizeof(text)) && std::strcmp(text, "Lowest") == 0);
    CHECK(r.params->value_to_text(r.plugin, 1, 3, text, sizeof(text)) && std::strcmp(text, "7 (2+2+3)") == 0);
    CHECK(r.params->value_to_text(r.plugin, 19, 0, text, sizeof(text)) && std::strcmp(text, "New each start") == 0);

    // Every tier always plays: sixteenths at 120 BPM, each bar's downbeat the lowest note.
    for (clap_id t = 0; t < 5; ++t) r.set(20 + t * 5, 1.0);
    r.set(12, 32);
    clap_event_transport_t tr{};
    tr.header = {sizeof(tr), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    tr.flags  = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    tr.tempo  = 120.0;
    Input in;
    in.midi1(0, 0x90, 64, 100);
    in.midi1(0, 0x90, 60, 100);
    std::vector<Event> all;
    for (int b = 0; b < 375; ++b) {   // 2 bars of 96000, in blocks of 512
        tr.song_pos_beats = (int64_t(b) * 512 << 31) / 24000;
        Input none;
        auto out = r.process(b == 0 ? in : none, 512, &tr);
        all.insert(all.end(), out.begin(), out.end());
    }
    std::vector<Event> ons;
    for (const Event& e : all)
        if (isMidi1NoteOn(e)) ons.push_back(e);
    CHECK(ons.size() == 32);
    CHECK(ons.size() == 32 && ons[0].time == 0 && ons[0].bytes[1] == 60 && ons[16].bytes[1] == 60);   // (times: below, ±1 for the host's rounding)
    for (size_t i = 0; i < ons.size(); ++i) CHECK(std::llabs(ons[i].time - int64_t(i) * 6000) <= 1);

    Sink                 sink;
    const clap_ostream_t os = {&sink, Sink::write};
    CHECK(r.state->save(r.plugin, &os));
    CHECK(sink.text.rfind("NoteGran Grid 1\n", 0) == 0 && sink.text.find("t3_pick 2\n") != std::string::npos);
}

void testBurst() {
    Instance r(kBurstId);
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK(r.params->count(r.plugin) == 34);
    clap_param_info_t info{};
    CHECK(r.params->get_info(r.plugin, 11, &info) && std::strcmp(info.name, "T1 Rotation") == 0 && info.default_value == 25);
    char text[64];
    CHECK(r.params->value_to_text(r.plugin, 5, 2, text, sizeof(text)) && std::strcmp(text, "Loop") == 0);
    CHECK(r.params->value_to_text(r.plugin, 31, 0, text, sizeof(text)) && std::strcmp(text, "None") == 0);

    // A MIDI 1.0 note-on: up to 8 grains (two on one key at once are one) over the 800 ms phrase
    // (38400 samples), from the note-on, in MIDI 1.0; released at once, it plays out (Finish).
    // Then it sleeps.
    r.set(26, 32);   // Voices
    r.set(33, 1);    // Seed
    r.set(16, 1);    // Length, ms
    Input in;
    in.midi1(100, 0x90, 60, 100);
    in.midi1(200, 0x80, 60, 0);
    std::vector<Event> all;
    for (int b = 0; b < 100; ++b) {   // 51200 samples
        Input none;
        auto  out = r.process(b == 0 ? in : none, 512);
        all.insert(all.end(), out.begin(), out.end());
    }
    std::vector<Event> ons;
    for (const Event& e : all)
        if (isMidi1NoteOn(e)) ons.push_back(e);
    CHECK(ons.size() >= 6 && ons.size() <= 8 && ons[0].time == 100);
    for (const Event& e : ons) CHECK(e.time >= 100 && e.time < 100 + 38400);
    CHECK(r.lastStatus == CLAP_PROCESS_SLEEP);

    // Loop: released in the second pass, so two passes, the second the first again.
    r.set(5, 2);
    Input held;
    held.midi1(0, 0x90, 62, 100);
    ons.clear();
    for (int b = 0; b < 200; ++b) {   // 102400 samples
        Input none;
        if (b == 140) none.midi1(0, 0x80, 62, 0);   // at 71680: the pass from 38400 finishes
        auto out = r.process(b == 0 ? held : none, 512);
        for (const Event& e : out)
            if (isMidi1NoteOn(e)) ons.push_back(e);
    }
    const size_t pass = ons.size() / 2;
    CHECK(pass >= 6 && ons.size() == 2 * pass && ons.back().time < 51200 + 2 * 38400);
    for (size_t i = pass; i < ons.size(); ++i) CHECK(ons[i].time == ons[i - pass].time + 38400);

    Sink                 sink;
    const clap_ostream_t os = {&sink, Sink::write};
    CHECK(r.state->save(r.plugin, &os));
    CHECK(sink.text.rfind("NoteGran Burst 1\n", 0) == 0 && sink.text.find("release 2\n") != std::string::npos);
}

void testBuffer() {
    Instance r(kBufferId);
    CHECK(r.ok());
    if (!r.ok()) return;
    CHECK(r.params->count(r.plugin) == 32);
    clap_param_info_t info{};
    CHECK(r.params->get_info(r.plugin, 26, &info) && std::strcmp(info.name, "Dry") == 0 && info.default_value == 1);
    char text[64];
    CHECK(r.params->value_to_text(r.plugin, 10, 1, text, sizeof(text)) && std::strcmp(text, "Fragment") == 0);
    CHECK(r.params->value_to_text(r.plugin, 29, 0, text, sizeof(text)) && std::strcmp(text, "None") == 0);

    // A MIDI 1.0 note, captured and passed through (Dry): then grains of it every 4800 samples
    // (10 a second, on the grid), at the newest end of the window, in MIDI 1.0. Once the note is
    // older than the 4 s window and the grains have ended, it sleeps.
    r.set(31, 1);    // Seed
    r.set(24, 32);   // Voices
    r.set(4, 1);     // Position: newest
    r.set(6, 0);     // Spray
    r.set(7, 10);    // Density
    r.set(9, 0);     // Onset random
    Input in;
    in.midi1(100, 0x90, 60, 100);
    in.midi1(200, 0x80, 60, 0);
    std::vector<Event> ons;
    for (int b = 0; b < 420; ++b) {   // 215040 samples
        Input none;
        for (const Event& e : r.process(b == 0 ? in : none, 512))
            if (isMidi1NoteOn(e)) ons.push_back(e);
    }
    CHECK(ons.size() == 41 && ons[0].time == 100);
    for (std::size_t i = 1; i < ons.size(); ++i) CHECK(ons[i].time == std::int64_t(i) * 4800 && ons[i].bytes[1] == 60);
    CHECK(r.lastStatus == CLAP_PROCESS_SLEEP);

    Sink                 sink;
    const clap_ostream_t os = {&sink, Sink::write};
    CHECK(r.state->save(r.plugin, &os));
    CHECK(sink.text.rfind("NoteGran Buffer 1\n", 0) == 0 && sink.text.find("position 1\n") != std::string::npos);
}

void testEditor() {
    // Each plugin has its editor (embedded only), at its own design size and shape.
    struct Design {
        const char* id;
        uint32_t    width, height;
    };
    for (const Design& d : {Design{kCloudId, 900, 560}, Design{kGridId, 980, 700}, Design{kBurstId, 960, 660},
                            Design{kChordsId, 920, 600}, Design{kBufferId, 940, 600}}) {
        Instance r(d.id);
        CHECK(r.ok());
        if (!r.ok()) continue;
        const auto* gui = static_cast<const clap_plugin_gui_t*>(r.plugin->get_extension(r.plugin, CLAP_EXT_GUI));
        CHECK(gui != nullptr && r.plugin->get_extension(r.plugin, CLAP_EXT_TIMER_SUPPORT) != nullptr);
        if (gui == nullptr) continue;
#ifdef __APPLE__
        const char* api = CLAP_WINDOW_API_COCOA;
#else
        const char* api = CLAP_WINDOW_API_X11;
#endif
        const char* preferred = nullptr;
        bool        floating  = true;
        CHECK(gui->get_preferred_api(r.plugin, &preferred, &floating) && std::strcmp(preferred, api) == 0 && !floating);
        CHECK(!gui->is_api_supported(r.plugin, api, true));
        // Made and sized with no window yet (the host's parent comes later), then destroyed.
        CHECK(gui->create(r.plugin, api, false));
        uint32_t w = 0, h = 0;
        CHECK(gui->get_size(r.plugin, &w, &h) && w == d.width && h == d.height);
        clap_gui_resize_hints_t hints{};
        CHECK(gui->get_resize_hints(r.plugin, &hints) && hints.preserve_aspect_ratio &&
              hints.aspect_ratio_width == d.width && hints.aspect_ratio_height == d.height);
        uint32_t aw = 1000, ah = 10;
        const auto expected = static_cast<uint32_t>(std::lround(1000.0 * d.height / d.width));
        CHECK(gui->adjust_size(r.plugin, &aw, &ah) && aw == 1000 && ah == expected);
        aw = 100;   // never below 0.8× the design
        CHECK(gui->adjust_size(r.plugin, &aw, &ah) && aw == d.width * 4 / 5);
        CHECK(gui->set_size(r.plugin, 1000, expected) && gui->get_size(r.plugin, &w, &h) && w == 1000 && h == expected);
        gui->destroy(r.plugin);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: notegran-clap-tests /path/to/NoteGran.clap\n");
        return 2;
    }
    if (!openLibrary(argv[1])) {
        std::fprintf(stderr, "can't load %s\n", argv[1]);
        return 1;
    }
    testDescriptor();
    testPorts();
    testParams();
    testMidi1Arpeggio();
    testMidi2AndBlockSizes();
    testClapNotes();
    testDryAndTriggers();
    testParamMidBlock();
    testTransport();
    testResetAndSleep();
    testState();
    testChords();
    testGrid();
    testBurst();
    testBuffer();
    testEditor();
    gEntry->deinit();
    if (failures != 0) {
        std::fprintf(stderr, "NoteGran CLAP: %d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
