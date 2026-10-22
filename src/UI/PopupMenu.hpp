// PopupMenu.hpp — a reusable overlay menu for DPF NanoVG UIs. It is meant to be a UI-level
// subwidget created LAST (so it draws on top of every panel and, while visible, is offered
// mouse events first — modal capture). The UI opens it for a given control, and gets the chosen
// row back via the Callback. A `tag` carries the caller's identity (e.g. a host param index).
// Copied from StruckSynth's src/UI (the house widget kit); NoteGran's changes are marked "NoteGran:".

#ifndef NOTEGRAN_POPUPMENU_HPP
#define NOTEGRAN_POPUPMENU_HPP

#include "NanoVG.hpp"

#include <cstdint>
#include <string>
#include <vector>

START_NAMESPACE_DGL

class PopupMenu : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void menuSelected(PopupMenu* menu, int index) = 0;
    };

    explicit PopupMenu(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
        hide();
    }

    // Open anchored to a control's box (window-absolute). Normally drops below the box (at
    // boxBottom); if that would run past the bottom of the window it flips to open above the box
    // instead (boxTop - height), so bottom-row dropdowns aren't clipped. windowH is the UI height.
    void openFor(uint32_t tag, const std::vector<std::string>& items, int current,
                 int x, int boxBottom, int boxTop, int width, int windowH)
    {
        fTag = tag; fItems = items; fCurrent = current; fHover = -1;
        const int h = static_cast<int>(items.size()) * fRowH + 2 * fPad;
        // Width: at least the anchor box, but widened to fit the longest label — menus like the
        // 26 materials have items far longer than the compact box that opens them.
        const float fs = kFontBase * fUiScale;
        size_t maxChars = 1;
        for (const auto& it : items) maxChars = std::max(maxChars, it.size());
        const int contentW = static_cast<int>(maxChars * fs * 0.66f + 34 * fUiScale);
        const int useW = std::max(width, contentW);
        int y = boxBottom;
        if (y + h > windowH)   // not enough room below → open above the box
            y = boxTop - h;
        if (y < 0)             // last-resort clamp for a very short window
            y = 0;
        setAbsolutePos(x, y);
        setSize(static_cast<uint>(useW), static_cast<uint>(h));
        toFront();
        show();
        repaint();
    }

    // Close without selecting anything (focus loss, Escape).
    void dismiss()
    {
        if (!isVisible())
            return;
        hideAndRedraw();
    }

    uint32_t tag() const { return fTag; }
    // Scales font size AND row height/padding together — scaling font alone (without the row
    // it sits in) is what causes menu text to overlap adjacent rows.
    void setUiScale(float s)
    {
        fUiScale = s;
        fRowH = static_cast<int>(25 * s);
        fPad  = static_cast<int>(4 * s);
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth(), h = getHeight();
        beginPath(); roundedRect(0, 0, w, h, 5.0f);
        fillColor(Color(38, 42, 49)); fill();
        strokeColor(Color(80, 86, 96)); strokeWidth(1.0f); stroke();

        for (int i = 0; i < static_cast<int>(fItems.size()); ++i)
        {
            const float ry = fPad + i * fRowH;
            if (i == fHover)
            {
                beginPath(); rect(2, ry, w - 4, fRowH);
                fillColor(Color(63, 185, 207, 60)); fill();
            }
            if (fHasFont)
            {
                fontFace(NANOVG_DEJAVU_SANS_TTF);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fontSize(kFontBase * fUiScale);
                fillColor(i == fCurrent ? Color(120, 210, 225) : Color(226, 230, 236));
                text(12.0f * fUiScale, ry + fRowH * 0.5f, fItems[i].c_str(), nullptr);
            }
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!isVisible())
            return false;
        if (ev.press) // any press while open resolves (select a row, or dismiss)
        {
            const int idx = rowAt(ev.pos.getX(), ev.pos.getY());
            hideAndRedraw();
            if (idx >= 0)
                fCallback->menuSelected(this, idx);
        }
        return true; // modal: consume all events while visible
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!isVisible())
            return false;
        if (ev.press && ev.key == kKeyEscape)
            dismiss();
        return true; // modal
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!isVisible())
            return false;
        const int h = rowAt(ev.pos.getX(), ev.pos.getY());
        if (h != fHover) { fHover = h; repaint(); }
        return true;
    }

private:
    // SubWidget::repaint() is a no-op once the widget is hidden, so hide() + repaint() leaves the
    // menu's last frame on screen. Invalidate the window area under the menu explicitly instead.
    void hideAndRedraw()
    {
        hide();
        if (TopLevelWidget* const topw = getTopLevelWidget())
            topw->repaint(getConstrainedAbsoluteArea());
    }

    int rowAt(double mx, double my) const
    {
        if (mx < 0 || mx > getWidth()) return -1;
        const double ry = my - fPad;
        if (ry < 0) return -1;
        const int idx = static_cast<int>(ry / fRowH);
        return (idx >= 0 && idx < static_cast<int>(fItems.size())) ? idx : -1;
    }

    Callback*                fCallback;
    std::vector<std::string> fItems;
    int                      fCurrent = 0;
    int                      fHover = -1;
    uint32_t                 fTag = 0;
    bool                     fHasFont = false;
    float                    fUiScale = 1.0f;

    static constexpr float   kFontBase = 14.5f; // shared by openFor width calc + onNanoDisplay
    int fRowH = 24, fPad = 4;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PopupMenu)
};

END_NAMESPACE_DGL

#endif // NOTEGRAN_POPUPMENU_HPP
