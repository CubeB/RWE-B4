#include "LineOfFire.h"

#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <variant>

namespace rwe
{
    namespace
    {
        /**
         * How far the ground may stand above the line before the shot counts
         * as blocked. A shallow rise the round passes a few feet over is not
         * a hill, and the sampled heightmap is bilinear where the collision
         * is not, so a tolerance keeps the answer from turning on a rounding
         * of the terrain rather than on its shape.
         */
        constexpr SimScalar BlockTolerance = 12_ss;

        /**
         * Samples along one shot, and stand points along one approach. Both
         * are caps rather than counts: a short shot takes a sample every
         * height tile and a long one spreads the same budget out, so the
         * cost of the test does not grow with the range of the weapon. The
         * pass runs over every combat unit the player owns.
         */
        constexpr int MaxShotSamples = 24;
        constexpr int MaxStandPoints = 8;

        SimVector muzzleOf(const SimVector& position)
        {
            return SimVector(position.x, position.y + MuzzleHeightAboveFeet, position.z);
        }

        /**
         * The reaching weapons of a unit, answered as a pair: how far the
         * longest one reaches, and whether any of them lobs.
         *
         * A lobbing weapon is exempt from the whole question. Ballistic and
         * dropped are the two the shipped data gives that clear a ridge by
         * design; a line-of-sight round, a tracking missile and a
         * self-propelled one all fly at what they are aimed at and stop on
         * the first thing they meet, ground included.
         */
        struct WeaponReach
        {
            SimScalar directRange{0_ss};
            bool hasLobbingWeapon{false};
        };

        WeaponReach weaponReachOf(const GameSimulation& sim, const UnitDefinition& def)
        {
            WeaponReach reach;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                if (weaponName.empty())
                {
                    continue;
                }
                auto it = sim.weaponDefinitions.find(weaponName);
                if (it == sim.weaponDefinitions.end())
                {
                    continue;
                }
                const auto& weapon = it->second;
                if (std::holds_alternative<ProjectilePhysicsTypeBallistic>(weapon.physicsType)
                    || std::holds_alternative<ProjectilePhysicsTypeBomb>(weapon.physicsType))
                {
                    reach.hasLobbingWeapon = true;
                    continue;
                }
                reach.directRange = rweMax(reach.directRange, weapon.maxRange);
            }
            return reach;
        }
    }

    bool shotClearsTerrain(const GameSimulation& sim, const SimVector& from, const SimVector& to)
    {
        auto delta = to - from;
        delta.y = 0_ss;
        auto flat = rweSqrt(delta.lengthSquared());
        if (flat <= MapTerrain::HeightTileWidthInWorldUnits)
        {
            // Point blank. There is no room between them for a hill.
            return true;
        }

        auto steps = static_cast<int>((flat / MapTerrain::HeightTileWidthInWorldUnits).value);
        steps = std::clamp(steps, 2, MaxShotSamples);

        // The ends are skipped deliberately. A unit stands on the ground, so
        // the ground at either end is level with the shot by construction
        // and would read as blocking every shot ever fired.
        for (int i = 1; i < steps; ++i)
        {
            auto t = intToSimScalar(i) / intToSimScalar(steps);
            auto point = from + ((to - from) * t);
            auto ground = sim.terrain.tryGetHeightAt(point.x, point.z);
            if (!ground)
            {
                continue;
            }
            if (*ground > point.y + BlockTolerance)
            {
                return false;
            }
        }
        return true;
    }

    bool terrainBlocksShot(const GameSimulation& sim, const UnitState& unit, const UnitState& target)
    {
        auto defIt = sim.unitDefinitions.find(unit.unitType);
        if (defIt == sim.unitDefinitions.end())
        {
            return false;
        }
        const auto& def = defIt->second;
        if (def.canFly)
        {
            // Nothing between an aircraft and the ground it is over. This is
            // also why the air arm is the answer to a map that keeps the
            // ground arm apart: see AirManager.
            return false;
        }

        auto reach = weaponReachOf(sim, def);
        if (reach.hasLobbingWeapon || reach.directRange <= 0_ss)
        {
            return false;
        }

        auto targetDefIt = sim.unitDefinitions.find(target.unitType);
        if (targetDefIt != sim.unitDefinitions.end() && targetDefIt->second.canFly)
        {
            // An aircraft is above the ridge, not behind it.
            return false;
        }

        return !shotClearsTerrain(sim, muzzleOf(unit.position), muzzleOf(target.position));
    }

    std::optional<SimVector> positionForClearShot(const GameSimulation& sim, const UnitState& unit, const UnitState& target)
    {
        if (!terrainBlocksShot(sim, unit, target))
        {
            return std::nullopt;
        }

        auto muzzleTarget = muzzleOf(target.position);
        for (int i = 1; i <= MaxStandPoints; ++i)
        {
            auto t = intToSimScalar(i) / intToSimScalar(MaxStandPoints + 1);
            auto candidate = unit.position + ((target.position - unit.position) * t);
            auto ground = sim.terrain.tryGetHeightAt(candidate.x, candidate.z);
            if (!ground)
            {
                continue;
            }
            candidate.y = *ground;
            if (shotClearsTerrain(sim, muzzleOf(candidate), muzzleTarget))
            {
                return candidate;
            }
        }

        // Every point on the line is still blocked, so the line itself is
        // the problem -- a wall, not a brow. Walk at the target and let the
        // pathfinder find the way round.
        return target.position;
    }
}
