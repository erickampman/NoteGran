// ScatterEditorView.hpp — Burst's attractor editor, from PMU's (PolygraphModularUnified
// dgl-ui/src/UI/ScatterEditorView.hpp, itself a port of IFSGranView.swift's ScatterEditorView). A
// 2D space (x time, y pitch, both 0..1) with each transform as a filled dot (its fixed point)
// joined to a hollow one (its handle: the distance is Contraction, the angle Rotation). Dragging a
// dot edits that transform's X and Y; dragging a handle its Contraction and Rotation.
//
// NoteGran's changes: its colours; the points drawn are the real attractor (the owner hands them in
// from the core, so the editor shows what Burst plays), the first phrase's larger; and a drag is
// one gesture per field it moves, so the host records it as automation.

#ifndef NOTEGRAN_SCATTEREDITORVIEW_HPP
#define NOTEGRAN_SCATTEREDITORVIEW_HPP

#include "NanoVG.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

START_NAMESPACE_DGL

class ScatterEditorView : public NanoSubWidget
{
public:
    // The fields a drag moves, in the order of Burst's parameters (T1 X .. T2 Rotation).
    enum Field { T1X, T1Y, T1Contraction, T1Rotation, T2X, T2Y, T2Contraction, T2Rotation };

    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void scatterDragStarted(Field a, Field b) = 0;
        virtual void scatterFieldChanged(Field field, float value) = 0;
        virtual void scatterDragFinished(Field a, Field b) = 0;
    };

    struct CloudPoint { float x, y; bool t1, first; };

    ScatterEditorView(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setUiScale(float s) { fUiScale = s; }
    // The values shown (ignored for the fields being dragged).
    void setTransforms(const float values[8])
    {
        for (int i = 0; i < 8; ++i)
            if (!fDrag || (i != fieldsOf(*fDrag).first && i != fieldsOf(*fDrag).second)) fV[i] = values[i];
        repaint();
    }
    void setCloud(std::vector<CloudPoint> cloud) { fCloud = std::move(cloud); repaint(); }
    bool dragging() const { return fDrag.has_value(); }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight(), s = fUiScale;
        beginPath(); roundedRect(1.5f, 1.5f, w - 3.0f, h - 3.0f, 8.0f);
        fillColor(Color(20, 22, 26)); fill();
        strokeColor(Color(62, 67, 77)); strokeWidth(1.5f); stroke();

        const float px0 = pad(), py0 = pad(), pw = w - 2 * pad(), ph = h - 2 * pad();
        // Quarter lines, the middle a little brighter.
        for (int i = 1; i < 4; ++i)
        {
            const float a = i == 2 ? 0.16f : 0.07f;
            strokeColor(Color(1.0f, 1.0f, 1.0f, a)); strokeWidth(1.0f);
            beginPath(); moveTo(px0 + pw * i / 4, py0); lineTo(px0 + pw * i / 4, py0 + ph); stroke();
            beginPath(); moveTo(px0, py0 + ph * i / 4); lineTo(px0 + pw, py0 + ph * i / 4); stroke();
        }

        for (const CloudPoint& p : fCloud)
        {
            beginPath();
            circle(px0 + p.x * pw, py0 + (1.0f - p.y) * ph, (p.first ? 3.2f : 1.6f) * s);
            const Color c = p.t1 ? kT1 : kT2;
            fillColor(Color(c.red, c.green, c.blue, p.first ? 0.95f : 0.45f));
            fill();
        }

        drawTransform(fV[T1X], fV[T1Y], fV[T1Contraction], fV[T1Rotation], kT1);
        drawTransform(fV[T2X], fV[T2Y], fV[T2Contraction], fV[T2Rotation], kT2);

        if (fHasFont)
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(11.0f * s);
            const float ly = h - 9.0f * s;
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath(); circle(12.0f * s, ly, 3.5f * s); fillColor(kT1); fill();
            fillColor(Color(150, 156, 166)); text(19.0f * s, ly, "T1", nullptr);
            beginPath(); circle(42.0f * s, ly, 3.5f * s); fillColor(kT2); fill();
            fillColor(Color(150, 156, 166)); text(49.0f * s, ly, "T2", nullptr);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(w - 10.0f * s, ly, "x time \xC2\xB7 y pitch \xC2\xB7 drag a dot or its handle", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (ev.press)
        {
            if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth() || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
                return false;
            fDrag = hitTest(ev.pos);
            const auto f = fieldsOf(*fDrag);
            fCallback->scatterDragStarted(static_cast<Field>(f.first), static_cast<Field>(f.second));
            // A dot jumps to the pointer; a handle waits for a move (one drawn at the edge isn't
            // where its value is).
            if (*fDrag == Target::T1Dot || *fDrag == Target::T2Dot) apply(ev.pos);
            fMoved = false;
            return true;
        }
        if (!fDrag) return false;
        if (fMoved) apply(ev.pos);
        const auto f = fieldsOf(*fDrag);
        fDrag.reset();
        fCallback->scatterDragFinished(static_cast<Field>(f.first), static_cast<Field>(f.second));
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fDrag) return false;
        fMoved = true;
        apply(ev.pos);
        return true;
    }

private:
    enum class Target { T1Dot, T1Handle, T2Dot, T2Handle };

    // Contraction → the handle's distance (of the plot); the minimum keeps it grabbable near 0.
    static constexpr float kRMin = 0.12f, kRSpan = 0.30f;

    float pad() const { return 10.0f * fUiScale; }
    static float clamp01(float v) { return std::max(0.0f, std::min(1.0f, v)); }
    static float clampC(float v) { return std::max(0.01f, std::min(0.99f, v)); }

    static std::pair<int, int> fieldsOf(Target t)
    {
        switch (t)
        {
        case Target::T1Dot: return {T1X, T1Y};
        case Target::T1Handle: return {T1Contraction, T1Rotation};
        case Target::T2Dot: return {T2X, T2Y};
        default: return {T2Contraction, T2Rotation};
        }
    }

    static void handleOf(float x, float y, float c, float deg, float& hx, float& hy)
    {
        const float r = kRMin + kRSpan * clampC(c), th = deg * 3.14159265f / 180.0f;
        hx = x + r * std::cos(th);
        hy = y + r * std::sin(th);
    }

    void toScreen(float nx, float ny, float& sx, float& sy) const
    {
        sx = pad() + nx * (getWidth() - 2 * pad());
        sy = pad() + (1.0f - ny) * (getHeight() - 2 * pad());
    }
    // A handle can reach past the plot (a transform near an edge, turned outward): it's drawn and
    // grabbed at the view's edge, along its line, so it can always be reached.
    void handleToScreen(float fx, float fy, float hx, float hy, float& sx, float& sy) const
    {
        float ax, ay;
        toScreen(fx, fy, ax, ay);
        toScreen(hx, hy, sx, sy);
        const float m = 6.0f * fUiScale, x0 = m, y0 = m, x1 = getWidth() - m, y1 = getHeight() - m;
        float t = 1.0f;
        if (sx < x0 && sx < ax) t = std::min(t, (x0 - ax) / (sx - ax));
        if (sx > x1 && sx > ax) t = std::min(t, (x1 - ax) / (sx - ax));
        if (sy < y0 && sy < ay) t = std::min(t, (y0 - ay) / (sy - ay));
        if (sy > y1 && sy > ay) t = std::min(t, (y1 - ay) / (sy - ay));
        t = std::max(t, 0.0f);
        sx = ax + (sx - ax) * t;
        sy = ay + (sy - ay) * t;
    }

    Target hitTest(const Point<double>& pos) const
    {
        float h1x, h1y, h2x, h2y;
        handleOf(fV[T1X], fV[T1Y], fV[T1Contraction], fV[T1Rotation], h1x, h1y);
        handleOf(fV[T2X], fV[T2Y], fV[T2Contraction], fV[T2Rotation], h2x, h2y);
        const struct { Target t; float x, y, fx, fy; bool handle; } c[] = {
            {Target::T1Dot, fV[T1X], fV[T1Y], 0, 0, false}, {Target::T2Dot, fV[T2X], fV[T2Y], 0, 0, false},
            {Target::T1Handle, h1x, h1y, fV[T1X], fV[T1Y], true}, {Target::T2Handle, h2x, h2y, fV[T2X], fV[T2Y], true},
        };
        Target best = c[0].t;
        float  bestD = 1e30f;
        for (const auto& k : c)
        {
            float sx, sy;
            if (k.handle) handleToScreen(k.fx, k.fy, k.x, k.y, sx, sy);
            else toScreen(k.x, k.y, sx, sy);
            const float d = std::hypot(static_cast<float>(pos.getX()) - sx, static_cast<float>(pos.getY()) - sy);
            if (d < bestD) { bestD = d; best = k.t; }
        }
        return best;
    }

    void set(int field, float v)
    {
        fV[field] = v;
        fCallback->scatterFieldChanged(static_cast<Field>(field), v);
    }

    void apply(const Point<double>& pos)
    {
        const float nx = clamp01((static_cast<float>(pos.getX()) - pad()) / (getWidth() - 2 * pad()));
        const float ny = clamp01(1.0f - (static_cast<float>(pos.getY()) - pad()) / (getHeight() - 2 * pad()));
        const auto  f  = fieldsOf(*fDrag);
        if (*fDrag == Target::T1Dot || *fDrag == Target::T2Dot)
        {
            set(f.first, nx);
            set(f.second, ny);
        }
        else
        {
            const int   x = *fDrag == Target::T1Handle ? T1X : T2X;
            const float dx = nx - fV[x], dy = ny - fV[x + 1];
            set(f.first, clampC((std::hypot(dx, dy) - kRMin) / kRSpan));
            set(f.second, std::atan2(dy, dx) * 180.0f / 3.14159265f);
        }
        repaint();
    }

    void drawTransform(float x, float y, float c, float deg, Color color)
    {
        const float s = fUiScale;
        float hx, hy, fx, fy, sx, sy;
        handleOf(x, y, c, deg, hx, hy);
        toScreen(x, y, fx, fy);
        handleToScreen(x, y, hx, hy, sx, sy);
        strokeColor(Color(color.red, color.green, color.blue, 0.7f)); strokeWidth(1.5f * s);
        beginPath(); moveTo(fx, fy); lineTo(sx, sy); stroke();
        beginPath(); circle(sx, sy, 4.5f * s); strokeColor(color); strokeWidth(1.6f * s); stroke();
        beginPath(); circle(fx, fy, 6.0f * s); fillColor(color); fill();
        strokeColor(Color(1.0f, 1.0f, 1.0f, 0.6f)); strokeWidth(0.8f * s); stroke();
    }

    Callback*                fCallback;
    bool                     fHasFont = false;
    float                    fUiScale = 1.0f;
    float                    fV[8] = {0.2f, 0.4f, 0.6f, 25.0f, 0.8f, 0.6f, 0.6f, -25.0f};
    std::vector<CloudPoint>  fCloud;
    std::optional<Target>    fDrag;
    bool                     fMoved = false;

    const Color kT1 { 170, 130, 235 };   // violet, as PMU's T1 purple
    const Color kT2 { 230, 200, 90 };    // yellow, as PMU's T2

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScatterEditorView)
};

END_NAMESPACE_DGL

#endif // NOTEGRAN_SCATTEREDITORVIEW_HPP
