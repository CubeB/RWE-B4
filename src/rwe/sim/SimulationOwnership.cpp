#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    bool simulatesLocally(const GameSimulation& sim, PlayerId player)
    {
        return sim.getPlayer(player).simulation == PlayerSimulation::Local;
    }

    bool hasRemotePlayer(const GameSimulation& sim)
    {
        for (const auto& player : sim.players)
        {
            if (player.simulation == PlayerSimulation::Remote)
            {
                return true;
            }
        }
        return false;
    }

    void applyRemoteDamage(GameSimulation& sim, UnitId unitId, unsigned int damagePoints)
    {
        auto unitRef = sim.tryGetUnitState(unitId);
        if (!unitRef || unitRef->get().isDead())
        {
            return;
        }

        // Only the owner declares a death, with the cause and corpse level
        // its record carries; until it does, the unit is alive here.
        auto& unit = unitRef->get();
        unit.hitPoints = unit.hitPoints > damagePoints ? unit.hitPoints - damagePoints : 1;
    }
}
