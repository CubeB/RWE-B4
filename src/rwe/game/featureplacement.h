#pragma once

#include <functional>
#include <rwe/grid/Point.h>
#include <string>
#include <utility>
#include <vector>

namespace rwe
{
    /** What the placement pass needs to know about a feature type. */
    struct FeaturePlacementInfo
    {
        int footprintX;
        int footprintZ;
        bool indestructible;
    };

    struct FeaturePlacementResult
    {
        /** The features that stand once every overlap is settled, in their original order. */
        std::vector<std::pair<Point, std::string>> placed;
        /** Features that lost their ground to a later one. */
        int replaced{0};
        /** Features that could not be placed: over an indestructible one, or off the map. */
        int dropped{0};
    };

    /**
     * Settles overlapping map features the way the original does when it
     * populates a map, so that a map with two features drawn on one cell
     * ends up with the same one TA shows.
     *
     * The features come in the order the original reads them: the TNT grid
     * row by row, then the OTA schema's numbered list. Each is walked cell by
     * cell across its footprint. A footprint that leaves the map is skipped
     * before it claims anything. A cell already owned by a standing feature
     * decides the fight: if that feature is indestructible the newcomer is
     * dropped on the spot, and if it is not, it is the older feature that
     * goes and the newcomer keeps claiming. A newcomer dropped part way
     * through does not give back the destructible features it has already
     * displaced; that is the original's behaviour as decoded, and it is kept.
     *
     * The grid is the map's cell grid, one smaller than the TNT attribute
     * grid in each direction, which is also why a feature drawn on the last
     * attribute row or column never appears in the original.
     */
    FeaturePlacementResult resolveFeatureOverlaps(
        const std::vector<std::pair<Point, std::string>>& features,
        int cellsWide,
        int cellsHigh,
        const std::function<FeaturePlacementInfo(const std::string&)>& infoFor);
}
