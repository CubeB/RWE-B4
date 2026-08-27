#include "ScoutManager.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    void ScoutManager::update(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        // Lost our scout? Forget it.
        if (bb.scoutUnitId)
        {
            auto scout = sim.tryGetUnitState(*bb.scoutUnitId);
            if (!scout || scout->get().isDead())
            {
                bb.scoutUnitId.reset();
            }
        }

        ++ticksSinceLastUpdate;
        if (ticksSinceLastUpdate < profile.scoutTickInterval)
        {
            return;
        }
        ticksSinceLastUpdate = 0;

        if (profile.scoutCount <= 0 || threatMap.isEmpty())
        {
            return;
        }

        // Recruit: the first combat unit becomes the scout while the army is
        // still small; once the army has plenty, pick the newest.
        if (!bb.scoutUnitId && !bb.combatUnits.empty())
        {
            bb.scoutUnitId = bb.combatUnits.front();
            bb.armySize = static_cast<int>(bb.combatUnits.size()) - 1;
        }
        if (!bb.scoutUnitId)
        {
            return;
        }

        const auto& scout = sim.getUnitState(*bb.scoutUnitId);
        if (!scout.orders.empty())
        {
            return;
        }

        auto target = threatMap.bestScoutTarget(scout.position);
        if (target)
        {
            outCommands.emplace_back(PlayerUnitCommand(*bb.scoutUnitId, PlayerUnitCommand::IssueOrder(MoveOrder(*target), PlayerUnitCommand::IssueOrder::IssueKind::Immediate)));
        }
    }
}
