# Changelog

All notable changes to NoteGran. Versions follow the git tags; the format is
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

## [0.9.1] - 2026-11-26

### Added

- Chords follows a slide: a held note's Pitch 7.25 (KeyboardEDO's Glide, a CLAP tuning
  expression) moves its whole chord with it, keeping the chord's shape, whether the slide
  lands on the scale or off it. A tuning sent with the note-on picks the chord where the note
  really is.
- An AUv2 build on macOS: `NoteGran.component`, with Cloud, Chords, Grid, Burst and Buffer each
  an AUv2 MIDI effect (`aumi`, Logic's MIDI FX), built through Unlikelyware's clap-wrapper fork. It keeps MIDI 2.0, so microtonal notes
  (Pitch 7.9) reach the next plugin unrounded. `NOTEGRAN_BUILD_AUV2` (on on macOS) and
  `install-auv2`.

### Fixed

- Chords: sliding from a chorded note onto an off-scale step (KeyboardEDO without Glide, in an
  EDO) no longer goes silent. At one sample, a note's end now goes out before a note passed
  through on the same key.

### Changed

- Help opens the help page in a window of the plugin's own at the effect's section, not in the web browser. Nothing is
  written to the temp folder, and on Linux no web server runs in the host any more.

## [0.9.0] - 2026-11-14

First public preview.

### Added

- Granular synthesis with notes as the grains, in five MIDI note effects: Cloud, Chords, Grid, Burst
  and Buffer.
- Microtonal throughout: pitches in 1/512 semitone, sent as MIDI 2.0 Pitch 7.9.
- Repeatable with a seed: the same input at the same times gives the same notes.
- CLAP plugin for Linux (x86-64) and macOS 26 or later (Apple Silicon).
