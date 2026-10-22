// plugin_gui.h — the seam between the plugin (src/plugin.cpp, the raw CLAP plugin) and its editor
// (src/UI/NoteGranUI.cpp, DPF + NanoVG), as StruckSynth's. Neither side includes the other's
// internals; they meet only at these C-linkage functions.
//
// Provided by plugin.cpp, called by the editor:
//   ng_plugin_*   per-instance plumbing, and which plugin this is
//   ng_param_*    a parameter's description, value and text, and edits with gestures
//   ng_live_read  what's playing, for the editor's live views
//   ng_grid_cycle, ng_burst_*, ng_chords_*   what the current settings would make, for the editors' previews
// Provided by the editor, called by plugin.cpp:
//   ng_gui_extension   the CLAP gui vtable for this platform
//   ng_gui_on_timer    CLAP timer-support: pumps the DPF/pugl event loop and refreshes the editor
//
// Host automation needs no push: the editor reads every value on its timer and shows what changed.

#pragma once

#include <clap/clap.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- provided by plugin.cpp, called by the editor --------------------------------------------

enum { NG_CLOUD = 0, NG_CHORDS = 1, NG_GRID = 2, NG_BURST = 3 };

const clap_host_t* ng_plugin_host(const clap_plugin_t* plugin);
int                ng_plugin_kind(const clap_plugin_t* plugin);   // NG_CLOUD ..

// The editor owns its per-instance state; plugin.cpp only holds the pointer for it.
void  ng_plugin_set_gui(const clap_plugin_t* plugin, void* gui);
void* ng_plugin_get_gui(const clap_plugin_t* plugin);

enum { NG_PARAM_NUMBER = 0, NG_PARAM_STEPPED = 1, NG_PARAM_MENU = 2 };

typedef struct ng_param_info {
    const char*        name;
    double             min, max, def;
    int                kind;     // NG_PARAM_*
    const char* const* labels;   // a menu's, one per step from min
} ng_param_info_t;

uint32_t ng_param_count(const clap_plugin_t* plugin);
bool     ng_param_info(const clap_plugin_t* plugin, uint32_t index, ng_param_info_t* info);
double   ng_param_value(const clap_plugin_t* plugin, uint32_t index);
// The text the host shows for `value` (units, "All", "New each start").
void     ng_param_text(const clap_plugin_t* plugin, uint32_t index, double value, char* out, uint32_t size);

// A drag is begin_gesture, set_from_gui (repeatedly), end_gesture. plugin.cpp applies each value at
// its next process() (or flush) and tells the host, so the move is recorded as automation.
void ng_param_begin_gesture(const clap_plugin_t* plugin, uint32_t index);
void ng_param_set_from_gui(const clap_plugin_t* plugin, uint32_t index, double value);
void ng_param_end_gesture(const clap_plugin_t* plugin, uint32_t index);

// What's playing: the set's members, the notes the plugin started lately (a ring, numbered so the
// editor can tell which are new), and the notes sounding now. Pitches in 1/512 semitone,
// velocities 16-bit. Grid adds the slot it's at; Chords the scale it's using.
enum { NG_LIVE_MEMBERS = 64, NG_LIVE_NOTES = 64, NG_LIVE_SOUNDING = 64, NG_LIVE_SCALE = 96 };

typedef struct ng_live {
    uint32_t memberCount;
    uint16_t memberPitch[NG_LIVE_MEMBERS];
    uint16_t memberVelocity[NG_LIVE_MEMBERS];
    uint32_t notesStarted;                    // ever; note k is at [k % NG_LIVE_NOTES]
    uint16_t notePitch[NG_LIVE_NOTES];
    uint16_t noteVelocity[NG_LIVE_NOTES];
    uint32_t soundingCount;
    uint16_t soundingPitch[NG_LIVE_SOUNDING];
    uint16_t soundingVelocity[NG_LIVE_SOUNDING];
    int64_t  slot;                            // Grid: the slot of its rate now; -1 otherwise
    uint32_t scaleEdo, scaleOrigin, scaleKeyStep, scaleCount;   // Chords (scaleCount 0 otherwise)
    uint8_t  scaleSteps[NG_LIVE_SCALE];       // above the key, ascending
} ng_live_t;

// A consistent copy of the latest; false if none could be taken just now (try again next tick).
bool ng_live_read(const clap_plugin_t* plugin, ng_live_t* out);

// Grid, from the current settings: each slot of one cycle of its meter, as its tier (0 .. 4);
// returns how many.
enum { NG_GRID_CYCLE = 32 };
int ng_grid_cycle(const clap_plugin_t* plugin, int tiers[NG_GRID_CYCLE]);

// Burst, from the current settings (main thread). The attractor behind phrase `key`, in its own
// space (0..1 each way; t1: which map made each point), and the phrase it makes: each point's time
// (0..1 of Duration) and offset from the root in semitones. Both return how many (Count).
enum { NG_BURST_POINTS = 64 };
int ng_burst_attractor(const clap_plugin_t* plugin, int64_t key, float* xs, float* ys, bool* t1);
int ng_burst_phrase(const clap_plugin_t* plugin, int64_t key, float* times, float* offsets);

// Chords, from the current settings (main thread): the chord on scale degree `degree` from the
// key in octave 4, as pitches in 1/512 semitone, lowest first; returns how many (0: none, as with a
// scale channel, whose scale only the playing plugin knows).
int ng_chords_preview(const clap_plugin_t* plugin, int degree, int32_t* pitches);
// The scale the current settings make (as ng_live_t's scale fields); false with a scale channel,
// whose scale is ng_live_t's.
bool ng_chords_scale(const clap_plugin_t* plugin, uint32_t* edo, uint32_t* origin, uint32_t* keyStep, uint32_t* count,
                     uint8_t steps[NG_LIVE_SCALE]);

// --- provided by the editor, called by plugin.cpp --------------------------------------------
const clap_plugin_gui_t* ng_gui_extension(void);
void                     ng_gui_on_timer(const clap_plugin_t* plugin, clap_id timer_id);

#ifdef __cplusplus
}
#endif
