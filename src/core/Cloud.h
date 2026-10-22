#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Assigner.h"
#include "core/Clock.h"
#include "core/NoteEvents.h"
#include "core/NoteSet.h"
#include "core/OwnedNotes.h"
#include "core/Pitch.h"
#include "core/Random.h"
#include "uw/ump/MidiMessage.h"

// NoteGran Cloud's logic (PLAN.md, "NoteGran Cloud"), with no CLAP: grains whose pitches are picked
// from the set, at a rate on the clock. The plugin layer (phase 2) and a host's generator both drive
// it the same way: inputs and a transport in, NoteEvents out, all in absolute sample time. What it
// shares with the other assigners is Assigner's.
namespace notegran {

struct CloudParams {
    Clock::Mode  sync         = Clock::Mode::Free;
    double       density      = 8.0;           // Free: grains per second
    std::int64_t divisionNum  = 4;             // Beats: divisionNum per divisionDen beats
    std::int64_t divisionDen  = 1;
    double       lengthMs     = 100.0;         // Free
    std::int64_t lengthBeats  = kBeat / 4;     // Beats, in kBeat units
    std::int32_t onsetRandom  = 0;             // Q16, 0 .. 1: grid .. Poisson dust
    Pick         pick         = Pick::Random;      // Random .. Played
    std::int32_t weight       = 0;             // Q16, 0 .. 1: Random's weighting by velocity
    PitchParams  pitch;
    std::int32_t level        = 65536;         // Q16, 0 .. 2
    std::int32_t velSpread    = 0;             // Q16, 0 .. 1
    std::int32_t lenSpread    = 0;             // Q16, 0 .. 1
    int          voices       = 8;
    WhenFull     whenFull     = WhenFull::Skip;
    bool         dry          = false;
    double       holdOverMs   = 20.0;
    int          setChannel   = NoteSet::kAll;
    int          triggerChannel = NoteSet::kNone;
    bool         latch        = false;
    std::uint64_t seed        = 1;             // the plugin layer picks one when Seed is 0
};

class CloudCore : public Assigner {
public:
    // Applied from the next block. The seed only when it changes.
    void setParams(const CloudParams& params) noexcept;

private:
    void         reapply() noexcept override { setParams(params_); }
    void         collectGrains(std::size_t& count) noexcept override;
    void         play(const Grain& g, NoteOut& out) noexcept override;
    Grid         grid() const noexcept;
    std::int64_t baseLength() const noexcept;

    CloudParams params_;
};

} // namespace notegran
