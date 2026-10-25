# NoteGran milestones

## Tests
- all: cmake -S . -B build -G Ninja && ninja -C build && ctest --test-dir build --output-on-failure
- validator: clap-validator validate build/NoteGran.clap (no failures; skipped: audio ports, presets, 64-bit audio)
- install: ninja -C build install-clap (symlinked)

## M0: Plan
- [x] docs/design.md: note sets and assigners, the shared core, the five effects (2026-10-21)
- [x] Counter-based random draws, keyed on the clock, so output is seekable and block-size independent
- [x] Voices (1–32, default 8) and When full (Skip | Steal) in every effect
- [x] UMP code from uw-core's uw::ump, not NoteGran's own

## M1: Core library
- [x] The set: hold-over, pitch moves, latch, set and trigger channels, Dry
- [x] Clock: Free and Beats, from an absolute sample count; transport stops and jumps
- [x] Owned-note table, Voices, the same-key rule
- [x] Pitch: 7.9 maths, Octaves, Transpose, Scatter, Quantize (EDO, Set), nearest free key
- [x] Tests: notes always end, repeated strikes keep the set, any block size gives the same notes, same seed same notes, MIDI 1.0 / 2.0 / CLAP dialects

## M2: NoteGran Cloud
- [x] CLAP plugin: factory, note ports, 25 parameters, state; clap-validator clean; headless host test
- [x] Heard in StageHand: KeyboardEDO → Cloud → a synth (2026-10-21)
- [x] Heard in Bitwig with notes (2026-10-24)
- [ ] Heard: a one-slot sequence as the set; KeyboardEDO in 31-EDO with Quantize EDO

## M3: NoteGran Chords
- [x] Familiar scales stored spelled, mapped into any EDO (By fifths | Nearest); scale from a channel
- [x] Size, Stack, Inversion, Voicing, Octave; Off-scale Pass | Nearest degree | Drop
- [x] Tested in the core and through the .clap; clap-validator clean
- [x] Heard in StageHand: KeyboardEDO → Chords → a synth (2026-10-21)
- [ ] Heard: C lydian dominant in 19-EDO from a 12-EDO keyboard; KeyboardEDO in 19-EDO with Input EDO; Chords → Cloud

## M4: NoteGran Grid
- [x] Meters and tiers after ClockGran; per-tier Probability, Level, Offset, Chaos, Pick
- [x] Tested in the core and through the .clap; clap-validator clean
- [x] Heard in StageHand: KeyboardEDO → Grid → a synth (2026-10-21)

## M5: NoteGran Burst
- [x] IFS attractor phrases, Release Finish | Stop | Loop, Vary; tested; clap-validator clean
- [x] Heard with notes (2026-10-24)
- [ ] Checked: Loop and Stop; a trigger channel

## M6: NoteGran Buffer
- [x] Capture ring, Freeze, Position / Scan / Spray, Note and Fragment grains, Gate; tested
- [x] Heard: StageHand looper feeding it, Freeze, REAPER (12-EDO only), Bitwig (microtonal output intact)
- [x] Fragment notes keep their real length instead of being cut at the grain's end (decided 2026-10-24); replaced or reset fragments still end their sounding notes
- [ ] An editor view of its own: the capture on a timeline, the read point and spray, the fragment's span

## M7: Editors
- [x] DPF for drawing only (DGL, NanoVG, pugl); the house widget kit; one shell, an accent per effect
- [x] Cloud, Chords, Grid and Burst editors with live views and previews
- [x] All five checked with notes playing in Bitwig (2026-10-24)

## M8: Hosts (by hand)
- [x] Bitwig, Linux, CLAP: all five effects, microtonal output reaches the synth (2026-10-24)
- [x] REAPER, Linux: works, but plays the output as 12-EDO (2026-10-24)
- [ ] Bitwig and REAPER on macOS
- [ ] StageHand on both platforms, KeyboardEDO driving it

## M9: Other formats (clap-wrapper)
- [ ] VST3 and AUv2 (macOS) via clap-wrapper, MIDI 1.0 (rounded) only, as Retune's M4

## Later
- [ ] NoteGran Echo: a Markov generator that answers in the style of what's played
- [ ] Glides: each grain bends with MIDI 2.0 per-note pitch bend over its life
- [ ] Modal quantize: grains on the partial ratios of struck materials (pairs with StruckSynth)
- [ ] Chords: Inversion Nearest (voice leading), chord shapes (4:5:6:7, neutral triads), related chords
- [ ] Native EDO scales (MOS) beside the familiar ones
