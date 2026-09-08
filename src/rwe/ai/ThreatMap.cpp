#include "ThreatMap.h"
#include <algorithm>
#include <cmath>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    namespace
    {
        /** Rough damage per second of a unit's primary weapon against ground targets. */
        float estimateDps(const GameSimulation& sim, const UnitDefinition& def)
        {
            float total = 0.0f;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                if (weaponName.empty())
                {
                    continue;
                }
                // Both keyed upper case by the loader, whatever the FBI and
                // the weapon TDF happen to spell them. This looked up
                // "default" in lower case and so found nothing in a real
                // game: every weapon scored zero damage, the whole
                // anti-ground layer of the influence map was flat, and
                // bestAttackTarget was choosing on economic value with the
                // threat term it is weighted against permanently absent.
                // The unit test that covers it passed because the fixture
                // spelled the key the same wrong way.
                auto it = sim.weaponDefinitions.find(toUpper(weaponName));
                if (it == sim.weaponDefinitions.end())
                {
                    continue;
                }
                const auto& w = it->second;
                auto defaultDamage = w.damage.find("DEFAULT");
                float damage = defaultDamage == w.damage.end() ? 0.0f : static_cast<float>(defaultDamage->second);
                float reload = std::max(0.1f, w.reloadTime.value);
                total += damage * static_cast<float>(std::max(1, w.burst)) / reload;
            }
            return total;
        }

        /**
         * How far the unit's guns reach.
         *
         * Keyed upper case, for the same reason the damage lookup above is:
         * the loader upper-cases every weapon name and a unit's FBI does not,
         * so looking one up as written found nothing in a real game. Every
         * enemy's range came back zero, which put its threat on the single
         * cell it stood in instead of over the ground it covers -- so the
         * whole anti-ground layer was a scatter of points, and a raid target
         * "the threat map reads zero at" meant almost anywhere.
         */
        float weaponRange(const GameSimulation& sim, const UnitDefinition& def)
        {
            float best = 0.0f;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                if (weaponName.empty())
                {
                    continue;
                }
                auto it = sim.weaponDefinitions.find(toUpper(weaponName));
                if (it != sim.weaponDefinitions.end())
                {
                    best = std::max(best, it->second.maxRange.value);
                }
            }
            return best;
        }

        /** How far the unit reaches at an aircraft, or zero if it cannot touch one. */
        float antiAirRange(const GameSimulation& sim, const UnitDefinition& def)
        {
            float best = 0.0f;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                if (weaponName.empty())
                {
                    continue;
                }
                auto it = sim.weaponDefinitions.find(toUpper(weaponName));
                if (it == sim.weaponDefinitions.end() || !it->second.toAirWeapon)
                {
                    continue;
                }
                best = std::max(best, it->second.maxRange.value);
            }
            return best;
        }
    }

    ThreatMap::ThreatMap(int width, int height)
        : antiGround(width, height, 0.0f),
          antiAirCover(width, height, 0.0f),
          economic(width, height, 0.0f),
          staleness(width, height, 100000.0f)
    {
    }

    Point ThreatMap::cellAt(const SimVector& position) const
    {
        auto x = static_cast<int>(std::floor((position.x.value - origin.x.value) / cellSize));
        auto y = static_cast<int>(std::floor((position.z.value - origin.z.value) / cellSize));
        return Point(x, y);
    }

    SimVector ThreatMap::cellCenter(int x, int y) const
    {
        return SimVector(
            origin.x + SimScalar((static_cast<float>(x) + 0.5f) * cellSize),
            0_ss,
            origin.z + SimScalar((static_cast<float>(y) + 0.5f) * cellSize));
    }

    void ThreatMap::rebuild(const GameSimulation& sim, PlayerId aiOwner, const AiBlackboard& bb, bool omniscient)
    {
        const auto& vis = sim.playerVisibility.at(aiOwner.value);
        auto width = vis.explored.getWidth();
        auto height = vis.explored.getHeight();
        if (width != antiGround.getWidth() || height != antiGround.getHeight())
        {
            antiGround = Grid<float>(width, height, 0.0f);
            antiAirCover = Grid<float>(width, height, 0.0f);
            economic = Grid<float>(width, height, 0.0f);
            staleness = Grid<float>(width, height, 100000.0f);
            economicCellIndices.clear();
            antiGroundCellIndices.clear();
            antiAirCoverCellIndices.clear();
        }
        origin = sim.terrain.heightmapIndexToWorldCorner(0, 0);
        cellSize = MapTerrain::HeightTileWidthInWorldUnits.value * static_cast<float>(PlayerVisibility::VisionCellSizeInTiles);

        // Threat and value are recomputed from scratch; staleness ages.
        // Straight over the backing vectors: this is every cell on the map,
        // and Grid::get/set pays for an index calculation and a bounds check
        // each time.
        auto& antiGroundCells = antiGround.getVector();
        auto& antiAirCoverCells = antiAirCover.getVector();
        auto& economicCells = economic.getVector();
        auto& stalenessCells = staleness.getVector();
        const auto& visibleCells = vis.visible.getVector();
        // Threat and value only ever land on cells near a known enemy, and
        // the last pass wrote down which those were, so wiping just those is
        // far cheaper than blanking the whole map twice.
        for (auto index : antiGroundCellIndices)
        {
            antiGroundCells[index] = 0.0f;
        }
        for (auto index : antiAirCoverCellIndices)
        {
            antiAirCoverCells[index] = 0.0f;
        }
        for (auto index : economicCellIndices)
        {
            economicCells[index] = 0.0f;
        }
        antiGroundCellIndices.clear();
        antiAirCoverCellIndices.clear();
        economicCellIndices.clear();
        if (omniscient)
        {
            std::fill(stalenessCells.begin(), stalenessCells.end(), 0.0f);
        }
        else
        {
            for (std::size_t i = 0; i < stalenessCells.size(); ++i)
            {
                stalenessCells[i] = visibleCells[i] != 0 ? 0.0f : stalenessCells[i] + 1.0f;
            }
        }

        for (const auto& [_, enemy] : bb.knownEnemies)
        {
            auto defIt = sim.unitDefinitions.find(enemy.unitType);
            if (defIt == sim.unitDefinitions.end())
            {
                continue;
            }
            const auto& def = defIt->second;
            auto cell = cellAt(enemy.lastKnownPosition);

            if (enemy.isBuilding && cell.x >= 0 && cell.y >= 0 && cell.x < width && cell.y < height)
            {
                auto index = static_cast<std::size_t>((cell.y * width) + cell.x);
                if (economicCells[index] == 0.0f)
                {
                    economicCellIndices.push_back(index);
                }
                economicCells[index] += def.buildCostMetal.value;
            }

            // Each unit that can reach an aircraft puts a 1 on every cell it
            // covers, so the layer reads as "how many things will shoot at a
            // bomber over here".
            auto aaRange = antiAirRange(sim, def);
            if (aaRange > 0.0f)
            {
                auto aaRadiusCells = static_cast<int>(std::ceil(aaRange / cellSize));
                auto aaRadiusSquared = static_cast<float>(aaRadiusCells * aaRadiusCells);
                for (int dy = -aaRadiusCells; dy <= aaRadiusCells; ++dy)
                {
                    for (int dx = -aaRadiusCells; dx <= aaRadiusCells; ++dx)
                    {
                        auto x = cell.x + dx;
                        auto y = cell.y + dy;
                        if (x < 0 || y < 0 || x >= width || y >= height)
                        {
                            continue;
                        }
                        if (static_cast<float>((dx * dx) + (dy * dy)) > aaRadiusSquared)
                        {
                            continue;
                        }
                        auto index = static_cast<std::size_t>((y * width) + x);
                        if (antiAirCoverCells[index] == 0.0f)
                        {
                            antiAirCoverCellIndices.push_back(index);
                        }
                        antiAirCoverCells[index] += 1.0f;
                    }
                }
            }

            auto dps = estimateDps(sim, def);
            if (dps <= 0.0f)
            {
                continue;
            }
            auto range = weaponRange(sim, def);
            auto radiusCells = static_cast<int>(std::ceil(range / cellSize));
            auto radiusSquared = static_cast<float>(radiusCells * radiusCells);
            for (int dy = -radiusCells; dy <= radiusCells; ++dy)
            {
                for (int dx = -radiusCells; dx <= radiusCells; ++dx)
                {
                    auto x = cell.x + dx;
                    auto y = cell.y + dy;
                    if (x < 0 || y < 0 || x >= width || y >= height)
                    {
                        continue;
                    }
                    if (static_cast<float>((dx * dx) + (dy * dy)) > radiusSquared)
                    {
                        continue;
                    }
                    auto index = static_cast<std::size_t>((y * width) + x);
                    if (antiGroundCells[index] == 0.0f)
                    {
                        antiGroundCellIndices.push_back(index);
                    }
                    antiGroundCells[index] += dps;
                }
            }
        }

        // Known enemies come in unit id order, so put the cells they landed
        // in back into map scan order: that is the order the old full sweep
        // saw them in, and it decides ties in bestAttackTarget.
        std::sort(economicCellIndices.begin(), economicCellIndices.end());
    }

    float ThreatMap::antiGroundAt(const SimVector& position) const
    {
        auto c = cellAt(position);
        if (c.x < 0 || c.y < 0 || c.x >= getWidth() || c.y >= getHeight())
        {
            return 0.0f;
        }
        return antiGround.get(c.x, c.y);
    }

    float ThreatMap::antiAirCoverAt(const SimVector& position) const
    {
        auto c = cellAt(position);
        if (c.x < 0 || c.y < 0 || c.x >= getWidth() || c.y >= getHeight())
        {
            return 0.0f;
        }
        return antiAirCover.get(c.x, c.y);
    }

    float ThreatMap::economicAt(const SimVector& position) const
    {
        auto c = cellAt(position);
        if (c.x < 0 || c.y < 0 || c.x >= getWidth() || c.y >= getHeight())
        {
            return 0.0f;
        }
        return economic.get(c.x, c.y);
    }

    float ThreatMap::antiGroundInRadius(const SimVector& position, float radiusWorldUnits) const
    {
        auto c = cellAt(position);
        auto r = static_cast<int>(std::ceil(radiusWorldUnits / cellSize));
        float total = 0.0f;
        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                auto x = c.x + dx;
                auto y = c.y + dy;
                if (x < 0 || y < 0 || x >= getWidth() || y >= getHeight())
                {
                    continue;
                }
                total += antiGround.get(x, y);
            }
        }
        return total;
    }

    std::optional<SimVector> ThreatMap::bestScoutTarget(const SimVector& from) const
    {
        return bestScoutTarget(from, [](int, int, const SimVector&) { return true; });
    }

    std::optional<SimVector> ThreatMap::bestAttackTarget(float threatAversion) const
    {
        // Only cells holding an enemy building can be a target, and the
        // rebuild pass already noted which those are, so there is no need to
        // sweep the whole map again. The list is in the same scan order the
        // sweep used, so the pick is unchanged.
        std::optional<SimVector> best;
        float bestScore = 0.0f;
        auto width = getWidth();
        const auto& economicCells = economic.getVector();
        const auto& antiGroundCells = antiGround.getVector();
        for (auto index : economicCellIndices)
        {
            auto value = economicCells[index];
            if (value <= 0.0f)
            {
                continue;
            }
            auto score = value - (antiGroundCells[index] * threatAversion);
            if (!best || score > bestScore)
            {
                bestScore = score;
                best = cellCenter(index % width, index / width);
            }
        }
        return best;
    }
}
