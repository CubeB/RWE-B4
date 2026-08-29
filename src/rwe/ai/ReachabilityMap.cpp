#include "ReachabilityMap.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/movement.h>
#include <vector>

namespace rwe
{
    void ReachabilityMap::rebuild(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from)
    {
        auto mc = sim.getAdHocMovementClass(mover);
        const auto& heights = sim.terrain.getHeightMap();
        // A footprint's top-left tile must leave room for the whole footprint.
        int width = heights.getWidth() - static_cast<int>(mc.footprintX);
        int height = heights.getHeight() - static_cast<int>(mc.footprintZ);
        if (width <= 0 || height <= 0)
        {
            walkable = Grid<unsigned char>();
            reachable = Grid<unsigned char>();
            return;
        }

        walkable = Grid<unsigned char>(width, height, static_cast<unsigned char>(0));
        reachable = Grid<unsigned char>(width, height, static_cast<unsigned char>(0));
        walkableTiles = 0;
        reachableTiles = 0;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (isGridPointWalkable(sim.terrain, mc, static_cast<unsigned int>(x), static_cast<unsigned int>(y)))
                {
                    walkable.set(x, y, 1);
                    ++walkableTiles;
                }
            }
        }

        // Flood out from the base over walkable tiles.
        auto start = sim.terrain.worldToHeightmapCoordinate(from);
        if (start.x < 0 || start.y < 0 || start.x >= width || start.y >= height)
        {
            return;
        }
        // The base itself stands on the tile even if the exact tile is not walkable (buildings sit on flat ground, but be lenient).
        std::vector<Point> open;
        open.push_back(start);
        reachable.set(start.x, start.y, 1);
        ++reachableTiles;
        while (!open.empty())
        {
            auto p = open.back();
            open.pop_back();
            const Point neighbours[4] = {Point(p.x + 1, p.y), Point(p.x - 1, p.y), Point(p.x, p.y + 1), Point(p.x, p.y - 1)};
            for (const auto& n : neighbours)
            {
                if (n.x < 0 || n.y < 0 || n.x >= width || n.y >= height)
                {
                    continue;
                }
                if (reachable.get(n.x, n.y) || !walkable.get(n.x, n.y))
                {
                    continue;
                }
                reachable.set(n.x, n.y, 1);
                ++reachableTiles;
                open.push_back(n);
            }
        }
    }

    bool ReachabilityMap::isReachable(const GameSimulation& sim, const SimVector& position) const
    {
        if (!isValid())
        {
            return true;
        }
        auto p = sim.terrain.worldToHeightmapCoordinate(position);
        if (p.x < 0 || p.y < 0 || p.x >= reachable.getWidth() || p.y >= reachable.getHeight())
        {
            return false;
        }
        return reachable.get(p.x, p.y) != 0;
    }

    bool ReachabilityMap::isWalkable(const GameSimulation& sim, const SimVector& position) const
    {
        if (!isValid())
        {
            return true;
        }
        auto p = sim.terrain.worldToHeightmapCoordinate(position);
        if (p.x < 0 || p.y < 0 || p.x >= walkable.getWidth() || p.y >= walkable.getHeight())
        {
            return false;
        }
        return walkable.get(p.x, p.y) != 0;
    }
}
