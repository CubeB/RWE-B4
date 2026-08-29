#include "ThreatMap.h"
#include <algorithm>
#include <cmath>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/WeaponDefinition.h>

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
                auto it = sim.weaponDefinitions.find(weaponName);
                if (it == sim.weaponDefinitions.end())
                {
                    continue;
                }
                const auto& w = it->second;
                auto defaultDamage = w.damage.find("default");
                float damage = defaultDamage == w.damage.end() ? 0.0f : static_cast<float>(defaultDamage->second);
                float reload = std::max(0.1f, w.reloadTime.value);
                total += damage * static_cast<float>(std::max(1, w.burst)) / reload;
            }
            return total;
        }

        float weaponRange(const GameSimulation& sim, const UnitDefinition& def)
        {
            float best = 0.0f;
            for (const auto& weaponName : {def.weapon1, def.weapon2, def.weapon3})
            {
                auto it = weaponName.empty() ? sim.weaponDefinitions.end() : sim.weaponDefinitions.find(weaponName);
                if (it != sim.weaponDefinitions.end())
                {
                    best = std::max(best, it->second.maxRange.value);
                }
            }
            return best;
        }
    }

    ThreatMap::ThreatMap(int width, int height)
        : antiGround(width, height, 0.0f),
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
            economic = Grid<float>(width, height, 0.0f);
            staleness = Grid<float>(width, height, 100000.0f);
        }
        origin = sim.terrain.heightmapIndexToWorldCorner(0, 0);
        cellSize = MapTerrain::HeightTileWidthInWorldUnits.value * static_cast<float>(PlayerVisibility::VisionCellSizeInTiles);

        // Threat and value are recomputed from scratch; staleness ages.
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                antiGround.set(x, y, 0.0f);
                economic.set(x, y, 0.0f);
                auto seen = omniscient || vis.visible.get(x, y) != 0;
                staleness.set(x, y, seen ? 0.0f : staleness.get(x, y) + 1.0f);
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
                economic.set(cell.x, cell.y, economic.get(cell.x, cell.y) + def.buildCostMetal.value);
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
                    antiGround.set(x, y, antiGround.get(x, y) + dps);
                }
            }
        }
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

    std::optional<SimVector> ThreatMap::bestScoutTarget(const SimVector& from, const std::function<bool(int, int, const SimVector&)>& accept) const
    {
        std::optional<SimVector> best;
        float bestScore = -1.0f;
        for (int y = 0; y < getHeight(); ++y)
        {
            for (int x = 0; x < getWidth(); ++x)
            {
                auto stale = staleness.get(x, y);
                if (stale <= 0.0f)
                {
                    continue;
                }
                auto center = cellCenter(x, y);
                if (!accept(x, y, center))
                {
                    continue;
                }
                auto distance = std::max(1.0f, (center - from).length().value);
                // Prefer ground that has gone unseen for a long time, but not at any distance.
                auto score = std::min(stale, 3000.0f) / distance;
                if (score > bestScore)
                {
                    bestScore = score;
                    best = center;
                }
            }
        }
        return best;
    }

    std::optional<SimVector> ThreatMap::bestAttackTarget(float threatAversion) const
    {
        std::optional<SimVector> best;
        float bestScore = 0.0f;
        for (int y = 0; y < getHeight(); ++y)
        {
            for (int x = 0; x < getWidth(); ++x)
            {
                auto value = economic.get(x, y);
                if (value <= 0.0f)
                {
                    continue;
                }
                auto score = value - (antiGround.get(x, y) * threatAversion);
                if (!best || score > bestScore)
                {
                    bestScore = score;
                    best = cellCenter(x, y);
                }
            }
        }
        return best;
    }
}
