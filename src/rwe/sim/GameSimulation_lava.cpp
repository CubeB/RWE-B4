#include "GameSimulation.h"
#include <algorithm>

namespace rwe
{
    void GameSimulation::initializeLavaExclusions(bool lavaWorld)
    {
        // BT-264/265: prepare once after map features, before pass maps.
        // See docs/agents/lava-movement.md for reference and lifetime evidence.
        std::vector<unsigned int> indices;
        const auto& heights = terrain.getHeightMap();
        const auto& mask = terrain.getLavaExclusions();
        const auto sea = simScalarToUInt(terrain.getSeaLevel());
        if (lavaWorld)
        {
            for (int y = 0; y < mask.getHeight(); ++y)
            {
                for (int x = 0; x < mask.getWidth(); ++x)
                {
                    auto low = std::min({heights.get(x, y), heights.get(x + 1, y),
                        heights.get(x, y + 1), heights.get(x + 1, y + 1)});
                    if (low > sea)
                    {
                        continue;
                    }
                    if (auto id = occupiedGrid.get(x, y).featureId)
                    {
                        const auto& feature = getFeature(*id);
                        const auto& def = getFeatureDefinition(feature.featureName);
                        const auto rect = computeFootprintRegion(feature.position, def.footprintX, def.footprintZ);
                        if (rect.left() == x && rect.top() == y)
                        {
                            continue;
                        }
                    }
                    indices.push_back(y * mask.getWidth() + x);
                }
            }
        }
        terrain.setLavaExclusions(indices);
    }
}
