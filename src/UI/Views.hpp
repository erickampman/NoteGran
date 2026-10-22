// Views.hpp — the editors' live views (PLAN.md, "Editors"): what's playing, and for Grid, Burst and
// Chords what their settings make. Each is handed its data by NoteGranUI.cpp on the timer; none
// talks to the plugin.
//
// - LiveView: a keyboard of equal semitones (pitches are exact, so it's a scale, not a piano)
//   over the range in play, the set's members lit, and each note started rising from its pitch and
//   fading. Its subclasses draw their own band above the rising notes.
// - GridView: one cycle of Grid's meter, each slot a bar as tall as its tier is strong and as
//   bright as its Probability, the playhead on the slot now, and slots flashing as they play.
// - PhraseView: Burst's next phrase (time across Duration, pitch up and down from the root, size
//   by velocity after Decay), a second pass ghosted with Loop.
// - ChordCircle: Chords' scale on a circle of N steps, the key at the top, the degrees numbered,
//   the notes sounding lit and joined; a degree clicked shows the chord built on it.

#ifndef NOTEGRAN_VIEWS_HPP
#define NOTEGRAN_VIEWS_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "plugin_gui.h"

START_NAMESPACE_DGL

namespace views {

inline Color withAlpha(Color c, float a) { c.alpha = a; return c; }
inline Color lighten(Color c, float f)
{
    c.red += (1.0f - c.red) * f;
    c.green += (1.0f - c.green) * f;
    c.blue += (1.0f - c.blue) * f;
    return c;
}
const Color kBack(20, 22, 26);
const Color kBorder(62, 67, 77);
const Color kDim(96, 102, 114);
const Color kText(176, 182, 190);

} // namespace views

// --- LiveView ------------------------------------------------------------------------------------

class LiveView : public NanoSubWidget
{
public:
    explicit LiveView(Widget* parent) : NanoSubWidget(parent) { fHasFont = loadSharedResources(); }

    void setAccent(Color c) { fAccent = c; }
    void setUiScale(float s) { fUiScale = s; }

    // A new reading; `now` in seconds.
    virtual void update(const ng_live_t& live, double now)
    {
        fMembers.clear();
        for (uint32_t i = 0; i < live.memberCount; ++i)
            fMembers.push_back({live.memberPitch[i] / 512.0f, live.memberVelocity[i] / 65535.0f});
        if (!fSeen)   // the first reading: what came before isn't new
        {
            fSeen    = true;
            fStarted = live.notesStarted;
        }
        fFresh = std::min<uint32_t>(live.notesStarted - fStarted, NG_LIVE_NOTES);
        for (uint32_t k = live.notesStarted - fFresh; k != live.notesStarted; ++k)
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
    // A band above the rising notes for a subclass, as a share of the inner height.
    virtual float topShare() const { return 0.0f; }
    virtual void  drawTop(float /*x*/, float /*y*/, float /*w*/, float /*h*/) {}
    virtual const char* emptyHint() const { return "Hold notes: they light here, and each grain rises from its pitch"; }

    void onNanoDisplay() override
    {
        using namespace views;
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        beginPath(); roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        fillColor(kBack); fill();
        strokeColor(kBorder); strokeWidth(1.5f); stroke();

        const float pad = 8.0f * s, inner = h - 2 * pad;
        const float topH = inner * topShare(), keysH = std::min(46.0f * s, inner * 0.24f);
        const float x0 = pad, x1 = w - pad, ky = h - pad - keysH, riseTop = pad + topH + (topH > 0 ? 6.0f * s : 0.0f);
        if (topH > 0) drawTop(pad, pad, w - 2 * pad, topH);

        const float span = fHi - fLo + 1.0f, keyW = (x1 - x0) / span;
        auto xOf = [&](float pitch) { return x0 + (pitch - fLo + 0.5f) / span * (x1 - x0); };

        // The strip pale, the members lit under the black keys' bars, a line between neighbouring
        // white keys, each C named.
        const float left = xOf(fLo) - keyW * 0.5f, right = xOf(fHi) + keyW * 0.5f, blackH = keysH * 0.62f;
        beginPath(); rect(left, ky, right - left, keysH); fillColor(Color(150, 156, 166)); fill();
        for (const Member& m : fMembers)
        {
            if (m.pitch < fLo - 0.5f || m.pitch > fHi + 0.5f) continue;
            beginPath(); rect(xOf(m.pitch) - keyW * 0.5f, ky, keyW, keysH);
            fillColor(withAlpha(fAccent, 0.6f + 0.4f * m.velocity)); fill();
        }
        for (int k = static_cast<int>(fLo); k <= static_cast<int>(fHi); ++k)
        {
            const float x = xOf(static_cast<float>(k));
            if (isBlack(k))
            {
                beginPath(); rect(x - keyW * 0.5f, ky, keyW, blackH); fillColor(Color(40, 43, 50)); fill();
            }
            else if (k > static_cast<int>(fLo) && !isBlack(k - 1))
            {
                beginPath(); moveTo(x - keyW * 0.5f, ky); lineTo(x - keyW * 0.5f, ky + keysH);
                strokeColor(Color(96, 102, 114)); strokeWidth(1.0f); stroke();
            }
            if (k % 12 == 0 && fHasFont)
            {
                char name[8];
                std::snprintf(name, sizeof(name), "C%d", k / 12 - 1);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(std::min(10.0f * s, keyW * 1.6f));
                textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
                fillColor(Color(60, 64, 74));
                text(x - keyW * 0.5f + 2.0f, ky + keysH - 2.0f, name, nullptr);
            }
        }
        // Notes started: rising and fading.
        const float rise = std::max(1.0f, ky - 4.0f * s - riseTop);
        for (const Note& n : fNotes)
        {
            if (n.pitch < fLo - 0.5f || n.pitch > fHi + 0.5f) continue;
            const float age = static_cast<float>((fNow - n.born) / kLife);
            const float r   = std::max(1.5f, keyW * (0.18f + 0.32f * n.velocity));
            beginPath(); circle(xOf(n.pitch), ky - 4.0f * s - age * rise, r);
            fillColor(withAlpha(lighten(fAccent, 0.35f), 0.9f * (1.0f - age))); fill();
        }
        if (fMembers.empty() && fNotes.empty() && fHasFont && emptyHint() != nullptr)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(12.0f * s);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(kDim);
            text(w * 0.5f, (riseTop + ky) * 0.5f, emptyHint(), nullptr);
        }
    }

    Color    fAccent{130, 170, 220};
    bool     fHasFont = false;
    float    fUiScale = 1.0f;
    uint32_t fFresh   = 0;     // notes new in the last reading

private:
    struct Member { float pitch, velocity; };
    struct Note { float pitch, velocity; double born; };
    static constexpr double kLife = 1.5;

    static bool isBlack(int key)
    {
        const int pc = ((key % 12) + 12) % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    // Three octaves at least, from a C, around what's held and sounding; kept while nothing is.
    void followRange()
    {
        float lo = 1000.0f, hi = -1000.0f;
        for (const Member& m : fMembers) lo = std::min(lo, m.pitch), hi = std::max(hi, m.pitch);
        for (const Note& n : fNotes) lo = std::min(lo, n.pitch), hi = std::max(hi, n.pitch);
        if (lo > hi) return;
        lo = std::floor((lo - 5.0f) / 12.0f) * 12.0f;
        hi = std::ceil((hi + 5.0f) / 12.0f) * 12.0f;
        if (hi - lo < 36.0f)
        {
            lo = std::floor((std::round((lo + hi) / 24.0f) * 12.0f - 18.0f) / 12.0f) * 12.0f;
            hi = lo + 36.0f;
        }
        // Widen at once; narrow only when the notes are well inside.
        if (lo < fLo || hi > fHi + 1.0f || (lo > fLo + 12.0f && hi < fHi - 12.0f))
        {
            fLo = std::max(0.0f, lo);
            fHi = std::min(127.0f, hi) - 1.0f;
        }
    }

    std::vector<Member> fMembers;
    std::vector<Note>   fNotes;
    bool                fSeen    = false;
    uint32_t            fStarted = 0;
    double              fNow     = 0.0;
    float               fLo = 48.0f, fHi = 83.0f;   // C3 to B5

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LiveView)
};

// --- GridView ------------------------------------------------------------------------------------

class GridView : public LiveView
{
public:
    explicit GridView(Widget* parent) : LiveView(parent) {}

    // One cycle: each slot's tier (0 the strongest), and each tier's Probability.
    void setCycle(std::vector<int> tiers, const float probability[5])
    {
        if (tiers.size() != fTiers.size()) fFlash.assign(tiers.size(), -10.0);
        fTiers = std::move(tiers);
        for (int i = 0; i < 5; ++i) fProbability[i] = probability[i];
    }

    void update(const ng_live_t& live, double now) override
    {
        fSlot = live.slot;
        LiveView::update(live, now);
        if (fFresh > 0 && fSlot >= 0 && !fTiers.empty())
            fFlash[static_cast<std::size_t>(fSlot % static_cast<int64_t>(fTiers.size()))] = now;
        fNowS = now;
    }

protected:
    float topShare() const override { return 0.42f; }
    const char* emptyHint() const override { return "Hold notes; the cycle's slots light as they play"; }

    void drawTop(float x, float y, float w, float h) override
    {
        using namespace views;
        const int n = static_cast<int>(fTiers.size());
        if (n == 0) return;
        const float s = fUiScale, gap = 3.0f * s, labelH = 12.0f * s;
        const float cw = (w - gap * (n - 1)) / n, baseY = y + h - labelH;
        const int   now = fSlot >= 0 ? static_cast<int>(fSlot % n) : -1;
        for (int i = 0; i < n; ++i)
        {
            const int   t   = std::clamp(fTiers[static_cast<std::size_t>(i)], 0, 4);
            const float bh  = (h - labelH - 2.0f) * (1.0f - 0.17f * t);
            const float cx  = x + i * (cw + gap);
            const float age = static_cast<float>(fNowS - fFlash[static_cast<std::size_t>(i)]);
            const float lit = age >= 0.0f && age < 0.35f ? 1.0f - age / 0.35f : 0.0f;
            beginPath(); roundedRect(cx, baseY - bh, cw, bh, 2.0f * s);
            fillColor(withAlpha(fAccent, 0.18f + 0.55f * fProbability[t] + 0.27f * lit)); fill();
            if (i == now)
            {
                strokeColor(Color(240, 244, 248)); strokeWidth(1.5f * s); stroke();
            }
            if (fHasFont)
            {
                char label[4];
                std::snprintf(label, sizeof(label), "%d", t + 1);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(std::min(10.0f * s, cw * 0.9f));
                textAlign(ALIGN_CENTER | ALIGN_TOP);
                fillColor(i == now ? kText : kDim);
                text(cx + cw * 0.5f, baseY + 2.0f, label, nullptr);
            }
        }
    }

private:
    std::vector<int>    fTiers;
    std::vector<double> fFlash;
    float               fProbability[5] = {1, 1, 1, 1, 1};
    int64_t             fSlot = -1;
    double              fNowS = 0.0;
};

// --- PhraseView ----------------------------------------------------------------------------------

class PhraseView : public LiveView
{
public:
    explicit PhraseView(Widget* parent) : LiveView(parent) {}

    // The next phrase: each point's time (0..1 of Duration) and offset (semitones); Range in
    // semitones, Decay 0..1, and whether it loops.
    void setPhrase(const float* times, const float* offsets, int n, float range, float decay, bool loop,
                   std::string duration)
    {
        fTimes.assign(times, times + n);
        fOffsets.assign(offsets, offsets + n);
        fRange    = std::max(range, 1.0f);
        fDecay    = decay;
        fLoop     = loop;
        fDuration = std::move(duration);
        repaint();
    }

protected:
    float topShare() const override { return 0.55f; }
    const char* emptyHint() const override { return nullptr; }

    void drawTop(float x, float y, float w, float h) override
    {
        using namespace views;
        const float s = fUiScale, labelH = 14.0f * s, inset = 7.0f * s;   // the largest dot's radius
        const float ph = h - labelH, passes = fLoop ? 2.0f : 1.0f, pw = w / passes;
        const float midY = y + ph * 0.5f;
        // The root's line, and ±Range.
        strokeColor(withAlpha(fAccent, 0.35f)); strokeWidth(1.0f);
        beginPath(); moveTo(x, midY); lineTo(x + w, midY); stroke();
        strokeColor(Color(1.0f, 1.0f, 1.0f, 0.06f));
        beginPath(); moveTo(x, y + 1); lineTo(x + w, y + 1); stroke();
        beginPath(); moveTo(x, y + ph - 1); lineTo(x + w, y + ph - 1); stroke();
        if (fLoop)
        {
            strokeColor(Color(1.0f, 1.0f, 1.0f, 0.12f));
            beginPath(); moveTo(x + pw, y); lineTo(x + pw, y + ph); stroke();
        }
        for (int pass = 0; pass < static_cast<int>(passes); ++pass)
            for (std::size_t i = 0; i < fTimes.size(); ++i)
            {
                const float px = x + inset + pass * pw + fTimes[i] * (pw - 2.0f * inset);
                const float py = midY - std::clamp(fOffsets[i] / fRange, -1.0f, 1.0f) * (ph * 0.5f - 4.0f * s);
                const float v  = 1.0f - fDecay * fTimes[i];
                beginPath(); circle(px, py, (2.5f + 3.5f * v) * s);
                fillColor(withAlpha(lighten(fAccent, 0.2f), (pass == 0 ? 0.9f : 0.35f) * (0.35f + 0.65f * v))); fill();
            }
        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(10.5f * s);
            fillColor(kDim);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(x, y + ph + 2.0f, "the next phrase", nullptr);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            text(x + pw, y + ph + 2.0f, fDuration.c_str(), nullptr);
        }
    }

private:
    std::vector<float> fTimes, fOffsets;
    float              fRange = 12.0f, fDecay = 0.5f;
    bool               fLoop = false;
    std::string        fDuration;
};

// --- ChordCircle ---------------------------------------------------------------------------------

class ChordCircle : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void degreeClicked(int degree) = 0;   // -1: none
    };

    ChordCircle(Widget* parent, Callback* callback) : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setAccent(Color c) { fAccent = c; }
    void setUiScale(float s) { fUiScale = s; }
    void setScale(int edo, int origin, int keyStep, std::vector<int> steps)
    {
        fEdo = std::max(edo, 1); fOrigin = origin; fKeyStep = keyStep; fSteps = std::move(steps);
        repaint();
    }
    void setSounding(std::vector<float> pitches) { fSounding = std::move(pitches); repaint(); }
    void setPreview(int degree, std::vector<float> pitches) { fPreviewDegree = degree; fPreview = std::move(pitches); repaint(); }
    void setLines(std::vector<std::string> lines) { fLines = std::move(lines); repaint(); }

protected:
    void onNanoDisplay() override
    {
        using namespace views;
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        beginPath(); roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        fillColor(kBack); fill();
        strokeColor(kBorder); strokeWidth(1.5f); stroke();

        geometry(fCx, fCy, fR);
        // The ring and its N steps.
        beginPath(); circle(fCx, fCy, fR); strokeColor(Color(56, 60, 70)); strokeWidth(1.5f * s); stroke();
        for (int k = 0; k < fEdo; ++k)
        {
            float x0, y0, x1, y1;
            at(k, fR - 4.0f * s, x0, y0);
            at(k, fR + 4.0f * s, x1, y1);
            beginPath(); moveTo(x0, y0); lineTo(x1, y1);
            strokeColor(Color(70, 75, 86)); strokeWidth(1.0f); stroke();
        }
        // The chord shown: what's sounding, else the degree clicked.
        const bool live = !fSounding.empty();
        drawChord(live ? fSounding : fPreview, live ? 1.0f : 0.55f);
        // The degrees, numbered; the key's larger.
        for (std::size_t d = 0; d < fSteps.size(); ++d)
        {
            float x, y;
            at(fSteps[d], fR, x, y);
            const bool key = d == 0, chosen = static_cast<int>(d) == fPreviewDegree && !live;
            beginPath(); circle(x, y, (key ? 7.0f : 5.5f) * s);
            fillColor(chosen ? lighten(fAccent, 0.3f) : withAlpha(fAccent, key ? 0.95f : 0.7f)); fill();
            if (fHasFont)
            {
                float lx, ly;
                at(fSteps[d], fR + 16.0f * s, lx, ly);
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                fontSize(11.0f * s);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(kText);
                text(lx, ly, degreeName(static_cast<int>(d)).c_str(), nullptr);
            }
        }
        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            const float tx = fCx + fR + 34.0f * s;
            float       ty = 16.0f * s;
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            for (std::size_t i = 0; i < fLines.size(); ++i)
            {
                fontSize((i == 0 ? 15.0f : 12.0f) * s);
                fillColor(i == 0 ? fAccent : kText);
                text(tx, ty, fLines[i].c_str(), nullptr);
                ty += (i == 0 ? 22.0f : 18.0f) * s;
            }
            fontSize(11.0f * s);
            fillColor(kDim);
            text(tx, h - 30.0f * s, live ? "Sounding now" : "Click a degree to see its chord", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1 || !ev.press) return false;
        const float px = static_cast<float>(ev.pos.getX()), py = static_cast<float>(ev.pos.getY());
        if (px < 0 || py < 0 || px >= getWidth() || py >= getHeight()) return false;
        int   best = -1;
        float bestD = 14.0f * fUiScale;
        for (std::size_t d = 0; d < fSteps.size(); ++d)
        {
            float x, y;
            at(fSteps[d], fR, x, y);
            const float dist = std::hypot(px - x, py - y);
            if (dist < bestD) { bestD = dist; best = static_cast<int>(d); }
        }
        fCallback->degreeClicked(best == fPreviewDegree ? -1 : best);
        return true;
    }

private:
    void geometry(float& cx, float& cy, float& r) const
    {
        const float s = fUiScale, h = getHeight();
        r  = std::max(10.0f, h * 0.5f - 30.0f * s);
        cx = 30.0f * s + r;
        cy = h * 0.5f;
    }

    // Step k above the key, at the angle for it (the key at the top, clockwise).
    void at(int k, float radius, float& x, float& y) const
    {
        const float a = -1.5707963f + 6.2831853f * static_cast<float>(k) / static_cast<float>(fEdo);
        x = fCx + radius * std::cos(a);
        y = fCy + radius * std::sin(a);
    }

    // A pitch's step above the key (its pitch class on the N-step grid).
    int stepOf(float pitch) const
    {
        const float above = std::fmod(pitch - static_cast<float>(fOrigin), 12.0f);
        const int   step  = static_cast<int>(std::lround((above < 0 ? above + 12.0f : above) * fEdo / 12.0f));
        return ((step - fKeyStep) % fEdo + fEdo) % fEdo;
    }

    void drawChord(const std::vector<float>& pitches, float alpha)
    {
        using namespace views;
        if (pitches.empty()) return;
        const float s = fUiScale;
        std::vector<int> steps;
        for (float p : pitches) steps.push_back(stepOf(p));
        std::vector<int> ring = steps;
        std::sort(ring.begin(), ring.end());
        ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
        if (ring.size() > 1)
        {
            beginPath();
            for (std::size_t i = 0; i < ring.size(); ++i)
            {
                float x, y;
                at(ring[i], fR, x, y);
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            closePath();
            fillColor(withAlpha(fAccent, 0.16f * alpha)); fill();
            strokeColor(withAlpha(lighten(fAccent, 0.3f), 0.8f * alpha)); strokeWidth(2.0f * s); stroke();
        }
        for (std::size_t i = 0; i < steps.size(); ++i)
        {
            float x, y;
            at(steps[i], fR, x, y);
            beginPath(); circle(x, y, (i == 0 ? 11.0f : 9.5f) * s);
            strokeColor(withAlpha(Color(240, 244, 248), 0.85f * alpha)); strokeWidth(2.0f * s); stroke();
        }
    }

    std::string degreeName(int d) const
    {
        static const char* kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        if (fSteps.size() == 7) return kRoman[d];
        return std::to_string(d + 1);
    }

    Callback*                fCallback;
    Color                    fAccent{170, 140, 220};
    bool                     fHasFont = false;
    float                    fUiScale = 1.0f;
    int                      fEdo = 12, fOrigin = 9, fKeyStep = 3;
    std::vector<int>         fSteps;
    std::vector<float>       fSounding, fPreview;   // pitches, semitones
    int                      fPreviewDegree = -1;
    std::vector<std::string> fLines;
    float                    fCx = 0, fCy = 0, fR = 10;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordCircle)
};

END_NAMESPACE_DGL

#endif // NOTEGRAN_VIEWS_HPP
