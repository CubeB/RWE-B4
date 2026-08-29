#include "ScoutManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        // Two scouts do not chase the same ground.
        const SimScalar ScoutTargetSeparation = 320_ss;

        // Retreat once this badly hurt; the scout is more use alive.
        bool isBadlyHurt(const UnitState& unit, const UnitDefinition& def)
        {
            return def.maxHitPoints > 0 && unit.hitPoints * 3 < def.maxHitPoints;
        }

        PlayerCommand moveCommand(UnitId unit, const SimVector& to, PlayerUnitCommand::IssueOrder::IssueKind kind)
        {
            return PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(MoveOrder(to), kind));
        }
    }

    void ScoutManager::update(
        const GameSimulation& sim,
        const AiTuningProfile& profile,
        const ThreatMap& threatMap,
        const ReachabilityMap& reachability,
        AiBlackboard& bb,
        std::vector<PlayerCommand>& outCommands)
    {
        // Lost our stand-in scout? Forget it.
        if (bb.scoutUnitId)
        {
            auto scout = sim.tryGetUnitState(*bb.scoutUnitId);
            if (!scout || scout->get().isDead())
            {
                bb.scoutUnitId.reset();
            }
        }
        // Targets of scouts that are gone are free again. (An idle scout keeps
        // its claim: its orders take a few ticks to arrive, and it is given a
        // fresh target the next time it is sent out anyway.)
        for (auto it = bb.scoutTargets.begin(); it != bb.scoutTargets.end();)
        {
            auto unit = sim.tryGetUnitState(UnitId(it->first));
            if (!unit || unit->get().isDead())
            {
                it = bb.scoutTargets.erase(it);
            }
            else
            {
                ++it;
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

        // Dedicated scouts do the job once we have them; until then, and if
        // none is on the way, the first combat unit stands in.
        std::vector<UnitId> scouts = bb.scoutUnits;
        if (scouts.empty())
        {
            const auto& s = bb.sideUnits;
            auto onTheWay = [&](const std::string& t) { return !t.empty() && bb.ownedTotalCounts.count(t) > 0; };
            if (!onTheWay(s.scoutPlane) && !onTheWay(s.scoutVehicle))
            {
                if (!bb.scoutUnitId && !bb.combatUnits.empty())
                {
                    bb.scoutUnitId = bb.combatUnits.front();
                    bb.armySize = static_cast<int>(bb.combatUnits.size()) - 1;
                }
                if (bb.scoutUnitId)
                {
                    scouts.push_back(*bb.scoutUnitId);
                }
            }
        }
        else if (bb.scoutUnitId)
        {
            // The stand-in goes back to the army.
            bb.scoutUnitId.reset();
        }

        for (auto scoutId : scouts)
        {
            sendScout(sim, threatMap, reachability, bb, scoutId, outCommands);
        }
    }

    void ScoutManager::sendScout(
        const GameSimulation& sim,
        const ThreatMap& threatMap,
        const ReachabilityMap& reachability,
        AiBlackboard& bb,
        UnitId scoutId,
        std::vector<PlayerCommand>& outCommands) const
    {
        const auto& scout = sim.getUnitState(scoutId);
        const auto& def = sim.unitDefinitions.at(scout.unitType);
        bool flies = def.canFly;

        // Hurt: head home, where the base can look after it.
        if (isBadlyHurt(scout, def) && bb.baseAnchor)
        {
            if (scout.position.distanceSquared(*bb.baseAnchor) > (256_ss * 256_ss) && scout.orders.empty())
            {
                outCommands.push_back(moveCommand(scoutId, *bb.baseAnchor, PlayerUnitCommand::IssueOrder::IssueKind::Immediate));
                bb.scoutTargets.erase(scoutId.value);
            }
            return;
        }

        if (!scout.orders.empty())
        {
            return;
        }

        std::vector<SimVector> chosen;
        auto accept = [&](int x, int y, const SimVector& center) {
            // Someone else is already going there.
            for (const auto& [otherId, target] : bb.scoutTargets)
            {
                if (otherId != scoutId.value && target.distanceSquared(center) < ScoutTargetSeparation * ScoutTargetSeparation)
                {
                    return false;
                }
            }
            for (const auto& c : chosen)
            {
                if (c.distanceSquared(center) < ScoutTargetSeparation * ScoutTargetSeparation)
                {
                    return false;
                }
            }
            // Known guns cover it: a scout that flies in dies for nothing.
            if (threatMap.antiGroundAtCell(x, y) > 0.0f)
            {
                return false;
            }
            if (!flies)
            {
                // Ground scouts keep to land they can reach.
                if (bb.groundReachabilityValid && !reachability.isReachable(sim, center))
                {
                    return false;
                }
                if (sim.terrain.getHeightAt(center.x, center.z) < sim.terrain.getSeaLevel())
                {
                    return false;
                }
            }
            return true;
        };

        // A plane strings several legs together so it keeps flying rather
        // than hovering at the first spot; a ground scout takes one at a time.
        int legs = flies ? 3 : 1;
        auto from = scout.position;
        for (int i = 0; i < legs; ++i)
        {
            auto target = threatMap.bestScoutTarget(from, accept);
            if (!target)
            {
                break;
            }
            SimVector destination(target->x, sim.terrain.getHeightAt(target->x, target->z), target->z);
            chosen.push_back(destination);
            from = destination;
        }

        if (chosen.empty())
        {
            return;
        }
        LOG_DEBUG << "AI scout " << scoutId.value << " (" << scout.unitType << "): " << chosen.size() << " leg(s), ending at " << chosen.back().x.value << "," << chosen.back().z.value;
        for (std::size_t i = 0; i < chosen.size(); ++i)
        {
            outCommands.push_back(moveCommand(scoutId, chosen[i], i == 0 ? PlayerUnitCommand::IssueOrder::IssueKind::Immediate : PlayerUnitCommand::IssueOrder::IssueKind::Queued));
        }
        bb.scoutTargets[scoutId.value] = chosen.back();
    }
}
