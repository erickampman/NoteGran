# Building NoteGran

NoteGran builds the same way on Linux and macOS: CMake + Ninja, git submodules, C++20. For now
(phase 1) the build makes the core library `notegran_core` and its test program
`notegran-core-tests`. The `.clap` comes in phase 2.

## What you need

| | Linux | macOS |
|---|---|---|
| Compiler | GCC 11+ or Clang 14+ (C++20) | Xcode Command Line Tools (Apple Clang) |
| CMake | 3.21+ | 3.21+ |
| Ninja | yes | yes |
| Install | `sudo apt install build-essential cmake ninja-build git` | `xcode-select --install`, then `brew install cmake ninja` |

No JACK, ALSA, CoreAudio or CoreMIDI is needed: NoteGran builds uw-core with `UW_CORE_UMP_ONLY`,
which makes only `uw::ump`.

## Get the source

```bash
git clone https://github.com/erickampman/NoteGran.git
cd NoteGran
git submodule update --init
```

After a `git pull` that moves a submodule pin, run `git submodule update` (or pull with
`--recurse-submodules`).

## Build and test

```bash
cmake -S . -B build -G Ninja
ninja -C build
ctest --test-dir build --output-on-failure
```

To build against a uw-core change that isn't pushed and pinned yet, add
`-DNOTEGRAN_UW_CORE_DIR=/path/to/uw-core` to the first command.
