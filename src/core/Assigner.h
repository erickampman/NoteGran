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
// only when its grains are (collectGrains) and what each plays (play); Cloud, Grid and Burst are
// the three, and Buffer, a whole generator, uses it too.
//
// In a block: the grains the assigner collects for it, sorted by time, and the inputs, in time
// order, with the inputs at a sample before the grains at it (a set change comes first). Before
// each grain, the set settles and the notes due end, so whether a key is free never depends on
// where blocks fall. With a trigger channel, each trigger note-on is a grain instead (its count is
// its index), and the assigner collects none of its own. Burst instead starts phrases: it hears
// strikes and releases (heard), its trigger note-ons are roots and not grains, and its grains play
// whether or not the set is empty (a phrase plays out after its root is released). Buffer hears the
// set's notes too (to capture them) and plays with an empty set, and a Fragment grain adds the
// grains of the notes it replays (addGrain).
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
    virtual bool busy() const noexcept { return !owned_.empty() || !set_.empty(); }

    // Where the last block ended, in beat units, or -1 when it isn't on beats (an editor's playhead).
    std::int64_t beatPosition() const noexcept;

    const NoteSet&    set() const noexcept { return set_; }
    const OwnedNotes& owned() const noexcept { return owned_; }

protected:
    struct Grain {
        std::int64_t  time;
        std::int64_t  index;     // for the random draws
        std::uint16_t trigger;   // a trigger's velocity, or 0xFFFF
        std::uint8_t  tag;       // Grid's tier, Burst's phrase; 0 otherwise
    };

    void applySettings(const AssignerSettings& settings) noexcept;
    // The derived assigner's parameters again (after a sample-rate change).
    virtual void reapply() noexcept = 0;
    // The grains starting in this block (clock_ is set up for it), into grains_, sorted by time.
    // With a trigger channel (and not startsPhrases()), the triggers are the onsets: only grains
    // of another kind (a Fragment's notes) are collected.
    virtual void collectGrains(std::size_t& count) noexcept = 0;
    // One grain, the set settled and non-empty (unless startsPhrases()) and the due notes ended.
    virtual void play(const Grain& g, NoteOut& out) noexcept = 0;

    // Burst: trigger note-ons aren't grains, they're heard(); grains play with an empty set.
    virtual bool startsPhrases() const noexcept { return false; }
    // Grains play with an empty set (Burst, Buffer).
    virtual bool playsWithoutSet() const noexcept { return startsPhrases(); }
    // An input on a set channel (or, with startsPhrases(), the trigger channel) at `time`, after
    // the set has read it. grains_[next, count) are the block's grains not yet played, all at
    // `time` or later: it may add to them or remove from them, leaving them sorted.
    virtual void heard(const Heard& h, std::int64_t time, std::size_t next, std::size_t& count) noexcept {
        (void)h, (void)time, (void)next, (void)count;
    }
    // Every note was ended and the set cleared (reset(), All Notes Off, the transport stopping).
    virtual void cleared() noexcept {}

    // A trigger channel's note-ons are the grains (Cloud, Grid and Buffer collect no onsets).
    bool triggered() const noexcept { return triggerChannel_ != NoteSet::kNone && !startsPhrases(); }

    // From play() or heard(): a grain later in this block (before blockEnd(), at or after the one playing),
    // put in time order among those still to play. False if there's no room.
    bool addGrain(const Grain& g) noexcept;
    // From play() or heard(): drops the grains with `tag` still to play.
    void dropGrains(std::uint8_t tag) noexcept;

    // Cloud's and Buffer's onsets (PLAN.md, "Time"): the grains of `grid` in this block, with
    // Onset random `onsetRandom` (Q16), appended to grains_ and sorted.
    void collectOnsets(const Grid& grid, std::int32_t onsetRandom, std::size_t& count) noexcept;

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

    std::size_t  next_           = 0;   // in process(): the grain playing, and the block's count
    std::size_t  count_          = 0;
    bool         playing_        = false;   // in play(): grains_[next_] is playing
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
