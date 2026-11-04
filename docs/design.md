# NoteGran — plan

A family of MIDI-in → MIDI-out note effects that **generate notes from the notes they hear**: granular
synthesis with notes as the grains, and a chord generator beside it. The synth after it on the row (or
track) makes the sound; NoteGran makes no audio. CLAP-first, with the same layout as Retune: a
hand-written raw CLAP entry point over a pure C++ core, and the vendored free-audio **clap-wrapper**
for the other formats.

**Status (2026-10-22):** phases 1–4 done: Cloud, Chords and Grid are built, pass their tests and
`clap-validator`, and are heard working in StageHand with KeyboardEDO. Phase 5, Burst, is built and
tested (2026-10-22), not yet heard. Editors (2026-10-22): all four are built and draw in StageHand; not
yet seen with notes playing (Editors, below). Phase 6, Buffer, is built and tested (2026-10-24), with a
first editor (Cloud's live view); not yet heard or seen in a host.

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
- **As built (2026-10-21): onsets per slot.** With Onset random r, each slot of the rate is, with
  chance r, dust (a Poisson(1) count of grains, each at a uniform place in the slot), and otherwise
  one grain, up to r of the way into it. At 0, one grain on each slot; at 1, exactly a Poisson process;
  the mean rate is the same throughout. All keyed on the slot (Timing draws 0, 1, 2 + j), so it's
  seekable; grain j of slot n is index n · 16 + j for its other draws.

### Pitch

- Worked in 1/512 semitone (Pitch 7.9 units) throughout, with `uw::ump`'s `notePitch79` /
  `transposeNote79` doing the UMP side.
- `Transpose` (semitones, fractional), `Octaves` (0 to 4: a pick may move up or down by up to that
  many octaves) and `Scatter` (± semitones, random per grain, triangular), as BufferGran's.
- `Quantize`: Off | EDO N (the `EDO` parameter, 1 to 96, as Retune's Divisions) | **Set**, which snaps
  a scattered pitch to the nearest member (or a member's octave), so a scatter stays in the set's
  harmony. PMU's mode-ratio discretizer is a later phase.
- `Root` (pitch class, default **A**): where Quantize EDO's steps start, as KeyboardEDO's origin
  (default A), so KeyboardEDO → Cloud in 31-EDO lands on the steps being played.
- **As built (2026-10-21):** in order Octaves (Pitch draw 0, uniform over −N..+N), Transpose, Scatter
  (Pitch draw 1, triangular), Quantize, then folded into 0..127 + 511/512 by octaves (not clamped, so
  the top and bottom don't pile up on one key). EDO steps and Set snapping round to the nearest, a
  half up; a Set tie (a tritone, or halfway between two members) goes down.
- **MIDI 1.0** rounds to the nearest key. **MIDI 2.0** sends the 7.9 pitch, and the note number is the
  nearest one that's free on that channel (StageHand's rule), so two grains a few cents apart don't
  share a key: outward from the nearest key, the closer side first. With every key sounding, the
  nearest is struck again (the same-key rule). CLAP: the same key, the fraction as its tuning.

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
- `When full` (decided 2026-10-21), in every plugin: **Skip** (the default) | **Steal**. Skip
  never cuts a note short: a new note isn't played if Voices are all sounding, or if its key is
  sounding on that channel (MIDI 1.0's nearest key, mostly). Steal ends the oldest first, and
  strikes a sounding key again (the same-key rule). Skip keeps grains whole and thins a dense cloud;
  Steal keeps the newest notes, but at a high density cuts every grain short. (Built first with the
  same-key strike always allowed, Skip played as many grains as Steal in MIDI 1.0 with a small set.)
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
- **As built (2026-10-21):** SplitMix64's finaliser, chained over seed, stream, index (a clock slot
  n, or a trigger's count) and draw number. Integer distributions only: below(n), unit and
  triangular (Q32), chance, and a Poisson(1) count (by its CDF in Q32, no `exp`) for onset dust.
  Streams: Timing, Pick, Pitch, Velocity, Length, Position, Chance. The tests pin some values: a
  change to them changes saved songs, so it's versioned, not just re-pinned.
- `Seed` (0 to 9999; **0 = new each start**, the default). The core takes whatever seed it's given;
  "new each start" is the plugin layer's, which picks one at `activate()`, off the audio thread. With a seed set, the same input at the same
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
- **Built as core logic in phase 1 (`CloudCore`, 2026-10-21):** inputs (time-sorted, each marked to
  pass through or not) and a transport in, `NoteEvent`s out; phase 2 only wraps it in CLAP. Picks see
  the set in a fixed order (pitch, then arrival; Played by arrival), never its storage order. Random
  doesn't move the ordered picks' place. Tested end to end: any block size gives the same notes, Free
  and Beats, with and without a host timeline; the same seed the same notes; all three dialects;
  notes end on release, All Notes Off and a transport stop; Voices and When full; triggers; Dry.
- **Dry and keys:** a MIDI 2.0 or CLAP grain avoids keys the set is holding on its channel. A MIDI
  1.0 grain that lands on a dry note's key, which the plugin doesn't own and can't end first, is
  skipped (open question 9, decided 2026-10-21).

## NoteGran Chords (phase 3)

A transformer: each note held becomes a chord built on it in a scale. The output is the chord's notes,
held as long as the input note, so Chords can feed an assigner (Chords → Cloud) or a synth directly.
Long wanted on its own (2026-10-21); it fits here because a chord is just a set.

- **Microtonalizing a familiar scale (decided 2026-10-21).** Take a scale everyone knows, find the
  notes of an N-EDO close to it, and build each degree's chord there. Example, lydian dominant in
  19-EDO:

  | Degree | C | D | E | F# | G | A | Bb |
  |---|---|---|---|---|---|---|---|
  | 19-EDO step | 0 | 3 | 6 | 9 | 11 | 14 | 16 |
  | Cents | 0 | 189 | 379 | 568 | 695 | 884 | 1011 |

  Its triads keep their qualities with 19-EDO intervals (C and D major, 6 + 5 steps; E and F#
  diminished; G and A minor; Bb augmented), and C's seventh is 16 steps (1011¢).
- **Familiar scales are stored spelled** (F# is not Gb): each degree as its place on the chain of
  fifths, e.g. lydian dominant `0 2 4 6 1 3 −2` (C D E F# G A Bb). The menu: the modes of major, of
  melodic minor (lydian dominant among them) and of harmonic minor, harmonic major, the
  pentatonics, whole tone. One list for every EDO: the mapping does the rest (so open question 3 is
  mostly answered).
- `Map`: **By fifths** | **Nearest** (the default; decided 2026-10-25, with KeyboardEDO's scale
  highlight, which defaults to it): most people choosing a familiar scale want the notes nearest the
  ones they know, and By fifths is the deliberate choice of someone after the EDO's own version.
  Songs saved with By fifths keep it; only new instances start at Nearest.
  - *By fifths:* a degree n fifths from the key goes to n × F mod N steps, F the EDO's own fifth
    (its nearest to 702¢: 11 of 19, 18 of 31, 13 of 22). It keeps the scale's structure (thirds stay
    thirds) and gives the EDO's own character: in 22-EDO, E above C is 436¢ (superpyth). Never ties.
  - *Nearest:* each degree to the step nearest its 12-EDO cents. Closest to the familiar sound; in
    22-EDO, E is 382¢. A tie (F# in 19-EDO, exactly between 9 and 10) goes by the spelling: a sharp up,
    a flat down. Two degrees landing on one step (small EDOs) merge.
- **Where the grid is:** `EDO`, and `Origin` (a pitch class, default **A**): step 0 of the EDO, as
  KeyboardEDO's origin and Cloud's Quantize EDO `Root`, so all three agree. (Cloud's parameter shows
  as "Root" today; it's the same thing, and could be renamed "Origin" in display only, keeping its
  state symbol.) `Key` (a spelled pitch class, Cb .. C#) is placed on that grid by the same `Map`:
  with an A origin in 19-EDO, C is 5 steps up (316¢), not 300¢.
- **The scale can come from a set** instead (`Scale from`: Familiar | Scale channel): notes held (or
  latched) on the scale channel are the scale, already in the EDO, by pitch class. Play a scale on
  KeyboardEDO, or hold it with a one-slot sequence. No mapping then, and no editor needed for a
  custom scale.
- `Input`: **Familiar** | **EDO** (decided 2026-10-21), what a played note is matched against:
  - *Familiar:* an ordinary 12-EDO keyboard. A note's nearest 12-EDO key is in the scale if it's one
    of the familiar degrees (D, in C lydian dominant); its chord comes out in the EDO. (Matching
    against the mapped steps instead would fail: in 22-EDO by fifths, a played E at 400¢ is far from
    its step at 436¢.)
  - *EDO:* KeyboardEDO (or anything) playing the EDO. A note's nearest EDO step is in the scale if
    it's one of the mapped degrees.
- **An in-scale note sounds on its EDO step** (decided 2026-10-21): played D comes out at 189¢ with
  its chord, so the whole chord is in the EDO. `Off-scale`: **Pass** (the default, decided
  2026-10-21: the note alone, as played, no chord) | Nearest degree | Drop.
- **The chord** stacks degrees from there: `Size` (1 to 6 notes), `Stack` (every 2nd degree for
  thirds, 3rd for fourths, 4th for fifths), `Inversion`, `Voicing` (Close | Open | Drop-2 | Spread) and
  `Octave`. Size 1 is a scale quantizer.
- `Inversion` (Root, 1st .. 5th; asked for 2026-10-21) is the inversion every chord gets: that many of
  its lowest notes an octave up. Later, an `Inversion: Nearest` that picks each chord's inversion to
  lie closest to the last one (voice leading).
- **As built (2026-10-21):** Voicing after the inversion: Open raises the 2nd, 4th and 6th notes from
  the bottom an octave, Drop-2 lowers the second-highest, Spread raises note i by i / 2 octaves. A
  note past MIDI's range is left out. A scale channel's scale is latched (it stays when let go, until
  a new note there starts the next); with nothing there yet, every note passes. With a scale channel,
  Input is EDO (there are no familiar degrees). A chorded note's per-note messages are dropped; a
  passed note's pass. 16 parameters; state header `NoteGran Chords 1`.
- **MIDI 1.0 can't carry a microtone.** A chord in an EDO other than 12 from a MIDI 1.0 note would be
  rounded back to 12-EDO keys. So Chords sends a chord note with a fraction as MIDI 2.0 (the host
  translates down for a synth that takes only MIDI 1.0), the one exception to "output in its note's
  dialect" (open question 10, decided 2026-10-21). In 12-EDO nothing has a fraction: MIDI 1.0 stays. StageHand gives plugins MIDI 2.0 anyway; Bitwig sends CLAP notes, which carry
  tuning.
- **Chord shapes (later):** chords that aren't stacked from the scale, mapped into the EDO by
  nearest step: the harmonic 4:5:6:7 (its 7th, 969¢, is 15 steps of 19 or 25 of 31), neutral triads
  (where the EDO has a neutral third: 9 steps of 31, 348¢; 19-EDO has none), otonal and utonal
  chords. `Chord`: From scale | a shape.
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
- **As built (2026-10-21):** always Beats (its own count with no timeline). ClockGran's meter table
  and tiers: ticks counted 2 to an eighth; Straight is 16 ticks (tier 1 the bar's downbeat, 2 the
  half, 3 the quarters, 4 the eighths, 5 the sixteenths); a grouped meter's tiers are its groups'
  downbeats (1 the bar's), then the other beats, then the off-beats, so a 2-group meter leaves tier 5
  unused. A slot's place in the cycle is its number mod the cycle, from the timeline's beat 0 (not
  the host's bar start: a bar of 4/4 at 4 per beat lines up; others line up from beat 0).
- Per slot: its tier's Probability (Chance draw 0), then Density notes (1–4; a fraction by Chance
  draw 1), each at a uniform place up to Smear × the tier's Chaos into the slot (Timing draw j).
  Each note: the tier's Pick (Lowest | Highest | Next, which is Cloud's Up | Random, unweighted), its
  Offset as a transpose, Scatter × its Chaos (triangular, as ClockGran's: Chaos scales both), the
  tier's Level times the member's velocity. Global: Rate, Meter, Density, Smear, Scatter (default
  ±12 st), Length (beats), the spreads, Transpose, Quantize / EDO / Root, and the assigner settings.
  Defaults: every tier Chaos 0; Probability 1, 1, 1, 0.75, 0.5; Level 1, 0.85, 0.75, 0.65, 0.55;
  Pick Lowest, Next, Next, Next, Random. 45 parameters (tier n's five as "Tier n …").
- **What Cloud and Grid share is `Assigner`** (`src/core/Assigner`): the set, clock, owned notes,
  draws, the block's order (inputs before grains at a sample), triggers, Dry, picks and key choice.
  Each says only when its grains are and what each plays. Burst will be the third.

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
- **As built (2026-10-22)** (`src/core/Burst`): the attractor is IFSGran's `computeScatter` (PMU
  `src/Module/IFSGranModule.cpp`), 30 warm-up steps, worked in Q30 fixed point with Timing draws
  keyed on the phrase (its strike count with `Vary` on, else 0, so every phrase has the same shape).
  Time is normalised: the first point is on the note-on and the last at (Count − 1) / Count of
  Duration, so Duration is one pass with its last gap and Loop repeats every Duration. Pitch is
  (y − 0.5) × 2 × Range from the root. Positions are samples (Duration and Length in beats are turned
  into samples at the strike and the grain), so a host loop or seek doesn't move a phrase.
- Every set note-on starts a phrase, a quick re-strike included (the hold-over keeps the member in
  the set but doesn't stop a phrase). With a trigger channel, only its note-ons do, rooted on the
  newest member at the trigger's velocity, and the trigger's note-off releases it. Loop's release
  finishes the pass it falls in. All Notes Off and the transport stopping end every phrase.
- Grains play with an empty set (a phrase plays out after its root is gone); Quantize Set uses
  whatever is held. Defaults: T1 (0.2, 0.4, 0.6, 25°), T2 (0.8, 0.6, 0.6, −25°), Probability 0.5,
  Count 8, Duration 800 ms / 2 beats, Range ±12, Finish, Vary on, Length 100 ms / ¼ beat, Decay
  0.5, Dry off. 34 parameters. Assigner gained the hooks Burst needs (`startsPhrases`, `heard`,
  `cleared`); `Heard` now reports a set note's strike and every release.

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
    every note starting in it, replayed with its timing and its own length. A chord stays a chord,
    and a short fragment of a run becomes a figure. The nearest thing to an audio grain, which reads a
    stretch of sound and not one sample.
- An empty capture makes nothing. Dry defaults to on here: the capture is usually of a part being
  played.
- **Input note-offs** end the input note (if Dry) and fix its length in the capture. They don't stop
  grains. `Gate` (on/off, default off): on, grains spawn only while a key is held.
- **As built (2026-10-24)** (`src/core/Buffer`): an assigner like the others (the set for Gate, Dry
  and Quantize Set; the clock, owned notes and draws), which captures what the set hears instead of
  picking from it. The capture is a ring of the last 512 set-channel notes; of those, the ones
  starting in [E − Window, E] are read, E the grain's time (or the freeze's, the block start where
  Freeze went on). Frozen, nothing new is recorded; a note-off still fixes a held note's length.
- **The read point, as BufferGran's** (PMU `src/Module/BufferGran.cpp`): a fraction of the window
  from its oldest end, Position + Scan's phase + Spray, wrapped (1 with no drift or spray is the
  newest end), rounded to the nearest sample. Scan is windows per window length (−4 to 4), its phase
  Scan × t / Window from the absolute sample count, so it's the same for any block size (a change to
  Scan or Window moves the phase at once); Spray is uniform ± (Position draw 0).
- **Onsets** are Cloud's (Density or Rate, Onset random: now `Assigner::collectOnsets`). With a
  trigger channel its note-ons are the grains, and Gate doesn't apply.
- **Note:** the captured note starting nearest the read point (the earlier on a tie), with Octaves,
  Transpose, Scatter and Quantize, velocity × Level, and Length, or its own length with `Length
  from` Source (provisional while held).
- **Fragment:** the notes starting in [read point, + Length) are copied (up to 64) when the grain
  starts, each replayed at the grain's time + its offset, its own length (past the fragment's end
  too; decided 2026-10-24, built 2026-10-25). One pitch draw for the fragment (the grain's index),
  so a chord stays a chord; Quantize per note; velocity draws per note. Up to 32 fragments; a 33rd replaces the oldest, whose notes to come are dropped (those
  sounding end at their length; All Notes Off, reset and the transport stopping end them). A
  fragment plays out whatever Gate does. An open note's length is how long it's been held so far.
- All Notes Off, reset and the transport stopping clear the capture. Defaults: Window 4 s / 8
  beats, Position 0.5, Scan 0, Spray 0.1, Density 8/s or 4 per beat, Onset random 0.5, Note, Length
  100 ms / ¼ beat, Dry on. 32 parameters; state header `NoteGran Buffer 1`.
- **Assigner gained** `playsWithoutSet`, `addGrain` / `dropGrains` (a grain adding grains later in
  its block) and `triggered()`; `heard()` is now called for every set note, and `collectGrains` with
  a trigger channel too (Cloud and Grid collect nothing then).
- **Still to do:** an editor view of its own (the capture on a timeline, the read point and spray
  over it, the fragment's span), and hearing it in StageHand behind a looper.

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

## Editors (2026-10-22)

Every plugin gets its own editor; the generic editors were a stopgap. Decided with the author:

- **DPF for drawing only, as StruckSynth:** DGL, NanoVG and pugl (`external/dpf`, StruckSynth's pin),
  the window built by hand with `UIExporter`; the CLAP entry point stays ours. The plugin and the
  editor meet only at `src/plugin_gui.h`. macOS and Linux (X11), embedded windows only.
- **The house widget kit**, copied from StruckSynth into `src/UI/` (Knob, Dropdown, Toggle, GroupBox,
  PopupMenu, Logo), each change marked "NoteGran:": charcoal panels, stepped knobs, dimmed menus,
  labels and menu choices that shrink to fit, boxes that wrap into rows and take wide slots. Plus
  NoteGran's own compact Slider (the house slider, for tables).
- **NoteGran's colours:** one charcoal family, an accent per plugin: Cloud steel blue, Chords
  violet, Grid amber, Burst teal.
- **One shell** (`src/UI/NoteGranUI.cpp`): the header (logo, "NoteGran" and the plugin's name in its
  accent) and a layout per plugin, at its own design size: rows by weight of groups and views by
  cells; groups of slots, each one parameter's control or two swapped by a third (Density / Rate by
  Sync), wrapping into rows of the group's columns; settings unused just now dimmed ("unless" too:
  dimmed when a value is one thing). Controls are built from the
  parameters' own descriptions, so ranges, menus and texts are the host's.
- **Values are polled:** the editor reads every value on its 60 Hz timer and shows what changed, so
  automation and a loaded state need no push. Its edits go through a ring, applied (and sent to the
  host as gestures and values) at the next process() or flush.
- **Live views:** process() publishes a snapshot under a sequence count (`ng_live_t`): the set's
  members, the notes it started lately and those sounding, Grid's slot and Chords' scale. The editor
  reads it on its timer.
- **Previews:** what the current settings make, asked whenever a value changes (main thread, from
  preview cores built from the values): Grid's cycle by tier (`ng_grid_cycle`), Burst's attractor and
  next phrase (`ng_burst_attractor`, `ng_burst_phrase`), Chords' scale and the chord on a degree
  (`ng_chords_scale`, `ng_chords_preview`).
- **Only `clap_entry` is exported** (a linker export list), since DGL's libraries don't hide theirs.
- **Cloud (built):** the live view (the set lit on a keyboard of equal semitones, each grain rising
  from its exact pitch and fading), then Timing, Pick, Pitch, Dynamics and Input. 900 × 560, resizable
  at that ratio from 0.8×. Checked in StageHand: draws, menus and knobs work, Sync swaps Density for
  Rate, Quantize EDO undims EDO and Root. Not yet seen with notes playing.
- **Grid (built), 980 × 700:** one cycle of its meter over the live keyboard (each slot a bar as tall
  as its tier is strong and as bright as its Probability, the playhead on the slot now, slots
  flashing as they play) beside Clock; the tiers' table (a column per tier, named from the meter:
  Straight's by note value, a grouped meter's by group, then Beats and Off-beats, a tier the meter
  doesn't use dimmed; rows Probability, Level, Offset, Chaos as sliders and Pick as menus); Pitch and
  Input.
- **Burst (built), 960 × 660:** PMU's scatter editor (`ScatterEditorView.hpp`): the real attractor
  of the next 16 phrases with Vary (the first's larger), each transform's dot and handle dragged as
  one gesture per field moved, a handle past the plot drawn and grabbed at the edge; beside it the
  next phrase (time across Duration, pitch about the root, size by Decay, a ghost pass with Loop) over
  the live keyboard. Then Phrase, Grain, Pitch and Input.
- **Chords (built), 920 × 600:** the scale on a circle of N steps (the key at the top, degrees
  numbered, Roman with seven), the notes sounding lit and joined, a degree clicked showing its chord
  and naming its notes (with cents off 12-EDO); with a scale channel, the scale the playing plugin
  has. Beside it Chord; then Scale and Output.
- **Help** (2026-11-03): a "?" box at the header's right, StruckSynth's HelpButton as Retune adapted
  it (no caption). One source for all five effects (`resources/help.md`, in the synths' layout: an
  On This Page index, a card per effect and per shared group of settings), and a page per effect
  built from it (2026-11-04): `tools/help_pages.py` puts that effect's card among the shared ones
  (the settings groups it has; Chords has no Timing or Pitch), gives it its own index, and runs
  pandoc with the products' shared template into `resources/help-<effect>.html`, committed. A
  click writes the effect's page to `NoteGran-help` in the temp folder, with the logo inside it as
  a data: URL, and opens it. Not one page opened at its section, as first (a one-line page that
  redirected to `#buffer`, since `open` drops a #fragment): snap Firefox on Linux is handed only
  the file opened, through the document portal, so the redirect's target was "file not found",
  and a logo beside the page can go missing the same way (EDOSpread12's did).
- **Checked in StageHand** (2026-10-22): all four open at their sizes and draw. Not yet seen with
  notes playing, nor dragged and clicked by hand.
- Every DGL plugin registers the same Objective-C class names (`DGL_17000604_Pugl…`) as
  StruckSynth's and StageHand's own, as all of the house's DGL code already does.

## Layout (mirrors Retune)

```
NoteGran/
  CMakeLists.txt         raw .clap MODULE target, install-clap symlink target, ctest
  external/clap          submodule
  external/uw-core       submodule, UMP-only build (UW_CORE_UMP_ONLY): uw::ump
  external/clap-wrapper  submodule (phase 7)
  external/dpf           submodule, StruckSynth's pin: the editors' drawing (DGL, NanoVG, pugl)
  src/core/              pure C++, no CLAP: clock, the set, owned-note table, pitch, scales, random,
                         and each plugin's logic
  src/plugin.cpp         clap_entry, the factory (one descriptor per plugin), ports, params, state
  src/plugin_gui.h       the seam between the plugin and its editors
  src/UI/                the editors: NoteGranUI.cpp and the widget kit
  resources/             the logo, embedded at build time
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
- **PMU's code** (the attractor, the tier classifier, `ModalDiscretizer`)
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
2. **NoteGran Cloud, as a CLAP plugin.** `clap-validator` clean, headless host test (both done,
   2026-10-21: `src/plugin.cpp`, `test/ClapTests.cpp`). Built: 25 parameters (Voices 1..32, default
   8; When full Skip; Seed 0 new at each activate); CLAP notes in as MIDI 2.0 tagged Clap, a tuning
   expression as Pitch 7.25; a parameter or transport event splits the block at its sample, and
   `Transport::continues` keeps the clock from re-anchoring mid-block; notes left at a reset or
   reactivation end at the next process(); SLEEP once silent with an empty set. MIDI 2.0 notes are
   built with `uw::ump::midi2NoteOn` / `midi2NoteOff` (added to uw-core for this, 2026-10-21).
   Heard (2026-10-21): StageHand, KeyboardEDO → Cloud → a synth, working well. Still to hear: a
   one-slot sequence as the set, 31-EDO with Quantize EDO, and Bitwig (MIDI 1.0 / CLAP notes). Then listen in
   StageHand: a one-slot sequence → Cloud → StruckSynth, and KeyboardEDO → Cloud in 31-EDO. And in
   Bitwig (MIDI 1.0 there).
3. **NoteGran Chords,** with the generic editor and the scale from a channel. Then Chords → Cloud.
   Built 2026-10-21 (`src/core/Scales`, `src/core/Chords`; the plugin table in `src/plugin.cpp`),
   tested in the core and through the .clap, `clap-validator` clean. Heard (2026-10-21): StageHand,
   KeyboardEDO → Chords → a synth, working. Still to hear: C lydian dominant in
   19-EDO from a 12-EDO keyboard; KeyboardEDO in 19-EDO with Input EDO; Chords → Cloud.
4. **NoteGran Grid.** Built 2026-10-21 (`src/core/Grid`), tested in the core and through the .clap,
   `clap-validator` clean. Heard (2026-10-21): StageHand, KeyboardEDO → Grid → a synth, working.
5. **NoteGran Burst.** Built 2026-10-22 (`src/core/Burst`), tested in the core and through the .clap,
   `clap-validator` clean. Still to hear: StageHand, KeyboardEDO → Burst → a synth; Loop and Stop;
   a trigger channel.
6. **NoteGran Buffer.** Note mode first, then Fragment.
7. **AUv2 / VST® 3 via clap-wrapper**, MIDI 1.0 (rounded) only, as Retune's M4. Retune's table of what
   the wrapper does with a note effect applies unchanged.
8. **Later** (above), each its own phase, Chords' window among them.

## Open questions

1. **The name.** NoteGran is a working name, and with Chords in the family it fits less well.
2. **Hold over:** is 20 ms right for a sequence struck again, and should a looper's or a player's
   quick re-strike count too? (Proposed: one setting for all.)
3. ~~**Chords' built-in scales** per EDO~~: one list of familiar scales, stored spelled, mapped into
   any EDO (decided 2026-10-21, Chords). Still open: which "native" EDO scales (MOS scales, e.g.
   19-EDO's or 31-EDO's own) to add beside them.
4. **Chords' "related" chords:** which moves first, and how they're defined outside 12-EDO.
5. ~~**Buffer's Fragment mode:**~~ Decided 2026-10-24: a fragment's notes keep their own lengths
   when they run past the grain's end.
6. **Echo:** when does it answer (after a gap of how long?), how long does it play, and is its model
   kept in the plugin's state, unlike the capture?
7. **Output channels for MPE synths:** spread grains over channels (an MPE zone) so each can be bent
   on its own in MIDI 1.0? Only matters with Glides.
8. **In StageHand:** it drops a plugin's wildcard notes and note expressions
   (`StageHand/docs/midi-effects.md`). NoteGran sends neither, except CLAP-dialect tuning expressions,
   which only matter when the input is CLAP notes. Check that before phase 2.
9. ~~**Dry in MIDI 1.0:**~~ Decided 2026-10-21: a grain on the key and channel of a dry
   (passed-through) note is skipped.
10. ~~**Chords from MIDI 1.0 input in an EDO other than 12**~~: decided 2026-10-21, sent as MIDI 2.0.

VST is a registered trademark of Steinberg Media Technologies GmbH.
