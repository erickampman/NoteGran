# Contributing to NoteGran

Bug reports and pull requests are welcome. Building: [BUILDING.md](BUILDING.md); how it works and
why: [docs/design.md](docs/design.md), whose Layout and Code sections map the source. This page is
the rules the code depends on.

## Before each commit

```sh
ctest --test-dir build --output-on-failure
clap-validator validate build/NoteGran.clap
```

`notegran-core` tests the core (the set, the clock, the owned-note table, each effect);
`notegran-clap` loads the built `.clap` and plays it headless, so it needs no MIDI hardware. After
rebuilding, quit and restart the host: a running host keeps the plugin it loaded first.

## Rules

- **Parameters: append, never insert.** The CLAP parameter id is the parameter's index, and hosts
  save sessions and automation by id: a new parameter goes at the end of its effect's list, and
  none is reordered or removed. Only parameters are saved; the set and anything captured aren't.
- **Notes always end.** Every note goes out through the owned-note table, which is the only way its
  note-off is sent. `reset()`, All Notes Off, deactivating and the transport stopping end every
  note, and `process()` keeps returning `CLAP_PROCESS_CONTINUE` while any note sounds.
- **Real time.** No allocation, locks or threads on the audio thread: every table is fixed-size and
  allocated at `activate()`.
- **Repeatable.** Random draws are counter-based (seed, stream, index), never a running generator,
  and the timing path uses integers, so a set seed gives the same notes on any machine at any block
  size. The tests pin some draws: changing them changes saved songs.
- **The core has no CLAP in it** (`src/core/`), so hosts can share it: input as `uw::MidiMessage`,
  output as `NoteEvent`s, time from the caller.
- **Editor captions** are the parameters' names, shortened in `caption()` (`src/UI/NoteGranUI.cpp`)
  only where a name is too long for its cell; hosts keep the full name.

## Changing uw-core or DPF

UMP code comes from uw-core (`uw::ump`); NoteGran writes no UMP parsing of its own. A change it needs
there, or in DPF, is committed and pushed first (pugl, then DPF), then the pin moves here.
`git config push.recurseSubmodules check` makes git refuse a pin the other repo's remote doesn't
have yet.

## Third-party code and licenses

NoteGran is MIT licensed, and contributions are taken under the same license: a pull request is
offered as MIT.

- **What ships.** [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every third-party
  component in the builds, with its version, license and full license text, and every binary
  package includes it. A change that adds a component, or moves one to a new version, updates its
  entry in the same commit.
- **Upstream code stays as it came.** Submodules aren't edited in place: a fix goes upstream, to
  Unlikelyware's DPF or pugl fork, or into a patch file that `tools/fetch_deps.sh` applies.
  Copied-in files keep their copyright and license headers.
- **A new dependency** must allow MIT-licensed source and binary distribution with nothing more
  than keeping its notice: MIT, BSD, ISC, zlib, Apache-2.0 (with its NOTICE file, if it has one) or
  public domain, and the SIL Open Font License for fonts. No GPL or LGPL code.
- **Trademarks.** VST® is a registered trademark of Steinberg: the first "VST" in any document
  carries the ®, and a document that names VST ends with Steinberg's attribution line. Showing the
  VST Compatible Logo brings in the rest of [Steinberg's VST Usage Guidelines](https://github.com/steinbergmedia/vst3sdk/blob/master/VST3_Usage_Guidelines.pdf). Name
  hosts and other products plainly, to say what works with what, never so it reads as an
  endorsement.

VST is a registered trademark of Steinberg Media Technologies GmbH.
