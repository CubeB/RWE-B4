#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;

    /** One hit this machine caused on a Remote unit, to send as a 0x0b. `damage` is the raw figure the weapon delivered, before veterancy and armour scale it, which is what the wire carries. */
    struct OutgoingDamage
    {
        UnitId victim;
        std::optional<UnitId> attacker;
        unsigned int damage;
    };

    /** One death of a Local unit, to send as a 0x0c. `causeAndLevel` packs the death cause in the high nibble and the corpse level in the low one, exactly as DemoRecorder packs the 0x0c's last byte. */
    struct OutgoingDeath
    {
        UnitId unit;
        std::optional<UnitId> killer;
        unsigned int severity;
        uint8_t causeAndLevel;

        /** The damage-type death cause, `unit+0xF5`. */
        unsigned int cause() const { return causeAndLevel >> 4; }

        /** The corpse level the script wrote back: 0 none, 1 the wreck, 2-3 below it. */
        unsigned int corpseLevel() const { return causeAndLevel & 0x0fu; }
    };

    /** The damage and deaths this machine's own units produced, for the network layer (#427) to send. Like SimEventLog it is never hashed, saved or dumped, and never read by the simulation, so filling it cannot move a lockstep hash or an outcome. */
    struct MixedOwnershipOutbox
    {
        std::vector<OutgoingDamage> damage;
        std::vector<OutgoingDeath> deaths;

        void clear()
        {
            damage.clear();
            deaths.clear();
        }
    };

    /** The simulation's outbox, created on first use. */
    MixedOwnershipOutbox& mixedOwnershipOutboxOf(GameSimulation& sim);

    /** Records a death of a Local unit with the cause and corpse level the simulation decided. Nothing is recorded with no Remote player to tell. */
    void recordLocalDeath(
        GameSimulation& sim,
        UnitId unit,
        std::optional<UnitId> killer,
        unsigned int severity,
        unsigned int cause,
        unsigned int corpseLevel);

    /** Applies an incoming 0x0b to one of this machine's units through the ordinary damage path, so the local `Killed` script picks the death and corpse and the Remote attacker is credited. A victim that is not Local is left alone. */
    void applyIncomingDamage(GameSimulation& sim, UnitId victim, std::optional<UnitId> attacker, unsigned int damage);

    /** A Local unit's hit on a Remote unit: the health falls for display and a 0x0b is recorded, but it is never killed -- only its owner's 0x0c and full-state record settle it. Damage that came from a Remote machine is ignored. */
    void applyLocalDamageToRemoteUnit(
        GameSimulation& sim,
        UnitId victim,
        std::optional<UnitId> attacker,
        unsigned int damage,
        bool paralyzer,
        std::optional<PlayerId> sourceOwner);

    /** The DELETEPLAYER hook: a Remote peer has left, so its units are removed where they stand, the commander through TA's cause-8 quit death. */
    void removeRemotePlayer(GameSimulation& sim, PlayerId player);
}
