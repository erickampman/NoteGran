// NoteGranUI.cpp — NoteGran's editors (PLAN.md, "Editors"), DPF + NanoVG, after StruckSynth's.
//
// One shell for the four plugins: the header (the Unlikelyware logo, "NoteGran" and the plugin's
// name in its colour), the house widget kit (Knob, Dropdown, Toggle, GroupBox, PopupMenu, copied
// from StruckSynth), and a layout per plugin: groups of slots, each slot one parameter's control,
// or two swapped by a third (Cloud's Density and Rate, by Sync). Controls come from the
// parameters' own descriptions (plugin_gui.h), so their ranges, menus and texts are the host's.
// Settings unused just now are dimmed (Weight unless Pick is Random; EDO and Root unless Quantize
// is EDO).
//
// The editor reads every value on its timer and shows what changed, so host automation and a
// loaded state need no push. Edits go out through DPF's editParameter / setParameterValue, which
// the UIExporter hands to the seam as gestures and values.
//
// Cloud also has a live view of what's playing: the set's members lit on a keyboard, and each note
// the plugin starts rising from its key and fading.

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
#include "Toggle.hpp"
#include "plugin_gui.h"

START_NAMESPACE_DISTRHO

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::Dropdown;
using DGL_NAMESPACE::GroupBox;
using DGL_NAMESPACE::Knob;
using DGL_NAMESPACE::Logo;
using DGL_NAMESPACE::PopupMenu;
using DGL_NAMESPACE::Toggle;

namespace {

// --- the look ----------------------------------------------------------------------------------

// One family, an accent per plugin: Cloud steel blue, Chords violet, Grid amber, Burst teal.
Color accentFor(int kind) {
    switch (kind) {
    case NG_CHORDS: return Color(170, 140, 220);
    case NG_GRID: return Color(220, 170, 90);
    case NG_BURST: return Color(90, 195, 190);
    default: return Color(130, 170, 220);
    }
}
const char* nameFor(int kind) {
    switch (kind) {
    case NG_CHORDS: return "Chords";
    case NG_GRID: return "Grid";
    case NG_BURST: return "Burst";
    default: return "Cloud";
    }
}
const Color kWindow(26, 28, 33);
const Color kViewBack(20, 22, 26);
const Color kTitle(226, 230, 236);

// --- layouts -----------------------------------------------------------------------------------

constexpr uint32_t kNone = 0xFFFFFFFF;

// One control's place: `param`, or `alt` instead while `by` is nonzero (Free / Beats).
struct Slot {
    uint32_t param;
    uint32_t alt = kNone;
    uint32_t by  = kNone;
};
// Dimmed unless `when` is `equals`.
struct Enable {
    uint32_t param, when;
    int      equals;
};
struct Group {
    const char*       title;
    std::vector<Slot> slots;
};
struct Layout {
    std::vector<Group>  groups;
    std::vector<Enable> enables;
    // Rows of group indices; -1 is the live view. Each row's width goes by its groups' slots.
    std::vector<std::vector<int>> rows;
};

// Cloud's parameter ids (plugin.cpp's ParamId): appended, never moved.
enum : uint32_t {
    cSync, cDensity, cRate, cLengthMs, cLengthBeats, cOnsetRandom, cPick, cWeight, cOctaves, cTranspose,
    cScatter, cQuantize, cEdo, cRoot, cLevel, cVelSpread, cLenSpread, cVoices, cWhenFull, cDry, cHoldOver,
    cSetChannel, cTriggerChannel, cLatch, cSeed,
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
    l.enables = {{cWeight, cPick, 0}, {cEdo, cQuantize, 1}, {cRoot, cQuantize, 1}};
    l.rows    = {{-1, 0}, {1, 2}, {3, 4}};
    return l;
}

constexpr int kLiveCells = 4;   // the live view's width, in control cells

// --- the live view -----------------------------------------------------------------------------

// What's playing: a keyboard along the bottom over the range the set is in, its members lit at
// their exact pitches, and each note started rising from its pitch and fading over a second and a
// half, its size by velocity.
class LiveView : public NanoSubWidget {
public:
    explicit LiveView(Widget* parent) : NanoSubWidget(parent) { fHasFont = loadSharedResources(); }

    void setAccent(Color c) { fAccent = c; }

    // A new reading; `now` in seconds.
    void update(const ng_live_t& live, double now) {
        fMembers.clear();
        for (uint32_t i = 0; i < live.memberCount; ++i)
            fMembers.push_back({live.memberPitch[i] / 512.0f, live.memberVelocity[i] / 65535.0f});
        if (!fSeen) {   // the first reading: what came before isn't new
            fSeen    = true;
            fStarted = live.notesStarted;
        }
        const uint32_t fresh = std::min<uint32_t>(live.notesStarted - fStarted, NG_LIVE_NOTES);
        for (uint32_t k = live.notesStarted - fresh; k != live.notesStarted; ++k)
            fNotes.push_back({live.notePitch[k % NG_LIVE_NOTES] / 512.0f, live.noteVelocity[k % NG_LIVE_NOTES] / 65535.0f, now});
        fStarted = live.notesStarted;
        fNotes.erase(std::remove_if(fNotes.begin(), fNotes.end(), [&](const Note& n) { return now - n.born > kLife; }),
                     fNotes.end());
        if (fNotes.size() > 512) fNotes.erase(fNotes.begin(), fNotes.end() - 512);
        fNow = now;
        followRange();
        repaint();
    }

protected:
    void onNanoDisplay() override {
        const float w = getWidth(), h = getHeight();
        beginPath();
        roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        fillColor(kViewBack);
        fill();
        strokeColor(Color(62, 67, 77));
        strokeWidth(1.5f);
        stroke();

        const float pad = 8.0f, keysH = std::min(46.0f, h * 0.24f);
        const float x0 = pad, x1 = w - pad, ky = h - pad - keysH;
        auto xOf = [&](float pitch) { return x0 + (pitch - fLo + 0.5f) / (fHi - fLo + 1.0f) * (x1 - x0); };
        const float keyW = (x1 - x0) / (fHi - fLo + 1.0f);

        // A keyboard with every semitone the same width (pitches are exact, so the strip is a scale,
        // not a piano): the strip pale, the black keys dark bars over its top, a line between
        // neighbouring white keys, each C named.
        const float left = xOf(fLo) - keyW * 0.5f, right = xOf(fHi) + keyW * 0.5f, blackH = keysH * 0.62f;
        beginPath();
        rect(left, ky, right - left, keysH);
        fillColor(Color(150, 156, 166));
        fill();
        // Members: lit at their exact pitch, under the black keys' bars.
        for (const Member& m : fMembers) {
            if (m.pitch < fLo - 0.5f || m.pitch > fHi + 0.5f) continue;
            beginPath();
            rect(xOf(m.pitch) - keyW * 0.5f, ky, keyW, keysH);
            fillColor(withAlpha(fAccent, 0.6f + 0.4f * m.velocity));
            fill();
        }
        for (int k = static_cast<int>(fLo); k <= static_cast<int>(fHi); ++k) {
            const float x = xOf(static_cast<float>(k));
            if (isBlack(k)) {
                beginPath();
                rect(x - keyW * 0.5f, ky, keyW, blackH);
                fillColor(Color(40, 43, 50));
                fill();
            } else if (k > static_cast<int>(fLo) && !isBlack(k - 1)) {   // E-F and B-C
                beginPath();
                moveTo(x - keyW * 0.5f, ky);
                lineTo(x - keyW * 0.5f, ky + keysH);
                strokeColor(Color(96, 102, 114));
                strokeWidth(1.0f);
                stroke();
            }
            if (k % 12 == 0 && fHasFont) {
                char name[8];
                std::snprintf(name, sizeof(name), "C%d", k / 12 - 1);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(std::min(10.0f, keyW * 1.6f));
                textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
                fillColor(Color(60, 64, 74));
                text(x - keyW * 0.5f + 2.0f, ky + keysH - 2.0f, name, nullptr);
            }
        }
        if (fMembers.empty() && fNotes.empty() && fHasFont) {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(Color(96, 102, 114));
            text(w * 0.5f, ky * 0.5f, "Hold notes: they light here, and each grain rises from its pitch", nullptr);
        }
        // Notes started: rising from the keyboard and fading.
        const float rise = ky - pad;
        for (const Note& n : fNotes) {
            if (n.pitch < fLo - 0.5f || n.pitch > fHi + 0.5f) continue;
            const float age = static_cast<float>((fNow - n.born) / kLife);
            const float r   = std::max(1.5f, keyW * (0.18f + 0.32f * n.velocity));
            beginPath();
            circle(xOf(n.pitch), ky - 4.0f - age * rise, r);
            fillColor(withAlpha(lighten(fAccent, 0.35f), 0.9f * (1.0f - age)));
            fill();
        }
    }

private:
    struct Member {
        float pitch, velocity;
    };
    struct Note {
        float  pitch, velocity;
        double born;
    };
    static constexpr double kLife = 1.5;

    static bool isBlack(int key) {
        const int pc = ((key % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
    static Color withAlpha(Color c, float a) {
        c.alpha = a;
        return c;
    }
    static Color lighten(Color c, float f) {
        c.red += (1.0f - c.red) * f;
        c.green += (1.0f - c.green) * f;
        c.blue += (1.0f - c.blue) * f;
        return c;
    }

    // Three octaves at least, from a C, around what's held and what's sounding; kept while nothing is.
    void followRange() {
        float lo = 1000.0f, hi = -1000.0f;
        for (const Member& m : fMembers) lo = std::min(lo, m.pitch), hi = std::max(hi, m.pitch);
        for (const Note& n : fNotes) lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch);
        if (lo > hi) return;
        lo = std::floor((lo - 5.0f) / 12.0f) * 12.0f;
        hi = std::ceil((hi + 5.0f) / 12.0f) * 12.0f;
        if (hi - lo < 36.0f) {
            const float mid = std::round((lo + hi) / 24.0f) * 12.0f;
            lo = mid - 18.0f;
            hi = mid + 18.0f;
            lo = std::floor(lo / 12.0f) * 12.0f;
            hi = lo + 36.0f;
        }
        // Widen at once, narrow only when the notes are well inside.
        if (lo < fLo || hi > fHi + 1.0f || (lo > fLo + 12.0f && hi < fHi - 12.0f)) {
            fLo = std::max(0.0f, lo);
            fHi = std::min(127.0f, hi) - 1.0f;
        }
    }

    Color               fAccent{130, 170, 220};
    bool                fHasFont = false;
    std::vector<Member> fMembers;
    std::vector<Note>   fNotes;
    bool                fSeen    = false;
    uint32_t            fStarted = 0;
    double              fNow     = 0.0;
    float               fLo = 48.0f, fHi = 83.0f;   // the keys shown: C3 to B5

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveView)
};

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
                   public PopupMenu::Callback {
public:
    NoteGranUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT) {
        setGeometryConstraints(720, 448, true, false);   // the 900:560 design, from 0.8×
        fHasFont = loadSharedResources();

        fPlugin = static_cast<const clap_plugin_t*>(getPluginInstancePointer());
        if (fPlugin == nullptr) fPlugin = gCreatingFor;
        fKind   = fPlugin != nullptr ? ng_plugin_kind(fPlugin) : NG_CLOUD;
        fAccent = accentFor(fKind);
        fLayout = cloudLayout();   // Grid's, Burst's and Chords' come with their editors

        fLogo = std::make_unique<Logo>(this, EmbeddedResources::Unlikelyware_Final_ColorData,
                                       EmbeddedResources::Unlikelyware_Final_ColorDataSize);
        fLive = std::make_unique<LiveView>(this);
        fLive->setAccent(fAccent);

        const uint32_t count = fPlugin != nullptr ? ng_param_count(fPlugin) : 0;
        fControls.resize(count);
        for (const Group& g : fLayout.groups) {
            auto box = std::make_unique<GroupBox>(this);
            box->setTitle(g.title);
            box->setAccentColor(fAccent);
            for (const Slot& s : g.slots) {
                makeControl(box.get(), s.param, true);
                if (s.alt != kNone) makeControl(box.get(), s.alt, false);
            }
            fGroups.push_back(std::move(box));
        }
        fMenu = std::make_unique<PopupMenu>(this, this);   // last, so it draws on top

        refresh(true);
        layout();
    }

protected:
    // Values are read on the timer (uiIdle); this only keeps a pushed value from being stale.
    void parameterChanged(uint32_t index, float value) override {
        if (index < fControls.size()) show(index, value);
    }

    // --- controls → the plugin ---
    void knobDragStarted(Knob* k) override {
        fControls[k->getId()].dragging = true;
        editParameter(k->getId(), true);
    }
    void knobDragFinished(Knob* k) override {
        editParameter(k->getId(), false);
        fControls[k->getId()].dragging = false;
    }
    void knobValueChanged(Knob* k, float v) override {
        setParameterValue(k->getId(), v);
        fControls[k->getId()].shown = v;
        updateState();
    }
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

    // The timer: values and what's playing.
    void uiIdle() override {
        if (!fInitialSizeNudged) {   // some Linux hosts apply the first size only once events run
            fInitialSizeNudged = true;
            setSize(getWidth(), getHeight());
        }
        refresh(false);
        ng_live_t live;
        if (fPlugin != nullptr && ng_live_read(fPlugin, &live)) fLive->update(live, secondsNow());
    }

    void onNanoDisplay() override {
        const float w = getWidth(), h = getHeight();
        beginPath();
        rect(0, 0, w, h);
        fillColor(kWindow);
        fill();
        if (!fHasFont) return;
        const float s = w / DISTRHO_UI_DEFAULT_WIDTH;
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
        std::unique_ptr<Dropdown> dropdown;
        std::unique_ptr<Toggle>   toggle;
        SubWidget* widget() const {
            if (knob) return knob.get();
            if (dropdown) return dropdown.get();
            return toggle.get();
        }
    };

    // An Off/On menu is a toggle, any other menu a dropdown, a number or a step a knob. Only
    // primaries are laid out by the box; an alternative takes its primary's place.
    void makeControl(GroupBox* box, uint32_t id, bool primary) {
        if (id >= fControls.size()) return;
        Control& c = fControls[id];
        if (!ng_param_info(fPlugin, id, &c.info)) return;
        c.present = true;
        const int  steps  = static_cast<int>(c.info.max - c.info.min) + 1;
        const bool offOn  = c.info.kind == NG_PARAM_MENU && steps == 2 && std::strcmp(c.info.labels[0], "Off") == 0;
        if (offOn) {
            c.toggle = std::make_unique<Toggle>(box, this);
            c.toggle->setId(id);
            c.toggle->setLabel(c.info.name);
            c.toggle->setAccentColor(fAccent);
        } else if (c.info.kind == NG_PARAM_MENU) {
            c.dropdown = std::make_unique<Dropdown>(box, this);
            c.dropdown->setId(id);
            c.dropdown->setLabel(c.info.name);
            std::vector<std::string> choices;
            for (int i = 0; i < steps; ++i) choices.emplace_back(c.info.labels[i]);
            c.dropdown->setChoices(choices);
        } else {
            c.knob = std::make_unique<Knob>(box, this);
            c.knob->setId(id);
            c.knob->setLabel(c.info.name);
            c.knob->setAccentColor(fAccent);
            // A wide range from above 0 turns logarithmically (Density, the lengths).
            const bool log = c.info.min > 0.0 && c.info.max / c.info.min >= 100.0;
            c.knob->configure(static_cast<float>(c.info.min), static_cast<float>(c.info.max), log,
                              static_cast<float>(c.info.def));
            c.knob->setInteger(c.info.kind == NG_PARAM_STEPPED);
            const clap_plugin_t* plugin = fPlugin;
            c.knob->setFormatter([plugin, id](float v) {
                char text[64];
                ng_param_text(plugin, id, v, text, sizeof(text));
                return std::string(text);
            });
        }
        if (primary) box->addControl(c.widget());
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
        if (c.dropdown) c.dropdown->setIndex(static_cast<int>(std::lround(v - c.info.min)));
        if (c.toggle) c.toggle->setState(v >= 0.5f);
        updateState();
    }

    // Every value from the plugin; what changed (or everything) is shown. A control being dragged
    // keeps what the drag says.
    void refresh(bool all) {
        if (fPlugin == nullptr) return;
        for (uint32_t i = 0; i < fControls.size(); ++i) {
            Control& c = fControls[i];
            if (!c.present || c.dragging) continue;
            const float v = static_cast<float>(ng_param_value(fPlugin, i));
            if (all || v != c.shown) show(i, v);
        }
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
            const bool on = std::lround(fControls[e.when].shown) == e.equals;
            Control&   c  = fControls[e.param];
            if (c.knob) c.knob->setEnabled(on);
            if (c.dropdown) c.dropdown->setEnabled(on);
        }
    }

    void layout() {
        const int   W = static_cast<int>(getWidth()), H = static_cast<int>(getHeight());
        const float s = W / static_cast<float>(DISTRHO_UI_DEFAULT_WIDTH);
        const int   M = static_cast<int>(14 * s), gap = static_cast<int>(10 * s), hdr = static_cast<int>(46 * s);

        fLogo->setHeightPreservingAspect(22 * s);
        fLogo->setAbsolutePos(M, static_cast<int>(11 * s));
        fTitleX = static_cast<float>(M + static_cast<int>(fLogo->getWidth()) + static_cast<int>(12 * s));
        fMenu->setUiScale(s);

        const int rows = static_cast<int>(fLayout.rows.size());
        const int rowH = (H - hdr - M - (rows - 1) * gap) / std::max(rows, 1);
        for (int r = 0; r < rows; ++r) {
            const std::vector<int>& row = fLayout.rows[static_cast<std::size_t>(r)];
            const int y = hdr + r * (rowH + gap);
            int       total = 0;
            for (int g : row) total += cellsOf(g);
            const int rowW = W - 2 * M - (static_cast<int>(row.size()) - 1) * gap;
            int       x = M, used = 0, done = 0;
            for (int g : row) {
                used += cellsOf(g);
                const int right = rowW * used / total;   // cumulative, so rounding never drifts
                SubWidget* w = g < 0 ? static_cast<SubWidget*>(fLive.get()) : fGroups[static_cast<std::size_t>(g)].get();
                w->setAbsolutePos(x, y);
                w->setSize(static_cast<uint>(right - done), static_cast<uint>(rowH));
                x += right - done + gap;
                done = right;
            }
        }

        const float knobD = std::min(62.0f * s, rowH * 0.42f);
        for (Control& c : fControls) {
            if (c.knob) c.knob->setDiameter(knobD, s);
            if (c.dropdown) c.dropdown->setUiScale(s);
            if (c.toggle) c.toggle->setUiScale(s);
        }
        for (auto& g : fGroups) {
            g->setUiScale(s);
            g->relayout();
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

    int cellsOf(int group) const {
        if (group < 0) return kLiveCells;
        return std::max(1, static_cast<int>(fLayout.groups[static_cast<std::size_t>(group)].slots.size()));
    }

    const clap_plugin_t*                   fPlugin = nullptr;
    int                                    fKind   = NG_CLOUD;
    Color                                  fAccent;
    Layout                                 fLayout;
    std::vector<Control>                   fControls;
    std::vector<std::unique_ptr<GroupBox>> fGroups;
    std::unique_ptr<LiveView>              fLive;
    std::unique_ptr<Logo>                  fLogo;
    std::unique_ptr<PopupMenu>             fMenu;
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
    uint32_t width        = DISTRHO_UI_DEFAULT_WIDTH;
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
    auto* gui   = new GuiState();
    gui->plugin = plugin;
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

bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
    hints->can_resize_horizontally = true;
    hints->can_resize_vertically   = true;
    hints->preserve_aspect_ratio   = true;
    hints->aspect_ratio_width      = DISTRHO_UI_DEFAULT_WIDTH;
    hints->aspect_ratio_height     = DISTRHO_UI_DEFAULT_HEIGHT;
    return true;
}

bool gui_adjust_size(const clap_plugin_t*, uint32_t* width, uint32_t* height) {
    // Keep the design's ratio and the smallest size, as the UI's own constraints do.
    *width  = std::max<uint32_t>(*width, 720);
    *height = static_cast<uint32_t>(std::lround(*width * double(DISTRHO_UI_DEFAULT_HEIGHT) / DISTRHO_UI_DEFAULT_WIDTH));
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
