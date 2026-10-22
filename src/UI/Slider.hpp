// Slider.hpp — NoteGran's compact horizontal slider, for tables of settings (Grid's tiers) where a
// knob with its own caption won't fit. The house slider (KeyboardEDO's, StageHand's faders): a
// recessed slot lit in the accent from its rest to its value (the rest is 0 when the range
// crosses it, else the minimum), the thumb a small glowing sphere with a white dot, and the value
// in a dark box to its right. A drag moves it as far as the pointer moves (so a click never jumps
// it), Shift is finer, a double-click returns to the default, and the wheel nudges it.

#ifndef NOTEGRAN_SLIDER_HPP
#define NOTEGRAN_SLIDER_HPP

#include "NanoVG.hpp"

#include <cmath>
#include <functional>
#include <string>

START_NAMESPACE_DGL

class Slider : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void sliderDragStarted(Slider* slider) = 0;
        virtual void sliderDragFinished(Slider* slider) = 0;
        virtual void sliderValueChanged(Slider* slider, float value) = 0;
    };

    Slider(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void configure(float min, float max, float def, bool integer)
    {
        fMin = min; fMax = max; fDef = def; fInteger = integer;
        fRest = (min < 0.0f && max > 0.0f) ? 0.0f : min;
    }
    void setValue(float v)                  { fValue = clamp(v); repaint(); }
    float value() const                     { return fValue; }
    void setAccentColor(Color c)            { fAccent = c; }
    void setFormatter(std::function<std::string(float)> f) { fFormat = std::move(f); }
    void setUiScale(float s)                { fUiScale = s; }
    void setEnabled(bool e)                 { if (fEnabled != e) { fEnabled = e; repaint(); } }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        const float boxW = std::fmin(58.0f * s, w * 0.4f), gap = 6.0f * s;
        const float x0 = 8.0f * s, x1 = w - boxW - gap - 8.0f * s, cy = h * 0.5f;
        const float slotH = 6.0f * s, r = 7.0f * s;
        const Color accent = fEnabled ? fAccent : Color(104, 108, 116);

        // The slot, recessed.
        beginPath(); roundedRect(x0, cy - slotH * 0.5f, x1 - x0, slotH, slotH * 0.5f);
        fillColor(Color(16, 18, 22)); fill();
        strokeColor(Color(56, 60, 70)); strokeWidth(1.0f); stroke();

        // Lit from the rest to the value, with a faint glow.
        const float xr = xOf(fRest, x0, x1), xv = xOf(fValue, x0, x1);
        const float a = std::fmin(xr, xv), b = std::fmax(xr, xv);
        if (b - a > 0.5f)
        {
            beginPath(); roundedRect(a, cy - slotH * 0.5f, b - a, slotH, slotH * 0.5f);
            fillColor(withAlpha(accent, fEnabled ? 0.85f : 0.4f)); fill();
            beginPath(); roundedRect(a - 2, cy - slotH, b - a + 4, slotH * 2, slotH);
            fillColor(withAlpha(accent, 0.12f)); fill();
        }
        if (fRest > fMin)   // the rest marked across the slot
        {
            beginPath(); moveTo(xr, cy - slotH); lineTo(xr, cy + slotH);
            strokeColor(Color(120, 126, 136)); strokeWidth(1.0f); stroke();
        }

        // The thumb: the knob's sphere, small.
        Paint glow = radialGradient(xv, cy, r * 0.1f, r, withAlpha(accent, fEnabled ? 0.95f : 0.5f),
                                    withAlpha(accent, 0.15f));
        beginPath(); circle(xv, cy, r); fillPaint(glow); fill();
        beginPath(); circle(xv, cy, r);
        strokeColor(withAlpha(Color(255, 255, 255), 0.2f)); strokeWidth(1.0f); stroke();
        beginPath(); circle(xv, cy, std::fmax(1.5f, r * 0.28f));
        fillColor(fEnabled ? Color(244, 247, 250) : Color(150, 154, 160)); fill();

        if (fHasFont)
        {
            const float bx = w - boxW - 4.0f * s, bh = 18.0f * s;
            beginPath(); roundedRect(bx, cy - bh * 0.5f, boxW, bh, 3.0f);
            fillColor(Color(20, 22, 27)); fill();
            strokeColor(Color(46, 50, 58)); strokeWidth(1.0f); stroke();
            const std::string t = fFormat ? fFormat(fValue) : std::to_string(fValue);
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(11.5f * s);
            Rectangle<float> tb;
            textBounds(0, 0, t.c_str(), nullptr, tb);
            if (tb.getWidth() > boxW - 4.0f && tb.getWidth() > 0.0f)
                fontSize(11.5f * s * (boxW - 4.0f) / tb.getWidth());
            fillColor(fEnabled ? Color(224, 228, 234) : Color(120, 125, 132));
            text(bx + boxW * 0.5f, cy, t.c_str(), nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (ev.press)
        {
            if (!fEnabled || !contains(ev.pos)) return false;
            if (ev.time - fLastClick < 300 && fLastClick != 0)   // double-click: the default
            {
                fLastClick = 0;
                fCallback->sliderDragStarted(this);
                change(fDef);
                fCallback->sliderDragFinished(this);
                return true;
            }
            fLastClick = ev.time;
            fDragging  = true;
            fFree      = fValue;   // a drag starts from the value shown
            fPending   = 0.0f;
            fLastX     = ev.pos.getX();
            fCallback->sliderDragStarted(this);
            return true;
        }
        if (!fDragging) return false;
        fDragging = false;
        fCallback->sliderDragFinished(this);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fDragging) return false;
        const float s = fUiScale, boxW = std::fmin(58.0f * s, getWidth() * 0.4f);
        const float track = std::fmax(10.0f, getWidth() - boxW - 6.0f * s - 16.0f * s);
        const double dx = ev.pos.getX() - fLastX;
        fLastX = ev.pos.getX();
        const float fine = (ev.mod & kModifierShift) ? 0.2f : 1.0f;
        fPending += static_cast<float>(dx) / track * (fMax - fMin) * fine;
        float v = fFree + fPending;
        if (fInteger) v = std::round(v);
        change(v, false);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!fEnabled || !contains(ev.pos)) return false;
        const float step = fInteger ? 1.0f : (fMax - fMin) / 100.0f;
        const float dir  = (ev.delta.getY() > 0 || ev.delta.getX() > 0) ? 1.0f : -1.0f;
        fCallback->sliderDragStarted(this);
        change(fValue + dir * step);
        fCallback->sliderDragFinished(this);
        return true;
    }

private:
    // A drag accumulates unrounded, so a stepped slider still moves under slow drags.
    void change(float v, bool resetDrag = true)
    {
        v = clamp(v);
        if (resetDrag) { fFree = v; fPending = 0.0f; }
        if (v == fValue) return;
        fValue = v;
        fCallback->sliderValueChanged(this, v);
        repaint();
    }

    bool contains(const Point<double>& p) const
    {
        return p.getX() >= 0 && p.getY() >= 0 && p.getX() < getWidth() && p.getY() < getHeight();
    }
    float clamp(float v) const { return v < fMin ? fMin : (v > fMax ? fMax : v); }
    float xOf(float v, float x0, float x1) const
    {
        return fMax > fMin ? x0 + (clamp(v) - fMin) / (fMax - fMin) * (x1 - x0) : x0;
    }
    static Color withAlpha(Color c, float a) { c.alpha = a; return c; }

    Callback*   fCallback;
    std::function<std::string(float)> fFormat;
    Color       fAccent { 130, 170, 220 };
    bool        fHasFont = false, fEnabled = true, fInteger = false, fDragging = false;
    float       fMin = 0.0f, fMax = 1.0f, fDef = 0.0f, fRest = 0.0f, fValue = 0.0f, fUiScale = 1.0f;
    float       fFree = 0.0f, fPending = 0.0f;
    double      fLastX = 0.0;
    uint        fLastClick = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Slider)
};

END_NAMESPACE_DGL

#endif // NOTEGRAN_SLIDER_HPP
