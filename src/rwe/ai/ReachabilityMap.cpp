#include "ReachabilityMap.h"
#include <algorithm>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/movement.h>
#include <vector>

namespace rwe
{
    namespace
    {
        bool sameMovementClass(const MovementClassDefinition& a, const MovementClassDefinition& b)
        {
            return a.footprintX == b.footprintX
                && a.footprintZ == b.footprintZ
                && a.minWaterDepth == b.minWaterDepth
                && a.maxWaterDepth == b.maxWaterDepth
                && a.maxSlope == b.maxSlope
                && a.maxWaterSlope == b.maxWaterSlope;
        }
    }

    void ReachabilityMap::rebuild(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from)
    {
        auto mc = sim.getAdHocMovementClass(mover);
        if (!labelledFor || !sameMovementClass(*labelledFor, mc))
        {
            labelComponents(sim, mc);
            labelledFor = mc;
        }
        setAnchor(sim, from);
    }

    void ReachabilityMap::labelComponents(const GameSimulation& sim, const MovementClassDefinition& mc)
    {
        const auto& heights = sim.terrain.getHeightMap();
        // A footprint's top-left tile must leave room for the whole footprint.
        int width = heights.getWidth() - static_cast<int>(mc.footprintX);
        int height = heights.getHeight() - static_cast<int>(mc.footprintZ);
        walkableTiles = 0;
        reachableTiles = 0;
        componentSizes.clear();
        homeComponents.clear();
        anchorTile.reset();
        if (width <= 0 || height <= 0)
        {
            components = Grid<int>();
            return;
        }

        components = Grid<int>(width, height, 0);
        // The grid is written cell by cell in scan order, so go through the
        // backing vector directly rather than paying for a bounds check per tile.
        auto& cells = components.getVector();
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (isGridPointWalkable(sim.terrain, mc, static_cast<unsigned int>(x), static_cast<unsigned int>(y)))
                {
                    // -1 marks "walkable but not yet assigned to a region".
                    cells[(y * width) + x] = -1;
                    ++walkableTiles;
                }
            }
        }

        // Flood each walkable region in turn, in scan order, so the ids
        // depend only on the terrain.
        std::vector<int> open;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto index = (y * width) + x;
                if (cells[index] != -1)
                {
                    continue;
                }
                auto id = static_cast<int>(componentSizes.size()) + 1;
                int size = 0;
                open.clear();
                open.push_back(index);
                cells[index] = id;
                while (!open.empty())
                {
                    auto p = open.back();
                    open.pop_back();
                    ++size;
                    auto px = p % width;
                    auto py = p / width;
                    const int neighbours[4] = {
                        px + 1 < width ? p + 1 : -1,
                        px > 0 ? p - 1 : -1,
                        py + 1 < height ? p + width : -1,
                        py > 0 ? p - width : -1};
                    for (auto n : neighbours)
                    {
                        if (n < 0 || cells[n] != -1)
                        {
                            continue;
                        }
                        cells[n] = id;
                        open.push_back(n);
                    }
                }
                componentSizes.push_back(size);
            }
        }
    }

    void ReachabilityMap::setAnchor(const GameSimulation& sim, const SimVector& from)
    {
        homeComponents.clear();
        anchorTile.reset();
        reachableTiles = 0;
        if (!isValid())
        {
            return;
        }

        auto width = components.getWidth();
        auto height = components.getHeight();
        auto start = sim.terrain.worldToHeightmapCoordinate(from);
        if (start.x < 0 || start.y < 0 || start.x >= width || start.y >= height)
        {
            return;
        }
        anchorTile = start;

        auto claim = [&](int id) {
            if (id != 0 && std::find(homeComponents.begin(), homeComponents.end(), id) == homeComponents.end())
            {
                homeComponents.push_back(id);
                reachableTiles += componentSizes[static_cast<std::size_t>(id) - 1];
            }
        };

        auto startId = components.get(start.x, start.y);
        if (startId != 0)
        {
            claim(startId);
            return;
        }

        // The base itself stands on the tile even if the exact tile is not
        // walkable (buildings sit on flat ground, but be lenient): count it,
        // and treat whatever it touches as home.
        reachableTiles = 1;
        const Point neighbours[4] = {Point(start.x + 1, start.y), Point(start.x - 1, start.y), Point(start.x, start.y + 1), Point(start.x, start.y - 1)};
        for (const auto& n : neighbours)
        {
            if (n.x < 0 || n.y < 0 || n.x >= width || n.y >= height)
            {
                continue;
            }
            claim(components.get(n.x, n.y));
        }
    }

    bool ReachabilityMap::isReachable(const GameSimulation& sim, const SimVector& position) const
    {
        if (!isValid())
        {
            return true;
        }
        auto p = sim.terrain.worldToHeightmapCoordinate(position);
        if (p.x < 0 || p.y < 0 || p.x >= components.getWidth() || p.y >= components.getHeight())
        {
            return false;
        }
        if (anchorTile && p.x == anchorTile->x && p.y == anchorTile->y)
        {
            return true;
        }
        auto id = components.get(p.x, p.y);
        return id != 0 && std::find(homeComponents.begin(), homeComponents.end(), id) != homeComponents.end();
    }

    bool ReachabilityMap::isWalkable(const GameSimulation& sim, const SimVector& position) const
    {
        if (!isValid())
        {
            return true;
        }
        auto p = sim.terrain.worldToHeightmapCoordinate(position);
        if (p.x < 0 || p.y < 0 || p.x >= components.getWidth() || p.y >= components.getHeight())
        {
            return false;
        }
        return components.get(p.x, p.y) != 0;
    }
}
