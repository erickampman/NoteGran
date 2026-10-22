// src/plugin.cpp — NoteGran: the CLAP plugins around the core. One .clap whose factory lists one
// plugin per job (PLAN.md, "The plugins"): NoteGran Cloud, Chords and Grid. Each has its own
// parameter table, state header and core; the events, dialects, block splitting and state code are
// shared. A note effect: one note
// input and one note output port, CLAP | MIDI | MIDI2, preferring MIDI2 (as Retune). Hand-written
// CLAP entry point, as Retune and the synths.
//
// - Inputs go to the core as UMP: MIDI 2.0 as it is, MIDI 1.0 wrapped as MT 0x2, CLAP notes as
//   MIDI 2.0 notes tagged Dialect::Clap (a tuning expression as Pitch 7.25, so the set follows it).
//   The core marks each input to pass through or not; system MIDI and SysEx always pass.
// - Grains leave in the dialect of the note they came from (PLAN.md, "Ports and dialects").
// - A parameter or transport event splits the block at its sample: the cores give the same notes
//   for any block sizes, so this makes automation sample-accurate for nothing.
// - Time is the plugin's own running sample count, so the core sees an absolute clock.
// - reset() can't send events, so the notes still sounding then (or at deactivate) are ended at
//   the start of the next process().

#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>

#include "core/Chords.h"
#include "core/Cloud.h"
#include "core/Grid.h"
#include "core/Shape.h"
#include "uw/ump/Ump.h"

#ifndef NOTEGRAN_VERSION_STRING
#define NOTEGRAN_VERSION_STRING "0.1.0"
#endif

namespace {

namespace ump = uw::ump;
using namespace notegran;

// ---------------------------------------------------------------------------------------------
// Parameters (appended, never inserted: the ids are the indices)
// ---------------------------------------------------------------------------------------------

enum class Kind : std::uint8_t { Number, Stepped, Menu };

struct ParamSpec {
    const char*        name;
    const char*        symbol;   // state file key
    double             min, max, def;
    Kind               kind;
    const char* const* labels;   // Menu: one per step from min
    const char*        unit;     // Number: shown after the value
};

const char* const kSyncLabels[]     = {"Free", "Beats"};
const char* const kRateLabels[]     = {"1/4", "1/2", "1", "2", "3", "4", "6", "8"};
const char* const kPickLabels[]     = {"Random", "Up", "Down", "Up-down", "Played"};
const char* const kQuantizeLabels[] = {"Off", "EDO", "Set"};
const char* const kRootLabels[]     = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kWhenFullLabels[] = {"Skip", "Steal"};
const char* const kOffOnLabels[]    = {"Off", "On"};

// --- NoteGran Cloud ---

// Rate (Beats): grains per beat, as num / den.
constexpr std::int64_t kRateNum[] = {1, 1, 1, 2, 3, 4, 6, 8};
constexpr std::int64_t kRateDen[] = {4, 2, 1, 1, 1, 1, 1, 1};

enum ParamId : std::uint32_t {
    kSync, kDensity, kRate, kLengthMs, kLengthBeats, kOnsetRandom, kPick, kWeight, kOctaves,
    kTranspose, kScatter, kQuantize, kEdo, kRoot, kLevel, kVelSpread, kLenSpread, kVoices,
    kWhenFull, kDry, kHoldOver, kSetChannel, kTriggerChannel, kLatch, kSeed,
};

constexpr ParamSpec kCloudParams[] = {
    {"Sync", "sync", 0, 1, 0, Kind::Menu, kSyncLabels, ""},
    {"Density", "density", 0.1, 200, 8, Kind::Number, nullptr, "/s"},
    {"Rate", "rate", 0, 7, 5, Kind::Menu, kRateLabels, ""},
    {"Length", "length_ms", 1, 5000, 100, Kind::Number, nullptr, "ms"},
    {"Length (beats)", "length_beats", 1.0 / 64, 16, 0.25, Kind::Number, nullptr, "beats"},
    {"Onset random", "onset_random", 0, 1, 0.5, Kind::Number, nullptr, ""},
    {"Pick", "pick", 0, 4, 0, Kind::Menu, kPickLabels, ""},
    {"Weight", "weight", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Octaves", "octaves", 0, 4, 0, Kind::Stepped, nullptr, ""},
    {"Transpose", "transpose", -48, 48, 0, Kind::Number, nullptr, "st"},
    {"Scatter", "scatter", 0, 24, 0, Kind::Number, nullptr, "st"},
    {"Quantize", "quantize", 0, 2, 0, Kind::Menu, kQuantizeLabels, ""},
    {"EDO", "edo", 1, 96, 12, Kind::Stepped, nullptr, ""},
    {"Root", "root", 0, 11, 9, Kind::Menu, kRootLabels, ""},
    {"Level", "level", 0, 2, 1, Kind::Number, nullptr, ""},
    {"Velocity spread", "velocity_spread", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Length spread", "length_spread", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Voices", "voices", 1, 32, 8, Kind::Stepped, nullptr, ""},
    {"When full", "when_full", 0, 1, 0, Kind::Menu, kWhenFullLabels, ""},
    {"Dry", "dry", 0, 1, 0, Kind::Menu, kOffOnLabels, ""},
    {"Hold over", "hold_over", 0, 200, 20, Kind::Number, nullptr, "ms"},
    {"Set channel", "set_channel", 0, 16, 0, Kind::Stepped, nullptr, ""},          // 0: all
    {"Trigger channel", "trigger_channel", 0, 16, 0, Kind::Stepped, nullptr, ""},  // 0: none
    {"Latch", "latch", 0, 1, 0, Kind::Menu, kOffOnLabels, ""},
    {"Seed", "seed", 0, 9999, 0, Kind::Stepped, nullptr, ""},                      // 0: new each start
};

// --- NoteGran Chords ---

const char* const kKeyLabels[]       = {"Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F", "C",
                                        "G",  "D",  "A",  "E",  "B",  "F#", "C#"};   // fifths -7 .. 7
const char* const kMapLabels[]       = {"By fifths", "Nearest"};
const char* const kInputLabels[]     = {"Familiar", "EDO"};
const char* const kStackLabels[]     = {"Thirds", "Fourths", "Fifths"};
const char* const kInversionLabels[] = {"Root", "1st", "2nd", "3rd", "4th", "5th"};
const char* const kVoicingLabels[]   = {"Close", "Open", "Drop-2", "Spread"};
const char* const kOffScaleLabels[]  = {"Pass", "Nearest", "Drop"};

// The familiar scales' names, from the core's table.
const std::array<const char*, 64> kScaleLabels = [] {
    std::array<const char*, 64> labels{};
    for (std::size_t i = 0; i < familiarScaleCount() && i < labels.size(); ++i) labels[i] = familiarScales()[i].name;
    return labels;
}();

enum ChordsParamId : std::uint32_t {
    kScale, kKey, kMap, kEdoSteps, kOrigin, kInput, kScaleChannel, kSize, kStack, kInversion, kVoicing,
    kOctave, kOffScale, kChordLevel, kChordVoices, kChordWhenFull,
};

const ParamSpec kChordsParams[] = {
    {"Scale", "scale", 0, double(familiarScaleCount() - 1), 0, Kind::Menu, kScaleLabels.data(), ""},
    {"Key", "key", 0, 14, 7, Kind::Menu, kKeyLabels, ""},
    {"Map", "map", 0, 1, 0, Kind::Menu, kMapLabels, ""},
    {"EDO", "edo", 1, 96, 12, Kind::Stepped, nullptr, ""},
    {"Origin", "origin", 0, 11, 9, Kind::Menu, kRootLabels, ""},
    {"Input", "input", 0, 1, 0, Kind::Menu, kInputLabels, ""},
    {"Scale channel", "scale_channel", 0, 16, 0, Kind::Stepped, nullptr, ""},   // 0: the menu's scale
    {"Size", "size", 1, 6, 3, Kind::Stepped, nullptr, ""},
    {"Stack", "stack", 0, 2, 0, Kind::Menu, kStackLabels, ""},
    {"Inversion", "inversion", 0, 5, 0, Kind::Menu, kInversionLabels, ""},
    {"Voicing", "voicing", 0, 3, 0, Kind::Menu, kVoicingLabels, ""},
    {"Octave", "octave", -2, 2, 0, Kind::Stepped, nullptr, ""},
    {"Off-scale", "off_scale", 0, 2, 0, Kind::Menu, kOffScaleLabels, ""},
    {"Level", "level", 0, 2, 1, Kind::Number, nullptr, ""},
    {"Voices", "voices", 1, 32, 8, Kind::Stepped, nullptr, ""},
    {"When full", "when_full", 0, 1, 0, Kind::Menu, kWhenFullLabels, ""},
};

// --- NoteGran Grid ---

const char* const kMeterLabels[]    = {"Straight", "5 (3+2)", "5 (2+3)", "7 (2+2+3)", "7 (3+2+2)", "7 (2+3+2)", "9 (3+3+3)"};
const char* const kTierPickLabels[] = {"Lowest", "Highest", "Next", "Random"};

enum GridParamId : std::uint32_t {
    kGRate, kGMeter, kGDensity, kGSmear, kGScatter, kGLength, kGLenSpread, kGVelSpread, kGTranspose, kGQuantize,
    kGEdo, kGRoot, kGVoices, kGWhenFull, kGDry, kGHoldOver, kGSetChannel, kGTriggerChannel, kGLatch, kGSeed,
    kGTiers,   // then, per tier: Probability, Level, Offset, Chaos, Pick
};
constexpr std::uint32_t kGTierParams = 5;

// Tiers (PLAN.md, "NoteGran Grid"): in Straight, 1 the bar's downbeat, 2 the half, 3 the quarters,
// 4 the eighths, 5 the sixteenths; in a grouped meter, the groups' downbeats, then the other beats,
// then the off-beats.
const ParamSpec kGridParams[] = {
    {"Rate", "rate", 0, 7, 5, Kind::Menu, kRateLabels, ""},
    {"Meter", "meter", 0, 6, 0, Kind::Menu, kMeterLabels, ""},
    {"Density", "density", 1, 4, 1, Kind::Number, nullptr, ""},
    {"Smear", "smear", 0, 1, 0.5, Kind::Number, nullptr, ""},
    {"Scatter", "scatter", 0, 24, 12, Kind::Number, nullptr, "st"},
    {"Length", "length_beats", 1.0 / 64, 16, 0.125, Kind::Number, nullptr, "beats"},
    {"Length spread", "length_spread", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Velocity spread", "velocity_spread", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Transpose", "transpose", -48, 48, 0, Kind::Number, nullptr, "st"},
    {"Quantize", "quantize", 0, 2, 0, Kind::Menu, kQuantizeLabels, ""},
    {"EDO", "edo", 1, 96, 12, Kind::Stepped, nullptr, ""},
    {"Root", "root", 0, 11, 9, Kind::Menu, kRootLabels, ""},
    {"Voices", "voices", 1, 32, 8, Kind::Stepped, nullptr, ""},
    {"When full", "when_full", 0, 1, 0, Kind::Menu, kWhenFullLabels, ""},
    {"Dry", "dry", 0, 1, 0, Kind::Menu, kOffOnLabels, ""},
    {"Hold over", "hold_over", 0, 200, 20, Kind::Number, nullptr, "ms"},
    {"Set channel", "set_channel", 0, 16, 0, Kind::Stepped, nullptr, ""},
    {"Trigger channel", "trigger_channel", 0, 16, 0, Kind::Stepped, nullptr, ""},
    {"Latch", "latch", 0, 1, 0, Kind::Menu, kOffOnLabels, ""},
    {"Seed", "seed", 0, 9999, 0, Kind::Stepped, nullptr, ""},
    {"Tier 1 Probability", "t1_probability", 0, 1, 1, Kind::Number, nullptr, ""},
    {"Tier 1 Level", "t1_level", 0, 2, 1, Kind::Number, nullptr, ""},
    {"Tier 1 Offset", "t1_offset", -24, 24, 0, Kind::Number, nullptr, "st"},
    {"Tier 1 Chaos", "t1_chaos", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Tier 1 Pick", "t1_pick", 0, 3, 0, Kind::Menu, kTierPickLabels, ""},
    {"Tier 2 Probability", "t2_probability", 0, 1, 1, Kind::Number, nullptr, ""},
    {"Tier 2 Level", "t2_level", 0, 2, 0.85, Kind::Number, nullptr, ""},
    {"Tier 2 Offset", "t2_offset", -24, 24, 0, Kind::Number, nullptr, "st"},
    {"Tier 2 Chaos", "t2_chaos", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Tier 2 Pick", "t2_pick", 0, 3, 2, Kind::Menu, kTierPickLabels, ""},
    {"Tier 3 Probability", "t3_probability", 0, 1, 1, Kind::Number, nullptr, ""},
    {"Tier 3 Level", "t3_level", 0, 2, 0.75, Kind::Number, nullptr, ""},
    {"Tier 3 Offset", "t3_offset", -24, 24, 0, Kind::Number, nullptr, "st"},
    {"Tier 3 Chaos", "t3_chaos", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Tier 3 Pick", "t3_pick", 0, 3, 2, Kind::Menu, kTierPickLabels, ""},
    {"Tier 4 Probability", "t4_probability", 0, 1, 0.75, Kind::Number, nullptr, ""},
    {"Tier 4 Level", "t4_level", 0, 2, 0.65, Kind::Number, nullptr, ""},
    {"Tier 4 Offset", "t4_offset", -24, 24, 0, Kind::Number, nullptr, "st"},
    {"Tier 4 Chaos", "t4_chaos", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Tier 4 Pick", "t4_pick", 0, 3, 2, Kind::Menu, kTierPickLabels, ""},
    {"Tier 5 Probability", "t5_probability", 0, 1, 0.5, Kind::Number, nullptr, ""},
    {"Tier 5 Level", "t5_level", 0, 2, 0.55, Kind::Number, nullptr, ""},
    {"Tier 5 Offset", "t5_offset", -24, 24, 0, Kind::Number, nullptr, "st"},
    {"Tier 5 Chaos", "t5_chaos", 0, 1, 0, Kind::Number, nullptr, ""},
    {"Tier 5 Pick", "t5_pick", 0, 3, 3, Kind::Menu, kTierPickLabels, ""},
};

// --- the plugins ---

enum class Which : std::uint8_t { Cloud, Chords, Grid };

struct PluginDef {
    const clap_plugin_descriptor_t* desc;
    Which                           which;
    const ParamSpec*                params;
    std::uint32_t                   count;
    const char*                     stateHeader;   // then the version, then "symbol value" lines
};

constexpr std::uint32_t kMaxParams = 64;

double clampParam(const PluginDef& def, std::uint32_t id, double value) {
    const ParamSpec& s = def.params[id];

    if (!std::isfinite(value)) value = s.def;
    if (s.kind != Kind::Number) value = std::round(value);
    return std::clamp(value, s.min, s.max);
}

// ---------------------------------------------------------------------------------------------
// The plugin
// ---------------------------------------------------------------------------------------------

constexpr std::size_t kMaxInputs  = 1024;   // per sub-block; more split the block
constexpr std::size_t kMaxOutputs = 4096;   // note events per process call

struct Pending {
    std::int64_t               time;    // absolute
    const clap_event_header_t* event;   // passes through unchanged
};

struct Plugin {
    clap_plugin_t      clap{};
    const clap_host_t* host = nullptr;
    const PluginDef*   def  = nullptr;

    // Parameter values: written by process/flush and by state_load; read by the main thread.
    std::atomic<double> values[kMaxParams];
    std::atomic<bool>   valuesChangedByState{false};   // state_load → process: re-apply

    std::unique_ptr<CloudCore>  cloud;    // the one this plugin is
    std::unique_ptr<GridCore>   grid;
    std::unique_ptr<ChordsCore> chords;
    Assigner*                   assigner = nullptr;   // cloud or grid
    std::uint64_t startSeed = 1;       // Seed 0: chosen at activate
    std::int64_t  now       = 0;       // absolute sample count at the block's start
    bool          endAll    = false;   // reset / reactivation: end what's sounding first
    std::string   scratch;

    // Per process call, fixed size.
    std::array<InputEvent, kMaxInputs>                   inputs{};
    std::array<const clap_event_header_t*, kMaxInputs>   inputEvents{};
    std::array<NoteEvent, kMaxOutputs>                   notes{};
    std::array<Pending, kMaxOutputs>                     passed{};
    Transport                                            transport;
    bool                                                 hasTransport = false;
};

Plugin* self(const clap_plugin_t* plugin) { return static_cast<Plugin*>(plugin->plugin_data); }

double value(const Plugin* p, std::uint32_t id) { return p->values[id].load(std::memory_order_relaxed); }
int    intValue(const Plugin* p, std::uint32_t id) { return static_cast<int>(std::lround(value(p, id))); }

CloudParams cloudParams(const Plugin* p) {
    CloudParams c;
    c.sync             = intValue(p, kSync) == 1 ? Clock::Mode::Beats : Clock::Mode::Free;
    c.density          = value(p, kDensity);
    const int rate     = std::clamp(intValue(p, kRate), 0, 7);
    c.divisionNum      = kRateNum[rate];
    c.divisionDen      = kRateDen[rate];
    c.lengthMs         = value(p, kLengthMs);
    c.lengthBeats      = std::llround(value(p, kLengthBeats) * static_cast<double>(kBeat));
    c.onsetRandom      = toQ16(value(p, kOnsetRandom));
    c.pick             = static_cast<Pick>(std::clamp(intValue(p, kPick), 0, 4));
    c.weight           = toQ16(value(p, kWeight));
    c.pitch.octaves    = intValue(p, kOctaves);
    c.pitch.transpose  = PitchParams::fromSemitones(value(p, kTranspose));
    c.pitch.scatter    = PitchParams::fromSemitones(value(p, kScatter));
    c.pitch.quantize   = static_cast<Quantize>(std::clamp(intValue(p, kQuantize), 0, 2));
    c.pitch.edo        = intValue(p, kEdo);
    c.pitch.root       = intValue(p, kRoot);
    c.level            = toQ16(value(p, kLevel));
    c.velSpread        = toQ16(value(p, kVelSpread));
    c.lenSpread        = toQ16(value(p, kLenSpread));
    c.voices           = intValue(p, kVoices);
    c.whenFull         = intValue(p, kWhenFull) == 1 ? WhenFull::Steal : WhenFull::Skip;
    c.dry              = intValue(p, kDry) == 1;
    c.holdOverMs       = value(p, kHoldOver);
    c.setChannel       = intValue(p, kSetChannel) - 1;       // 0 (all) becomes -1
    c.triggerChannel   = intValue(p, kTriggerChannel) - 1;   // 0 (none) becomes -1
    c.latch            = intValue(p, kLatch) == 1;
    const int seed     = intValue(p, kSeed);
    c.seed             = seed == 0 ? p->startSeed : static_cast<std::uint64_t>(seed);
    return c;
}

ChordsParams chordsParams(const Plugin* p) {
    ChordsParams c;
    c.scale        = static_cast<std::size_t>(std::max(intValue(p, kScale), 0));
    c.keyFifths    = intValue(p, kKey) - 7;
    c.map          = intValue(p, kMap) == 1 ? MapBy::Nearest : MapBy::Fifths;
    c.edo          = intValue(p, kEdoSteps);
    c.origin       = intValue(p, kOrigin);
    c.input        = intValue(p, kInput) == 1 ? InputMatch::Edo : InputMatch::Familiar;
    c.scaleChannel = intValue(p, kScaleChannel) - 1;   // 0 (the menu) becomes -1
    c.size         = intValue(p, kSize);
    c.stack        = intValue(p, kStack) + 2;
    c.inversion    = intValue(p, kInversion);
    c.voicing      = static_cast<Voicing>(std::clamp(intValue(p, kVoicing), 0, 3));
    c.octave       = intValue(p, kOctave);
    c.offScale     = static_cast<OffScale>(std::clamp(intValue(p, kOffScale), 0, 2));
    c.level        = toQ16(value(p, kChordLevel));
    c.voices       = intValue(p, kChordVoices);
    c.whenFull     = intValue(p, kChordWhenFull) == 1 ? WhenFull::Steal : WhenFull::Skip;
    return c;
}

GridParams gridParams(const Plugin* p) {
    GridParams g;
    AssignerSettings& a = g.settings;
    a.voices            = intValue(p, kGVoices);
    a.whenFull          = intValue(p, kGWhenFull) == 1 ? WhenFull::Steal : WhenFull::Skip;
    a.dry               = intValue(p, kGDry) == 1;
    a.holdOverMs        = value(p, kGHoldOver);
    a.setChannel        = intValue(p, kGSetChannel) - 1;
    a.triggerChannel    = intValue(p, kGTriggerChannel) - 1;
    a.latch             = intValue(p, kGLatch) == 1;
    const int seed      = intValue(p, kGSeed);
    a.seed              = seed == 0 ? p->startSeed : static_cast<std::uint64_t>(seed);
    const int rate      = std::clamp(intValue(p, kGRate), 0, 7);
    g.rateNum           = kRateNum[rate];
    g.rateDen           = kRateDen[rate];
    g.meter             = static_cast<Meter>(std::clamp(intValue(p, kGMeter), 0, 6));
    g.density           = toQ16(value(p, kGDensity));
    g.smear             = toQ16(value(p, kGSmear));
    g.scatter           = PitchParams::fromSemitones(value(p, kGScatter));
    g.lengthBeats       = std::llround(value(p, kGLength) * static_cast<double>(kBeat));
    g.lenSpread         = toQ16(value(p, kGLenSpread));
    g.velSpread         = toQ16(value(p, kGVelSpread));
    g.pitch.transpose   = PitchParams::fromSemitones(value(p, kGTranspose));
    g.pitch.quantize    = static_cast<Quantize>(std::clamp(intValue(p, kGQuantize), 0, 2));
    g.pitch.edo         = intValue(p, kGEdo);
    g.pitch.root        = intValue(p, kGRoot);
    for (std::uint32_t t = 0; t < kTiers; ++t) {
        const std::uint32_t base = kGTiers + t * kGTierParams;
        GridTier&           tier = g.tiers[t];
        tier.probability         = toQ16(value(p, base));
        tier.level               = toQ16(value(p, base + 1));
        tier.offset              = PitchParams::fromSemitones(value(p, base + 2));
        tier.chaos               = toQ16(value(p, base + 3));
        tier.pick                = static_cast<TierPick>(std::clamp(intValue(p, base + 4), 0, 3));
    }
    return g;
}

void applyParams(Plugin* p) {
    if (p->cloud) p->cloud->setParams(cloudParams(p));
    if (p->grid) p->grid->setParams(gridParams(p));
    if (p->chords) p->chords->setParams(chordsParams(p));
}

void storeParam(Plugin* p, const clap_event_param_value_t* ev) {
    if (ev->param_id >= p->def->count) return;
    p->values[ev->param_id].store(clampParam(*p->def, ev->param_id, ev->value), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// Input: CLAP events to the core's UMP
// ---------------------------------------------------------------------------------------------

uw::MidiMessage midi2(std::uint8_t status, std::uint8_t channel, std::uint8_t index1, std::uint8_t index2,
                      std::uint32_t data, std::uint8_t group = 0) {
    uw::MidiMessage m;
    m.words[0]  = 0x40000000u | std::uint32_t(group & 0xF) << 24 | std::uint32_t(status & 0xF) << 20 |
                 std::uint32_t(channel & 0xF) << 16 | std::uint32_t(index1) << 8 | index2;
    m.words[1]  = data;
    m.wordCount = 2;
    return m;
}

std::uint16_t velocity16(double v) {
    return static_cast<std::uint16_t>(std::lround(std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 1.0) * 65535.0));
}

// The UMP for one CLAP event, and its dialect. False: not for the core (it passes through, or is
// dropped by the caller).
bool toInput(const clap_event_header_t* h, uw::MidiMessage& m, Dialect& dialect) {
    switch (h->type) {
    case CLAP_EVENT_MIDI2: {
        const auto* ev = reinterpret_cast<const clap_event_midi2_t*>(h);
        std::memcpy(m.words, ev->data, sizeof(m.words));
        m.wordCount = ump::wordCountForType(static_cast<std::uint8_t>(m.words[0] >> 28));
        dialect     = Dialect::Midi2;
        return true;
    }
    case CLAP_EVENT_MIDI: {
        const auto* ev = reinterpret_cast<const clap_event_midi_t*>(h);
        if (ev->data[0] < 0x80 || ev->data[0] >= 0xF0) return false;   // system: passes through
        m           = uw::MidiMessage{};
        m.words[0]  = 0x20000000u | std::uint32_t(ev->data[0]) << 16 | std::uint32_t(ev->data[1] & 0x7F) << 8 |
                     std::uint32_t(ev->data[2] & 0x7F);
        m.wordCount = 1;
        dialect     = Dialect::Midi1;
        return true;
    }
    case CLAP_EVENT_NOTE_ON:
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
        const auto* ev = reinterpret_cast<const clap_event_note_t*>(h);
        // Wildcards address no one note of the set: they pass through.
        if (ev->channel < 0 || ev->channel > 15 || ev->key < 0 || ev->key > 127) return false;
        const auto ch = static_cast<std::uint8_t>(ev->channel);
        const auto k  = static_cast<std::uint8_t>(ev->key);
        m = h->type == CLAP_EVENT_NOTE_ON ? midi2(0x9, ch, k, 0, std::uint32_t(velocity16(ev->velocity)) << 16)
                                          : midi2(0x8, ch, k, 0, 0);
        dialect = Dialect::Clap;
        return true;
    }
    case CLAP_EVENT_NOTE_EXPRESSION: {
        const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(h);
        if (ev->channel < 0 || ev->channel > 15 || ev->key < 0 || ev->key > 127) return false;
        const auto ch = static_cast<std::uint8_t>(ev->channel);
        const auto k  = static_cast<std::uint8_t>(ev->key);
        if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING) {
            // The note's absolute pitch, as Pitch 7.25 (registered per-note controller 3).
            const double semitones = std::clamp(k + (std::isfinite(ev->value) ? ev->value : 0.0), 0.0, 127.99);
            m = midi2(0x0, ch, k, 3, static_cast<std::uint32_t>(std::llround(semitones * 33554432.0)));
        } else {
            // Any other expression: a per-note message the set ignores, so it's classed (and
            // passes with Dry) like the note it's on.
            m = midi2(0x1, ch, k, 0, 0);
        }
        dialect = Dialect::Clap;
        return true;
    }
    default:
        return false;
    }
}

Transport fromClap(const clap_event_transport_t& t) {
    Transport r;
    r.hasTempo = (t.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0;
    r.tempo    = t.tempo;
    r.hasBeats = (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
    r.playing  = (t.flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
    r.beats    = t.song_pos_beats;   // clap_beattime: 2^31 per beat, as kBeat
    return r;
}

// ---------------------------------------------------------------------------------------------
// Output: the core's NoteEvents in their dialect
// ---------------------------------------------------------------------------------------------

bool push(const clap_output_events_t* out, const clap_event_header_t* h) {
    return out != nullptr && out->try_push != nullptr && out->try_push(out, h);
}

void pushNote(const NoteEvent& e, std::uint32_t time, const clap_output_events_t* out) {
    switch (e.dialect) {
    case Dialect::Midi1: {
        clap_event_midi_t ev{};
        ev.header     = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0};
        ev.port_index = 0;
        ev.data[0]    = static_cast<std::uint8_t>((e.on ? 0x90 : 0x80) | (e.channel & 0xF));
        ev.data[1]    = e.key;
        ev.data[2]    = e.on ? velocity7(e.velocity) : 0;
        push(out, &ev.header);
        break;
    }
    case Dialect::Midi2: {
        clap_event_midi2_t ev{};
        ev.header     = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI2, 0};
        ev.port_index = 0;
        const uw::MidiMessage m = e.on ? ump::midi2NoteOn(e.group, e.channel, e.key, e.velocity, e.pitch79)
                                       : ump::midi2NoteOff(e.group, e.channel, e.key);
        std::memcpy(ev.data, m.words, sizeof(ev.data));
        push(out, &ev.header);
        break;
    }
    case Dialect::Clap: {
        clap_event_note_t ev{};
        ev.header     = {sizeof(ev), time, CLAP_CORE_EVENT_SPACE_ID,
                         static_cast<std::uint16_t>(e.on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
        ev.note_id    = -1;
        ev.port_index = 0;
        ev.channel    = e.channel;
        ev.key        = e.key;
        ev.velocity   = e.on ? e.velocity / 65535.0 : 0.0;
        push(out, &ev.header);
        const int fraction = e.pitch79 - (e.key << 9);
        if (e.on && fraction != 0) {
            clap_event_note_expression_t x{};
            x.header        = {sizeof(x), time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_EXPRESSION, 0};
            x.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
            x.note_id       = -1;
            x.port_index    = 0;
            x.channel       = e.channel;
            x.key           = e.key;
            x.value         = fraction / 512.0;
            push(out, &x.header);
        }
        break;
    }
    }
}

// ---------------------------------------------------------------------------------------------
// clap_plugin
// ---------------------------------------------------------------------------------------------

bool plug_init(const clap_plugin_t*) { return true; }

void plug_destroy(const clap_plugin_t* plugin) { delete self(plugin); }

bool plug_activate(const clap_plugin_t* plugin, double sampleRate, std::uint32_t, std::uint32_t) {
    Plugin* p = self(plugin);
    // Seed 0: a new seed each start (main thread, so random_device is fine here).
    std::random_device device;
    p->startSeed = (std::uint64_t{device()} << 32 | device()) | 1;
    if (p->assigner) p->assigner->setSampleRate(sampleRate);
    applyParams(p);
    p->valuesChangedByState.store(false, std::memory_order_relaxed);
    p->endAll = true;   // anything left from before ends first
    return true;
}

void plug_deactivate(const clap_plugin_t*) {}
bool plug_start_processing(const clap_plugin_t*) { return true; }
void plug_stop_processing(const clap_plugin_t*) {}
void plug_reset(const clap_plugin_t* plugin) { self(plugin)->endAll = true; }

clap_process_status plug_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    Plugin* p = self(plugin);
    if (p->valuesChangedByState.exchange(false, std::memory_order_acquire)) applyParams(p);

    const std::int64_t blockStart = p->now;
    const std::int64_t frames     = process->frames_count;
    p->hasTransport               = process->transport != nullptr;
    if (p->hasTransport) p->transport = fromClap(*process->transport);   // continues = false

    NoteOut     notes(p->notes.data(), p->notes.size());
    std::size_t passed = 0;
    if (p->endAll) {
        if (p->assigner) p->assigner->reset(blockStart, notes);
        if (p->chords) p->chords->reset(blockStart, notes);
        p->endAll = false;
    }

    // Sub-blocks: from `from`, the inputs gathered so far, up to a parameter or transport change.
    std::int64_t from   = blockStart;
    std::size_t  inputs = 0;
    auto runUntil = [&](std::int64_t until) {
        if (until <= from) return;
        NoteOut rest(p->notes.data() + notes.size(), p->notes.size() - notes.size());
        if (p->assigner)
            p->assigner->process(from, until - from, p->hasTransport ? &p->transport : nullptr, p->inputs.data(), inputs,
                                 rest);
        if (p->chords) p->chords->process(p->inputs.data(), inputs, rest);
        for (std::size_t i = 0; i < rest.size(); ++i) notes.push(rest[i]);
        for (std::size_t i = 0; i < inputs; ++i)
            if (p->inputs[i].pass && passed < p->passed.size())
                p->passed[passed++] = Pending{p->inputs[i].time, p->inputEvents[i]};
        inputs                 = 0;
        from                   = until;
        p->transport.continues = true;   // later sub-blocks: the host's position is the block's start
    };

    const clap_input_events_t* in    = process->in_events;
    const std::uint32_t        count = in != nullptr ? in->size(in) : 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        const std::int64_t t = blockStart + std::min<std::int64_t>(h->time, frames > 0 ? frames - 1 : 0);
        if (h->type == CLAP_EVENT_PARAM_VALUE) {
            runUntil(t);
            storeParam(p, reinterpret_cast<const clap_event_param_value_t*>(h));
            applyParams(p);
            continue;
        }
        if (h->type == CLAP_EVENT_TRANSPORT) {
            runUntil(t);
            p->transport    = fromClap(*reinterpret_cast<const clap_event_transport_t*>(h));
            p->hasTransport = true;
            continue;
        }
        uw::MidiMessage m;
        Dialect         d = Dialect::Midi2;
        if (!toInput(h, m, d)) {
            // System MIDI, SysEx, wildcard notes: through unchanged, in their place.
            if (h->type == CLAP_EVENT_MIDI || h->type == CLAP_EVENT_MIDI_SYSEX || h->type == CLAP_EVENT_MIDI2 ||
                h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF || h->type == CLAP_EVENT_NOTE_CHOKE ||
                h->type == CLAP_EVENT_NOTE_EXPRESSION) {
                runUntil(t);
                if (passed < p->passed.size()) p->passed[passed++] = Pending{t, h};
            }
            continue;
        }
        if (inputs == p->inputs.size()) runUntil(t);
        p->inputs[inputs]      = InputEvent{t, m, d, false};
        p->inputEvents[inputs] = h;
        ++inputs;
    }
    runUntil(blockStart + frames);
    p->now = blockStart + frames;

    // Merge the passed-through events and the notes by time; at the same sample, passed first.
    std::size_t a = 0, b = 0;
    while (a < passed || b < notes.size()) {
        const bool takePassed = b == notes.size() || (a < passed && p->passed[a].time <= notes[b].time);
        const std::int64_t t  = takePassed ? p->passed[a].time : notes[b].time;
        const auto offset     = static_cast<std::uint32_t>(std::clamp<std::int64_t>(t - blockStart, 0, frames > 0 ? frames - 1 : 0));
        if (takePassed) {
            const clap_event_header_t* h = p->passed[a++].event;
            if (h->time == offset) {
                push(process->out_events, h);
            } else {   // a note-off delayed past its block, or a clamped time
                alignas(8) std::uint8_t copy[256];
                if (h->size <= sizeof(copy)) {
                    std::memcpy(copy, h, h->size);
                    reinterpret_cast<clap_event_header_t*>(copy)->time = offset;
                    push(process->out_events, reinterpret_cast<const clap_event_header_t*>(copy));
                }
            }
        } else {
            pushNote(notes[b++], offset, process->out_events);
        }
    }
    const bool busy = (p->assigner && p->assigner->busy()) || (p->chords && p->chords->busy());
    return busy ? CLAP_PROCESS_CONTINUE : CLAP_PROCESS_SLEEP;
}

void plug_on_main_thread(const clap_plugin_t*) {}

// ---------------------------------------------------------------------------------------------
// audio-ports extension: none (a note effect)
// ---------------------------------------------------------------------------------------------

std::uint32_t audio_ports_count(const clap_plugin_t*, bool) { return 0; }
bool audio_ports_get(const clap_plugin_t*, std::uint32_t, bool, clap_audio_port_info_t*) { return false; }
const clap_plugin_audio_ports_t kAudioPorts = {audio_ports_count, audio_ports_get};

// ---------------------------------------------------------------------------------------------
// note-ports extension: one in, one out
// ---------------------------------------------------------------------------------------------

std::uint32_t note_ports_count(const clap_plugin_t*, bool) { return 1; }

bool note_ports_get(const clap_plugin_t*, std::uint32_t index, bool isInput, clap_note_port_info_t* info) {
    if (index != 0) return false;
    std::memset(info, 0, sizeof(*info));
    info->id                 = 0;   // ids may overlap between inputs and outputs
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI2;
    info->preferred_dialect  = CLAP_NOTE_DIALECT_MIDI2;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Notes in" : "Notes out");
    return true;
}

const clap_plugin_note_ports_t kNotePorts = {note_ports_count, note_ports_get};

// ---------------------------------------------------------------------------------------------
// params extension
// ---------------------------------------------------------------------------------------------

std::uint32_t params_count(const clap_plugin_t* plugin) { return self(plugin)->def->count; }

bool params_get_info(const clap_plugin_t* plugin, std::uint32_t index, clap_param_info_t* info) {
    const PluginDef& def = *self(plugin)->def;
    if (index >= def.count) return false;
    const ParamSpec& s = def.params[index];
    std::memset(info, 0, sizeof(*info));
    info->id    = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    if (s.kind != Kind::Number) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (s.kind == Kind::Menu) info->flags |= CLAP_PARAM_IS_ENUM;
    info->min_value     = s.min;
    info->max_value     = s.max;
    info->default_value = s.def;
    std::snprintf(info->name, sizeof(info->name), "%s", s.name);
    return true;
}

bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* out) {
    if (id >= self(plugin)->def->count) return false;
    *out = value(self(plugin), id);
    return true;
}

// A stepped parameter's word for its 0, if it has one.
const char* zeroText(const PluginDef& def, clap_id id) {
    if (def.which == Which::Cloud) {
        if (id == kSetChannel) return "All";
        if (id == kTriggerChannel) return "None";
        if (id == kSeed) return "New each start";
    } else if (def.which == Which::Grid) {
        if (id == kGSetChannel) return "All";
        if (id == kGTriggerChannel) return "None";
        if (id == kGSeed) return "New each start";
    } else if (id == kScaleChannel) {
        return "None";
    }
    return nullptr;
}

bool params_value_to_text(const clap_plugin_t* plugin, clap_id id, double v, char* out, std::uint32_t size) {
    const PluginDef& def = *self(plugin)->def;
    if (id >= def.count) return false;
    const ParamSpec& s = def.params[id];
    v = clampParam(def, id, v);
    const int step = static_cast<int>(v);
    if (s.kind == Kind::Menu) std::snprintf(out, size, "%s", s.labels[step - static_cast<int>(s.min)]);
    else if (step == 0 && zeroText(def, id) != nullptr) std::snprintf(out, size, "%s", zeroText(def, id));
    else if (s.kind == Kind::Stepped) std::snprintf(out, size, "%d", step);
    else std::snprintf(out, size, *s.unit ? "%.3g %s" : "%.3g", v, s.unit);
    return true;
}

bool params_text_to_value(const clap_plugin_t* plugin, clap_id id, const char* text, double* out) {
    const PluginDef& def = *self(plugin)->def;
    if (id >= def.count) return false;
    const ParamSpec& s = def.params[id];
    if (s.kind == Kind::Menu) {
        for (int i = 0; i <= static_cast<int>(s.max - s.min); ++i)
            if (std::strcmp(text, s.labels[i]) == 0) {
                *out = s.min + i;
                return true;
            }
    }
    if (zeroText(def, id) != nullptr && std::strcmp(text, zeroText(def, id)) == 0) {
        *out = 0;
        return true;
    }
    char*        end = nullptr;
    const double v   = std::strtod(text, &end);
    if (end == text) return false;
    *out = clampParam(def, id, v);
    return true;
}

void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t*) {
    // Never concurrent with process, so the core can be updated here too.
    Plugin*             p     = self(plugin);
    const std::uint32_t count = in != nullptr ? in->size(in) : 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->space_id == CLAP_CORE_EVENT_SPACE_ID && h->type == CLAP_EVENT_PARAM_VALUE)
            storeParam(p, reinterpret_cast<const clap_event_param_value_t*>(h));
    }
    applyParams(p);
}

const clap_plugin_params_t kParamsExt = {params_count,         params_get_info,      params_get_value,
                                         params_value_to_text, params_text_to_value, params_flush};

// ---------------------------------------------------------------------------------------------
// state extension: "NoteGran Cloud 1" (or Chords) then "<symbol> <value>" lines; unknown symbols
// are ignored
// ---------------------------------------------------------------------------------------------

bool state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    Plugin* p  = self(plugin);
    p->scratch = std::string(p->def->stateHeader) + "1\n";
    char line[96];
    for (std::uint32_t i = 0; i < p->def->count; ++i) {
        std::snprintf(line, sizeof(line), "%s %.17g\n", p->def->params[i].symbol, value(p, i));
        p->scratch += line;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(p->scratch.data());
    for (std::uint64_t done = 0; done < p->scratch.size();) {
        const std::int64_t written = stream->write(stream, bytes + done, p->scratch.size() - done);
        if (written <= 0) return false;
        done += static_cast<std::uint64_t>(written);
    }
    return true;
}

bool state_load(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    Plugin*     p = self(plugin);
    std::string text;
    char        buffer[256];
    for (;;) {
        const std::int64_t read = stream->read(stream, buffer, sizeof(buffer));
        if (read < 0) return false;
        if (read == 0) break;
        text.append(buffer, static_cast<std::size_t>(read));
    }
    if (text.rfind(p->def->stateHeader, 0) != 0) return false;   // empty, or not ours

    std::size_t pos = text.find('\n');
    while (pos != std::string::npos && pos + 1 < text.size()) {
        const std::size_t start = pos + 1;
        pos                     = text.find('\n', start);
        const std::string line  = text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) continue;
        const PluginDef& def = *p->def;
        for (std::uint32_t i = 0; i < def.count; ++i)
            if (line.compare(0, space, def.params[i].symbol) == 0 && space == std::strlen(def.params[i].symbol))
                p->values[i].store(clampParam(def, i, std::strtod(line.c_str() + space + 1, nullptr)),
                                   std::memory_order_relaxed);
    }
    p->valuesChangedByState.store(true, std::memory_order_release);

    const auto* hostParams = static_cast<const clap_host_params_t*>(p->host->get_extension(p->host, CLAP_EXT_PARAMS));
    if (hostParams != nullptr && hostParams->rescan != nullptr) hostParams->rescan(p->host, CLAP_PARAM_RESCAN_VALUES);
    return true;
}

const clap_plugin_state_t kStateExt = {state_save, state_load};

const void* plug_get_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &kParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &kStateExt;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// descriptors + factory + entry
// ---------------------------------------------------------------------------------------------

const char* kFeatures[] = {CLAP_PLUGIN_FEATURE_NOTE_EFFECT, nullptr};

const clap_plugin_descriptor_t kCloud = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "com.unlikelyware.notegran.cloud",
    .name         = "NoteGran Cloud",
    .vendor       = "Unlikelyware",
    .url          = "https://unlikelyware.com",
    .manual_url   = "",
    .support_url  = "",
    .version      = NOTEGRAN_VERSION_STRING,
    .description  = "Granular notes: a scatter of grains whose pitches are picked from the notes held "
                    "on its input, from an arpeggio to a cloud.",
    .features     = kFeatures,
};

const clap_plugin_descriptor_t kChords = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "com.unlikelyware.notegran.chords",
    .name         = "NoteGran Chords",
    .vendor       = "Unlikelyware",
    .url          = "https://unlikelyware.com",
    .manual_url   = "",
    .support_url  = "",
    .version      = NOTEGRAN_VERSION_STRING,
    .description  = "Each note held becomes a chord in a scale; a familiar scale can be mapped into any "
                    "equal division of the octave. Notes off the scale pass through.",
    .features     = kFeatures,
};

const clap_plugin_descriptor_t kGrid = {
    .clap_version = CLAP_VERSION_INIT,
    .id           = "com.unlikelyware.notegran.grid",
    .name         = "NoteGran Grid",
    .vendor       = "Unlikelyware",
    .url          = "https://unlikelyware.com",
    .manual_url   = "",
    .support_url  = "",
    .version      = NOTEGRAN_VERSION_STRING,
    .description  = "Notes on a beat grid whose positions have tiers (bar, beats, off-beats), each with its "
                    "own chance, level, offset and pick from the notes held on its input.",
    .features     = kFeatures,
};

// One per plugin, in the factory's order; the next ones (Burst, Buffer) are appended.
const PluginDef kPlugins[] = {
    {&kCloud, Which::Cloud, kCloudParams, sizeof(kCloudParams) / sizeof(kCloudParams[0]), "NoteGran Cloud "},
    {&kChords, Which::Chords, kChordsParams, sizeof(kChordsParams) / sizeof(kChordsParams[0]), "NoteGran Chords "},
    {&kGrid, Which::Grid, kGridParams, sizeof(kGridParams) / sizeof(kGridParams[0]), "NoteGran Grid "},
};
constexpr std::uint32_t kPluginCount = sizeof(kPlugins) / sizeof(kPlugins[0]);

const clap_plugin_t* factory_create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    const PluginDef* def = nullptr;
    for (const PluginDef& d : kPlugins)
        if (std::strcmp(id, d.desc->id) == 0) def = &d;
    if (def == nullptr || def->count > kMaxParams) return nullptr;
    auto* p = new (std::nothrow) Plugin();
    if (p == nullptr) return nullptr;
    p->host = host;
    p->def  = def;
    switch (def->which) {
    case Which::Cloud:
        p->cloud.reset(new (std::nothrow) CloudCore());
        p->assigner = p->cloud.get();
        break;
    case Which::Grid:
        p->grid.reset(new (std::nothrow) GridCore());
        p->assigner = p->grid.get();
        break;
    case Which::Chords: p->chords.reset(new (std::nothrow) ChordsCore()); break;
    }
    if (p->assigner == nullptr && !p->chords) {
        delete p;
        return nullptr;
    }
    for (std::uint32_t i = 0; i < def->count; ++i) p->values[i].store(def->params[i].def, std::memory_order_relaxed);
    if (p->assigner) p->assigner->setSampleRate(48000.0);
    applyParams(p);
    p->clap = clap_plugin_t{
        .desc             = def->desc,
        .plugin_data      = p,
        .init             = plug_init,
        .destroy          = plug_destroy,
        .activate         = plug_activate,
        .deactivate       = plug_deactivate,
        .start_processing = plug_start_processing,
        .stop_processing  = plug_stop_processing,
        .reset            = plug_reset,
        .process          = plug_process,
        .get_extension    = plug_get_extension,
        .on_main_thread   = plug_on_main_thread,
    };
    return &p->clap;
}

std::uint32_t factory_count(const clap_plugin_factory_t*) { return kPluginCount; }

const clap_plugin_descriptor_t* factory_get(const clap_plugin_factory_t*, std::uint32_t index) {
    return index < kPluginCount ? kPlugins[index].desc : nullptr;
}

const clap_plugin_factory_t kFactory = {factory_count, factory_get, factory_create};

bool entry_init(const char*) { return true; }
void entry_deinit() {}

const void* entry_get_factory(const char* factoryId) {
    return std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0 ? &kFactory : nullptr;
}

} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init         = entry_init,
    .deinit       = entry_deinit,
    .get_factory  = entry_get_factory,
};
