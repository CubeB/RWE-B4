#include "MapIntel.h"

#include <rwe/util/Index.h>

namespace rwe
{
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
        intel.valid = true;
        return intel;
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
