// HelpButton.hpp — a "?" box with a caption above it, styled like Dropdown so the two sit together
// in the header. A click writes the embedded help page to a folder under the system temp directory
// and opens it in the system's default browser, so the built plugin still has no filesystem
// dependency of its own. The images it shows go inside it, as data: URLs: a sandboxed browser (snap
// or Flatpak Firefox on Linux) is handed only the one file opened, not the files beside it.
// Copied from Retune's src/UI, itself from StruckSynth's (the house widget kit); Retune's changes are
// marked "Retune:".

#ifndef NOTEGRAN_HELPBUTTON_HPP
#define NOTEGRAN_HELPBUTTON_HPP

#include "NanoVG.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

START_NAMESPACE_DGL

class HelpButton : public NanoSubWidget
{
public:
    // A file the page needs, under `name` (the name the HTML refers to): an image goes into the page.
    struct File
    {
        const char*  name;
        const uchar* data;
        uint         size;
    };

    // files[0] is the page itself; the rest are the images it shows (`src="name"`).
    HelpButton(Widget* parent, std::vector<File> files)
        : NanoSubWidget(parent), fFiles(std::move(files))
    {
        fHasFont = loadSharedResources();
    }

    void setLabel(const std::string& label) { fLabel = label; }   // Retune: empty, the box alone
    // Retune: the folder under the temp directory the page is written to.
    void setFolder(const std::string& folder) { fFolder = folder; }
    void setUiScale(float s)
    {
        fUiScale = s;
        fBoxH   = static_cast<int>(26 * s);
        fLabelH = fLabel.empty() ? 0 : static_cast<int>(16 * s);   // Retune: no caption, no room for one
    }

protected:
    void onNanoDisplay() override
    {
        const float w = getWidth();
        const float by = boxTop(), bh = fBoxH, bw = bh, bx = (w - bw) * 0.5f;

        if (fHasFont && !fLabel.empty())
        {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fontSize(13.0f * fUiScale);
            fillColor(Color(176, 182, 190));
            text(w * 0.5f, by - fLabelH + 1.0f, fLabel.c_str(), nullptr);
        }

        beginPath(); roundedRect(bx, by, bw, bh, 4.0f);
        fillColor(fPressed ? Color(48, 52, 60) : Color(34, 37, 43)); fill();
        strokeColor(Color(70, 76, 86)); strokeWidth(1.0f); stroke();

        if (fHasFont)
        {
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fontSize(15.0f * fUiScale);
            fillColor(Color(226, 230, 236));
            text(bx + bw * 0.5f, by + bh * 0.5f, "?", nullptr);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        // DPF offers every sibling this event regardless of position — only claim our own area.
        if (ev.button != 1)
            return false;
        if (!ev.press)
        {
            if (!fPressed)
                return false;
            fPressed = false;
            repaint();
            return true;
        }
        if (ev.pos.getX() < 0 || ev.pos.getX() >= getWidth()
            || ev.pos.getY() < 0 || ev.pos.getY() >= getHeight())
            return false;
        fPressed = true;
        repaint();
        openHelp();
        return true;
    }

private:
    float boxTop() const
    {
        const float rem = getHeight() - (fLabelH + fBoxH);
        return (rem > 0.0f ? rem * 0.5f : 0.0f) + fLabelH;
    }

    // Rewritten on every click, so a newer build's page replaces an older one's.
    void openHelp() const
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / fFolder;
        if (ec) return;
        fs::create_directories(dir, ec);
        if (ec) return;

        std::string html(reinterpret_cast<const char*>(fFiles.front().data), fFiles.front().size);
        for (size_t i = 1; i < fFiles.size(); ++i)
        {
            const std::string src = "src=\"" + std::string(fFiles[i].name) + "\"";
            const std::string url = "src=\"data:" + mimeType(fFiles[i].name) + ";base64,"
                                  + base64(fFiles[i].data, fFiles[i].size) + "\"";
            for (size_t at = html.find(src); at != std::string::npos; at = html.find(src, at + url.size()))
                html.replace(at, src.size(), url);
        }
        {
            std::ofstream out(dir / fFiles.front().name, std::ios::binary | std::ios::trunc);
            if (!out) return;
            out.write(html.data(), static_cast<std::streamsize>(html.size()));
        }

        // The path is ours (temp dir + fixed names), but quote it: temp paths can contain spaces.
        const std::string page = (dir / fFiles.front().name).string();
#if defined(DISTRHO_OS_MAC)
        const std::string cmd = "open \"" + page + "\"";
#elif defined(DISTRHO_OS_WINDOWS)
        const std::string cmd = "start \"\" \"" + page + "\"";
#else
        const std::string cmd = "xdg-open \"" + page + "\" >/dev/null 2>&1 &";
#endif
        const int rc = std::system(cmd.c_str());
        (void)rc; // best-effort; nothing sensible to do if the browser launch fails
    }

    static std::string mimeType(const std::string& name)
    {
        const std::string ext = name.substr(name.find_last_of('.') + 1);
        if (ext == "png") return "image/png";
        if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
        if (ext == "svg") return "image/svg+xml";
        return "application/octet-stream";
    }

    static std::string base64(const uchar* data, uint size)
    {
        static const char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((size + 2) / 3 * 4);
        for (uint i = 0; i < size; i += 3)
        {
            const uint n = (uint(data[i]) << 16) | (i + 1 < size ? uint(data[i + 1]) << 8 : 0u)
                         | (i + 2 < size ? uint(data[i + 2]) : 0u);
            out += kDigits[(n >> 18) & 63];
            out += kDigits[(n >> 12) & 63];
            out += i + 1 < size ? kDigits[(n >> 6) & 63] : '=';
            out += i + 2 < size ? kDigits[n & 63] : '=';
        }
        return out;
    }

    std::vector<File> fFiles;
    std::string       fLabel;
    std::string       fFolder = "NoteGran-help";
    bool              fHasFont = false;
    bool              fPressed = false;
    float             fUiScale = 1.0f;

    int fBoxH = 26, fLabelH = 16;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpButton)
};

END_NAMESPACE_DGL

#endif // NOTEGRAN_HELPBUTTON_HPP
