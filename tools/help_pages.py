#!/usr/bin/env python3
"""Builds the five effects' help pages from resources/help.md.

Each effect's ? box opens a page of its own (resources/help-<effect>.html): the shared cards
(Overview, Getting Started, The Window, the settings groups it has, Hosts and Microtones onwards)
around that effect's card, with a contents card of its own. One page per effect, not one page
opened at a #section: macOS `open` drops a #fragment from a file path, and a sandboxed browser
(snap or Flatpak Firefox on Linux) is handed only the one file opened, so a page can't send it on to
another beside it.

Run from the repo's root after editing help.md:
    tools/help_pages.py [path/to/help-template.html]
The template defaults to the products' shared one, ../../Dev-Audio/Shared/Help/help-template.html
(it inlines its help.css). Needs pandoc.
"""

import re
import subprocess
import sys
import tempfile
from pathlib import Path

EFFECTS = [  # id, name, the shared settings groups its editor has
    ("cloud", "Cloud", ["timing", "pitch", "dynamics", "input"]),
    ("chords", "Chords", ["dynamics", "input"]),
    ("grid", "Grid", ["timing", "pitch", "dynamics", "input"]),
    ("burst", "Burst", ["timing", "pitch", "dynamics", "input"]),
    ("buffer", "Buffer", ["timing", "pitch", "dynamics", "input"]),
]
BEFORE = ["overview", "getting-started", "window", "gestures"]
AFTER = ["hosts", "midi", "tips", "troubleshooting"]
GROUP_NAMES = {"timing": "Timing", "pitch": "Pitch", "dynamics": "Dynamics", "input": "Input"}
PLAYING = "**Playing —** [Hosts and Microtones](#hosts) · [Notes and MIDI](#midi) · [Tips](#tips) · [Troubleshooting](#troubleshooting)"


def split(text):
    """The header and intro before the first card, and the cards by id."""
    starts = [m.start() for m in re.finditer(r"^(?:::: \{|<details)", text, re.M)]
    head = text[: starts[0]]
    cards = {}
    for a, b in zip(starts, starts[1:] + [len(text)]):
        block = text[a:b]
        if block.startswith("<details"):
            cards["gestures"] = block
        elif m := re.search(r"^## .*\{#([\w-]+)\}", block, re.M):
            cards[m.group(1)] = block
        # the source's own On This Page card has no id; each page gets its own
    return head, cards


def page(head, cards, eid, name, groups):
    ids = BEFORE + [eid] + groups + AFTER
    others = " · ".join(n for i, n, _ in EFFECTS if i != eid)
    toc = (
        "::: {.callout .callout-note .card}\n## On This Page\n"
        f"[Overview](#overview) · [Getting Started](#getting-started) · [The Window](#window) · [{name}](#{eid})\n\n"
        "**Shared settings —** " + " · ".join(f"[{GROUP_NAMES[g]}](#{g})" for g in groups) + "\n\n"
        + PLAYING + "\n\n"
        f"**The other effects —** {others}: each has a page of its own, opened from the **?** box in its editor.\n"
        ":::\n\n"
    )
    head = head.replace('<h1 style="margin: 0;">NoteGran</h1>', f'<h1 style="margin: 0;">NoteGran {name}</h1>')
    body = head + toc + "".join(cards[i] for i in ids)
    # A link to a card that isn't on this page (another effect's) becomes its text.
    return re.sub(r"\[([^\]]+)\]\(#([\w-]+)\)", lambda m: m.group(0) if m.group(2) in ids else m.group(1), body)


def main():
    root = Path(__file__).resolve().parent.parent
    template = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "../../Dev-Audio/Shared/Help/help-template.html"
    head, cards = split((root / "resources/help.md").read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        for eid, name, groups in EFFECTS:
            md = Path(tmp) / f"help-{eid}.md"
            md.write_text(page(head, cards, eid, name, groups), encoding="utf-8")
            subprocess.run(["pandoc", "-f", "commonmark_x-smart", "-t", "html5", "--template", str(template),
                            "--metadata", f"pagetitle=help-NoteGran-{name}", str(md),
                            "-o", str(root / f"resources/help-{eid}.html")], check=True)


if __name__ == "__main__":
    main()
