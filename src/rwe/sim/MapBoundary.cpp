#include "MapBoundary.h"
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    void initializeMapBoundaryExclusions(GameSimulation& simulation)
    {
        auto& terrain = simulation.terrain;
        const auto& heights = terrain.getHeightMap();
        const auto& mask = terrain.getLavaExclusions();
        // The legacy name also holds border/void exclusions: the original
        // uses the same persistent blocker for lava and reserved borders.
        // Keeping one mask also preserves its save/hash/dump and collision paths.
        auto indices = terrain.lavaExclusionIndices();
        auto mark = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= mask.getWidth() || y >= mask.getHeight())
            {
                return;
            }
            if (auto id = simulation.occupiedGrid.get(x, y).featureId)
            {
                const auto& feature = simulation.getFeature(*id);
                const auto& definition = simulation.getFeatureDefinition(feature.featureName);
                auto footprint = simulation.computeFootprintRegion(feature.position, definition.footprintX, definition.footprintZ);
                if (footprint.x == x && footprint.y == y)
                {
                    return; // A real feature origin survives; its continuations do not.
                }
            }
            indices.push_back(static_cast<unsigned int>(y * mask.getWidth() + x));
        };

        for (int x = 0; x < heights.getWidth(); ++x)
        {
            if (x >= heights.getWidth() - 2)
            {
                for (int y = 0; y < heights.getHeight(); ++y)
                {
                    mark(x, y);
                }
            }
            for (int y = 0; y < heights.getHeight() && y * 16 - heights.get(x, y) / 2 < 0; ++y)
            {
                mark(x, y);
            }
            const auto bottom = heights.getHeight() * 16 - 128;
            for (int y = heights.getHeight() - 1; y >= 0 && y * 16 - heights.get(x, y) / 2 > bottom; --y)
            {
                mark(x, y - 1); // The reference marks the preceding row, not the sample.
            }
        }
        terrain.setLavaExclusions(indices);
    }
}
