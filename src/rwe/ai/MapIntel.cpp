#include "MapIntel.h"

#include <rwe/sim/movement.h>
#include <rwe/util/Index.h>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * Floods one connected body of water outward from `startIndex`,
         * stamping every cell it reaches with `id` in `cells` and returning
         * how many it found. Mirrors ReachabilityMap::labelComponents' flood
         * fill exactly -- same scan order, same open-list approach -- so the
         * two give the same answer to "are these connected" wherever they
         * overlap (a footprint of 1x1 and no depth floor). `open` is an
         * out-param purely so the caller's scratch vector is reused across
         * every region instead of being reallocated per region.
         */
        int floodWaterRegion(std::vector<int>& cells, int width, int height, int startIndex, int id, std::vector<int>& open)
        {
            int size = 0;
            open.clear();
            open.push_back(startIndex);
            cells[startIndex] = id;
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
            return size;
        }
    }

    const char* mapCharacterName(MapCharacter character)
    {
        switch (character)
        {
            case MapCharacter::Land:
                return "land";
            case MapCharacter::Water:
                return "water";
            default:
                return "mixed";
        }
    }

    MapIntel analyseMap(const MapTerrain& terrain, std::vector<SimVector> startPositions)
    {
        MapIntel intel;
        intel.startPositions = std::move(startPositions);

        // Heights are stored as bytes that ARE the world height -- MaxHeight
        // is 255 world units -- so the cells can be compared with the sea
        // level directly. Going through getHeightAt would be the same answer
        // through a line/triangle intersection per cell, which on a large map
        // is hundreds of thousands of them for no gain.
        const auto& heights = terrain.getHeightMap();
        auto seaLevel = terrain.getSeaLevel();

        std::size_t total = 0;
        std::size_t below = 0;
        for (std::size_t y = 0; y < heights.getHeight(); ++y)
        {
            for (std::size_t x = 0; x < heights.getWidth(); ++x)
            {
                ++total;
                if (SimScalar(static_cast<float>(heights.get(x, y))) < seaLevel)
                {
                    ++below;
                }
            }
        }

        intel.waterFraction = total == 0 ? 0.0f : static_cast<float>(below) / static_cast<float>(total);
        intel.character = intel.waterFraction >= WaterMapWaterFraction
            ? MapCharacter::Water
            : (intel.waterFraction >= MixedMapWaterFraction ? MapCharacter::Mixed : MapCharacter::Land);

        // Second pass: label connected water, and nominate shipyard sites.
        // Both are read off the same raw heightmap as the pass above, just
        // with different questions asked of each cell, so this stays a
        // second walk of the heightmap rather than anything costlier.
        auto width = static_cast<int>(heights.getWidth());
        auto height = static_cast<int>(heights.getHeight());
        auto seaLevelUInt = simScalarToUInt(seaLevel);

        intel.waterRegions = Grid<int>(width, height, 0);
        auto& waterCells = intel.waterRegions.getVector();
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (SimScalar(static_cast<float>(heights.get(x, y))) < seaLevel)
                {
                    // -1 marks "water but not yet assigned to a region",
                    // exactly as ReachabilityMap::labelComponents marks
                    // "walkable but not yet assigned".
                    waterCells[(y * width) + x] = -1;
                }
            }
        }

        std::vector<int> open;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto index = (y * width) + x;
                if (waterCells[index] != -1)
                {
                    continue;
                }
                auto id = static_cast<int>(intel.waterRegionSizes.size()) + 1;
                auto size = floodWaterRegion(waterCells, width, height, index, id, open);
                intel.waterRegionSizes.push_back(size);
            }
        }

        // Shipyard sites: every top-left tile whose 8x8 footprint clears
        // MinWaterDepth=30 on the raw heightmap. isWaterDepthWithinBounds is
        // the exact function GameSimulation::canBeBuiltAt reaches by way of
        // isGridPointWalkable, so this asks the real question -- just without
        // the collision/yard-map checks that need the live simulation state
        // MapIntel is not allowed to depend on.
        if (width >= static_cast<int>(NavalShipyardFootprintX) && height >= static_cast<int>(NavalShipyardFootprintZ))
        {
            int maxX = width - static_cast<int>(NavalShipyardFootprintX);
            int maxY = height - static_cast<int>(NavalShipyardFootprintZ);
            for (int y = 0; y <= maxY; ++y)
            {
                for (int x = 0; x <= maxX; ++x)
                {
                    if (!isWaterDepthWithinBounds(heights, seaLevelUInt, static_cast<unsigned int>(x), static_cast<unsigned int>(y), NavalShipyardFootprintX, NavalShipyardFootprintZ, NavalShipyardMinWaterDepth, 255u))
                    {
                        continue;
                    }

                    NavalSite site;
                    site.tile = Point(x, y);
                    auto corner = terrain.heightmapIndexToWorldCorner(x, y);
                    site.position = SimVector(
                        corner.x + (SimScalar(static_cast<float>(NavalShipyardFootprintX)) * MapTerrain::HeightTileWidthInWorldUnits / 2_ss),
                        seaLevel,
                        corner.z + (SimScalar(static_cast<float>(NavalShipyardFootprintZ)) * MapTerrain::HeightTileHeightInWorldUnits / 2_ss));
                    auto centreX = x + static_cast<int>(NavalShipyardFootprintX) / 2;
                    auto centreY = y + static_cast<int>(NavalShipyardFootprintZ) / 2;
                    site.waterRegion = intel.waterRegions.get(centreX, centreY);
                    intel.shipyardSites.push_back(site);
                }
            }
        }

        intel.valid = true;
        return intel;
    }

    int waterRegionAt(const MapIntel& intel, const MapTerrain& terrain, const SimVector& position)
    {
        if (intel.waterRegions.getWidth() <= 0 || intel.waterRegions.getHeight() <= 0)
        {
            return 0;
        }
        auto p = terrain.worldToHeightmapCoordinate(position);
        if (p.x < 0 || p.y < 0 || p.x >= intel.waterRegions.getWidth() || p.y >= intel.waterRegions.getHeight())
        {
            return 0;
        }
        return intel.waterRegions.get(p.x, p.y);
    }

    bool sameWaterBody(const MapIntel& intel, const MapTerrain& terrain, const SimVector& a, const SimVector& b)
    {
        auto idA = waterRegionAt(intel, terrain, a);
        return idA != 0 && idA == waterRegionAt(intel, terrain, b);
    }

    std::optional<Index> nearestStartPosition(const MapIntel& intel, const SimVector& position)
    {
        std::optional<Index> best;
        SimScalar bestDistance(0);
        for (Index i = 0; i < static_cast<Index>(intel.startPositions.size()); ++i)
        {
            // Compared in the horizontal plane: a start position's stored
            // height is the ground under it, and a commander that has walked
            // a little is not at that height any more.
            auto dx = intel.startPositions[i].x - position.x;
            auto dz = intel.startPositions[i].z - position.z;
            auto distance = (dx * dx) + (dz * dz);
            if (!best || distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        return best;
    }
}
