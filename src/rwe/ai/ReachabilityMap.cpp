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
        rebuildLayer(ground, sim, mover, from);
    }

    void ReachabilityMap::rebuildNaval(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from)
    {
        rebuildLayer(naval, sim, mover, from);
    }

    void ReachabilityMap::rebuildCommander(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from)
    {
        rebuildLayer(commander, sim, mover, from);
    }

    void ReachabilityMap::rebuildHover(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from)
    {
        rebuildLayer(hover, sim, mover, from);
    }

    void ReachabilityMap::rebuildLayer(Layer& layer, const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from) const
    {
        auto mc = sim.getAdHocMovementClass(mover);
        if (!layer.labelledFor || !sameMovementClass(*layer.labelledFor, mc))
        {
            labelComponents(layer, sim, mc);
            layer.labelledFor = mc;
        }
        setAnchor(layer, sim, from);
    }

    void ReachabilityMap::labelComponents(Layer& layer, const GameSimulation& sim, const MovementClassDefinition& mc) const
    {
        const auto& heights = sim.terrain.getHeightMap();
        // A footprint's top-left tile must leave room for the whole footprint.
        //
        // This looks like it is one short -- top-left positions run to
        // getWidth() - footprintX inclusive, so a grid of that width drops
        // the last one -- and it is not. isGridPointWalkable runs
        // isMaxSlopeGreaterThan, which calls getSlope on every cell of the
        // footprint, and getSlope reads past the cell it is handed. Asking
        // about the very last footprint therefore walks off the heightmap and
        // trips Grid's own bounds assertion. Tried, on 2026-09-17: widening
        // these two by one aborted rwe_test outright.
        //
        // MapIntel's naval site scan does use an inclusive bound for the same
        // "leave room for the footprint" problem, which is not a
        // contradiction: it calls isWaterDepthWithinBounds alone, and that
        // reads strictly inside the footprint.
        int width = heights.getWidth() - static_cast<int>(mc.footprintX);
        int height = heights.getHeight() - static_cast<int>(mc.footprintZ);
        layer.walkableTiles = 0;
        layer.reachableTiles = 0;
        layer.componentSizes.clear();
        layer.homeComponents.clear();
        layer.anchorTile.reset();
        if (width <= 0 || height <= 0)
        {
            layer.components = Grid<int>();
            return;
        }

        layer.components = Grid<int>(width, height, 0);
        // The grid is written cell by cell in scan order, so go through the
        // backing vector directly rather than paying for a bounds check per tile.
        auto& cells = layer.components.getVector();
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (isGridPointWalkable(sim.terrain, mc, static_cast<unsigned int>(x), static_cast<unsigned int>(y)))
                {
                    // -1 marks "walkable but not yet assigned to a region".
                    cells[(y * width) + x] = -1;
                    ++layer.walkableTiles;
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
                auto id = static_cast<int>(layer.componentSizes.size()) + 1;
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
                layer.componentSizes.push_back(size);
            }
        }
    }

    void ReachabilityMap::setAnchor(Layer& layer, const GameSimulation& sim, const SimVector& from) const
    {
        layer.homeComponents.clear();
        layer.anchorTile.reset();
        layer.reachableTiles = 0;
        if (layer.components.getWidth() <= 0)
        {
            return;
        }

        auto width = layer.components.getWidth();
        auto height = layer.components.getHeight();
        // Deliberately NOT footprintOriginTile, though every query below uses
        // it. Anchors do not all arrive in the convention queries do: the
        // ground layer is homed on baseAnchor, which is a unit's CENTRE, but
        // the naval layer is homed on a shipyard site's TOP-LEFT CORNER --
        // AiPlayerController builds it with heightmapIndexToWorldCorner for
        // exactly the reason this whole file cares about, that components are
        // labelled by footprint top-left.
        //
        // Subtracting half a footprint here would therefore shift that corner
        // a SECOND time: three whole tiles for a 6x6 hull. Tried, 2026-09-17 --
        // it walked the naval anchor off the channel onto dry land, setAnchor
        // found nothing there and nothing in its four neighbours, homeComponents
        // came back empty, and the entire map became unreachable to the navy.
        // It failed "the whole channel becomes home water for a hull" and took
        // both sea-ferry tests down with it, navalLandingNear refusing every
        // crossing once the naval layer had no home to probe towards.
        auto start = sim.terrain.worldToHeightmapCoordinate(from);
        if (start.x < 0 || start.y < 0 || start.x >= width || start.y >= height)
        {
            return;
        }
        layer.anchorTile = start;

        auto claim = [&](int id) {
            if (id != 0 && std::find(layer.homeComponents.begin(), layer.homeComponents.end(), id) == layer.homeComponents.end())
            {
                layer.homeComponents.push_back(id);
                layer.reachableTiles += layer.componentSizes[static_cast<std::size_t>(id) - 1];
            }
        };

        auto startId = layer.components.get(start.x, start.y);
        if (startId != 0)
        {
            claim(startId);
            return;
        }

        // The base itself stands on the tile even if the exact tile is not
        // walkable (buildings sit on flat ground, but be lenient): count it,
        // and treat whatever it touches as home.
        layer.reachableTiles = 1;
        const Point neighbours[4] = {Point(start.x + 1, start.y), Point(start.x - 1, start.y), Point(start.x, start.y + 1), Point(start.x, start.y - 1)};
        for (const auto& n : neighbours)
        {
            if (n.x < 0 || n.y < 0 || n.x >= width || n.y >= height)
            {
                continue;
            }
            claim(layer.components.get(n.x, n.y));
        }
    }

    bool ReachabilityMap::isReachable(const GameSimulation& sim, const SimVector& position) const
    {
        return isReachable(ground, sim, position);
    }

    bool ReachabilityMap::isWalkable(const GameSimulation& sim, const SimVector& position) const
    {
        return isWalkable(ground, sim, position);
    }

    bool ReachabilityMap::isNavalReachable(const GameSimulation& sim, const SimVector& position) const
    {
        return isReachable(naval, sim, position);
    }

    bool ReachabilityMap::isNavalWalkable(const GameSimulation& sim, const SimVector& position) const
    {
        return isWalkable(naval, sim, position);
    }

    bool ReachabilityMap::isCommanderReachable(const GameSimulation& sim, const SimVector& position) const
    {
        return isReachable(commander, sim, position);
    }

    bool ReachabilityMap::isHoverReachable(const GameSimulation& sim, const SimVector& position) const
    {
        return isReachable(hover, sim, position);
    }

    bool ReachabilityMap::isCommanderWalkable(const GameSimulation& sim, const SimVector& position) const
    {
        return isWalkable(commander, sim, position);
    }

    Point ReachabilityMap::footprintOriginTile(const Layer& layer, const GameSimulation& sim, const SimVector& position) const
    {
        // Before the layer has been labelled there is no footprint to offset
        // by, and the centre's own tile is the only answer available.
        if (!layer.labelledFor)
        {
            return sim.terrain.worldToHeightmapCoordinate(position);
        }
        auto halfFootprintX = SimScalar(layer.labelledFor->footprintX * MapTerrain::HeightTileWidthInWorldUnits.value / 2);
        auto halfFootprintZ = SimScalar(layer.labelledFor->footprintZ * MapTerrain::HeightTileHeightInWorldUnits.value / 2);
        return sim.terrain.worldToHeightmapCoordinateNearest(
            SimVector(position.x - halfFootprintX, position.y, position.z - halfFootprintZ));
    }

    bool ReachabilityMap::isReachable(const Layer& layer, const GameSimulation& sim, const SimVector& position) const
    {
        if (layer.components.getWidth() <= 0)
        {
            return true;
        }
        auto p = footprintOriginTile(layer, sim, position);
        if (p.x < 0 || p.y < 0 || p.x >= layer.components.getWidth() || p.y >= layer.components.getHeight())
        {
            return false;
        }
        if (layer.anchorTile && p.x == layer.anchorTile->x && p.y == layer.anchorTile->y)
        {
            return true;
        }
        auto id = layer.components.get(p.x, p.y);
        return id != 0 && std::find(layer.homeComponents.begin(), layer.homeComponents.end(), id) != layer.homeComponents.end();
    }

    bool ReachabilityMap::isWalkable(const Layer& layer, const GameSimulation& sim, const SimVector& position) const
    {
        if (layer.components.getWidth() <= 0)
        {
            return true;
        }
        auto p = footprintOriginTile(layer, sim, position);
        if (p.x < 0 || p.y < 0 || p.x >= layer.components.getWidth() || p.y >= layer.components.getHeight())
        {
            return false;
        }
        return layer.components.get(p.x, p.y) != 0;
    }
}
