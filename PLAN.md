# NoteGran — plan

A family of MIDI-in → MIDI-out note effects that **generate notes from the notes they hear**: granular
synthesis with notes as the grains, and a chord generator beside it. The synth after it on the row (or
track) makes the sound; NoteGran makes no audio. CLAP-first, with the same layout as Retune: a
hand-written raw CLAP entry point over a pure C++ core, and the vendored free-audio **clap-wrapper**
for the other formats.

**Status (2026-10-21):** phase 1 under way. The build, the owned-note table (`src/core/OwnedNotes`) the set (`src/core/NoteSet`) and the clock (`src/core/Clock`) are done. "NoteGran" is a working name.

Where this came from: StageHand's generators were split in two (2026-10-21). The ones that read what's
played are note effects (this plugin), so they work in StageHand, Umpire and any CLAP host. The ones
that run from their own settings alone (Euclidean rhythms, cellular automata, a random walk) stay
host-side: sources in StageHand (`StageHand/docs/generators.md`, to come) and generator clips in Umpire
(`UmpireDAW/docs/ideas.md`).

## Decisions so far

| Question | Answer |
|---|---|
| What it makes | Note events only. Every grain is a note: a start, a length, a pitch, a velocity |
| How it's divided (2026-10-21) | **Note sets** (which pitches) are produced outside the assigners; **assigners** (when, and which member) are the plugins. Where pitch and time can't be pulled apart, a **whole generator** does both |
| How a set reaches a plugin | **As the notes held on its input.** No new protocol: any MIDI source is a producer |
| A dedicated Note Set source in StageHand | **Not needed.** A sequence of one slot, on Repeat, is a note set; more slots are set changes |
| Base case | **MIDI 2.0**, so pitches can be fractional (Pitch 7.9). MIDI 1.0 is supported, rounded, as in Retune |
| Form | CLAP note effects, then the other formats clap-wrapper supports for MIDI → MIDI |

## The model

```
 producer                transformer (optional)       assigner / generator        instrument
 ─────────               ──────────────────────       ────────────────────        ──────────
 keyboard, KeyboardEDO   NoteGran Chords              NoteGran Cloud / Grid /     the synth
 StageHand sequence  ──▶ (a note → a chord   ──▶      Burst: when, and which  ──▶ on the row
 looper, a host clip      of a scale)                 member of the set
```

- **A producer** is anything that holds notes: a keyboard, **KeyboardEDO** (EDO pitches as Pitch 7.9,
  with per-note bends and glides), a StageHand sequence or looper, a clip in Bitwig or Umpire.
- **A transformer** turns held notes into other held notes. NoteGran Chords is the first.
- **An assigner** turns the set into a stream of notes in time. Cloud, Grid and Burst are assigners.
- **A whole generator** (Buffer, Echo) needs more than a set (the history of what was played, or a
  model of it), so it reads its input its own way.

In StageHand, one producer can play into several rows (a sequence's targets), each with its own
assigner, so a cloud, a fast grid and a slow burst all follow the same chord changes: a harmony track
for the rack. In StageHand these are MIDI effects on the row (`StageHand/docs/midi-effects.md`), in
order: Chords, then Cloud, then the synth.

### A sequence as a note set

- **One slot is enough.** A sequence of one slot holding a chord, on Repeat, holds that chord; the
  rate sets how often it's struck again, which the set ignores (below).
- **More slots are set changes:** a chord per slot, at a rate of a bar or two a slot, is a progression.
- **A looper is a producer too,** and so is KeyboardEDO played live.

## The set (the shared input rule)

The set is what an assigner or Chords is listening to now. The rules:

- **Members are the notes held on the set's input** (all channels, unless the input is split below),
  each with its pitch (7.9, so EDO pitches survive), velocity and arrival order.
- **Struck again isn't a change.** A note-off followed by a note-on of the same pitch within `Hold
  over` (default 20 ms, or the same sample) leaves the member in the set, with the new velocity. A
  sequence on Repeat strikes its notes again every pass; without this, the set would empty for an
  instant at every pass and an assigner could pick from nothing.
- **Pitch moves are followed.** Per-note pitch bend and Pitch 7.25 on a held note (KeyboardEDO's
  glides) move that member's pitch. Grains picked after the move use the new pitch; grains sounding
  keep theirs.
- **A set change comes first.** A note-on or note-off at the same sample as a pick is applied before
  the pick.
- **Velocity is a weight,** where an assigner has weighted picks. Order is by pitch or by arrival, the
  assigner's choice.
- **Splitting the input by channel** (`Set channel`, `Trigger channel`; default: all channels are the
  set): notes on the trigger channel are rhythm, each note-on asking for a pick, so a drum pattern or a
  footswitch can play the set. Channels rather than a second CLAP note port, because hosts route to a
  second note port badly.
- **Latch** (on/off): on, the set stays after its notes are released, until the next note-on starts a
  new one. So a keyboard player can let go.
- **An empty set** makes nothing; grains already sounding end as usual.
- **As built (2026-10-21):** "the same note" is the same group, channel and key, not the same pitch.
  A released member is still in the set until its hold-over ends; a re-strike exactly at the end
  still counts, and a pick at the end doesn't see it. The bend range is the synths' rule: registered
  controller 7, else the channel's 0, else ±2; a struck-again note keeps its 7.25 and bend until a
  Per-Note Management Reset, as in a synth. 512 members; a note past that is classed as a set note
  but not kept.
- **Dry** (on/off): whether the set's notes also sound. Off by default for the assigners, since the set
  is usually a source of pitches, not a part.

## The plugins

One `.clap` exposes several plugins from its factory, one per job, as PMU has one module per granular.
Each has a short parameter list of its own. A single plugin with an Algorithm menu would change what
every parameter means when the menu changes, which suits neither automation nor the generic editors in
StageHand and Bitwig. All of them share the core (below), so a fix to note handling fixes all.

| Plugin | Kind | From | Phase |
|---|---|---|---|
| **NoteGran Cloud** | assigner | the scatter of PMU's granulars | 2 |
| **NoteGran Chords** | transformer | new | 3 |
| **NoteGran Grid** | assigner | ClockGran (the beat-grid one) | 4 |
| **NoteGran Burst** | assigner (triggered) | IFSGran | 5 |
| **NoteGran Buffer** | whole generator | BufferGran | 6 |
| **NoteGran Echo** (Markov) | whole generator | new | later |

## What every plugin shares (the core)

### Ports and dialects

- One note input port and one note output port, `MIDI | MIDI2`, preferring `MIDI2` (as Retune). CLAP
  note dialect as in Retune's M3: nearest key plus a `CLAP_NOTE_EXPRESSION_TUNING` for the fraction.
- **Output uses the dialect of the note it came from.** A grain from a MIDI 1.0 member leaves as MIDI
  1.0, with its pitch rounded; from a MIDI 2.0 member, as MIDI 2.0 with Pitch 7.9.
- Output goes on the group and channel of its source note.
- **Everything that isn't a note** (CCs, channel pitch bend, program change, SysEx, clock) passes
  through unchanged, at its sample offset. Per-note messages on the set's notes update the set (above)
  and pass through only if Dry.

### Time

- **Two clocks,** a per-plugin choice (`Sync`): **Free**, with rates in grains per second and lengths
  in ms, or **Beats**, with rates per beat (a division: 1/4, 1/2, 1, 2, 3, 4, 6, 8 per beat) and
  lengths in beats.
- Beats come from CLAP's transport (`HAS_BEATS_TIMELINE`, `HAS_TEMPO`). StageHand sends both, locked to
  its rack clock. With a tempo but no timeline, or the transport stopped, Beats runs from the tempo on
  its own beat count. With no transport at all, it uses 120 BPM.
- **Schedule from an absolute clock, never per block.** Grain times and every random draw are worked
  out from a running sample count (or the beat position), so the output doesn't depend on block size.
  The tests check this.
- **As built (2026-10-21): slots.** A rate is a grid: slot n sits at a fixed position (sample
  ceil(n·sr/rate) in Free; n × beat/division in CLAP beat time, 2^31 per beat, in Beats), so a block
  only asks which slots start in it. Nothing is carried between blocks, a host can start anywhere,
  and n is what a random draw is keyed on. All integer and exact: the tempo becomes 1/65536 BPM, and
  sample ↔ beat is rational with 128-bit intermediates (120 BPM's beats land on exact samples).
  Following the host, each block's slots carry on from where the last ended, not from the host's
  rounded position, so none plays twice or is skipped; a jump of more than 1/64 beat (a loop, a
  seek) restarts from the host's position, less one sample for its rounding. Own count (stopped, or no
  timeline): beat 0 is sample 0, re-anchored only when the tempo changes. The clock reports the
  transport stopping and jumps; the caller ends notes.
- `Onset random` (0 to 1, as BufferGran's): 0 is evenly spaced, 1 is Poisson "dust". In Beats mode, 0
  lands exactly on the grid.

### Pitch

- Worked in 1/512 semitone (Pitch 7.9 units) throughout, with `uw::ump`'s `notePitch79` /
  `transposeNote79` doing the UMP side.
- `Transpose` (semitones, fractional), `Octaves` (0 to 4: a pick may move up or down by up to that
  many octaves) and `Scatter` (± semitones, random per grain, triangular), as BufferGran's.
- `Quantize`: Off | EDO N (the `EDO` parameter, 1 to 96, as Retune's Divisions) | **Set**, which snaps
  a scattered pitch to the nearest member (or a member's octave), so a scatter stays in the set's
  harmony. PMU's mode-ratio discretizer is a later phase.
- **MIDI 1.0** rounds to the nearest key. **MIDI 2.0** sends the 7.9 pitch, and the note number is the
  nearest one that's free on that channel (StageHand's rule), so two grains a few cents apart don't
  share a key.

### Velocity and length

- `Level` (0 to 2, times the source member's velocity) and `Velocity spread` (± random).
- `Length` (ms or beats) and `Length spread`.

### Notes always end

- An **owned-note table** remembers every note the plugin sent: group, channel, key, the dialect, and
  its end time (or none, for a held note, ended by its id). That's the only way its note-off is sent,
  so none can stick. One output port, so no port field. 1024 slots in a member array, never
  allocated; only the notes sounding are scanned. Full, a new note ends the oldest, as Voices does. A
  note-off that doesn't fit the output buffer goes out in the next block, late but never lost.
- **Voices are scarce (2026-10-21).** Unlike PMU's granulars, where a grain is cheap and dozens
  overlap, every grain here is a voice on the synth after it, and the author's synths have **at most
  32 voices**. Usually a part should stay well under that, leaving room for release tails. So the
  voice limit is part of how a NoteGran sounds, not a safety net: density, length and Voices are set
  together, and a dense cloud is made of short grains, not many long ones.
- `Voices` (1 to 32, **default 8**): at most this many notes sound at once, in every plugin (Chords'
  notes too). What a new note over the limit does is `When full`, below.
  The owned-note table has 1024 slots anyway: memory only, never voices.
- `When full` (decided 2026-10-21), in every plugin: **Skip** (the default: the new note isn't
  played) | **Steal** (the oldest ends first). Skip keeps grains whole and thins a dense cloud; Steal
  keeps the newest notes, but at a high density cuts every grain short. Either way a sounding key
  struck again replaces itself (the same-key rule), so it's never skipped.
- **The same key twice:** if a key is already sounding on that channel, the old one's note-off goes
  just before the new note-on (StageHand's sequence rule).
- `reset()`, All Notes Off and All Sound Off on input, deactivating, and the transport stopping (Beats
  mode) end every note at once, and clear the set. A bypassed or suspended plugin ends them on its way
  out.
- **Keep processing while notes sound.** `process()` returns `CLAP_PROCESS_CONTINUE` while any note or
  pending trigger is left, and `CLAP_PROCESS_SLEEP` only once silent with an empty set, so a host that
  sleeps idle plugins doesn't strand a note.

### Randomness and repeatability

- **Counter-based draws** (decided 2026-10-21): each draw is a fixed integer hash of the seed, its
  stream and its index (the grain's number, or its beat position), not the next value of a running
  generator. So any point can be worked out without replaying from the start, which Umpire needs to
  play a generator clip from bar 5 exactly as from bar 1 (Integration, below), and output can't depend
  on block size. Separate streams for timing, picks, pitch, velocity and position, so turning one knob
  doesn't reshuffle the others.
- `Seed` (0 to 9999; **0 = new each start**, the default). With a seed set, the same input at the same
  times gives exactly the same output, on any machine. Umpire records a NoteGran's output as plain
  notes, and a fixed seed makes a retake repeatable.
- No floating-point code that can differ between compilers in the timing path (integer samples and
  fixed-point beats).

### Real time

No allocation, no locks and no threads on the audio thread. Every table (set, capture, grains, pending
triggers) is fixed-size and allocated at `activate()`. A setting that needs recomputing (Burst's
attractor points, Chords' scale table) is redone once at the top of the next `process()`, as PMU does.

### State

Parameters only, appended, never inserted (as in the synths). **The set and anything captured aren't
saved**: a new session starts empty.

## NoteGran Cloud (phase 2)

The plain assigner: a scatter of grains whose pitches are picked from the set. The first to build,
since it proves the set and the core with the least of its own.

- `Density`, `Length`, `Onset random` as above.
- `Pick`: **Random** (weighted by velocity: `Weight` 0 is equal, 1 is by velocity) | **Up** | **Down**
  | **Up-down** (by pitch) | **Played** (by arrival). The ordered picks keep their place when the set
  changes: the next pick is the next member after the last one's pitch.
- Then Octaves, Transpose, Scatter and Quantize apply.
- In Beats mode with Onset random at 0 and an ordered Pick, it's an arpeggiator; with more Onset
  random and Scatter, it thins out into a cloud. That range in one plugin is the point.
- With a trigger channel, each trigger note-on makes one pick (Density is then ignored), its velocity
  times the member's.

## NoteGran Chords (phase 3)

A transformer: each note held becomes a chord built on it in a scale. The output is the chord's notes,
held as long as the input note, so Chords can feed an assigner (Chords → Cloud) or a synth directly.
Long wanted on its own (2026-10-21); it fits here because a chord is just a set.

- **The scale** is a set of steps of an N-EDO, from a root: 12-EDO major is `0 2 4 5 7 9 11`, 31-EDO
  meantone major `0 5 10 13 18 23 28`. `EDO`, `Root` and `Scale` (a menu of built-in scales per EDO).
- **The scale can come from a set** (`Scale from`: Menu | Scale channel): notes held (or latched) on
  the scale channel are the scale, by pitch class. Play a scale on KeyboardEDO, or hold it with a
  one-slot sequence, and Chords uses it. That's also the way round the hardest UI problem (below):
  a custom scale needs no editor.
- **An input note** goes to its scale degree. `Off-scale` (an input note not in the scale): Nearest
  degree | Pass (the note alone) | Drop.
- **The chord** stacks degrees from there: `Size` (1 to 6 notes), `Stack` (every 2nd degree for
  thirds, 3rd for fourths, 4th for fifths), `Inversion`, `Voicing` (Close | Open | Drop-2 | Spread) and
  `Octave`. Size 1 is a scale quantizer.
- **Latching, as Retune's:** each input note remembers the chord it made, so a change to the scale or
  the settings while notes are held can't leave a chord note sounding. New notes get new chords.
- **Two input notes making overlapping chords** share keys: a chord note already sounding from another
  input note isn't struck again, and ends when the last chord using it ends.
- **"Related" chords** beyond the one on that degree are later: substitutions, borrowed chords, and the
  neo-Riemannian moves (P, L, R), each needing a definition that works in an EDO, not only 12.
- **The UI is the hard part.** A scale per EDO, the chord on each degree, and seeing what will sound
  before playing it don't fit sliders and menus. The plan: the generic editor first (the parameters
  above, and the scale from a channel for anything the menu lacks), then a window of its own, which
  shows the scale on a circle of N steps with the chord on the current degree lit. Its design is its
  own phase.

## NoteGran Grid (phase 4)

The beat-grid assigner, after ClockGran: picks on a grid whose positions have tiers.

- `Rate` (a division per beat) and `Meter`: Straight, or ClockGran's groupings (5 as 3+2 or 2+3; 7 as
  2+2+3, 3+2+2 or 2+3+2; 9 as 3+3+3). Each position belongs to one of five tiers: bar downbeat, group
  downbeat, beat, off-beat, spare.
- **Per tier:** `Probability` (that the position plays), `Level`, `Offset` (semitones from the pick),
  `Chaos` (extra scatter), and `Pick` (Lowest | Highest | Next | Random member). So a downbeat can
  always take the root and an off-beat a random member.
- `Density` (1 to 4): sub-picks within a position, spread by `Smear`, as ClockGran's.
- Positions come from the beat timeline, so a Grid on one row and a sequence on another stay locked.

## NoteGran Burst (phase 5)

IFSGran's phrase, played from each new member. A note-on in the set (or a trigger note-on, with a
trigger channel) starts a phrase of grains, rooted on that note.

- **The phrase** is a set of points in time × pitch, from IFSGran's two-transform attractor: for each
  transform, a fixed point, a contraction and a rotation, and `Probability` (the share of T1). `Count`
  (1 to 64) points, sorted by time.
- `Duration` (ms or beats): the whole phrase's length. `Range` (± semitones the attractor's pitch axis
  covers).
- Each point's pitch is the root plus its offset, then Quantize (Set keeps the phrase in the set's
  harmony). Velocity is the root's, times Level, and `Decay` (0 to 1) fades it across the phrase.
- `Release` (what the root's note-off does): **Finish** (the phrase plays out) | **Stop** (no more
  grains; sounding ones end as usual) | **Loop** (the phrase repeats while the root is held).
- Up to 16 phrases at once, sharing the `Voices` limit (so at 8 voices, overlapping phrases thin
  each other out); a 17th replaces the oldest.
- Later, simpler shapes beside the attractor: a strum, a ratchet, a bounce (a phrase whose gaps shrink).

## NoteGran Buffer (phase 6)

A whole generator: BufferGran's three axes, over notes instead of samples. **When** grains fire
(density), **what pitch** they are (transpose, scatter), and **where in the capture** they read from
(position). Its input is a history, not a set: Position reads by when notes were played. A looper is
its natural producer; Buffer hears the looper's output and captures it as it plays.

- **The capture** is a ring of the notes played in the last `Window` (seconds or beats; default 4 s /
  8 beats; up to 512 notes): each note's start, length, pitch and velocity. A note still held has a
  provisional length that grows until its note-off.
- `Freeze` (on/off): latch the capture. Grains go on exploring that moment while new notes play
  through (if Dry) and aren't recorded.
- `Position` (0 oldest to 1 newest), `Scan` (drift of the read point: 0 hold, 1 real time, negative
  reverse) and `Spray` (random spread around it), as BufferGran's.
- `Density` (grains per second or per beat).
- `Grain`:
  - **Note**: a grain is the one captured note nearest the read point: its pitch (plus Transpose and
    Scatter), its velocity (times Level), and `Length`, or its own length when `Length` is at
    "source".
  - **Fragment**: a grain is the stretch of the capture `Length` long that starts at the read point:
    every note starting in it, replayed with its timing, cut at the grain's end. A chord stays a chord,
    and a short fragment of a run becomes a figure. The nearest thing to an audio grain, which reads a
    stretch of sound and not one sample.
- An empty capture makes nothing. Dry defaults to on here: the capture is usually of a part being
  played.
- **Input note-offs** end the input note (if Dry) and fix its length in the capture. They don't stop
  grains. `Gate` (on/off, default off): on, grains spawn only while a key is held.

## Later

- **NoteGran Echo (Markov):** a whole generator. Learns pitch intervals, lengths and gaps (first or
  second order) from what's played, and answers with phrases in that style when the player stops (call
  and response), or continuously. Needs its own design; open questions below.
- **Glides (MIDI 2.0 only):** each grain bends with MIDI 2.0 per-note pitch bend over its life (start
  and end offsets, a curve): the note version of a grain's pitch envelope. MIDI 1.0 output gets no
  glide.
- **Modal quantize:** PMU's mode-ratio discretizer (Snap, Scatter, material, stretch), giving grains
  the partial ratios of struck materials. Pairs with StruckSynth.
- **Windows.** The generic editors are enough at first, except for Chords (above). A view of the
  set, or of Buffer's capture with the read point, would make the assigners easier to play.

## Layout (mirrors Retune)

```
NoteGran/
  CMakeLists.txt         raw .clap MODULE target, install-clap symlink target, ctest
  external/clap          submodule
  external/uw-core       submodule, UMP-only build (UW_CORE_UMP_ONLY): uw::ump
  external/clap-wrapper  submodule (phase 7)
  src/core/              pure C++, no CLAP: clock, the set, owned-note table, pitch, scales, random,
                         and each plugin's logic
  src/plugin.cpp         clap_entry, the factory (one descriptor per plugin), ports, params, state
  test/                  core unit tests + a headless host test that dlopens the .clap
```

## Code

- **UMP code comes from uw-core** (`uw::ump`), as in Retune and StageHand. NoteGran writes no UMP
  parsing of its own; anything about UMP itself goes into uw-core first, committed and pushed before
  NoteGran's pin moves.
- **The core is written to be shared** with StageHand's and Umpire's host-side generators: no CLAP
  in it, input as `uw::MidiMessage` with sample offsets, output as `NoteEvent`s into a caller's
  buffer, time from the caller. Where it ends up: Integration, below.
- **Scales are shared with KeyboardEDO** in spirit: an EDO scale here should mean what a KeyboardEDO
  layout's tuning means. If both need the same table, it's a candidate for that shared layer.
- **PMU's code** (the attractor, the triangular draws, the LCG, the tier classifier, `ModalDiscretizer`)
  is copied in where needed, with a note naming the PMU file it came from.

## Integration (2026-10-21)

Two different things, kept apart.

**The plugins in a host** share no code with it: they're CLAP note effects. StageHand's row chain
(`StageHand/docs/midi-effects.md`) is built and runs Retune; Umpire hosts CLAP through the same
`uw::clap-host`; Bitwig sends MIDI 1.0 only. StageHand drops a plugin's note expressions, so the CLAP
dialect's fraction is lost there (open question 8); MIDI 2.0 output is fine.

**Host-side generators share the core.** StageHand's sources (Euclidean, cellular automata, random
walk) and Umpire's generator clips need the same pieces as the plugins:

| Piece | |
|---|---|
| Owned-note table, `NoteEvent` / `NoteOut` | generic |
| Clock (Free and Beats, from an absolute sample count) | generic |
| Counter-based random streams | generic |
| Pitch (7.9, Quantize, nearest free key) | generic |
| The set; each plugin's logic | NoteGran's, but callable from a host too |

A plugin calls the core from `process()`; a host from its engine, with its own sample count and beat
position, turning each `NoteEvent` into its own events.

- **Home (proposed): a `uw::gen` target in uw-core,** beside `uw::ump` and under its rules (no
  dependency but `uw::ump`, PIC, hidden symbols), so it's safe in a plugin. Every consumer already
  pins uw-core, so nothing new. To be confirmed in UmpireDAW's `docs/decisions.md` ("Shared code").
- **When:** the core stays here while its API settles (through phase 2), and moves when StageHand's
  first host-side generator starts. Moving earlier would put every phase-1 change through a uw-core
  push and pin.
- **Seeking:** Umpire's generator clips must give the same notes from any start point, hence
  counter-based draws and a clock worked out from absolute time (Randomness, Time, above).
- **Umpire's arpeggio clip is Cloud with a fixed set** (`UmpireDAW/docs/ideas.md`: up-down,
  sixteenths, C–E–G, a seed). Keeping each plugin's logic as plain core code gives Umpire set-reading
  generator clips nearly free.
- **Versioning:** once Umpire stores recipes, a change to shared generator code changes saved songs,
  so each host-side generator carries a version. The plugins don't: Umpire records their output as
  plain notes.
- **UMP note builders** (a `NoteEvent` to a MIDI 1.0 or 2.0 note on/off) are needed on both sides, so
  they go into `uw::ump`, not here. The CLAP dialect stays in NoteGran's plugin layer.

## Phases

1. **The core and its tests.** The set (hold-over, pitch moves, latch, channel split), clock (Free and
   Beats, from an absolute sample count), the owned-note table, Voices, the same-key rule, pitch (7.9
   maths, Quantize, nearest-free keys), seeded counter-based random streams. Tests: notes end in every case above, a
   sequence's repeated strikes don't empty the set, output independent of block size, the same seed
   gives the same output, MIDI 1.0 / 2.0 / CLAP dialects.
2. **NoteGran Cloud, as a CLAP plugin.** `clap-validator` clean, headless host test. Then listen in
   StageHand: a one-slot sequence → Cloud → StruckSynth, and KeyboardEDO → Cloud in 31-EDO. And in
   Bitwig (MIDI 1.0 there).
3. **NoteGran Chords,** with the generic editor and the scale from a channel. Then Chords → Cloud.
4. **NoteGran Grid.**
5. **NoteGran Burst.**
6. **NoteGran Buffer.** Note mode first, then Fragment.
7. **AUv2 / VST3 via clap-wrapper**, MIDI 1.0 (rounded) only, as Retune's M4. Retune's table of what
   the wrapper does with a note effect applies unchanged.
8. **Later** (above), each its own phase, Chords' window among them.

## Open questions

1. **The name.** NoteGran is a working name, and with Chords in the family it fits less well.
2. **Hold over:** is 20 ms right for a sequence struck again, and should a looper's or a player's
   quick re-strike count too? (Proposed: one setting for all.)
3. **Chords' built-in scales** per EDO: which ones, and where the list comes from (KeyboardEDO's
   layouts, or Scala files later)?
4. **Chords' "related" chords:** which moves first, and how they're defined outside 12-EDO.
5. **Buffer's Fragment mode:** should a fragment keep its notes' own lengths when they run past the
   grain's end, or always cut them there? (Proposed: cut.)
6. **Echo:** when does it answer (after a gap of how long?), how long does it play, and is its model
   kept in the plugin's state, unlike the capture?
7. **Output channels for MPE synths:** spread grains over channels (an MPE zone) so each can be bent
   on its own in MIDI 1.0? Only matters with Glides.
8. **In StageHand:** it drops a plugin's wildcard notes and note expressions
   (`StageHand/docs/midi-effects.md`). NoteGran sends neither, except CLAP-dialect tuning expressions,
   which only matter when the input is CLAP notes. Check that before phase 2.
