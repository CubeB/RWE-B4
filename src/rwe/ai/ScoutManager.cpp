#include "ScoutManager.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/Index.h>

namespace rwe
{
    namespace
    {
        // Two scouts do not chase the same ground.
        const SimScalar ScoutTargetSeparation = 320_ss;

        /**
         * How long a declared start position has to have gone unwatched before
         * a scout is sent to look at it, in threat-map rebuilds -- which is
         * about a second apiece at Standard. Low enough that the opening
         * sweep starts at once, high enough that a scout which has just been
         * there does not turn straight round.
         */
        const float StartPositionStaleThreshold = 20.0f;

        /**
         * A start position this close to our base anchor is our own, and
         * there is nothing to learn by scouting it.
         */
        const SimScalar OwnStartPositionRadius = 512_ss;

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

        // Opening priority: the map's own start positions.
        //
        // A player opens by looking at the other corners, because that is
        // where an enemy has to be. The AI is handed the same list a player
        // reads off the lobby preview (MapIntel.h) and does the same thing
        // with it -- and only until it has actually found the enemy base,
        // after which the staleness search below is the better guide.
        //
        // Nothing here tells the AI which position the enemy took. It is
        // still a search: the scout has to go and look.
        if (!bb.enemyBasePosition && bb.mapIntel.valid && !threatMap.isEmpty())
        {
            const auto& staleness = threatMap.getStaleness();
            std::optional<SimVector> bestStart;
            float bestStale = StartPositionStaleThreshold;
            for (const auto& start : bb.mapIntel.startPositions)
            {
                if (bb.baseAnchor && start.distanceSquared(*bb.baseAnchor) < OwnStartPositionRadius * OwnStartPositionRadius)
                {
                    continue;
                }
                auto cell = threatMap.cellAt(start);
                if (cell.x < 0 || cell.y < 0 || static_cast<std::size_t>(cell.x) >= staleness.getWidth() || static_cast<std::size_t>(cell.y) >= staleness.getHeight())
                {
                    continue;
                }
                auto stale = staleness.get(static_cast<std::size_t>(cell.x), static_cast<std::size_t>(cell.y));
                if (stale <= bestStale)
                {
                    continue;
                }
                if (!accept(cell.x, cell.y, start))
                {
                    continue;
                }
                bestStale = stale;
                bestStart = start;
            }
            if (bestStart)
            {
                chosen.push_back(*bestStart);
            }
        }

        // A plane strings several legs together so it keeps flying rather
        // than hovering at the first spot; a ground scout takes one at a time.
        int legs = flies ? 3 : 1;
        auto from = chosen.empty() ? scout.position : chosen.back();
        for (Index i = getSize(chosen); i < static_cast<Index>(legs); ++i)
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
