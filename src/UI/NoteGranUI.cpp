// NoteGranUI.cpp — NoteGran's editors (PLAN.md, "Editors"), DPF + NanoVG, after StruckSynth's.
//
// One shell for the four plugins: the header (the Unlikelyware logo, "NoteGran" and the plugin's
// name in its colour), the house widget kit (Knob, Dropdown, Toggle, GroupBox, PopupMenu, copied
// from StruckSynth; Slider, NoteGran's own), and a layout per plugin: rows of groups and views,
// each row as tall as its weight says and each item as wide as its cells. A group's slots are one
// parameter's control each, or two swapped by a third (Density and Rate, by Sync), wrapping into
// rows of the group's columns; Grid's tiers are a table, a column per tier and a row per setting.
// Controls come from the parameters' own descriptions (plugin_gui.h), so their ranges, menus and
// texts are the host's. Settings unused just now are dimmed (EDO and Root unless Quantize is EDO).
//
// The views (Views.hpp, ScatterEditorView.hpp): Cloud's live keyboard; Grid's cycle over it;
// Burst's attractor editor and its next phrase; Chords' scale circle. The editor reads every value
// on its timer and shows what changed, so host automation and a loaded state need no push; when a
// value changes, the previews (plugin_gui.h's ng_grid_cycle, ng_burst_*, ng_chords_*) are asked
// again. Edits go out through DPF's editParameter / setParameterValue, which the UIExporter hands
// to the seam as gestures and values.

#include "DistrhoUI.hpp"

#include "src/DistrhoUIInternal.hpp"   // DPF's UIExporter (DPF_ROOT_DIR/distrho is on the include path)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Dropdown.hpp"
#include "EmbeddedResources.hpp"   // build-time: the logo's bytes
#include "GroupBox.hpp"
#include "Knob.hpp"
#include "Logo.hpp"
#include "PopupMenu.hpp"
#include "ScatterEditorView.hpp"
#include "Slider.hpp"
#include "Toggle.hpp"
#include "Views.hpp"
#include "plugin_gui.h"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::ChordCircle;
using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Dropdown;
using DGL_NAMESPACE::GridView;
using DGL_NAMESPACE::GroupBox;
using DGL_NAMESPACE::Knob;
using DGL_NAMESPACE::LiveView;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::PhraseView;
using DGL_NAMESPACE::PopupMenu;
using DGL_NAMESPACE::ScatterEditorView;
using DGL_NAMESPACE::Slider;
using DGL_NAMESPACE::Toggle;

namespace {

// --- the look ----------------------------------------------------------------------------------

// One family, an accent per plugin: Cloud steel blue, Chords violet, Grid amber, Burst teal, Buffer
// rose.
Color accentFor(int kind) {
    switch (kind) {
    case NG_CHORDS: return Color(170, 140, 220);
    case NG_GRID: return Color(220, 170, 90);
    case NG_BURST: return Color(90, 195, 190);
    case NG_BUFFER: return Color(220, 125, 150);
    default: return Color(130, 170, 220);
    }
}
const char* nameFor(int kind) {
    switch (kind) {
    case NG_CHORDS: return "Chords";
    case NG_GRID: return "Grid";
    case NG_BURST: return "Burst";
    case NG_BUFFER: return "Buffer";
    default: return "Cloud";
    }
}
const Color kWindow(26, 28, 33);
const Color kTitle(226, 230, 236);
const Color kLabel(176, 182, 190);
const Color kDimLabel(104, 108, 116);

// --- layouts -----------------------------------------------------------------------------------

constexpr uint32_t kNone = 0xFFFFFFFF;

// One control's place: `param`, or `alt` instead while `by` is nonzero (Free / Beats), `cells` wide.
struct Slot {
    uint32_t param;
    uint32_t alt   = kNone;
    uint32_t by    = kNone;
    int      cells = 1;
};
// Dimmed unless `when` is `equals` (or, `unless`, dimmed when it is).
struct Enable {
    uint32_t param, when;
    int      equals;
    bool     unless = false;
};
// A titled box of slots in rows of `columns` cells (0: one row). With row labels it's a table:
// the slots row by row, a column each, the columns named by the editor.
struct Group {
    Group(const char* t, std::vector<Slot> s, int c = 0, std::vector<const char*> r = {})
        : title(t), slots(std::move(s)), columns(c), rowLabels(std::move(r)) {}
    const char*              title;
    std::vector<Slot>        slots;
    int                      columns;
    std::vector<const char*> rowLabels;
};
// Row items: a group's index, or a view.
enum : int { kMainView = -1, kSideView = -2 };
struct Layout {
    int                           width = 900, height = 560;   // the design size, in points
    std::vector<Group>            groups;
    std::vector<Enable>           enables;
    std::vector<std::vector<int>> rows;
    std::vector<int>              rowWeights;                  // each row's share of the height
    int                           viewCells[2] = {4, 4};       // the main and side views' widths
};

// The parameter ids (plugin.cpp's enums): appended, never moved.
enum : uint32_t {
    cSync, cDensity, cRate, cLengthMs, cLengthBeats, cOnsetRandom, cPick, cWeight, cOctaves, cTranspose,
    cScatter, cQuantize, cEdo, cRoot, cLevel, cVelSpread, cLenSpread, cVoices, cWhenFull, cDry, cHoldOver,
    cSetChannel, cTriggerChannel, cLatch, cSeed,
};
enum : uint32_t {
    hScale, hKey, hMap, hEdo, hOrigin, hInput, hScaleChannel, hSize, hStack, hInversion, hVoicing, hOctave,
    hOffScale, hLevel, hVoices, hWhenFull,
};
enum : uint32_t {
    gRate, gMeter, gDensity, gSmear, gScatter, gLength, gLenSpread, gVelSpread, gTranspose, gQuantize, gEdo,
    gRoot, gVoices, gWhenFull, gDry, gHoldOver, gSetChannel, gTriggerChannel, gLatch, gSeed,
    gTiers,   // then, per tier: Probability, Level, Offset, Chaos, Pick
};
constexpr uint32_t kTierParams = 5, kTierCount = 5;
enum : uint32_t {
    bSync, bDuration, bDurationBeats, bCount, bRange, bRelease, bVary, bProbability,
    bT1X, bT1Y, bT1Contraction, bT1Rotation, bT2X, bT2Y, bT2Contraction, bT2Rotation,
    bLength, bLengthBeats, bLevel, bDecay, bVelSpread, bLenSpread, bTranspose, bQuantize, bEdo, bRoot,
    bVoices, bWhenFull, bDry, bHoldOver, bSetChannel, bTriggerChannel, bLatch, bSeed,
};
enum : uint32_t {
    uSync, uWindow, uWindowBeats, uFreeze, uPosition, uScan, uSpray, uDensity, uRate, uOnsetRandom, uGrain,
    uLength, uLengthBeats, uLengthFrom, uOctaves, uTranspose, uScatter, uQuantize, uEdo, uRoot, uLevel, uVelSpread,
    uLenSpread, uGate, uVoices, uWhenFull, uDry, uHoldOver, uSetChannel, uTriggerChannel, uLatch, uSeed,
};

Layout cloudLayout() {
    Layout l;
    l.groups = {
        {"Timing", {{cSync}, {cDensity, cRate, cSync}, {cLengthMs, cLengthBeats, cSync}, {cOnsetRandom}}},
        {"Pick", {{cPick}, {cWeight}}},
        {"Pitch", {{cOctaves}, {cTranspose}, {cScatter}, {cQuantize}, {cEdo}, {cRoot}}},
        {"Dynamics", {{cLevel}, {cVelSpread}, {cLenSpread}}},
        {"Input", {{cVoices}, {cWhenFull}, {cDry}, {cHoldOver}, {cSetChannel}, {cTriggerChannel}, {cLatch}, {cSeed}}},
    };
    l.enables    = {{cWeight, cPick, 0}, {cEdo, cQuantize, 1}, {cRoot, cQuantize, 1}};
    l.rows       = {{kMainView, 0}, {1, 2}, {3, 4}};
    l.rowWeights = {1, 1, 1};
    return l;
}

// Grid: its cycle over the live keyboard beside the clock; the tiers' table; pitch and input.
Layout gridLayout() {
    Layout l;
    l.width = 980, l.height = 700;
    Group tiers{"Tiers", {}, int{kTierCount}, {"Probability", "Level", "Offset", "Chaos", "Pick"}};
    for (uint32_t k = 0; k < kTierParams; ++k)
        for (uint32_t t = 0; t < kTierCount; ++t) tiers.slots.push_back({gTiers + kTierParams * t + k});
    l.groups = {
        {"Clock", {{gRate}, {gMeter}, {gDensity}, {gSmear}, {gLength}, {gLenSpread}, {gVelSpread}}, 4},
        tiers,
        {"Pitch", {{gScatter}, {gTranspose}, {gQuantize}, {gEdo}, {gRoot}}},
        {"Input", {{gVoices}, {gWhenFull}, {gDry}, {gHoldOver}, {gSetChannel}, {gTriggerChannel}, {gLatch}, {gSeed}}},
    };
    l.enables      = {{gEdo, gQuantize, 1}, {gRoot, gQuantize, 1}};
    l.rows         = {{kMainView, 0}, {1}, {2, 3}};
    l.rowWeights   = {240, 230, 150};
    l.viewCells[0] = 6;
    return l;
}

// Burst: the attractor editor beside the phrase it makes; the phrase's and the grains' settings;
// pitch and input.
Layout burstLayout() {
    Layout l;
    l.width = 960, l.height = 660;
    l.groups = {
        {"Phrase", {{bSync}, {bDuration, bDurationBeats, bSync}, {bCount}, {bRange}, {bRelease}, {bVary}, {bProbability}}},
        {"Grain", {{bLength, bLengthBeats, bSync}, {bLevel}, {bDecay}, {bVelSpread}, {bLenSpread}}},
        {"Pitch", {{bTranspose}, {bQuantize}, {bEdo}, {bRoot}}},
        {"Input", {{bVoices}, {bWhenFull}, {bDry}, {bHoldOver}, {bSetChannel}, {bTriggerChannel}, {bLatch}, {bSeed}}},
    };
    l.enables    = {{bEdo, bQuantize, 1}, {bRoot, bQuantize, 1}};
    l.rows       = {{kMainView, kSideView}, {0, 1}, {2, 3}};
    l.rowWeights = {300, 140, 140};
    l.viewCells[0] = l.viewCells[1] = 6;
    return l;
}

// Buffer: the live keyboard beside the capture's settings; the grains' timing, pitch and input.
Layout bufferLayout() {
    Layout l;
    l.width = 940, l.height = 600;
    l.groups = {
        {"Capture", {{uSync}, {uWindow, uWindowBeats, uSync}, {uFreeze}, {uPosition}, {uScan}, {uSpray}}, 3},
        {"Grain", {{uGrain}, {uDensity, uRate, uSync}, {uOnsetRandom}, {uLength, uLengthBeats, uSync}, {uLengthFrom}, {uGate}}},
        {"Pitch", {{uOctaves}, {uTranspose}, {uScatter}, {uQuantize}, {uEdo}, {uRoot}}},
        {"Dynamics", {{uLevel}, {uVelSpread}, {uLenSpread}}},
        {"Input", {{uVoices}, {uWhenFull}, {uDry}, {uHoldOver}, {uSetChannel}, {uTriggerChannel}, {uLatch}, {uSeed}}},
    };
    // Length from is Note's; Density and Onset random mean nothing with a trigger channel.
    l.enables    = {{uEdo, uQuantize, 1}, {uRoot, uQuantize, 1}, {uLengthFrom, uGrain, 0},
                    {uDensity, uTriggerChannel, 0}, {uRate, uTriggerChannel, 0}, {uOnsetRandom, uTriggerChannel, 0}};
    l.rows       = {{kMainView, 0}, {1, 2}, {3, 4}};
    l.rowWeights = {1, 1, 1};
    return l;
}

// Chords: the scale's circle beside the chord's settings; the scale's and the output's.
Layout chordsLayout() {
    Layout l;
    l.width = 920, l.height = 600;
    l.groups = {
        {"Chord", {{hSize}, {hStack}, {hInversion}, {hVoicing}, {hOctave}}, 3},
        {"Scale", {{hScale, kNone, kNone, 2}, {hKey}, {hMap}, {hEdo}, {hOrigin}, {hInput}, {hScaleChannel}}},
        {"Output", {{hOffScale}, {hLevel}, {hVoices}, {hWhenFull}}},
    };
    // Map, Origin and Input only matter off 12-EDO; Scale and Key only without a scale channel.
    l.enables = {{hMap, hEdo, 12, true}, {hOrigin, hEdo, 12, true}, {hInput, hEdo, 12, true},
                 {hScale, hScaleChannel, 0}, {hKey, hScaleChannel, 0}};
    l.rows         = {{kMainView, 0}, {1, 2}};
    l.rowWeights   = {5, 3};
    l.viewCells[0] = 9;
    return l;
}

Layout layoutFor(int kind) {
    switch (kind) {
    case NG_GRID: return gridLayout();
    case NG_BURST: return burstLayout();
    case NG_CHORDS: return chordsLayout();
    case NG_BUFFER: return bufferLayout();
    default: return cloudLayout();
    }
}

int kindOf(const clap_plugin_t* plugin) { return plugin != nullptr ? ng_plugin_kind(plugin) : NG_CLOUD; }

// --- the tiers' table --------------------------------------------------------------------------

// A group laid out as a table: its rows named down the left, its columns along the top.
class TableBox : public GroupBox {
public:
    explicit TableBox(Widget* parent) : GroupBox(parent) { fHasFont = loadSharedResources(); }

    void setRowLabels(std::vector<const char*> labels) { fRows = std::move(labels); }
    void setColumnLabels(std::vector<std::string> labels, std::vector<bool> used) {
        fColumns = std::move(labels);
        fUsed    = std::move(used);
        repaint();
    }
    void addCell(SubWidget* w) { fCells.push_back(w); }
    void setScale(float s) {
        fS = s;
        setUiScale(s);
    }

    void layoutTable() {
        int x, y, w, h;
        contentBounds(x, y, w, h);
        const int cols = std::max<int>(1, static_cast<int>(fColumns.size()));
        const int rows = std::max<int>(1, static_cast<int>(fRows.size()));
        const int lw = labelW(), hh = headerH();
        for (std::size_t i = 0; i < fCells.size(); ++i) {
            const int r = static_cast<int>(i) / cols, c = static_cast<int>(i) % cols;
            const int x0 = lw + (w - lw) * c / cols, x1 = lw + (w - lw) * (c + 1) / cols;
            const int y0 = hh + (h - hh) * r / rows, y1 = hh + (h - hh) * (r + 1) / rows;
            fCells[i]->setAbsolutePos(x + x0, y + y0);
            fCells[i]->setSize(static_cast<uint>(x1 - x0), static_cast<uint>(y1 - y0));
        }
    }

protected:
    void onNanoDisplay() override {
        GroupBox::onNanoDisplay();
        if (!fHasFont) return;
        int x, y, w, h;
        contentBounds(x, y, w, h);
        x -= getAbsoluteX(), y -= getAbsoluteY();
        const int cols = std::max<int>(1, static_cast<int>(fColumns.size()));
        const int rows = std::max<int>(1, static_cast<int>(fRows.size()));
        const int lw = labelW(), hh = headerH();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(12.0f * fS);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        for (int c = 0; c < static_cast<int>(fColumns.size()); ++c) {
            const bool used = c >= static_cast<int>(fUsed.size()) || fUsed[static_cast<std::size_t>(c)];
            fillColor(used ? kLabel : kDimLabel);
            text(x + lw + (w - lw) * (c + 0.5f) / cols, y + hh * 0.5f, fColumns[static_cast<std::size_t>(c)].c_str(), nullptr);
        }
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kLabel);
        for (int r = 0; r < static_cast<int>(fRows.size()); ++r)
            text(x + 2.0f * fS, y + hh + (h - hh) * (r + 0.5f) / rows, fRows[static_cast<std::size_t>(r)], nullptr);
    }

private:
    int labelW() const { return static_cast<int>(84 * fS); }
    int headerH() const { return static_cast<int>(18 * fS); }

    std::vector<const char*> fRows;
    std::vector<std::string> fColumns;
    std::vector<bool>        fUsed;
    std::vector<SubWidget*>  fCells;
    bool                     fHasFont = false;
    float                    fS       = 1.0f;
};

// Grid's tiers, named from its meter: Straight's by note value; a grouped meter's by group, then
// its other beats and off-beats (a meter of two groups leaves the last tier unused).
void tierNames(bool straight, const int* tiers, int n, std::vector<std::string>& names, std::vector<bool>& used) {
    names.assign(kTierCount, "");
    used.assign(kTierCount, false);
    if (straight) {
        names = {"Bar", "Half", "Quarters", "Eighths", "Sixteenths"};
        used.assign(kTierCount, true);
        return;
    }
    int top = 0;
    for (int i = 0; i < n; ++i) top = std::max(top, tiers[i]);
    const int groups = std::max(1, top - 1);
    for (int t = 0; t < static_cast<int>(kTierCount); ++t) {
        used[static_cast<std::size_t>(t)] = t <= top;
        if (t == 0) names[0] = "Bar";
        else if (t < groups) names[static_cast<std::size_t>(t)] = "Group " + std::to_string(t + 1);
        else if (t == groups) names[static_cast<std::size_t>(t)] = "Beats";
        else if (t == groups + 1) names[static_cast<std::size_t>(t)] = "Off-beats";
        else names[static_cast<std::size_t>(t)] = "(unused)";
    }
}

// A pitch's name: its nearest key, and the cents off it when it's not on one.
std::string noteName(float semitones) {
    static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int k     = static_cast<int>(std::lround(semitones));
    const int cents = static_cast<int>(std::lround((semitones - static_cast<float>(k)) * 100.0f));
    char      text[24];
    if (cents != 0) std::snprintf(text, sizeof(text), "%s%d%+d\xC2\xA2", kNames[((k % 12) + 12) % 12], k / 12 - 1, cents);
    else std::snprintf(text, sizeof(text), "%s%d", kNames[((k % 12) + 12) % 12], k / 12 - 1);
    return text;
}

double secondsNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The plugin the next editor is for: set by gui_set_parent just before it builds the UIExporter,
// read by createUI() (both on the main thread).
const clap_plugin_t* gCreatingFor = nullptr;

} // namespace

// --- the editor --------------------------------------------------------------------------------

class NoteGranUI : public UI, public Knob::Callback, public Dropdown::Callback, public Toggle::Callback,
                   public Slider::Callback, public PopupMenu::Callback, public ScatterEditorView::Callback,
                   public ChordCircle::Callback {
public:
    NoteGranUI()
        : UI(static_cast<uint>(layoutFor(kindOf(gCreatingFor)).width),
             static_cast<uint>(layoutFor(kindOf(gCreatingFor)).height)) {
        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        if (fPlugin == nullptr) fPlugin = gCreatingFor;
        fKind   = kindOf(fPlugin);
        fAccent = accentFor(fKind);
        fLayout = layoutFor(fKind);
        // From 0.8× the design, keeping its shape.
        setGeometryConstraints(static_cast<uint>(fLayout.width * 4 / 5), static_cast<uint>(fLayout.height * 4 / 5), true,
                               false);
        fHasFont = loadSharedResources();

        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);
        switch (fKind) {
        case NG_GRID: fLive = std::make_unique<GridView>(this); break;
        case NG_BURST:
            fScatter = std::make_unique<ScatterEditorView>(this, this);
            fLive    = std::make_unique<PhraseView>(this);
            break;
        case NG_CHORDS:
            fCircle = std::make_unique<ChordCircle>(this, this);
            fCircle->setAccent(fAccent);
            break;
        default: fLive = std::make_unique<LiveView>(this); break;
        }
        if (fLive) fLive->setAccent(fAccent);

        const uint32_t count = fPlugin != nullptr ? ng_param_count(fPlugin) : 0;
        fControls.resize(count);
        fValues.assign(count, 0.0);
        for (const Group& g : fLayout.groups) {
            if (!g.rowLabels.empty()) {
                auto table = std::make_unique<TableBox>(this);
                table->setTitle(g.title);
                table->setAccentColor(fAccent);
                table->setRowLabels(g.rowLabels);
                for (const Slot& s : g.slots)
                    if (SubWidget* w = makeControl(table.get(), s.param, true)) table->addCell(w);
                fTable = table.get();
                fGroups.push_back(std::move(table));
                continue;
            }
            auto box = std::make_unique<GroupBox>(this);
            box->setTitle(g.title);
            box->setAccentColor(fAccent);
            box->setColumns(g.columns);
            for (const Slot& s : g.slots) {
                if (SubWidget* w = makeControl(box.get(), s.param, false)) box->addControl(w, s.cells);
                if (s.alt != kNone) makeControl(box.get(), s.alt, false);
            }
            fGroups.push_back(std::move(box));
        }
        fMenu = std::make_unique<PopupMenu>(this, this);   // last, so it draws on top

        refresh(true);
        updatePreviews();
        layout();
    }

    // A control is a child of its group's box, and a DGL child unlinks itself from its parent as
    // it goes: so the controls go first, then the boxes (members alone would free the boxes first).
    ~NoteGranUI() override {
        fMenu.reset();
        fControls.clear();
        fGroups.clear();
    }

protected:
    // Values are read on the timer (uiIdle); this only keeps a pushed value from being stale.
    void parameterChanged(uint32_t index, float value) override {
        if (index < fControls.size()) show(index, value);
    }

    // --- controls → the plugin ---
    void knobDragStarted(Knob* k) override { dragStarted(k->getId()); }
    void knobDragFinished(Knob* k) override { dragFinished(k->getId()); }
    void knobValueChanged(Knob* k, float v) override { dragged(k->getId(), v); }
    void sliderDragStarted(Slider* s) override { dragStarted(s->getId()); }
    void sliderDragFinished(Slider* s) override { dragFinished(s->getId()); }
    void sliderValueChanged(Slider* s, float v) override { dragged(s->getId(), v); }
    void toggleChanged(Toggle* t, bool on) override { setFromUi(t->getId(), on ? 1.0f : 0.0f); }
    void dropdownClicked(Dropdown* d) override {
        int x, y, w, h;
        d->boxRect(x, y, w, h);
        fMenu->openFor(d->getId(), d->choices(), d->index(), x, y + h, y, w, static_cast<int>(getHeight()));
    }
    void menuSelected(PopupMenu* menu, int index) override {
        const uint32_t id = menu->tag();
        if (id >= fControls.size()) return;
        setFromUi(id, static_cast<float>(fControls[id].info.min + index));
    }
    // A click outside the window never reaches us: losing focus closes an open menu.
    void uiFocus(bool focus, DGL_NAMESPACE::CrossingMode mode) override {
        UI::uiFocus(focus, mode);
        if (!focus && fMenu != nullptr) fMenu->dismiss();
    }

    // Burst's attractor editor: a drag is one gesture per field it moves.
    void scatterDragStarted(ScatterEditorView::Field a, ScatterEditorView::Field b) override {
        editParameter(bT1X + static_cast<uint32_t>(a), true);
        editParameter(bT1X + static_cast<uint32_t>(b), true);
    }
    void scatterFieldChanged(ScatterEditorView::Field field, float value) override {
        setParameterValue(bT1X + static_cast<uint32_t>(field), value);
    }
    void scatterDragFinished(ScatterEditorView::Field a, ScatterEditorView::Field b) override {
        editParameter(bT1X + static_cast<uint32_t>(a), false);
        editParameter(bT1X + static_cast<uint32_t>(b), false);
    }

    // Chords' circle: a degree clicked shows its chord (again to hide it).
    void degreeClicked(int degree) override {
        fDegree = degree;
        updateChordPreview();
    }

    // The timer: values, the previews when they changed, and what's playing.
    void uiIdle() override {
        if (!fInitialSizeNudged) {   // some Linux hosts apply the first size only once events run
            fInitialSizeNudged = true;
            setSize(getWidth(), getHeight());
        }
        if (refresh(false)) updatePreviews();
        ng_live_t live;
        if (fPlugin == nullptr || !ng_live_read(fPlugin, &live)) return;
        if (fLive) fLive->update(live, secondsNow());
        if (fCircle) {
            std::vector<float> sounding;
            for (uint32_t i = 0; i < live.soundingCount; ++i) sounding.push_back(live.soundingPitch[i] / 512.0f);
            std::sort(sounding.begin(), sounding.end());
            if (sounding != fSounding) {
                fSounding = sounding;
                fCircle->setSounding(sounding);
            }
            // With a scale channel only the playing plugin knows the scale.
            if (!fMenuScale && live.scaleCount > 0) {
                std::vector<int> steps(live.scaleSteps, live.scaleSteps + live.scaleCount);
                fCircle->setScale(static_cast<int>(live.scaleEdo), static_cast<int>(live.scaleOrigin),
                                  static_cast<int>(live.scaleKeyStep), steps);
            }
        }
    }

    void onNanoDisplay() override {
        const float w = getWidth(), h = getHeight();
        beginPath();
        rect(0, 0, w, h);
        fillColor(kWindow);
        fill();
        if (!fHasFont) return;
        const float s = scale();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fontSize(22 * s);
        fillColor(kTitle);
        const float after = text(fTitleX, 22 * s, "NoteGran ", nullptr);   // where the pen ends
        fillColor(fAccent);
        text(after, 22 * s, nameFor(fKind), nullptr);
    }
    void onResize(const ResizeEvent& ev) override {
        UI::onResize(ev);
        layout();
    }

private:
    struct Control {
        ng_param_info_t           info{};
        bool                      present  = false;
        bool                      dragging = false;
        float                     shown    = 0.0f;
        std::unique_ptr<Knob>     knob;
        std::unique_ptr<Slider>   slider;
        std::unique_ptr<Dropdown> dropdown;
        std::unique_ptr<Toggle>   toggle;
        SubWidget* widget() const {
            if (knob) return knob.get();
            if (slider) return slider.get();
            if (dropdown) return dropdown.get();
            return toggle.get();
        }
        void setEnabled(bool on) {
            if (knob) knob->setEnabled(on);
            if (slider) slider->setEnabled(on);
            if (dropdown) dropdown->setEnabled(on);
        }
    };

    float scale() const { return getWidth() / static_cast<float>(fLayout.width); }

    // An Off/On menu is a toggle, any other menu a dropdown, a number or a step a knob; in a
    // table, a number is a slider and a menu a dropdown without a caption.
    SubWidget* makeControl(Widget* parent, uint32_t id, bool inTable) {
        if (id >= fControls.size()) return nullptr;
        Control& c = fControls[id];
        if (!ng_param_info(fPlugin, id, &c.info)) return nullptr;
        c.present = true;
        const int  steps = static_cast<int>(c.info.max - c.info.min) + 1;
        const bool offOn = c.info.kind == NG_PARAM_MENU && steps == 2 && std::strcmp(c.info.labels[0], "Off") == 0;
        const clap_plugin_t* plugin = fPlugin;
        auto format = [plugin, id](float v) {
            char text[64];
            ng_param_text(plugin, id, v, text, sizeof(text));
            return std::string(text);
        };
        if (offOn && !inTable) {
            c.toggle = std::make_unique<Toggle>(parent, this);
            c.toggle->setId(id);
            c.toggle->setLabel(c.info.name);
            c.toggle->setAccentColor(fAccent);
        } else if (c.info.kind == NG_PARAM_MENU) {
            c.dropdown = std::make_unique<Dropdown>(parent, this);
            c.dropdown->setId(id);
            if (inTable) c.dropdown->setCompact(true);
            else c.dropdown->setLabel(c.info.name);
            std::vector<std::string> choices;
            for (int i = 0; i < steps; ++i) choices.emplace_back(c.info.labels[i]);
            c.dropdown->setChoices(choices);
        } else if (inTable) {
            c.slider = std::make_unique<Slider>(parent, this);
            c.slider->setId(id);
            c.slider->setAccentColor(fAccent);
            c.slider->configure(static_cast<float>(c.info.min), static_cast<float>(c.info.max),
                                static_cast<float>(c.info.def), c.info.kind == NG_PARAM_STEPPED);
            c.slider->setFormatter(format);
        } else {
            c.knob = std::make_unique<Knob>(parent, this);
            c.knob->setId(id);
            c.knob->setLabel(c.info.name);
            c.knob->setAccentColor(fAccent);
            // A wide range from above 0 turns logarithmically (Density, the lengths).
            const bool log = c.info.min > 0.0 && c.info.max / c.info.min >= 100.0;
            c.knob->configure(static_cast<float>(c.info.min), static_cast<float>(c.info.max), log,
                              static_cast<float>(c.info.def));
            c.knob->setInteger(c.info.kind == NG_PARAM_STEPPED);
            c.knob->setFormatter(format);
        }
        return c.widget();
    }

    void dragStarted(uint32_t id) {
        fControls[id].dragging = true;
        editParameter(id, true);
    }
    void dragFinished(uint32_t id) {
        editParameter(id, false);
        fControls[id].dragging = false;
    }
    void dragged(uint32_t id, float v) {
        setParameterValue(id, v);
        fControls[id].shown = v;
        updateState();
    }

    void setFromUi(uint32_t id, float v) {
        editParameter(id, true);
        setParameterValue(id, v);
        editParameter(id, false);
        show(id, v);
    }

    void show(uint32_t id, float v) {
        Control& c = fControls[id];
        c.shown    = v;
        if (c.knob) c.knob->setRealValue(v, false), c.knob->repaint();
        if (c.slider) c.slider->setValue(v);
        if (c.dropdown) c.dropdown->setIndex(static_cast<int>(std::lround(v - c.info.min)));
        if (c.toggle) c.toggle->setState(v >= 0.5f);
        updateState();
    }

    // Every value from the plugin; what changed (or everything) is shown. A control being dragged
    // keeps what the drag says. Returns whether any value changed.
    bool refresh(bool all) {
        if (fPlugin == nullptr) return false;
        bool changed = false;
        for (uint32_t i = 0; i < fControls.size(); ++i) {
            const double v = ng_param_value(fPlugin, i);
            if (v != fValues[i]) changed = true;
            fValues[i] = v;
            Control& c = fControls[i];
            if (!c.present || c.dragging) continue;
            if (all || static_cast<float>(v) != c.shown) show(i, static_cast<float>(v));
        }
        return changed || all;
    }

    // Swapped slots and dimmed controls, from the values shown.
    void updateState() {
        for (const Group& g : fLayout.groups)
            for (const Slot& s : g.slots) {
                if (s.alt == kNone || s.by >= fControls.size()) continue;
                const bool useAlt = fControls[s.by].shown >= 0.5f;
                if (fControls[s.param].present) fControls[s.param].widget()->setVisible(!useAlt);
                if (fControls[s.alt].present) fControls[s.alt].widget()->setVisible(useAlt);
            }
        for (const Enable& e : fLayout.enables) {
            if (e.param >= fControls.size() || e.when >= fControls.size()) continue;
            const bool is = std::lround(fControls[e.when].shown) == e.equals;
            fControls[e.param].setEnabled(e.unless ? !is : is);
        }
        // Grid: a tier its meter doesn't use.
        if (fKind == NG_GRID)
            for (uint32_t t = 0; t < kTierCount; ++t)
                for (uint32_t k = 0; k < kTierParams; ++k) {
                    const uint32_t id = gTiers + kTierParams * t + k;
                    if (id < fControls.size()) fControls[id].setEnabled(fTierUsed[t]);
                }
    }

    // What the settings make, asked again whenever a value changed.
    void updatePreviews() {
        if (fPlugin == nullptr) return;
        auto value = [&](uint32_t id) { return id < fValues.size() ? fValues[id] : 0.0; };
        switch (fKind) {
        case NG_GRID: {
            int       tiers[NG_GRID_CYCLE];
            const int n = ng_grid_cycle(fPlugin, tiers);
            float     probability[kTierCount];
            for (uint32_t t = 0; t < kTierCount; ++t) probability[t] = static_cast<float>(value(gTiers + kTierParams * t));
            static_cast<GridView*>(fLive.get())->setCycle(std::vector<int>(tiers, tiers + n), probability);
            std::vector<std::string> names;
            std::vector<bool>        used;
            tierNames(std::lround(value(gMeter)) == 0, tiers, n, names, used);
            for (uint32_t t = 0; t < kTierCount; ++t) fTierUsed[t] = used[t];
            if (fTable) fTable->setColumnLabels(names, used);
            updateState();
            break;
        }
        case NG_BURST: {
            float transforms[8];
            for (int i = 0; i < 8; ++i) transforms[i] = static_cast<float>(value(bT1X + static_cast<uint32_t>(i)));
            fScatter->setTransforms(transforms);
            // Each phrase has its own attractor with Vary; without it, every phrase is the first's.
            std::vector<ScatterEditorView::CloudPoint> cloud;
            float xs[NG_BURST_POINTS], ys[NG_BURST_POINTS];
            bool  t1[NG_BURST_POINTS];
            const int phrases = std::lround(value(bVary)) == 1 ? 16 : 1;
            for (int key = phrases - 1; key >= 0; --key) {   // the first last, on top
                const int n = ng_burst_attractor(fPlugin, key, xs, ys, t1);
                for (int i = 0; i < n; ++i) cloud.push_back({xs[i], ys[i], t1[i], key == 0});
            }
            fScatter->setCloud(std::move(cloud));
            float     times[NG_BURST_POINTS], offsets[NG_BURST_POINTS];
            const int n    = ng_burst_phrase(fPlugin, 0, times, offsets);
            const bool beats = std::lround(value(bSync)) == 1;
            char       duration[64];
            ng_param_text(fPlugin, beats ? bDurationBeats : bDuration, value(beats ? bDurationBeats : bDuration), duration,
                          sizeof(duration));
            static_cast<PhraseView*>(fLive.get())
                ->setPhrase(times, offsets, n, static_cast<float>(value(bRange)), static_cast<float>(value(bDecay)),
                            std::lround(value(bRelease)) == 2, duration);
            break;
        }
        case NG_CHORDS: {
            uint32_t edo = 12, origin = 0, keyStep = 0, count = 0;
            uint8_t  steps[NG_LIVE_SCALE];
            fMenuScale = ng_chords_scale(fPlugin, &edo, &origin, &keyStep, &count, steps);
            if (fMenuScale)
                fCircle->setScale(static_cast<int>(edo), static_cast<int>(origin), static_cast<int>(keyStep),
                                  std::vector<int>(steps, steps + count));
            updateChordPreview();
            break;
        }
        default: break;
        }
    }

    // Chords' circle: the chord on the degree clicked, and what the settings say in words.
    void updateChordPreview() {
        if (fCircle == nullptr) return;
        auto text = [&](uint32_t id) {
            char out[64];
            ng_param_text(fPlugin, id, fValues[id], out, sizeof(out));
            return std::string(out);
        };
        std::vector<float> chord;
        if (fDegree >= 0) {
            int32_t   pitches[16];
            const int n = ng_chords_preview(fPlugin, fDegree, pitches);
            for (int i = 0; i < n; ++i) chord.push_back(static_cast<float>(pitches[i]) / 512.0f);
        }
        fCircle->setPreview(chord.empty() ? -1 : fDegree, chord);

        std::vector<std::string> lines;
        const int channel = static_cast<int>(std::lround(fValues[hScaleChannel]));
        const int edo     = static_cast<int>(std::lround(fValues[hEdo]));
        if (channel > 0) lines.push_back("The scale on channel " + std::to_string(channel));
        else lines.push_back(text(hKey) + " " + text(hScale));
        if (edo == 12) lines.push_back("12-EDO");
        else lines.push_back(std::to_string(edo) + "-EDO, mapped " + text(hMap) + ", from " + text(hOrigin));
        lines.push_back(text(hSize) + " notes in " + text(hStack) + ", " + text(hVoicing) + ", " + text(hInversion) +
                        (fValues[hInversion] < 0.5 ? " position" : " inversion"));
        if (!chord.empty()) {
            std::string names;
            for (float p : chord) names += (names.empty() ? "" : "  ") + noteName(p);
            lines.push_back(names);
        }
        fCircle->setLines(lines);
    }

    void layout() {
        const int   W = static_cast<int>(getWidth()), H = static_cast<int>(getHeight());
        const float s = scale();
        const int   M = static_cast<int>(14 * s), gap = static_cast<int>(10 * s), hdr = static_cast<int>(46 * s);

        fLogo->setHeightPreservingAspect(22 * s);
        fLogo->setAbsolutePos(M, static_cast<int>(11 * s));
        fTitleX = static_cast<float>(M + static_cast<int>(fLogo->getWidth()) + static_cast<int>(12 * s));
        fMenu->setUiScale(s);
        if (fLive) fLive->setUiScale(s);
        if (fScatter) fScatter->setUiScale(s);
        if (fCircle) fCircle->setUiScale(s);

        // Rows by weight, items by cells; both cumulative, so rounding never drifts.
        const int rows   = static_cast<int>(fLayout.rows.size());
        const int height = H - hdr - M - (rows - 1) * gap;
        int       weights = 0;
        for (int w : fLayout.rowWeights) weights += w;
        int above = 0;
        for (int r = 0; r < rows; ++r) {
            const std::vector<int>& row = fLayout.rows[static_cast<std::size_t>(r)];
            const int top = height * above / std::max(weights, 1);
            above += fLayout.rowWeights[static_cast<std::size_t>(r)];
            const int bottom = height * above / std::max(weights, 1);
            const int y      = hdr + top + r * gap;
            int       total  = 0;
            for (int g : row) total += cellsOf(g);
            const int rowW = W - 2 * M - (static_cast<int>(row.size()) - 1) * gap;
            int       x = M, used = 0, done = 0;
            for (int g : row) {
                used += cellsOf(g);
                const int right = rowW * used / total;
                SubWidget* w    = itemWidget(g);
                w->setAbsolutePos(x, y);
                w->setSize(static_cast<uint>(right - done), static_cast<uint>(bottom - top));
                x += right - done + gap;
                done = right;
            }
        }

        for (auto& g : fGroups) {
            if (g.get() == fTable) {
                fTable->setScale(s);
                fTable->layoutTable();
            } else {
                g->setUiScale(s);
                g->relayout();
            }
        }
        // Knobs as large as their cells allow, to the design's size.
        for (Control& c : fControls) {
            if (c.knob) {
                const float d = std::min({62.0f * s, static_cast<float>(c.knob->getHeight()) - 38.0f * s,
                                          static_cast<float>(c.knob->getWidth()) - 6.0f});
                c.knob->setDiameter(std::max(d, 16.0f), s);
            }
            if (c.slider) c.slider->setUiScale(s);
            if (c.dropdown) c.dropdown->setUiScale(s);
            if (c.toggle) c.toggle->setUiScale(s);
        }
        // An alternative sits where its primary does.
        for (const Group& g : fLayout.groups)
            for (const Slot& sl : g.slots) {
                if (sl.alt == kNone || !fControls[sl.param].present || !fControls[sl.alt].present) continue;
                SubWidget* p = fControls[sl.param].widget();
                SubWidget* a = fControls[sl.alt].widget();
                a->setSize(p->getWidth(), p->getHeight());
                a->setAbsolutePos(p->getAbsoluteX(), p->getAbsoluteY());
            }
        updateState();
    }

    int cellsOf(int item) const {
        if (item == kMainView) return fLayout.viewCells[0];
        if (item == kSideView) return fLayout.viewCells[1];
        return fGroups[static_cast<std::size_t>(item)]->widthInCells();
    }

    SubWidget* itemWidget(int item) const {
        if (item >= 0) return fGroups[static_cast<std::size_t>(item)].get();
        if (item == kSideView || fKind == NG_CLOUD || fKind == NG_GRID || fKind == NG_BUFFER) return fLive.get();
        if (fKind == NG_BURST) return fScatter.get();
        return fCircle.get();
    }

    const clap_plugin_t*                   fPlugin = nullptr;
    int                                    fKind   = NG_CLOUD;
    Color                                  fAccent;
    Layout                                 fLayout;
    std::vector<Control>                   fControls;
    std::vector<double>                    fValues;   // every parameter's, as last read
    std::vector<std::unique_ptr<GroupBox>> fGroups;
    TableBox*                              fTable = nullptr;   // Grid's tiers, in fGroups
    std::unique_ptr<LiveView>              fLive;               // Cloud's, Grid's cycle, Burst's phrase
    std::unique_ptr<ScatterEditorView>     fScatter;            // Burst
    std::unique_ptr<ChordCircle>           fCircle;             // Chords
    std::unique_ptr<Logo>                  fLogo;
    std::unique_ptr<PopupMenu>             fMenu;
    std::array<bool, kTierCount>           fTierUsed{true, true, true, true, true};
    bool                                   fMenuScale = true;   // Chords: the scale is the menu's
    int                                    fDegree    = -1;     // Chords: the degree clicked
    std::vector<float>                     fSounding;
    bool                                   fHasFont           = false;
    bool                                   fInitialSizeNudged = false;
    float                                  fTitleX            = 16.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoteGranUI)
};

UI* createUI() { return new NoteGranUI(); }

// ================================================================================================
// CLAP `gui` extension, as StruckSynth's: gui_create() only makes a holder, since UIExporter can't
// be built until the host gives a parent window in set_parent().
// ================================================================================================

namespace {

struct GuiState {
    const clap_plugin_t*        plugin = nullptr;
    std::unique_ptr<UIExporter> exporter;
    uintptr_t                   parentWindow = 0;
    // The size in the host's units (points on macOS, pixels on X11): the source of truth, from
    // which the UIExporter's pixel size is derived at the live scale.
    uint32_t width        = DISTRHO_UI_DEFAULT_WIDTH;    // the plugin's design size, from gui_create
    uint32_t height       = DISTRHO_UI_DEFAULT_HEIGHT;
    double   appliedScale = 0.0;
    bool     timerRegistered = false;
    clap_id  timerId         = 0;
};

#ifdef __APPLE__
extern "C" double ng_gui_backing_scale(uintptr_t nsView);   // BackingScale_mac.mm
#endif

// Host units → pixels: on macOS the backing scale of the window the editor is in now (DPF caches
// its own once, maybe from the wrong screen); on X11 the sizes are pixels already.
double liveScale(const GuiState* gui) {
#ifdef __APPLE__
    return ng_gui_backing_scale(gui->parentWindow);
#else
    (void)gui;
    return 1.0;
#endif
}

void applySize(GuiState* gui) {
    if (!gui->exporter) return;
    const double scale = liveScale(gui);
    gui->appliedScale  = scale;
    gui->exporter->setWindowSizeFromHost(uint(std::lround(gui->width * scale)), uint(std::lround(gui->height * scale)));
}

#ifdef __APPLE__
constexpr const char* kWindowApi = CLAP_WINDOW_API_COCOA;
#else
constexpr const char* kWindowApi = CLAP_WINDOW_API_X11;
#endif

const clap_host_timer_support_t* hostTimer(const clap_plugin_t* plugin) {
    const clap_host_t* host = ng_plugin_host(plugin);
    return static_cast<const clap_host_timer_support_t*>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
}

bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool isFloating) {
    return !isFloating && std::strcmp(api, kWindowApi) == 0;
}

bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* isFloating) {
    *api        = kWindowApi;
    *isFloating = false;
    return true;
}

bool gui_create(const clap_plugin_t* plugin, const char* api, bool isFloating) {
    if (!gui_is_api_supported(plugin, api, isFloating)) return false;
    auto* gui         = new GuiState();
    gui->plugin       = plugin;
    const Layout l    = layoutFor(kindOf(plugin));
    gui->width        = static_cast<uint32_t>(l.width);
    gui->height       = static_cast<uint32_t>(l.height);
    ng_plugin_set_gui(plugin, gui);
    return true;
}

void gui_destroy(const clap_plugin_t* plugin) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    ng_plugin_set_gui(plugin, nullptr);
    if (gui == nullptr) return;
    if (gui->timerRegistered)
        if (const auto* timer = hostTimer(plugin)) timer->unregister_timer(ng_plugin_host(plugin), gui->timerId);
    delete gui;   // the UIExporter, and the editor with it
}

// The UIExporter's callbacks; `ptr` is the clap_plugin_t it was built with.
void onEditParam(void* ptr, uint32_t index, bool started) {
    const auto* plugin = static_cast<const clap_plugin_t*>(ptr);
    if (started) ng_param_begin_gesture(plugin, index);
    else ng_param_end_gesture(plugin, index);
}

void onSetParam(void* ptr, uint32_t index, float value) {
    ng_param_set_from_gui(static_cast<const clap_plugin_t*>(ptr), index, double(value));
}

bool gui_set_scale(const clap_plugin_t*, double) { return false; }   // cocoa's sizes are logical

bool gui_get_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    *width  = gui->width;
    *height = gui->height;
    return true;
}

bool gui_can_resize(const clap_plugin_t*) { return true; }

bool gui_get_resize_hints(const clap_plugin_t* plugin, clap_gui_resize_hints_t* hints) {
    const Layout l                 = layoutFor(kindOf(plugin));
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = true;
    hints->aspect_ratio_width      = static_cast<uint32_t>(l.width);
    hints->aspect_ratio_height     = static_cast<uint32_t>(l.height);
    return true;
}

bool gui_adjust_size(const clap_plugin_t* plugin, uint32_t* width, uint32_t* height) {
    // Keep the design's ratio and the smallest size (0.8×), as the UI's own constraints do.
    const Layout l = layoutFor(kindOf(plugin));
    *width         = std::max<uint32_t>(*width, static_cast<uint32_t>(l.width * 4 / 5));
    *height        = static_cast<uint32_t>(std::lround(*width * double(l.height) / l.width));
    return true;
}

bool gui_set_size(const clap_plugin_t* plugin, uint32_t width, uint32_t height) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    if (gui == nullptr) return false;
    gui->width  = width;
    gui->height = height;
    applySize(gui);
    return true;
}

bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    if (gui == nullptr || std::strcmp(window->api, kWindowApi) != 0) return false;
#ifdef __APPLE__
    gui->parentWindow = reinterpret_cast<uintptr_t>(window->cocoa);
#else
    gui->parentWindow = static_cast<uintptr_t>(window->x11);
#endif
    if (!gui->exporter) {
        gCreatingFor = plugin;
        gui->exporter.reset(new UIExporter(const_cast<clap_plugin_t*>(plugin), gui->parentWindow, 48000.0,
                                           onEditParam, onSetParam, nullptr, nullptr, nullptr, nullptr, nullptr,
                                           const_cast<clap_plugin_t*>(plugin), 0.0,
                                           DGL_NAMESPACE::Application::kTypeClassic));
        gCreatingFor = nullptr;
        applySize(gui);
    }
    return true;
}

bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return false; }   // embedded only
void gui_suggest_title(const clap_plugin_t*, const char*) {}

bool gui_show(const clap_plugin_t* plugin) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;
    if (!gui->timerRegistered)
        if (const auto* timer = hostTimer(plugin))
            gui->timerRegistered = timer->register_timer(ng_plugin_host(plugin), 16 /* ~60 fps */, &gui->timerId);
    return true;
}

bool gui_hide(const clap_plugin_t* plugin) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return false;
    if (gui->timerRegistered) {
        if (const auto* timer = hostTimer(plugin)) timer->unregister_timer(ng_plugin_host(plugin), gui->timerId);
        gui->timerRegistered = false;
    }
    return true;
}

const clap_plugin_gui_t kGui = {
    .is_api_supported  = gui_is_api_supported,
    .get_preferred_api = gui_get_preferred_api,
    .create            = gui_create,
    .destroy           = gui_destroy,
    .set_scale         = gui_set_scale,
    .get_size          = gui_get_size,
    .can_resize        = gui_can_resize,
    .get_resize_hints  = gui_get_resize_hints,
    .adjust_size       = gui_adjust_size,
    .set_size          = gui_set_size,
    .set_parent        = gui_set_parent,
    .set_transient     = gui_set_transient,
    .suggest_title     = gui_suggest_title,
    .show              = gui_show,
    .hide              = gui_hide,
};

} // namespace

extern "C" const clap_plugin_gui_t* ng_gui_extension(void) { return &kGui; }

// CLAP timer-support: re-derive the size if the window changed screens, then pump DPF/pugl.
extern "C" void ng_gui_on_timer(const clap_plugin_t* plugin, clap_id) {
    auto* gui = static_cast<GuiState*>(ng_plugin_get_gui(plugin));
    if (gui == nullptr || !gui->exporter) return;
    if (liveScale(gui) != gui->appliedScale) applySize(gui);
    gui->exporter->plugin_idle();
}

END_NAMESPACE_DISTRHO
