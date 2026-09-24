#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * The id blocks a demo's unit ids partition into.
     *
     * `id = block * maxUnits + index + 1`, with the block holding one owner's
     * units and zero meaning no unit -- the arithmetic
     * docs/TA-DEMOS.md calls the cheap filter, and `tadUnitIdOfIndex` is the
     * inverse. TA recycles ids, so slots are handed out lowest-free-first and
     * a dead unit's slot is reused by the next unit in that owner's block.
     *
     * A block that cannot seat another unit is a game TA cannot represent
     * either, so the caller is told rather than a unit being quietly dropped.
     * Blocks are assigned in first-allocation order; the numbers are stable
     * per owner but are deliberately not the demo's player numbers (the
     * corpus's demo 14733 has sender 1 in block 2, so nothing may assume
     * they agree).
     */
    class DemoIdAllocator
    {
    public:
        explicit DemoIdAllocator(uint16_t maxUnits = 1000);
        ~DemoIdAllocator();

        DemoIdAllocator(const DemoIdAllocator&) = delete;
        DemoIdAllocator& operator=(const DemoIdAllocator&) = delete;

        uint16_t maxUnits() const;

        /**
         * Gives `unit` the lowest free slot in `owner`'s block, and returns
         * the global demo id. Nothing once that block is full, or once the
         * block number itself would not fit an id.
         *
         * A unit that already holds an id is handed the same one back, so a
         * caller that is unsure whether it has seen a unit before can just ask.
         */
        std::optional<uint16_t> allocate(PlayerId owner, UnitId unit);

        /** The global demo id a unit was given, or nothing if it has none. */
        std::optional<uint16_t> idOf(UnitId unit) const;

        /** The block-relative index of a unit, or nothing. */
        std::optional<uint16_t> indexOf(UnitId unit) const;

        /** The owner whose block a unit sits in, or nothing. */
        std::optional<PlayerId> ownerOf(UnitId unit) const;

        /** Slots currently in use in an owner's block; zero for an unseen owner. */
        std::size_t usedBy(PlayerId owner) const;

        /** Frees a unit's slot for the next unit in its block. Nothing for a unit that has none. */
        void release(UnitId unit);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    /** The per-game part of a recording, frozen when the recorder is created. */
    struct DemoRecorderSettings
    {
        uint16_t maxUnits = 1000;

        /** Goes in the header; the reader takes it to the record's end. */
        std::string mapName;

        /**
         * The data set's unit types in TA's load order, 0-based:
         * `tadUnitLoadOrder` over the loader's `units` directory listing, so a
         * type's index on the wire is its position here plus one. It is the
         * caller's to supply because `simulation.unitDefinitions` is an
         * unordered_map and carries no order.
         */
        std::vector<std::string> unitLoadOrder;

        /**
         * Recorder version and date sectors. Fixed by default so that one
         * game records the same bytes however many times it is run -- the
         * tools ignore both, and a wall-clock read would only make two runs
         * of a replay differ. Callers with a real date to write inject it.
         */
        std::string recorderVersion{"rwe"};
        std::string date{"1970-01-01"};
    };

    /**
     * Writes a TA demo of a game RWE is simulating.
     *
     * A pure observer: it holds no simulation state, the simulation never
     * reads it back, and nothing it does can change a tick. It is attached to
     * the simulation by `GameSimulation::attachDemoRecorder` and driven by the
     * calls the simulation already makes -- unit creation, nanoframe creation,
     * a shot, a hit, a death, an ownership change, unit removal, and the end
     * of a tick. See docs/adr/0001-demo-recorder-is-a-pure-observer.md.
     *
     * **The ordering convention is load-bearing.** Per sender per tick the
     * packet is: the unit pass's records (0x09 build started, 0x12 build
     * finished, 0x0d shots), then the 0x2c, then the projectile pass's
     * (0x0b damage, 0x0c deaths), then the settle's (0x28 resources, every 120
     * ticks). The corpus oracles read a shot queued before the 0x2c one tick
     * early, and a build's first increment on a multiple of thirty; get it
     * wrong and every cell they score on RWE output reads a tick off.
     *
     * One packet per sender per tick, no watcher seat: the recorder is a wire
     * tap, not a player (ADR D1).
     */
    class DemoRecorder
    {
    public:
        /**
         * Writes the header and everything before the packet stream from
         * `simulation`, then watches it. Throws if the file will not open, if
         * a unit type is missing from `settings.unitLoadOrder`, or if a
         * player's block is already full.
         */
        DemoRecorder(const std::filesystem::path& path, const GameSimulation& simulation, DemoRecorderSettings settings);

        /** As above, into a stream: the test path, and what TadWriter itself does. */
        DemoRecorder(std::ostream& stream, const GameSimulation& simulation, DemoRecorderSettings settings);

        ~DemoRecorder();

        DemoRecorder(const DemoRecorder&) = delete;
        DemoRecorder& operator=(const DemoRecorder&) = delete;

        /** The simulation has created a unit. Gives it a demo id. */
        void unitCreated(const GameSimulation& simulation, UnitId unit);

        /** A unit is gone and its slot can be reused. */
        void unitRemoved(UnitId unit);

        /**
         * A nanoframe of type `unit` has been placed by `builder`: a 0x09,
         * with the builder remembered so the completion can name it.
         */
        void buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit);

        /**
         * A weapon fired: a 0x0d, sent by the shooter's owner.
         *
         * `weaponSlot` is the 0-based index into the shooter's own
         * Weapon1/Weapon2/Weapon3 -- the wire's trailing byte, which the corpus
         * pins by slot occupancy rather than by weapon count. `targetUnit` is
         * nothing where the shot was aimed at a place or at a projectile,
         * which the wire writes as a target id of zero. `direction` is the
         * launch direction the round actually left on, after the aim error the
         * original's turret handler scatters a shot by (0x49D6D7), which is
         * what the corpus's rotation triple records a real aiming error from.
         */
        void shotFired(
            const GameSimulation& simulation,
            UnitId shooter,
            unsigned int weaponSlot,
            std::optional<UnitId> targetUnit,
            const SimVector& origin,
            const SimVector& aimPoint,
            const SimVector& direction);

        /**
         * `victim` took `damage` from `attacker`, or from nothing where the
         * engine knows of no attacking unit: a 0x0b, sent by the attacker's
         * owner.
         *
         * The corpus is what settles the sender: 797,783 damage records are
         * sent by the peer that owns the attacker and not one by the peer that
         * owns the victim (docs/TA-DEMOS.md, "Pairing a 0x0d to the 0x0b it
         * caused"), so a shot and the damage it caused share one tick clock.
         * A record with no attacker to attribute -- a dying unit's `explodeAs`
         * is the only one RWE has -- goes to `sourceOwner`, the player whose
         * simulation ran the blast; the corpus's own no-attacker records carry
         * no id to compare against, but the one real recording they could be
         * checked in shows they are not the victim's owner's.
         */
        void damageApplied(
            const GameSimulation& simulation,
            UnitId victim,
            std::optional<UnitId> attacker,
            unsigned int damage,
            std::optional<PlayerId> sourceOwner);

        /**
         * `unit` died, with the severity and cause the engine itself worked
         * out: a 0x0c, sent by the victim's owner.
         *
         * The victim's owner is the sender on the one real recording this
         * could be measured against: of 754 deaths, all 298 whose killer is in
         * another block are sent by the victim's block, and all 53 with no
         * killer at all likewise. `cause` is the original's damage-type nibble
         * (TOTALA-EXE-WRECKS.md, the eleven causes) and `corpseLevel` the low
         * nibble: 0 leaves nothing, 1 the intact wreck.
         */
        void unitDied(
            const GameSimulation& simulation,
            UnitId unit,
            std::optional<UnitId> killer,
            unsigned int severity,
            unsigned int cause,
            unsigned int corpseLevel);

        /**
         * `unit` changed hands, which a demo cannot express as the same id
         * changing owner (ADR-0001 D10). The original's own answer is
         * destroy-and-replace -- cause 4 is "the unit changed owner" and
         * 0x488570 kills the old record and creates a new one under the
         * captor -- so this emits the cause-4 death for the old id, frees its
         * slot and gives the unit a fresh id in `newOwner`'s block.
         */
        void unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner);

        /**
         * End of a tick: a 0x2c per sender, then the packet. Called once per
         * simulation tick, after every phase.
         */
        void endOfTick(const GameSimulation& simulation);

        /** Flushes and closes. Idempotent; the destructor calls it. */
        void close();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
