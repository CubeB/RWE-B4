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
        auto& unit = sim.getUnitState(unitId);
        if (unit.isDead())
        {
            return;
        }

        if (unit.hitPoints <= damagePoints)
        {
            sim.killUnit(unitId);
        }
        else
        {
            unit.hitPoints -= damagePoints;
        }
    }
}
