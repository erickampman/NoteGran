# Building NoteGran

NoteGran builds the same way on Linux and macOS: CMake + Ninja, git submodules, C++20. The build
makes the core library `notegran_core`, the plugin `NoteGran.clap` (NoteGran Cloud, Chords, Grid and
Burst, with their editors), and two test
programs: `notegran-core-tests` (the core) and `notegran-clap-tests` (a headless host that loads
the built `.clap`).

## What you need

| | Linux | macOS |
|---|---|---|
| Compiler | GCC 11+ or Clang 14+ (C++20) | Xcode Command Line Tools (Apple Clang) |
| CMake | 3.21+ | 3.21+ |
| Ninja | yes | yes |
| Install | `sudo apt install build-essential cmake ninja-build git` | `xcode-select --install`, then `brew install cmake ninja` |
| For the editors | `sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev` | nothing more |

No JACK, ALSA, CoreAudio or CoreMIDI is needed: NoteGran builds uw-core with `UW_CORE_UMP_ONLY`,
which makes only `uw::ump`. The editors use DPF (`external/dpf`) for drawing only: DGL, NanoVG and
pugl, as StruckSynth does. DPF is the author's fork (`erickampman/DPF`, which pins the pugl fork
`erickampman/pugl` with PMU's macOS patches), as every DPF user's is (UmpireDAW's
`docs/decisions.md`, "DPF fork"); both are private, so cloning needs access to them. Python 3 embeds the logo at build time.

## Get the source

```bash
git clone https://github.com/erickampman/NoteGran.git
cd NoteGran
git submodule update --init --recursive
```

`--recursive` matters: DPF has pugl as a submodule of its own.

After a `git pull` that moves a submodule pin, adds a submodule, or changes a submodule's URL, run

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

A plain `git pull --recurse-submodules` moves pins but neither fetches a new submodule nor follows a
changed URL (as when DPF moved from upstream to the fork, 2026-10-22).

## Build and test

```bash
cmake -S . -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
```

The plugin is `build/NoteGran.clap`: a shared object on Linux, a bundle directory on macOS. To
use it in a host, link it into the CLAP folder (`~/.clap` on Linux,
`~/Library/Audio/Plug-Ins/CLAP` on macOS):

```bash
ninja -C build install-clap
```

`clap-validator validate build/NoteGran.clap` should pass with no failures (the skipped tests are
for audio ports, presets and 64-bit audio, which a note effect doesn't have).

To build against a uw-core change that isn't pushed and pinned yet, add
`-DNOTEGRAN_UW_CORE_DIR=/path/to/uw-core` to the first command.
