<div style="display: flex; align-items: center; gap: 0.75rem; margin: 1.5rem 0 0.5rem;">
  <a href="https://www.unlikelyware.com" target="_blank" rel="noopener noreferrer">
    <img src="Unlikelyware.png" alt="Unlikelyware" style="width: 56px; height: auto; border-radius: 10px; margin: 0; display: block;">
  </a>
  <h1 style="margin: 0;">NoteGran</h1>
</div>

<p>
  <em>NoteGran is a set of five CLAP note effects on macOS and Linux: Cloud, Chords, Grid, Burst and Buffer. Fine adjustment of a knob uses the Control key on both: on the Mac that is Control, not Cmd.</em>
</p>

::: {.callout .callout-note .card}
## On This Page
[Overview](#overview) · [Getting Started](#getting-started) · [The Window](#window)

**The effects —** [Cloud](#cloud) · [Chords](#chords) · [Grid](#grid) · [Burst](#burst) · [Buffer](#buffer)

**Shared settings —** [Timing](#timing) · [Pitch](#pitch) · [Dynamics](#dynamics) · [Input](#input)

**Playing —** [Hosts and Microtones](#hosts) · [Notes and MIDI](#midi) · [Tips](#tips) · [Troubleshooting](#troubleshooting)
:::

::: {.callout .callout-note .card}
## Overview {#overview}
NoteGran is **granular synthesis with notes as the grains**. Each effect makes no sound of its own: it sits in front of a synth, listens to the notes you hold, and plays new notes made from them. The synth after it makes the sound.

| Effect | What it does |
|---|---|
| **[Cloud](#cloud)** | Scatters grains whose pitches are picked from the notes you hold: at random, up, down or as played. Evenly on the beat it's an arpeggiator; with more randomness it thins into a cloud |
| **[Chords](#chords)** | Turns each note into a chord of a scale, in 12-EDO or mapped into any EDO |
| **[Grid](#grid)** | Grains on a beat grid whose places have tiers (downbeat, beat, off-beat…), each with its own probability, level, pick and offset |
| **[Burst](#burst)** | Each new note starts a phrase of grains, shaped by a fractal you edit in its window |
| **[Buffer](#buffer)** | Records what you played in the last few seconds or beats and replays grains from it, a note or a fragment at a time |

**The notes you hold are the input** (the *set*): a keyboard, KeyboardEDO, a sequencer or looper, or a host clip. Effects can be chained: Chords in front of Cloud, Grid or Burst makes grains from chords.

**Every grain is a note on the synth**, so each effect has a **Voices** limit (default 8) and ends every note it starts.
:::

::: {.callout .callout-note .card}
## Getting Started {#getting-started}
1. **Put a NoteGran effect in front of a synth**: in Bitwig, a note FX before the instrument; in REAPER, before the instrument in the track's FX chain; in StageHand, the row's **MIDI FX** menu. Start with **Cloud**.
2. **Hold a chord.** Its notes light in the view, and grains start rising from them.
3. **Turn Density** to play more or fewer grains, and **Length** for longer or shorter ones.
4. **Choose a Pick:** Up or Down with **Onset random** at 0 is an arpeggio; Random with more Onset random and some **Scatter** is a cloud.
5. **Tick Dry** to hear the chord itself as well.

The host saves every setting with its project. What you held, and Buffer's recording, aren't saved: a new session starts empty.
:::

::: {.callout .callout-note .card}
## The Window {#window}
**The header:** the Unlikelyware logo (a click opens unlikelyware.com), **NoteGran** and the effect's name in its colour (Cloud steel blue, Chords violet, Grid amber, Burst teal, Buffer rose), and **Help** at the right (the **?** box that opens this effect's help page in your browser).

**Below it:** the effect's view (or views) at the top, then its settings in titled groups. Each knob shows its name above it and its value below.

- **Dimmed settings** have no effect just now: EDO and Root unless Quantize is EDO, Weight unless Pick is Random, and others noted below.
- **Sync** swaps knobs: with **Free**, Density and Length (ms); with **Beats**, Rate and Length (beats).

**Resizing:** drag the host window's corner. The window keeps its shape and scales everything in it, down to 0.8 of its size.
:::

<details class="disclosure">
<summary class="disclosure-title subtle">Knobs, sliders, menus and checkboxes</summary>

::: disclosure-content
| Gesture | Result |
|---|---|
| Drag a knob up or down | Change the value |
| Control-drag a knob | Ten times finer (Control on the Mac too) |
| Double-click or Shift-click a knob | Back to the default |
| Drag a slider (Grid's tiers) | Moves it as far as the pointer moves; a click never jumps it |
| Shift-drag a slider | Finer |
| Double-click a slider | Back to the default |
| Scroll over a knob or slider | Nudges it |
| Click a menu | Opens its list; click a choice, or click elsewhere to close it |
| Click a checkbox | Turns it on or off |

A knob with a wide range from above zero (Density, the lengths, Duration, Window in seconds) turns on a logarithmic scale, so the low end gets as much travel as the high end.
:::

</details>

::: {.callout .callout-note .card}
## Cloud {#cloud}
**The plain one:** grains whose pitches are picked from the notes you hold. Its window is 900 × 560.

**The view:** a keyboard of equal semitones over the range you're playing. The notes you hold are lit on it, and each grain rises from its exact pitch and fades, so microtonal pitches show where they really are.

**Groups:** [Timing](#timing) (Sync, Density or Rate, Length, Onset random), **Pick**, [Pitch](#pitch), [Dynamics](#dynamics) and [Input](#input).

**Pick** (default Random), which held note a grain plays:
- **Random**, weighted by **Weight**: 0 picks every note equally, 1 picks louder notes more often.
- **Up** and **Down**, by pitch; **Up-down** goes up then down.
- **Played**, in the order you played the notes.

The ordered picks keep their place when you change the chord: the next grain plays the next note after the last one's pitch.

**An arpeggiator to a cloud:** with Sync on Beats, Onset random at 0 and Pick Up, Cloud is an arpeggiator; raise Onset random and Scatter and it thins into a cloud.
:::

::: {.callout .callout-note .card}
## Chords {#chords}
**A transformer:** each note you hold becomes a chord built on it in a scale, held as long as the note. Play it into a synth, or into Cloud, Grid or Burst. Its window is 920 × 660.

**The view:** the scale on a circle of N steps, the key at the top, its degrees numbered (Roman numerals for a seven-note scale). The notes sounding are lit and joined. **Click a degree** to see the chord on it, one note to a line: its name (with its cents off 12-EDO), then the just interval it's nearest above the chord's root and how far off, such as **E4−14¢ 5/4 −4.5¢**. Click it again to hide it. Beside the circle, the settings in words: the scale and key, the EDO and mapping, and the chord.

**The intervals are named within an octave,** so a 10th reads as a 3rd: from 1/1 (the root) through 16/15, 9/8, 8/7, 7/6, 6/5, 11/9, 5/4, 9/7, 4/3, 11/8, 7/5, 10/7, 3/2, 14/9, 8/5, 13/8, 5/3, 12/7, 7/4, 16/9, 9/5, 11/6 and 15/8. The nearest is shown however far off it is, so a large error means the interval isn't near any of them: a neutral or quarter-tone sound.

**Scale group:**
- **Scale:** the modes of major, melodic minor and harmonic minor, harmonic major, the pentatonics, whole tone and more. **Key** (C♭ … C♯): its key.
- **EDO** (1 to 96, default 12): the scale is mapped into this many steps per octave. **Origin** (default A) is step 0 of the EDO, as in KeyboardEDO, so they agree.
- **Map**, how each familiar degree finds its EDO step: **Nearest** (default), the step nearest its usual pitch, closest to the familiar sound; or **By fifths**, built from the EDO's own fifth, which keeps the scale's shape (thirds stay thirds) and gives the EDO's own character.
- **Input**, what a played note is matched against: **Familiar**, an ordinary keyboard (a note is in the scale if its usual key is); or **EDO**, a keyboard playing the EDO itself, such as KeyboardEDO.
- **Scale channel** (default None): hold notes on this channel and they *are* the scale, already in the EDO, kept until you play a new one there. Scale and Key are then dimmed.

Map, Origin and Input matter only away from 12-EDO, and are dimmed at 12.

**Chord group:** **Size** (1 to 6 notes; 1 is a scale quantizer), **Stack** (Thirds, Fourths or Fifths: every 2nd, 3rd or 4th degree), **Inversion** (Root, 1st … 5th: that many of its lowest notes go up an octave), **Voicing** (Close, Open, Drop-2 or Spread) and **Octave** (−2 to +2).

**Output group:** **Off-scale**, what a note outside the scale does: **Pass** (default; it plays alone, as played), **Nearest** (it becomes the nearest degree, with its chord) or **Drop**. Then Level, Voices and When full ([Dynamics](#dynamics), [Input](#input)).

An in-scale note sounds on its EDO step, so the whole chord is in the EDO. Changing the scale while you hold notes changes only the chords you play next.

**This EDO,** between Scale and Output: what the EDO does to chords, so you can see before you play whether stacked chords will be sweet, tense or strange. It follows the **EDO** setting. Each interval row gives steps · cents off the just ratio, twice: the EDO's **best** step for it, and the step the diatonic **scale** gives it when built from the EDO's fifth (as **Map: By fifths** builds it).

| Row | What it tells you |
|---|---|
| **Fifth 3/2** | The fifth every chord leans on, and how far from pure |
| **Major 3rd 5/4** | The sweet major third. Is the scale's major third (four fifths up) the EDO's best one? |
| **Minor 3rd 6/5** | The same for the minor third (three fifths down) |
| **7th 7/4** | The harmonic seventh, against the scale's minor seventh: the barbershop or bluesy seventh |
| **Diatonic** | The scale's large step to its small one (2:1 in 12), or 7 equal steps, 5 equal steps, mavila or none |
| **Meantone 81/80** | **yes:** a progression such as I–vi–ii–V–I comes back in tune, as in 12 |
| **Augmented 128/125** | **yes:** three major thirds make an octave, so the augmented triad splits it evenly |
| **Diminished 648/625** | **yes:** four minor thirds make an octave, so the diminished seventh splits it evenly |

**A scale value in green** is the EDO's best: a chord stacked in the scale gets that interval at its sweetest. Some examples:
- **19 and 31:** the thirds are green. Triads in the scale are the EDO's best, purer than 12's. The good 7/4 is there too, but not on the scale's minor seventh: it's a step lower, on the augmented sixth.
- **22 and 53:** the best major third is excellent (−4.5¢ and −1.4¢), but the scale's is a step above it (+50¢ in 22). Stacked in the scale with **By fifths**, triads sound tense and Pythagorean; with **Map: Nearest** in 22, C–E and G–B land on the good 7 steps, but F–A stays at 8: the scale is no longer one chain of fifths, so its thirds differ chord to chord.
- **17:** neither third is near 5/4 or 6/5. Its chords have their own bright, neutral colour, not 12's.

**A comma** is a tiny interval between two routes to "the same" note. A **yes** means the EDO treats the two as one, so chords built on that equivalence close up as they do in 12.
:::

::: {.callout .callout-note .card}
## Grid {#grid}
**Grains on a beat grid** whose places have **tiers**, each with its own settings: a downbeat can always take the lowest note, and an off-beat a random one. Grid always runs on beats. Its window is 980 × 700.

**The view:** one cycle of the meter over a keyboard of what you hold. Each slot is a bar as tall as its tier is strong and as bright as its Probability; the playhead marks the slot now, and slots flash as they play.

**Clock group:** **Rate** (slots per beat: 1/4 … 8), **Meter** (Straight, or grouped: 5 as 3+2 or 2+3, 7 as 2+2+3, 3+2+2 or 2+3+2, 9 as 3+3+3), **Density** (1 to 4 notes a slot) spread through the slot by **Smear**, **Length** (beats), and the length and velocity spreads.

**The tiers' table:** a column per tier, named from the meter. Straight's are **Bar, Half, Quarters, Eighths, Sixteenths**; a grouped meter's are its groups, then **Beats** and **Off-beats** (a meter of two groups leaves one tier unused, dimmed). A row per setting:
- **Probability:** the chance a slot of this tier plays.
- **Level:** times the held note's velocity.
- **Offset:** semitones added to the note picked.
- **Chaos:** how much of **Scatter** and **Smear** this tier gets; at 0 it plays exactly on the slot, at its pitch.
- **Pick:** **Lowest**, **Highest**, **Next** (up through the notes) or **Random**.

Defaults: Probability 1, 1, 1, 0.75, 0.5; Level 1, 0.85, 0.75, 0.65, 0.55; Pick Lowest, Next, Next, Next, Random; Chaos 0. **Scatter** (in Pitch) defaults to ±12 semitones here, so raising a tier's Chaos is all it takes.

The cycle counts from the host's beat 0, so a Grid stays locked to the song and to other Grids.
:::

::: {.callout .callout-note .card}
## Burst {#burst}
**Each new note starts a phrase** of grains, rooted on it, shaped by a two-transform fractal (an IFS attractor). Its window is 960 × 660.

**The attractor editor** (left): time across, pitch up. Each transform is a filled dot (its fixed point) joined to a hollow one (its handle). **Drag a dot** to move the transform; **drag its handle** to change its **Contraction** (the line's length) and **Rotation** (its angle). The points drawn are the real attractor, with **Vary** on, the next 16 phrases', the first's larger, so you see what Burst will play. A handle past the edge is drawn, and can be grabbed, at the edge.

**The phrase view** (right): the next phrase, time across its Duration and pitch up and down from the root, each grain's size by its velocity after Decay; a second pass is ghosted with Loop. Below, the keyboard of what you hold.

**Phrase group:**
- **Duration** (ms or beats): the phrase's length. **Count** (1 to 64): how many grains. **Range** (± semitones): how far up and down the attractor's pitch reaches.
- **Release**, what letting go of the root does: **Finish** (the phrase plays out), **Stop** (no more grains) or **Loop** (the phrase repeats while you hold the note).
- **Vary:** on, every phrase is a different draw from the attractor; off, every phrase has the same shape.
- **Probability:** the share of the points from T1 rather than T2.

**Grain group:** Length, Level, **Decay** (0 to 1: how much the grains fade across the phrase), and the spreads.

Up to 16 phrases play at once, sharing Voices; a 17th replaces the oldest. A quick re-strike starts a new phrase.
:::

::: {.callout .callout-note .card}
## Buffer {#buffer}
**Records what you play** and replays grains from the recording. Play a phrase (or put a looper in front) and Buffer explores it. Its window is 940 × 600.

**The view:** the keyboard of what you're playing, each grain rising from its pitch.

**Capture group:**
- **Window** (seconds or beats; default 4 s / 8 beats): how far back the recording reaches (up to 512 notes).
- **Freeze:** keeps the recording as it is; new notes play through (with Dry) but aren't recorded.
- **Position** (0 oldest to 1 newest): where grains read from. **Scan**: how the read point drifts: 0 holds it, 1 moves in real time, negative goes backwards (−4 to 4). **Spray**: a random spread around it.

**Grain group:**
- **Grain:** **Note**, a grain is the recorded note nearest the read point; **Fragment**, a grain is the stretch of the recording **Length** long from the read point, every note in it replayed with its timing, so a chord stays a chord and a bit of a run becomes a figure.
- **Density** or **Rate**, and **Onset random** ([Timing](#timing)); dimmed with a trigger channel, whose notes are the grains.
- **Length**, and **Length from** (Note only): **Length**, the knob; **Source**, the recorded note's own length.
- **Gate:** on, grains play only while you hold a key.

**Dry is on by default here:** you usually want to hear what you're playing. All Notes Off, a reset and the transport stopping clear the recording.
:::

::: {.callout .callout-note .card}
## Timing {#timing}
Cloud, Burst and Buffer have **Sync**; Grid always runs on beats.
- **Sync:** **Free** (grains per second, lengths in ms) or **Beats** (grains per beat, lengths in beats), from the host's tempo and position; with the transport stopped, from its tempo; with no host clock, 120 BPM.
- **Density** (Free; 0.1 to 200 per second, default 8) or **Rate** (Beats; 1/4, 1/2, 1, 2, 3, 4, 6 or 8 per beat, default 4).
- **Length** (1 ms to 5 s, default 100 ms; or 1/64 to 16 beats, default ¼).
- **Onset random** (0 to 1, default 0.5): 0 spaces grains evenly (in Beats, exactly on the grid); 1 is a random scatter at the same average rate.

Timing is worked out from the song's position, not from audio blocks, so with a fixed **Seed** the same input gives exactly the same grains.
:::

::: {.callout .callout-note .card}
## Pitch {#pitch}
Applied to every grain, in this order (Chords has its own pitch settings):
- **Octaves** (0 to 4; Cloud, Buffer): a grain may move up or down by up to that many octaves, at random.
- **Transpose** (±48 semitones, fractions allowed).
- **Scatter** (0 to 24 semitones): a random spread up and down, more often small than large.
- **Quantize:** **Off**; **EDO**, snapping to the steps of the **EDO** (1 to 96, from **Root**, default A, as in KeyboardEDO); or **Set**, snapping to the nearest held note (or its octave), so a scattered grain stays in your chord's harmony.

A pitch outside MIDI's range is folded back by octaves rather than piled on the top or bottom key.
:::

::: {.callout .callout-note .card}
## Dynamics {#dynamics}
- **Level** (0 to 2, default 1): times the held note's velocity.
- **Velocity spread** and **Length spread** (0 to 1): random variation of each grain's velocity and length.
:::

::: {.callout .callout-note .card}
## Input {#input}
How each effect listens, and how many notes it plays:
- **Voices** (1 to 32, default 8): at most this many notes at once. Every grain is a voice on your synth, so keep it under the synth's limit, with room for release tails: a dense cloud wants short grains, not many long ones.
- **When full:** **Skip** (default) doesn't play a new note while Voices are all sounding, or while its key is already sounding, so grains stay whole; **Steal** ends the oldest note to play the new one.
- **Dry:** whether the notes you hold also sound. Off by default, except in Buffer.
- **Hold over** (0 to 200 ms, default 20): a note released and struck again this quickly stays in the set, so a sequencer repeating a chord doesn't empty it for an instant.
- **Set channel** (default All): only notes on this channel are the set.
- **Trigger channel** (default None): notes on this channel are rhythm instead: each one plays a grain (in Burst, starts a phrase), at its velocity, and Density or Rate is ignored. A drum pattern or a footswitch can play the set.
- **Latch:** on, the set stays after you let go, until your next note starts a new one.
- **Seed** (0 to 9999; 0 is new each start): with a seed set, the same input at the same times gives exactly the same notes, on any machine.

Chords has only Voices and When full here.
:::

::: {.callout .callout-note .card}
## Hosts and Microtones {#hosts}
NoteGran works in pitches finer than a semitone throughout: from KeyboardEDO, from Chords in an EDO, from Scatter, or Quantize EDO. Whether the synth hears them depends on the host:

| Host | The synth hears |
|---|---|
| StageHand | The exact pitch (MIDI 2.0 Pitch 7.9) |
| Bitwig | The exact pitch, from a synth that honours tuning expressions (CLAP notes) |
| REAPER | The nearest semitone |

A grain leaves in the form its note came in: MIDI 2.0 with its exact pitch, MIDI 1.0 rounded to the nearest key, a CLAP note with a tuning expression for the fraction. **Chords** is the exception: a chord note with a fraction goes out as MIDI 2.0 even from a MIDI 1.0 note, so an EDO chord isn't rounded back to 12.

The Unlikelyware synths play Pitch 7.9, and PlainOldSynth, StruckSynth, StruckRoom and StruckGaussian also play CLAP tuning expressions.
:::

::: {.callout .callout-note .card}
## Notes and MIDI {#midi}
- **Every note NoteGran starts, it ends.** Letting go, All Notes Off or All Sound Off, a reset, and (in Beats) the transport stopping all end its notes.
- **Pitch moves are followed:** per-note pitch bend and Pitch 7.25 on a held note (KeyboardEDO's glides) move it in the set; grains picked after the move use the new pitch.
- **Two grains a few cents apart** go on different keys in MIDI 2.0, so they don't cut each other off.
- **Everything that isn't a note** passes through unchanged: CCs, channel pitch bend, program changes, SysEx, clock.
:::

::: {.callout .callout-note .card}
## Tips {#tips}
- **Chords → Cloud:** a chord from each note, scattered as grains. In StageHand, put Chords before Cloud in the row's MIDI FX.
- **An arpeggio:** Cloud, Sync Beats, Rate 4, Onset random 0, Pick Up.
- **Keep it in key:** Quantize Set snaps scattered grains to the notes you hold.
- **A pad that breathes:** Cloud with long Length, low Density, Steal, and Voices near your synth's limit.
- **A drummer plays your chord:** set Trigger channel to the drum track's channel.
- **Repeatable takes:** set a Seed; the same playing gives the same grains.
:::

::: {.callout .callout-note .card}
## Troubleshooting {#troubleshooting}
- **Nothing plays.** Nothing is held (or Set channel excludes it), or a Trigger channel is set and no trigger notes arrive. Hold a chord; check the channels.
- **Fewer grains than Density says.** Voices are full and When full is Skip. Raise Voices, shorten Length, or use Steal.
- **Grains are cut short.** When full is Steal at a high density. Lower Density, shorten Length, or use Skip.
- **I hear my chord as well as the grains.** Dry is on (it is by default in Buffer).
- **EDO, Root or Weight don't do anything.** They're dimmed: Quantize isn't EDO, or Pick isn't Random.
- **Microtones sound rounded.** See [Hosts and Microtones](#hosts).
- **Beats-mode grains drift from the song.** The host isn't sending its position; with the transport stopped NoteGran runs from the tempo alone.
- **A rebuilt NoteGran doesn't show its changes.** Quit and restart the host: a running host keeps the plugin it loaded first.
:::
