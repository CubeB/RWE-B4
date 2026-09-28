#pragma once

// One sender's TA subpackets, and the state that has to be kept to produce
// them: ids allocated out of the owner's block, the last mover put on the wire,
// and the data set's load order.
//
// The wire ordering is load-bearing and lives here, once. Per sender per tick
// it is the unit pass, then the 0x2c, then the projectile pass, then the
// settle, and the corpus oracles read a tick off it. DemoRecorder writes a
// tape into a demo file and TaLiveSender hands one to the network layer;
// neither owns the encoding, because the two must not be able to disagree.
// See docs/TA-DEMOS.md, "Writing one".

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_util.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/TaPeerIds.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /** What a tape is encoded against: the id block size agreed at launch, and the data set's unit load order. */
    struct TaWireTapeSettings
    {
        uint16_t maxUnits = 1000;

        /**
         * The first id block this tape's ids are allocated from. Zero for a
         * demo or a recording, which describes every player from the first
         * block; a live host passes 1, because a joining TA takes block 0 for
         * itself and two owners in one block is every unit of one of them
         * deleted on the other machine (docs/TA-NETWORK.md, "In game").
         */
        unsigned int firstBlock = 0;

        /**
         * The data set's unit types in TA's load order, 0-based, so a type's
         * index on the wire is its position here plus one. The caller's to
         * supply because `simulation.unitDefinitions` carries no order.
         */
        std::vector<std::string> unitLoadOrder;
    };

    /** One sender's subpackets for one tick, in the order they go on the wire. */
    struct TaTickRecords
    {
        /** 0x09 build started, 0x12 build finished, 0x0d shot fired. */
        std::vector<TadBytes> unitPass;

        /** The 0x2c. Empty only where the layout cannot describe the tick, which is a misconfigured load order rather than a game state. */
        TadBytes unitState;

        /** 0x0b damage, 0x0c death. */
        std::vector<TadBytes> projectilePass;

        /** 0x28 resources, on the settle cadence. */
        std::vector<TadBytes> settle;
    };

    /** Why a unit is not on the wire, when it is not. */
    enum class TaWireRefusal
    {
        None,

        /** A type the load order does not name, so it has no index to write. */
        UnknownType,

        /** The owner's block cannot seat another unit, which is a game TA cannot represent either. */
        BlockFull,
    };

    /**
     * Encodes what one sender's units did, per tick, as the subpackets TA puts
     * on the wire for them.
     *
     * Ids come from a `DemoIdAllocator` for the players in `describedPlayers`
     * and from `peerIds` for everyone else, so a tape either watches a whole
     * game (the recorder, which is a wire tap of every sender) or one machine's
     * own player (the live sender). Nothing here is simulation state and nothing
     * is read back: this is an observer's bookkeeping, and it decides nothing.
     */
    class TaWireTape
    {
    public:
        /**
         * Whatever is already standing gets an id too: a start-position
         * commander and anything a save restored were never built under this
         * tape and get no 0x09, but they are on the map and the full-state
         * round robin has to have a slot for them.
         *
         * Throws if `maxUnits` is zero or the load order is empty: a tape
         * cannot encode anything without them, and that is a misconfiguration
         * rather than a game state.
         */
        TaWireTape(
            const GameSimulation& simulation,
            TaWireTapeSettings settings,
            std::vector<PlayerId> describedPlayers,
            TaPeerIds peerIds = {});

        ~TaWireTape();

        TaWireTape(const TaWireTape&) = delete;
        TaWireTape& operator=(const TaWireTape&) = delete;

        /** Whether this tape puts a unit on the wire itself rather than leaving it to the peer. */
        bool describes(PlayerId player) const;

        /** The wire id a record naming this unit would carry, or nothing where the unit cannot be named. */
        std::optional<uint16_t> idOf(UnitId unit) const;

        /**
         * The DirectPlay id a 0x0c's killer is named by, or nothing. RWE has
         * none of its own, so a player this tape describes is numbered from
         * one -- as synthetic as the ids themselves -- and anyone else is the
         * peer's to name.
         */
        std::optional<uint32_t> dplayIdOf(PlayerId player) const;

        uint16_t maxUnits() const;

        TaWireRefusal unitCreated(const GameSimulation& simulation, UnitId unit);

        void unitRemoved(UnitId unit);

        /**
         * A nanoframe of type `unit` has been placed by `builder`: a 0x09,
         * with the builder remembered so the completion can name it. False
         * where either unit cannot be named, and the frame goes undescribed.
         */
        bool buildStarted(const GameSimulation& simulation, UnitId builder, UnitId unit);

        /**
         * An aim script started: a 0x10 naming the function in the unit's own
         * script, with the heading and pitch it was called with, so the peer
         * turns the turret its puppet shows. Dropped where the unit cannot be
         * named or the index does not fit the record.
         */
        void aimScriptStarted(UnitId unit, unsigned int functionIndex, int heading, int pitch);

        /**
         * A weapon fired: a 0x0d, sent by the shooter's owner. Dropped where
         * the shooter cannot be named. The weapon's TDF `ID` is read off the
         * shooter's slot and written into the record's first word, which is
         * the index the receiver looks its weapon flags up by.
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
         * owner -- 797,783 damage records in the corpus and not one sent by the
         * victim's owner (docs/TA-DEMOS.md, "Pairing a 0x0d to the 0x0b it
         * caused"). With no attacker to name, the peer that ran the blast sends
         * it and only the victim's owner remains.
         */
        void damageApplied(UnitId victim, std::optional<UnitId> attacker, unsigned int damage, std::optional<PlayerId> sourceOwner);

        /**
         * `unit` died: a 0x0c, sent by the victim's owner, which is the only
         * record that kills anything on the receiver. `cause` is the original's
         * damage-type nibble and `corpseLevel` the low nibble.
         */
        void unitDied(UnitId unit, std::optional<UnitId> killer, unsigned int severity, unsigned int cause, unsigned int corpseLevel);

        /**
         * `unit` changed hands, which the wire cannot express as one id
         * changing owner (ADR-0001 D10). The original's own answer is
         * destroy-and-replace: a cause-4 death for the old id, and a fresh id
         * in the new owner's block if this tape describes that owner.
         *
         * A unit this tape never described and now describes is a capture of
         * somebody else's, and arrives the way a creation does -- with no
         * 0x09, because a capture is not a nanoframe, so the next 0x2c is the
         * only record that will carry its type.
         *
         * Returns a refusal where the new block cannot seat the unit.
         */
        TaWireRefusal unitCaptured(const GameSimulation& simulation, UnitId unit, PlayerId newOwner);

        /**
         * Ends one sender's tick: the 0x12s whose frames completed, the 0x2c
         * with its waypoint entries and the full-state record for slot
         * `tick % maxUnits`, and the 0x28 where the tick is a settle. That
         * sender's queued records are dropped, so a caller takes each tick once.
         */
        TaTickRecords endOfTick(const GameSimulation& simulation, PlayerId player);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
