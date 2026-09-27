#include <rwe/sim/MixedOwnership.h>
#include <memory>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/UnitState.h>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * Whether this machine's simulation ran the hit, as opposed to applying
         * a result it received. A projectile names the player whose simulation
         * fired it as its owner, so a Local shooter's hit is ours and a replayed
         * Remote shot's is not -- and a hit with no source at all (acid water,
         * decay) belongs to the unit's owner to report.
         */
        bool damageIsThisMachines(const GameSimulation& sim, std::optional<UnitId> attacker, std::optional<PlayerId> sourceOwner)
        {
            if (sourceOwner)
            {
                return simulatesLocally(sim, *sourceOwner);
            }

            if (attacker)
            {
                if (auto attackerUnit = sim.tryGetUnitState(*attacker))
                {
                    return simulatesLocally(sim, attackerUnit->get().owner);
                }
            }

            return false;
        }
    }

    MixedOwnershipOutbox& mixedOwnershipOutboxOf(GameSimulation& sim)
    {
        if (!sim.mixedOwnershipOutbox)
        {
            sim.mixedOwnershipOutbox = std::make_unique<MixedOwnershipOutbox>();
        }
        return *sim.mixedOwnershipOutbox;
    }

    void recordLocalDeath(
        GameSimulation& sim,
        UnitId unit,
        std::optional<UnitId> killer,
        unsigned int severity,
        unsigned int cause,
        unsigned int corpseLevel)
    {
        // Only our own units' deaths are ours to report, and only when there is
        // a peer to report them to.
        auto unitRef = sim.tryGetUnitState(unit);
        if (!unitRef || !simulatesLocally(sim, unitRef->get().owner) || !hasRemotePlayer(sim))
        {
            return;
        }

        auto packed = static_cast<uint8_t>(((cause & 0xfu) << 4u) | (corpseLevel & 0xfu));
        mixedOwnershipOutboxOf(sim).deaths.push_back(OutgoingDeath{unit, killer, severity, packed});
    }

    void applyIncomingDamage(GameSimulation& sim, UnitId victim, std::optional<UnitId> attacker, unsigned int damage)
    {
        auto unitRef = sim.tryGetUnitState(victim);
        if (!unitRef || unitRef->get().isDead() || !simulatesLocally(sim, unitRef->get().owner))
        {
            return;
        }

        // The ordinary path: the local Killed script picks the corpse, and a
        // killing blow credits the Remote attacker.
        sim.applyDamage(victim, damage, attacker);
    }

    void applyLocalDamageToRemoteUnit(
        GameSimulation& sim,
        UnitId victim,
        std::optional<UnitId> attacker,
        unsigned int damage,
        bool paralyzer,
        std::optional<PlayerId> sourceOwner)
    {
        auto unitRef = sim.tryGetUnitState(victim);
        if (!unitRef || unitRef->get().isDead() || !damageIsThisMachines(sim, attacker, sourceOwner))
        {
            return;
        }

        // The record goes out whatever the damage type; the wire's 0x0b carries
        // the figure alone.
        mixedOwnershipOutboxOf(sim).damage.push_back(OutgoingDamage{victim, attacker, damage});

        // A paralyzer buys stun time, not hit points, and the stun is the
        // owner's to apply as well.
        if (paralyzer)
        {
            return;
        }

        // The health falls for display, as TA's attacker side shows it, but
        // never reaches zero: the owner's next full-state record overwrites it,
        // and only the owner's 0x0c declares the death.
        auto& unit = unitRef->get();
        unit.hitPoints = unit.hitPoints > damage ? unit.hitPoints - damage : 1;
    }

    void removeRemotePlayer(GameSimulation& sim, PlayerId player)
    {
        // A DELETEPLAYER names an id this machine has to look up, and a stream
        // can name one that is not a player at all.
        if (player.value >= sim.players.size())
        {
            return;
        }

        std::vector<UnitId> owned;
        for (const auto& [unitId, unit] : sim.units)
        {
            if (!unit.isDead() && unit.isOwnedBy(player))
            {
                owned.push_back(unitId);
            }
        }

        for (auto unitId : owned)
        {
            const auto& definition = sim.unitDefinitions.at(sim.getUnitState(unitId).unitType);
            if (definition.commander)
            {
                // TA's quit sequence: the commander's own 0x0c carries cause 8,
                // the quit, and its explosion follows.
                sim.unitDeathObservations[unitId.value] = UnitDeathObservation{"quit", "", std::nullopt};
                sim.killUnit(unitId);
            }
            else
            {
                // The rest vanish where they stand; the peer stops describing
                // their slots.
                sim.quietlyKillUnit(unitId);
            }
        }

        sim.getPlayer(player).status = GamePlayerStatus::Dead;
    }
}
