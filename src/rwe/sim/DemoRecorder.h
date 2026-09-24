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
#include <rwe/sim/UnitId.h>

namespace rwe
{
    class GameSimulation;

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
     * the simulation by `GameSimulation::attachDemoRecorder` and driven by
     * four calls the simulation already makes -- unit creation, nanoframe
     * creation, unit removal, and the end of a tick. See
     * docs/adr/0001-demo-recorder-is-a-pure-observer.md.
     *
     * **The ordering convention is load-bearing.** Per sender per tick the
     * packet is: the unit pass's records (0x09 build started, 0x12 build
     * finished, and later 0x0d/0x10), then the 0x2c, then the projectile
     * pass's (0x0b/0x0c), then the settle's (0x28). The corpus oracles read a
     * shot queued before the 0x2c one tick early, and a build's first
     * increment on a multiple of thirty; get it wrong and every cell they
     * score on RWE output reads a tick off. M3 writes the 0x09, 0x12 and 0x2c
     * and leaves the other slots standing empty; M4 fills them.
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
