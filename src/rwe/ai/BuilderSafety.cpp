#include "BuilderSafety.h"
#include <algorithm>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiMapBounds.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>

namespace rwe
{
    namespace
    {
        /** The longest reach of any weapon the definition names; zero for an unarmed one. */
        SimScalar weaponRange(const GameSimulation& sim, const UnitDefinition& def)
        {
            SimScalar best = 0_ss;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                auto it = weaponName.empty() ? sim.weaponDefinitions.end() : sim.weaponDefinitions.find(weaponName);
                if (it != sim.weaponDefinitions.end())
                {
                    best = rweMax(best, it->second.maxRange);
                }
            }
            return best;
        }

        /** Distance across the ground, ignoring height: units stand on terrain and targets are flat points. */
        SimScalar flatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }
    }

    BuilderExposure assessExposure(const GameSimulation& sim, PlayerId aiOwner, const AiBlackboard& bb, const BuilderSafetyParams& params, const SimVector& position, std::optional<UnitId> exclude)
    {
        BuilderExposure result;

        // Sum up the remembered armed enemies within reach of the position.
        // lastKnownPosition, not the enemy's live position, is used
        // throughout: the AI only knows what it last saw, and reacting to a
        // live position it has not actually observed would be reacting to
        // information it does not have.
        float threatMetal = 0.0f;
        SimScalar maxThreatRange = 0_ss;
        float centreX = 0.0f;
        float centreZ = 0.0f;
        unsigned int threatCount = 0;

        for (const auto& [id, enemy] : bb.knownEnemies)
        {
            if (!enemy.isArmed || enemy.isAir)
            {
                continue;
            }
            if (bb.now.value > enemy.lastSeen.value + params.memoryTicks)
            {
                continue;
            }

            auto unitOpt = sim.tryGetUnitState(enemy.unitId);
            if (!unitOpt)
            {
                continue;
            }
            const UnitState& unit = unitOpt->get();
            if (!unit.isAlive())
            {
                continue;
            }

            auto defIt = sim.unitDefinitions.find(enemy.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            const UnitDefinition& def = defIt->second;

            // A frame with no hit points is not a threat yet, whatever its
            // finished self can shoot.
            if (unit.isBeingBuilt(def))
            {
                continue;
            }

            auto range = weaponRange(sim, def);
            if (range <= 0_ss)
            {
                continue;
            }

            auto reach = range + (enemy.isBuilding ? params.staticThreatMargin : params.mobileThreatMargin);

            if (flatDistance(position, enemy.lastKnownPosition) <= reach)
            {
                threatMetal += def.buildCostMetal.value;
                maxThreatRange = rweMax(maxThreatRange, range);
                centreX += simScalarToFloat(enemy.lastKnownPosition.x);
                centreZ += simScalarToFloat(enemy.lastKnownPosition.z);
                ++threatCount;
            }
        }

        if (threatCount == 0)
        {
            return result;
        }

        result.threatMetal = threatMetal;
        result.maxThreatRange = maxThreatRange;
        auto centre = SimVector(
            floatToSimScalar(centreX / static_cast<float>(threatCount)),
            0_ss,
            floatToSimScalar(centreZ / static_cast<float>(threatCount)));
        result.threatCentre = centre;

        // The direction from the position toward the threat, flattened.
        // When the threat sits exactly on the position (direction comes
        // back zero), every mobile unit within coverRadius counts as cover:
        // there is no "behind" to exclude anything from.
        auto toThreat = centre - position;
        toThreat.y = 0_ss;
        auto direction = toThreat.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));

        float protectionMetal = 0.0f;

        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }
            if (exclude && *exclude == unitId)
            {
                continue;
            }

            auto defIt = sim.unitDefinitions.find(unit.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            const UnitDefinition& def = defIt->second;

            if (unit.isBeingBuilt(def))
            {
                continue;
            }
            if (def.canFly)
            {
                continue;
            }

            auto range = weaponRange(sim, def);
            if (range <= 0_ss)
            {
                continue;
            }

            float value = def.commander ? params.commanderCoverMetal : def.buildCostMetal.value;

            if (def.isMobile)
            {
                if (flatDistance(unit.position, position) > params.coverRadius)
                {
                    continue;
                }
                auto offset = unit.position - position;
                offset.y = 0_ss;
                if (offset.dot(direction) < -params.behindSlack)
                {
                    continue;
                }
                protectionMetal += value;
            }
            else
            {
                // A tower counts if its own range reaches the threat's
                // centre (with a little slack for the enemy having moved
                // since it was last seen there) or reaches the position
                // outright.
                if (flatDistance(unit.position, centre) <= range + 32_ss || flatDistance(unit.position, position) <= range)
                {
                    protectionMetal += value;
                }
            }
        }

        result.protectionMetal = protectionMetal;
        result.exposed = protectionMetal < threatMetal * params.protectionRatio;

        return result;
    }

    SimVector retreatPoint(const GameSimulation& sim, const AiBlackboard& bb, const BuilderSafetyParams& params, const SimVector& from, const BuilderExposure& exposure)
    {
        if (!exposure.threatCentre)
        {
            return from;
        }

        const auto& centre = *exposure.threatCentre;

        auto away = from - centre;
        away.y = 0_ss;
        away = away.normalizedOr(SimVector(1_ss, 0_ss, 0_ss));

        auto current = flatDistance(from, centre);
        auto needed = rweMax(96_ss, exposure.maxThreatRange + params.retreatExtraDistance - current);

        SimVector destination;

        if (bb.baseAnchor)
        {
            auto toBase = *bb.baseAnchor - from;
            toBase.y = 0_ss;

            if (toBase.dot(away) > 0_ss)
            {
                // The base lies on the safe side of the builder, away from
                // the threat: head there directly if it is close enough to
                // count as safety, or toward it otherwise.
                if (flatDistance(from, *bb.baseAnchor) <= needed * 2_ss)
                {
                    destination = *bb.baseAnchor;
                }
                else
                {
                    destination = from + toBase.normalizedOr(away) * needed;
                }
            }
            else
            {
                destination = from + away * needed;
            }
        }
        else
        {
            destination = from + away * needed;
        }

        return clampInsideVisibleMap(sim.terrain, destination, 64_ss);
    }

    std::vector<BuilderRetreat> planBuilderRetreats(const GameSimulation& sim, PlayerId aiOwner, const AiBlackboard& bb, const BuilderSafetyParams& params)
    {
        std::vector<BuilderRetreat> result;

        for (const auto& [unitId, unit] : sim.units)
        {
            if (unit.owner != aiOwner || !unit.isAlive())
            {
                continue;
            }

            auto defIt = sim.unitDefinitions.find(unit.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            const UnitDefinition& def = defIt->second;

            if (unit.isBeingBuilt(def))
            {
                continue;
            }
            if (!def.builder || !def.isMobile || def.commander || def.canFly)
            {
                continue;
            }

            auto exposure = assessExposure(sim, aiOwner, bb, params, unit.position, unitId);
            if (!exposure.exposed)
            {
                continue;
            }

            auto destination = retreatPoint(sim, bb, params, unit.position, exposure);

            if (!unit.orders.empty())
            {
                auto move = std::get_if<MoveOrder>(&unit.orders.front());
                if (move != nullptr && move->destination.distanceSquared(destination) < (64_ss * 64_ss))
                {
                    continue;
                }
            }

            result.push_back(BuilderRetreat{unitId, destination, exposure});
        }

        return result;
    }
}
