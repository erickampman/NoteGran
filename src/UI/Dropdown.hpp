// Dropdown.hpp — a reusable labelled dropdown/combo control for DPF NanoVG UIs. It draws a
// caption + a box showing the current choice + a chevron; clicking asks the host UI to open a
// PopupMenu (the UI owns the menu so it can draw over every other panel). Identify the bound
// parameter with Widget::setId(); the UI writes the chosen index back via setIndex().
// Copied from StruckSynth's src/UI (the house widget kit); NoteGran's changes are marked "NoteGran:".

#ifndef NOTEGRAN_DROPDOWN_HPP
#define NOTEGRAN_DROPDOWN_HPP

#include "NanoVG.hpp"

#include <string>
#include <vector>

START_NAMESPACE_DGL

class Dropdown : public NanoSubWidget
{
public:
    class Callback
    {
    public:
        virtual ~Callback() {}
        virtual void dropdownClicked(Dropdown* dropdown) = 0;
    };

    explicit Dropdown(Widget* parent, Callback* callback)
        : NanoSubWidget(parent), fCallback(callback)
    {
        fHasFont = loadSharedResources();
    }

    void setLabel(const std::string& label)          { fLabel = label; }
    // Text shown when there is no valid selection (fIndex out of range) — e.g. the preset box
    // before any preset is chosen. Empty by default, so ordinary dropdowns render nothing.
    void setPlaceholder(const std::string& p)        { fPlaceholder = p; }
    void setChoices(const std::vector<std::string>& c){ fChoices = c; }
    void setIndex(int i)                              { fIndex = i; repaint(); }
    // NoteGran: a disabled menu is dimmed and ignores clicks (its setting is unused just now).
    void setEnabled(bool e)                           { if (fEnabled != e) { fEnabled = e; repaint(); } }
    // Scales font size AND the label/box geometry together — scaling font alone (without the
    // box it sits in) is what causes text to overflow into neighbouring widgets.
    void setUiScale(float s)
    {
        fUiScale = s;
        fBoxH   = static_cast<int>(26 * s);
        fInset  = static_cast<int>(5 * s);   // small gap so the box nearly fills its cell
        fLabelH = fCompact ? 0 : static_cast<int>(16 * s);
    }
    // NoteGran: no caption band, for a table's cells (the row and column name it).
    void setCompact(bool c) { fCompact = c; setUiScale(fUiScale); }

    int index() const { return fIndex; }
    const std::vector<std::string>& choices() const { return fChoices; }

    // The box rectangle in window-absolute coords, so the UI can position the popup below it.
    void boxRect(int& x, int& y, int& w, int& h) const
    {
        x = getAbsoluteX() + fInset;
        y = getAbsoluteY() + boxTop();
        w = static_cast<int>(getWidth()) - 2 * fInset;
        h = fBoxH;
    }

protected:
    void onNanoDisplay() override
    {
        const float w  = getWidth();
        const float bx = fInset, bw = w - 2 * fInset, by = boxTop(), bh = fBoxH;

        if (fHasFont && !fLabel.empty())   // NoteGran: a compact menu has none
        {
            // Label sits in the reserved band above the box (always inside bounds).
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            // NoteGran: a long label shrinks, leaving a gap to the next control's (cells abut).
            fontSize(13.0f * fUiScale);
            Rectangle<float> lb;
            textBounds(0, 0, fLabel.c_str(), nullptr, lb);
            const float lroom = w - 8.0f * fUiScale;
            if (lb.getWidth() > lroom && lb.getWidth() > 0.0f)
                fontSize(std::fmax(13.0f * fUiScale * 0.6f, 13.0f * fUiScale * lroom / lb.getWidth()));
            fillColor(Color(176, 182, 190));
            text(w * 0.5f, by - fLabelH + 1.0f, fLabel.c_str(), nullptr);
        }

        beginPath(); roundedRect(bx, by, bw, bh, 4.0f);
        fillColor(Color(34, 37, 43)); fill();
        strokeColor(Color(70, 76, 86)); strokeWidth(1.0f); stroke();

        if (fHasFont && fIndex >= 0 && fIndex < static_cast<int>(fChoices.size()))
        {
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            // NoteGran: a choice wider than the box (beside its chevron) shrinks to fit.
            fontSize(13.0f * fUiScale);
            Rectangle<float> tb;
            textBounds(0, 0, fChoices[fIndex].c_str(), nullptr, tb);
            const float room = bw - 7.0f - 20.0f;
            if (tb.getWidth() > room && tb.getWidth() > 0.0f)
                fontSize(std::fmax(13.0f * fUiScale * 0.6f, 13.0f * fUiScale * room / tb.getWidth()));
            fillColor(fEnabled ? Color(226, 230, 236) : Color(110, 115, 122));
            text(bx + 7.0f, by + bh * 0.5f, fChoices[fIndex].c_str(), nullptr);
        }
        else if (fHasFont && !fPlaceholder.empty())
        {
            // No valid selection: dim placeholder so the box never implies a preset is loaded.
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fontSize(13.0f * fUiScale);
            fillColor(Color(120, 126, 134));
            text(bx + 7.0f, by + bh * 0.5f, fPlaceholder.c_str(), nullptr);
        }

        // chevron
        const float cx = bx + bw - 11.0f, cy = by + bh * 0.5f;
        beginPath(); moveTo(cx - 4, cy - 2); lineTo(cx + 4, cy - 2); lineTo(cx, cy + 3); closePath();
        fillColor(Color(150, 160, 172)); fill();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        // DPF offers every sibling this event regardless of position — only claim our own area.
        if (ev.button != 1 || !ev.press || !fEnabled)
            return false;
        if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
            || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
            return false;
        fCallback->dropdownClicked(this);
        return true;
    }

private:
    // Reserve kLabelH at the top for the caption; centre the label+box pair, clamped so a short
    // widget keeps the label inside its bounds.
    float boxTop() const
    {
        const float rem = getHeight() - (fLabelH + fBoxH);
        return (rem > 0.0f ? rem * 0.5f : 0.0f) + fLabelH;
    }

    Callback*                fCallback;
    bool                     fEnabled = true;
    std::string              fLabel;
    std::string              fPlaceholder;
    std::vector<std::string> fChoices;
    int                      fIndex = 0;
    bool                     fHasFont = false;
    bool                     fCompact = false;   // NoteGran
    float                    fUiScale = 1.0f;

    int fBoxH = 26, fInset = 12, fLabelH = 16;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Dropdown)
};

END_NAMESPACE_DGL

#endif // NOTEGRAN_DROPDOWN_HPP
