#include "featureplacement.h"

#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/util/Index.h>

namespace rwe
{
    FeaturePlacementResult resolveFeatureOverlaps(
        const std::vector<std::pair<Point, std::string>>& features,
        int cellsWide,
        int cellsHigh,
        const std::function<FeaturePlacementInfo(const std::string&)>& infoFor)
    {
        FeaturePlacementResult result;
        if (cellsWide <= 0 || cellsHigh <= 0)
        {
            result.dropped = static_cast<int>(features.size());
            return result;
        }

        // Which feature, by index into `features`, last claimed each cell.
        // A claim outlives the claimant -- a feature that is later replaced
        // or dropped leaves its index behind -- so `alive` is what says
        // whether a claim still counts.
        Grid<std::optional<Index>> owner(cellsWide, cellsHigh, std::nullopt);
        std::vector<bool> alive(features.size(), false);
        std::vector<FeaturePlacementInfo> infos;
        infos.reserve(features.size());

        for (Index i = 0; i < getSize(features); ++i)
        {
            const auto& [position, name] = features[i];
            infos.push_back(infoFor(name));
            const auto& info = infos.back();

            // The whole footprint has to fit before any cell is looked at.
            if (position.x < 0 || position.y < 0
                || info.footprintX <= 0 || info.footprintZ <= 0
                || position.x + info.footprintX > cellsWide
                || position.y + info.footprintZ > cellsHigh)
            {
                ++result.dropped;
                continue;
            }

            alive[i] = true;
            for (int dz = 0; dz < info.footprintZ && alive[i]; ++dz)
            {
                for (int dx = 0; dx < info.footprintX; ++dx)
                {
                    auto& cell = owner.get(position.x + dx, position.y + dz);
                    if (cell && *cell != i && alive[*cell])
                    {
                        if (infos[*cell].indestructible)
                        {
                            alive[i] = false;
                            ++result.dropped;
                            break;
                        }
                        alive[*cell] = false;
                        ++result.replaced;
                    }
                    cell = i;
                }
            }
        }

        for (Index i = 0; i < getSize(features); ++i)
        {
            if (alive[i])
            {
                result.placed.push_back(features[i]);
            }
        }
        return result;
    }
}
