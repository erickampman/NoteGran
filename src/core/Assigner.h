#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Clock.h"
#include "core/NoteEvents.h"
#include "core/NoteSet.h"
#include "core/OwnedNotes.h"
#include "core/Random.h"

// What every assigner shares (PLAN.md, "The model"): the set it picks from, the clock, the
// owned-note table, the random draws, and the order things happen in a block. An assigner says
// only when its grains are (collectGrains) and what each plays (play); Cloud and Grid are the two.
//
// In a block: the grains the assigner collects for it, sorted by time, and the inputs, in time
// order, with the inputs at a sample before the grains at it (a set change comes first). Before
// each grain, the set settles and the notes due end, so whether a key is free never depends on
// where blocks fall. With a trigger channel, each trigger note-on is a grain instead (its count is
// its index), and the assigner collects none of its own.
namespace notegran {

enum class Pick : std::uint8_t {
    Random,    // weighted by velocity, by `weight`
    Up,        // by pitch: the next above the last pick
    Down,
    UpDown,
    Played,    // by arrival
    Lowest,    // by pitch
    Highest,
};

// The settings every assigner has (PLAN.md, "What every plugin shares").
struct AssignerSettings {
    Clock::Mode   sync           = Clock::Mode::Free;
    int           voices         = 8;
    WhenFull      whenFull       = WhenFull::Skip;
    bool          dry            = false;
    double        holdOverMs     = 20.0;
    int           setChannel     = NoteSet::kAll;
    int           triggerChannel = NoteSet::kNone;
    bool          latch          = false;
    std::uint64_t seed           = 1;   // the plugin layer picks one when Seed is 0
};

class Assigner {
public:
    static constexpr std::size_t kMaxGrainsPerBlock = 1024;

    virtual ~Assigner() = default;

    void setSampleRate(double sampleRate) noexcept;

    // One block [start, start + frames): reads `inputs` (sorted by time, all in the block), writes
    // note starts and ends to `out`, sets each input's `pass`. Ends due in the block are sent.
    void process(std::int64_t start, std::int64_t frames, const Transport* transport, InputEvent* inputs,
                 std::size_t inputCount, NoteOut& out) noexcept;

    // Ends every note at `time` and clears the set: reset(), deactivating, a bypass.
    void reset(std::int64_t time, NoteOut& out) noexcept;

    // Notes sounding, or a set to play from: the plugin keeps being processed (PLAN.md).
    bool busy() const noexcept { return !owned_.empty() || !set_.empty(); }

    const NoteSet&    set() const noexcept { return set_; }
    const OwnedNotes& owned() const noexcept { return owned_; }

protected:
    struct Grain {
        std::int64_t  time;
        std::int64_t  index;     // for the random draws
        std::uint16_t trigger;   // a trigger's velocity, or 0xFFFF
        std::uint8_t  tier;      // Grid's tier; 0 otherwise
    };

    void applySettings(const AssignerSettings& settings) noexcept;
    // The derived assigner's parameters again (after a sample-rate change).
    virtual void reapply() noexcept = 0;
    // The grains starting in this block (clock_ is set up for it), into grains_, sorted by time.
    virtual void collectGrains(std::size_t& count) noexcept = 0;
    // One grain, the set settled and non-empty and the due notes ended.
    virtual void play(const Grain& g, NoteOut& out) noexcept = 0;

    // A member of the set by `mode` (its index in set_), or set_.size() if it's empty. Random
    // draws Pick draw 0 of `index`; the ordered picks go on from the last one.
    std::size_t pick(Pick mode, std::int32_t weight, std::int64_t index) noexcept;

    // Plays a note for member `m` at `pitch`: chooses its key (Dry: never a passing note's), and
    // starts it in the owned table.
    void start(const Member& m, std::int32_t pitch, std::int64_t time, std::int64_t length, std::uint16_t velocity,
               NoteOut& out) noexcept;

    Clock      clock_;
    NoteSet    set_;
    OwnedNotes owned_;
    Random     random_;
    std::array<Grain, kMaxGrainsPerBlock> grains_{};

private:
    void        playOne(const Grain& g, NoteOut& out) noexcept;
    std::size_t sortedMembers(bool byArrival) noexcept;

    bool         dry_            = false;
    int          triggerChannel_ = NoteSet::kNone;
    std::int64_t triggers_       = 0;   // trigger strikes so far: their grains' index
    std::array<std::uint16_t, NoteSet::kCapacity> order_{};   // member indices, sorted

    // The ordered picks' place: the last member picked, by (pitch, arrival), and Up-down's direction.
    bool          hasLast_     = false;
    std::int32_t  lastPitch_   = 0;
    std::uint64_t lastArrival_ = 0;
    bool          goingDown_   = false;
};

} // namespace notegran
