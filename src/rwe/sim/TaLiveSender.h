#pragma once

// The simulation side of sending RWE's units to a TA peer live (#427).
//
// A live sink with the DemoRecorder's shape: attached to a game RWE is
// simulating, told about the same events, and never feeding anything back.
// What differs is where the bytes go. A demo writes them to a file; this hands
// them to whoever owns the socket, one tick's worth at a time, and leaves the
// framing to the network layer.
//
// See docs/TA-NETWORK.md, "In game", for the rule that settles what has to go
// out: each unit's owner is authoritative, so this machine's full-state record
// is the only thing that keeps its copy of a unit alive on the peer, and an
// empty record for a slot this machine owns is a deletion.
//
// The settings, the batches and the peer's ids are in TaLiveBatch.h,
// TaWireTape.h and TaPeerIds.h, and this header holds nothing but the pimpl and
// the counters, because the simulation calls a sender's hooks from a header
// nearly everything includes.

#include <cstdint>
#include <memory>
#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/TaLiveSenderHooks.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;
    struct TaLiveBatch;
    struct TaLiveSenderSettings;
    struct TaPeerIds;
    struct TaWireTapeSettings;

    /** What a sender could not put on the wire, for the network layer to log and the tests to pin. */
    struct TaLiveSenderStats
    {
        uint64_t batches{0};

        /** A 0x2c the layout could not describe, so the tick's full-state record was left out. */
        uint64_t unitStateSkipped{0};

        /**
         * The 0x0b records sent: a hit this machine put on a unit the peer
         * simulates, named in the victim's owner's block. Zero over a run in
         * which nothing was hit is the answer a caller wants, and it is the
         * opposite of `recordsDroppedNoId`, which is a hit that could not be
         * named and so never reached the peer at all.
         */
        uint64_t damageSent{0};

        /** The 0x0c records sent: a death of one of this machine's own units. */
        uint64_t deathsSent{0};

        /** An outbox record naming a unit that cannot be named on the wire, and so cost that record. */
        uint64_t recordsDroppedNoId{0};

        /** A unit of a type the load order does not name, or one a full block could not seat, and so left undescribed. */
        uint64_t unitsRefused{0};
    };

    /**
     * Sends the Local player's traffic to a TA peer, as subpackets per tick.
     *
     * A pure observer, exactly as the demo recorder is (ADR-0001): it holds no
     * simulation state, nothing it does is hashed, saved or dumped, and the
     * simulation never reads it back.
     *
     * What it sends, and why that is the whole mandatory set: the `0x2c` with
     * a waypoint entry for every unit whose path changed and the full-state
     * record for slot `tick % maxUnits`; the `0x09`, `0x12` and `0x0d` of the
     * Local units; the `0x0b` for RWE's hits on TA's units and the `0x0c` for
     * Local deaths, both drained from the mixed-ownership outbox; and the
     * `0x28` on the settle cadence. The spike's fake host kept a player in a
     * game for five minutes on the `0x2c` alone.
     *
     * **No `0x10`.** A script-call echo is a quarter of a real stream by
     * subpacket count and RWE has no inventory of the `0x456200` call sites
     * that would say which script each one means (TA-DEMOS D9, a recorded
     * divergence the demo recorder carries too). What a peer shows without
     * them is set out in docs/TA-DEMOS.md, "Writing one": a unit appears and
     * moves and fires and dies correctly, from the `0x09` and the `0x2c` and
     * the `0x0d`, and what it does not do is run its own COB on the peer --
     * so no muzzle flash, no antenna deployment, no custom radar or sight
     * radius, and a factory builds from the `0x09`/`0x12` pair but does not
     * animate. Nothing is invisible or missing that a unit's own `0x2c` does
     * not already carry.
     */
    class TaLiveSender
    {
    public:
        /**
         * Watches `simulation` from here on: it seats the units already
         * standing, so this is built after the game has loaded and before the
         * first tick. Throws if the load order is empty or `maxUnits` is zero,
         * which are launch arguments rather than game states.
         *
         * `peerIds` is how a record naming a unit this machine does not
         * simulate can still name it: a `0x0b` for a hit on a TA unit carries
         * that unit's id in the peer's own owner block, which only the peer's
         * allocation says. Without one, such a record is written with a zero
         * target, which is the wire's "no unit" and which the peer drops.
         */
        TaLiveSender(const GameSimulation& simulation, TaLiveSenderSettings settings, TaPeerIds peerIds);

        ~TaLiveSender();

        TaLiveSender(const TaLiveSender&) = delete;
        TaLiveSender& operator=(const TaLiveSender&) = delete;

        /**
         * Where the simulation should tell this sink about events, and the
         * whole of the wiring: `simulation.setTaLiveSender(sender.hooks())`.
         *
         * The caller keeps the sender alive for as long as the table is
         * registered, and unregisters it -- `setTaLiveSender({})` -- before
         * dropping it. `TaLiveSenderHooks` says why it is a table and not a
         * pointer.
         */
        TaLiveSenderHooks hooks() const;

        /** The simulation has created a unit. Gives a Local one a wire id. */
        void unitCreated(const GameSimulation& simulation, UnitId unit);

        /** A unit is gone and its slot can be reused. */
        void unitRemoved(UnitId unit);

        /** A nanoframe has been placed by `builder`: a 0x09, with the builder remembered for the 0x12. */
        void buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit);

        /** A weapon fired: a 0x0d, sent by the shooter's owner. */
        void shotFired(
            const GameSimulation& simulation,
            UnitId shooter,
            unsigned int weaponSlot,
            std::optional<UnitId> targetUnit,
            const SimVector& origin,
            const SimVector& aimPoint,
            const SimVector& direction);

        /** `unit` changed hands; the old id is killed with a cause-4 death and, if the unit is ours now, a fresh id is seated. */
        void unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner);

        /** End of a tick: the tick's batch, ready to be taken. Called once per simulation tick, after every phase. */
        void endOfTick(const GameSimulation& simulation);

        /**
         * Takes the oldest tick's batch, or nothing where every tick so far
         * has been taken. Batches queue, because the network layer groups six
         * of them into a message and so does not take one every tick.
         *
         * `TaLiveBatch` is in TaLiveBatch.h, which a caller needs and the
         * simulation does not.
         */
        std::optional<TaLiveBatch> takeBatch();

        /**
         * The wire id a record naming this unit would carry, or nothing where
         * the unit is not one this sender describes. A host needs it to read
         * an incoming `0x0b`, which names its victim by the id this sender
         * gave it.
         */
        std::optional<uint16_t> wireIdOf(UnitId unit) const;

        const TaLiveSenderStats& stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
