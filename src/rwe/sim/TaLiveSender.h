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

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/TaPeerIds.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /** What a live sender is launched with: the player whose units this machine owns on the wire, and what to encode them against. */
    struct TaLiveSenderSettings
    {
        /** The Local player. Its units take the host's owner block, and nothing else is described. */
        PlayerId sender{0};

        /** The id block size, agreed with the peer at launch: a slot is `tick % maxUnits`. */
        uint16_t maxUnits = 1000;

        /** The data set's unit types in TA's load order, as `tadUnitLoadOrder` gives them. */
        std::vector<std::string> unitLoadOrder;
    };

    /**
     * One tick's subpackets, already in the order they go on the wire: the
     * unit pass (0x09, 0x12, 0x0d), the 0x2c, the projectile pass (0x0b, 0x0c),
     * then the settle's 0x28.
     *
     * One tick is a batch rather than a message because that is where the split
     * falls: the network layer puts six of them in a UDP message, thirty ticks
     * a second, which is what TA itself does.
     */
    struct TaLiveBatch
    {
        /** The simulation tick these records describe, which is the 0x2c's own serial. */
        uint32_t tick{0};

        std::vector<std::vector<uint8_t>> subPackets;
    };

    /** What a sender could not put on the wire, for the network layer to log and the tests to pin. */
    struct TaLiveSenderStats
    {
        uint64_t batches{0};

        /** A 0x2c the layout could not describe, so the tick's full-state record was left out. */
        uint64_t unitStateSkipped{0};

        /** An outbox record naming a unit that cannot be named on the wire, and so cost that record. */
        uint64_t recordsDroppedNoId{0};

        /** A unit of a type the load order does not name, and so left undescribed. */
        uint64_t unitsRefused{0};
    };

    /**
     * Sends the Local player's traffic to a TA peer, as subpackets per tick.
     *
     * A pure observer, exactly as the demo recorder is (ADR-0001): it holds no
     * simulation state, nothing it does is hashed, saved or dumped, and the
     * simulation never reads it back. It is attached by
     * `GameSimulation::attachTaLiveSender` and driven by the calls the
     * simulation already makes -- unit creation, a nanoframe, a shot, an
     * ownership change, unit removal, and the end of a tick.
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
         * Watches `simulation` from here on. Throws if the load order is empty
         * or `maxUnits` is zero, which are launch arguments rather than game
         * states.
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
         */
        std::optional<TaLiveBatch> takeBatch();

        const TaLiveSenderStats& stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
