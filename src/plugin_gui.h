// plugin_gui.h — the seam between the plugin (src/plugin.cpp, the raw CLAP plugin) and its editor
// (src/UI/NoteGranUI.cpp, DPF + NanoVG), as StruckSynth's. Neither side includes the other's
// internals; they meet only at these C-linkage functions.
//
// Provided by plugin.cpp, called by the editor:
//   ng_plugin_*   per-instance plumbing, and which plugin this is
//   ng_param_*    a parameter's description, value and text, and edits with gestures
//   ng_live_read  what's playing, for the editor's live views
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

// What's playing: the set's members, and the notes the plugin started lately (a ring, numbered so
// the editor can tell which are new). Pitches in 1/512 semitone, velocities 16-bit.
enum { NG_LIVE_MEMBERS = 64, NG_LIVE_NOTES = 64 };

typedef struct ng_live {
    uint32_t memberCount;
    uint16_t memberPitch[NG_LIVE_MEMBERS];
    uint16_t memberVelocity[NG_LIVE_MEMBERS];
    uint32_t notesStarted;                    // ever; note k is at [k % NG_LIVE_NOTES]
    uint16_t notePitch[NG_LIVE_NOTES];
    uint16_t noteVelocity[NG_LIVE_NOTES];
} ng_live_t;

// A consistent copy of the latest; false if none could be taken just now (try again next tick).
bool ng_live_read(const clap_plugin_t* plugin, ng_live_t* out);

// --- provided by the editor, called by plugin.cpp --------------------------------------------
const clap_plugin_gui_t* ng_gui_extension(void);
void                     ng_gui_on_timer(const clap_plugin_t* plugin, clap_id timer_id);

#ifdef __cplusplus
}
#endif
