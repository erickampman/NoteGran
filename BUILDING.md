# Building NoteGran

NoteGran builds the same way on Linux and macOS: CMake + Ninja, git submodules, C++20. The build
makes the core library `notegran_core`, the plugin `NoteGran.clap` (NoteGran Cloud, Chords, Grid,
Burst and Buffer, with their editors), and two test programs: `notegran-core-tests` (the core) and
`notegran-clap-tests` (a headless host that loads the built `.clap`).

## What you need

**macOS:** macOS 26 or later on Apple Silicon (arm64). Intel Macs aren't supported.

- The Xcode Command Line Tools: `xcode-select --install`. They bring clang, the macOS SDK, git and
  Python 3.
- [Homebrew](https://brew.sh), then `brew install cmake ninja`.
- Full Xcode isn't needed.

**Linux:** a C++20 compiler (GCC 11+ or Clang 14+), CMake 3.21+, Ninja, Python 3 and git. On Debian
or Ubuntu this installs them and every library the build needs:

```bash
sudo apt install build-essential cmake ninja-build pkg-config python3 git \
  libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxinerama-dev \
  libxcb1-dev libx11-xcb-dev libxcb-util-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libfontconfig1-dev libasound2-dev libjack-jackd2-dev \
  libxfixes-dev libxi-dev libxss-dev libxtst-dev libwayland-dev wayland-protocols \
  libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev libdecor-0-dev libudev-dev \
  libdbus-1-dev libibus-1.0-dev
```

It's one list for every Unlikelyware project (the same one CI installs), so it covers more than
NoteGran needs. On other distros, install the same libraries under their own names.

Of those libraries NoteGran uses only the X11 and OpenGL ones, for the editors. It builds uw-core
with `UW_CORE_UMP_ONLY`, which makes only `uw::ump` (no JACK, ALSA, CoreAudio or CoreMIDI). The
editors use DPF (`external/dpf`) for drawing only: DGL, NanoVG and pugl, as StruckSynth does. DPF is
the author's fork (`erickampman/DPF`, which pins the pugl fork `erickampman/pugl`), as every DPF
user's is; while the repos are private, cloning needs access to both. Python 3 embeds the logo at
build time.

## Get the source

```bash
git clone https://github.com/erickampman/NoteGran.git
cd NoteGran
tools/fetch_deps.sh
```

`tools/fetch_deps.sh` checks out the git submodules at their pinned commits, with DPF's own pugl
submodule. Run it again after a `git pull` that moves, adds or re-points a submodule: a plain pull
leaves them where they were.

## Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The same three commands on both platforms. Everything goes in `build/` (ignored by git).

The plugin is `build/NoteGran.clap`: a shared object on Linux, a bundle directory on macOS.

`clap-validator validate build/NoteGran.clap` should pass with no failures (the skipped tests are
for audio ports, presets and 64-bit audio, which a note effect doesn't have).

To build against a uw-core change that isn't pushed and pinned yet, add
`-DNOTEGRAN_UW_CORE_DIR=/path/to/uw-core` to the first command.

## Installing the plugin for hosts

A plain `cmake --build build` writes nothing outside `build/`. These targets put the plugin where
hosts look for it:

```bash
cmake --build build --target install-clap
```

| | Linux | macOS |
|---|---|---|
| CLAP | `~/.clap` | `~/Library/Audio/Plug-Ins/CLAP` |

`install-clap` symlinks the plugin in `build/`, so a host picks up a rebuild after a rescan. The
link points into this clone: delete the clone and the host loses the plugin, and running the
target in another build folder points the host there instead.

To keep the plugin without the clone, copy it instead (the `rm` clears a link the targets left):

```bash
# Linux
mkdir -p ~/.clap
rm -rf ~/.clap/NoteGran.clap
cp build/NoteGran.clap ~/.clap/
```

```bash
# macOS
mkdir -p ~/Library/Audio/Plug-Ins/CLAP
rm -rf ~/Library/Audio/Plug-Ins/CLAP/NoteGran.clap
cp -R build/NoteGran.clap ~/Library/Audio/Plug-Ins/CLAP/
```

For every user instead, the same folders without `~` (`/usr/lib/clap` on Linux,
`/Library/Audio/Plug-Ins/...` on macOS), with `sudo`. Then rescan in the host. A host that is
already running keeps the plugin it loaded first: quit and restart it after a rebuild. A plugin you
built yourself has no quarantine flag, so macOS loads it without the steps a downloaded one needs.
