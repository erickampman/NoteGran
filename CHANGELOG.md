# Changelog

All notable changes to NoteGran. Versions follow the git tags; the format is
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

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
