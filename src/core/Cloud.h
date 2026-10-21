#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Clock.h"
#include "core/NoteEvents.h"
#include "core/NoteSet.h"
#include "core/OwnedNotes.h"
#include "core/Pitch.h"
#include "core/Random.h"
#include "uw/ump/MidiMessage.h"

// NoteGran Cloud's logic (PLAN.md, "NoteGran Cloud"), with no CLAP: grains whose pitches are picked
// from the set, at a rate on the clock. The plugin layer (phase 2) and a host's generator both drive
// it the same way: inputs and a transport in, NoteEvents out, all in absolute sample time.
namespace notegran {

enum class Pick : std::uint8_t {
    Random,    // weighted by velocity, by `weight`
    Up,        // by pitch
    Down,
    UpDown,
    Played,    // by arrival
};

struct CloudParams {
    Clock::Mode  sync         = Clock::Mode::Free;
    double       density      = 8.0;           // Free: grains per second
    std::int64_t divisionNum  = 4;             // Beats: divisionNum per divisionDen beats
    std::int64_t divisionDen  = 1;
    double       lengthMs     = 100.0;         // Free
    std::int64_t lengthBeats  = kBeat / 4;     // Beats, in kBeat units
    std::int32_t onsetRandom  = 0;             // Q16, 0 .. 1: grid .. Poisson dust
    Pick         pick         = Pick::Random;
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

// One input message, in time order, and whether it goes out unchanged (set by process()).
struct CloudInput {
    std::int64_t    time    = 0;                 // absolute sample
    uw::MidiMessage message;
    Dialect         dialect = Dialect::Midi2;    // Clap for a CLAP note, converted to MIDI 2.0
    bool            pass    = false;             // out: Other always; set notes with Dry; Reset
};

class CloudCore {
public:
    static constexpr std::size_t kMaxGrainsPerBlock = 1024;

    void setSampleRate(double sampleRate) noexcept;
    // Applied from the next block. The seed only when it changes.
    void setParams(const CloudParams& params) noexcept;

    // One block [start, start + frames): reads `inputs` (sorted by time, all in the block), writes
    // note starts and ends to `out`, sets each input's `pass`. Ends due in the block are sent.
    void process(std::int64_t start, std::int64_t frames, const Transport* transport, CloudInput* inputs,
                 std::size_t inputCount, NoteOut& out) noexcept;

    // Ends every note at `time` and clears the set: reset(), deactivating, a bypass.
    void reset(std::int64_t time, NoteOut& out) noexcept;

    // Notes sounding, or a set to play from: the plugin keeps being processed (PLAN.md).
    bool busy() const noexcept { return !owned_.empty() || !set_.empty(); }

    const NoteSet&    set() const noexcept { return set_; }
    const OwnedNotes& owned() const noexcept { return owned_; }

private:
    struct Grain {
        std::int64_t  time;
        std::int64_t  index;     // for the random draws
        std::uint16_t trigger;   // trigger velocity, or 0xFFFF
    };

    void        collectGrains(std::size_t& count) noexcept;
    void        play(const Grain& g, NoteOut& out) noexcept;
    std::size_t pick(std::int64_t index) noexcept;   // a member's index in set_, or size()
    std::size_t sortedMembers(bool byArrival) noexcept;
    Grid        grid() const noexcept;
    std::int64_t baseLength() const noexcept;

    CloudParams params_;
    Clock       clock_;
    NoteSet     set_;
    OwnedNotes  owned_;
    Random      random_;

    std::array<Grain, kMaxGrainsPerBlock>             grains_{};
    std::array<std::uint16_t, NoteSet::kCapacity>     order_{};   // member indices, sorted

    // The ordered picks' place: the last member picked, by (pitch, arrival), and Up-down's direction.
    bool          hasLast_  = false;
    std::int32_t  lastPitch_ = 0;
    std::uint64_t lastArrival_ = 0;
    bool          goingDown_ = false;
    std::int64_t  triggers_ = 0;     // trigger strikes so far: their grains' index
};

} // namespace notegran
