#include "PlayerVisibility.h"
#include <algorithm>
#include <cmath>
#include <rwe/sim/MapTerrain.h>

namespace rwe
{
    namespace
    {
        int floorToInt(SimScalar s)
        {
            return static_cast<int>(std::floor(s.value));
        }

        int clampToByte(int value)
        {
            return std::clamp(value, 0, 255);
        }

        /**
         * The four ninety degree rotations of a quadrant offset. Rotating
         * rather than mirroring is what makes the authored fans tile the
         * whole circle: TA's rays run from the +y axis round to one cell
         * short of the +x axis, so the next rotation's first ray lands
         * exactly on the axis the previous one stopped short of.
         */
        Point rotateIntoQuadrant(const Point& p, int quadrant)
        {
            switch (quadrant)
            {
                case 0:
                    return Point(p.x, p.y);
                case 1:
                    return Point(p.y, -p.x);
                case 2:
                    return Point(-p.x, -p.y);
                default:
                    return Point(-p.y, p.x);
            }
        }
    }

    Point heightmapToVisionCell(SimScalar tileX, SimScalar tileZ, int terrainHeight)
    {
        // The cabinet skew is half the terrain height in world units; divide
        // by the tile width to express it in heightmap tiles.
        auto skewInTiles = intToSimScalar(terrainHeight) / (2_ss * MapTerrain::HeightTileHeightInWorldUnits);
        auto cells = intToSimScalar(PlayerVisibility::VisionCellSizeInTiles);
        return Point(floorToInt(tileX / cells), floorToInt((tileZ - skewInTiles) / cells));
    }

    PlayerVisibility::PlayerVisibility(int width, int height)
        : explored(width, height, static_cast<unsigned char>(0)),
          visible(width, height, static_cast<unsigned char>(0)),
          seenStamp(width, height, 0u)
    {
    }

    bool PlayerVisibility::contains(const Point& cell) const
    {
        return cell.x >= 0 && cell.y >= 0 && cell.x < explored.getWidth() && cell.y < explored.getHeight();
    }

    bool PlayerVisibility::isExplored(const Point& cell) const
    {
        return contains(cell) && explored.get(cell.x, cell.y) != 0;
    }

    bool PlayerVisibility::isVisible(const Point& cell) const
    {
        return contains(cell) && visible.get(cell.x, cell.y) != 0;
    }

    int PlayerVisibility::visibleCount(const Point& cell) const
    {
        return contains(cell) ? static_cast<int>(visible.get(cell.x, cell.y)) : 0;
    }

    void PlayerVisibility::clearCurrent()
    {
        auto& cells = visible.getVector();
        std::fill(cells.begin(), cells.end(), static_cast<unsigned char>(0));

        radarDetectors.clear();
        radarContacts.clear();
    }

    void PlayerVisibility::beginReveal()
    {
        ++currentStamp;
        if (currentStamp == 0)
        {
            auto& stamps = seenStamp.getVector();
            std::fill(stamps.begin(), stamps.end(), 0u);
            currentStamp = 1;
        }
    }

    void PlayerVisibility::revealCell(int x, int y)
    {
        if (seenStamp.get(x, y) == currentStamp)
        {
            return;
        }
        seenStamp.set(x, y, currentStamp);

        auto count = visible.get(x, y);
        if (count < 255)
        {
            visible.set(x, y, static_cast<unsigned char>(count + 1));
        }
        explored.set(x, y, 1);
    }

    void PlayerVisibility::revealWithLineOfSight(
        const Point& center,
        int radius,
        const VisionHeightGrid& heights,
        int eyeHeight,
        const LosTables& tables)
    {
        beginReveal();

        if (!contains(center))
        {
            return;
        }

        // Even a blind unit knows where it is standing.
        revealCell(center.x, center.y);

        radius = std::min(radius, tables.maxRadius());
        if (radius <= 0)
        {
            return;
        }

        const auto& table = tables.tableForRadius(radius);

        for (int quadrant = 0; quadrant < 4; ++quadrant)
        {
            for (const auto& ray : table.rays)
            {
                // Shadow casting with a running horizon: bestSlope/bestStep
                // is the steepest sight line the ray has had to climb over so
                // far, held as a fraction to keep the test integer only.
                int bestSlope = -1;
                int bestStep = 0;
                int t = 0;

                for (const auto& offset : ray)
                {
                    ++t;
                    auto step = rotateIntoQuadrant(offset, quadrant);
                    auto x = center.x + step.x;
                    auto y = center.y + step.y;
                    if (x < 0 || y < 0 || x >= visible.getWidth() || y >= visible.getHeight())
                    {
                        // A ray's offsets never shrink, so once it has left
                        // the grid it will not come back.
                        break;
                    }

                    auto lo = static_cast<int>(heights.reveal.get(x, y)) - eyeHeight;
                    auto hi = static_cast<int>(heights.occlude.get(x, y)) - eyeHeight;

                    if (lo * bestStep > bestSlope * t)
                    {
                        revealCell(x, y);

                        // The horizon only rises on cells that were actually
                        // revealed, so once a ray is blocked it stops rising
                        // and everything past the blocker stays dark.
                        if (hi * bestStep > bestSlope * t)
                        {
                            bestSlope = hi;
                            bestStep = t;
                        }
                    }
                }
            }
        }
    }

    VisionHeightGrid computeVisionHeights(const Grid<unsigned char>& heightmap, unsigned char seaLevel)
    {
        auto cells = PlayerVisibility::VisionCellSizeInTiles;
        auto width = (heightmap.getWidth() + cells - 1) / cells;
        auto height = (heightmap.getHeight() + cells - 1) / cells;

        // -1 / 256 mark a cell that no heightmap sample landed in.
        Grid<int> highest(width, height, -1);
        Grid<int> lowest(width, height, 256);

        for (int hy = 0; hy < heightmap.getHeight(); ++hy)
        {
            for (int hx = 0; hx < heightmap.getWidth(); ++hx)
            {
                auto h = static_cast<int>(heightmap.get(hx, hy));
                auto cell = heightmapToVisionCell(intToSimScalar(hx), intToSimScalar(hy), h);
                if (cell.x < 0 || cell.y < 0 || cell.x >= width || cell.y >= height)
                {
                    continue;
                }

                highest.set(cell.x, cell.y, std::max(highest.get(cell.x, cell.y), h));
                lowest.set(cell.x, cell.y, std::min(lowest.get(cell.x, cell.y), h));
            }
        }

        VisionHeightGrid result{
            Grid<unsigned char>(width, height, static_cast<unsigned char>(0)),
            Grid<unsigned char>(width, height, static_cast<unsigned char>(0))};

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                auto maxHeight = highest.get(x, y);
                auto minHeight = lowest.get(x, y);

                if (maxHeight < 0)
                {
                    // The skew carried this cell's ground up the map and left
                    // a cliff face behind. Take the plan-view block at the
                    // same index so the cell still occludes instead of
                    // becoming a hole in the terrain.
                    maxHeight = 0;
                    minHeight = 255;
                    for (int dy = 0; dy < cells; ++dy)
                    {
                        for (int dx = 0; dx < cells; ++dx)
                        {
                            auto sx = (x * cells) + dx;
                            auto sy = (y * cells) + dy;
                            if (sx < heightmap.getWidth() && sy < heightmap.getHeight())
                            {
                                auto h = static_cast<int>(heightmap.get(sx, sy));
                                maxHeight = std::max(maxHeight, h);
                                minHeight = std::min(minHeight, h);
                            }
                        }
                    }
                    if (minHeight > maxHeight)
                    {
                        minHeight = maxHeight;
                    }
                }

                // Biased towards the high side for being seen and towards the
                // low side for blocking: the same cell is easier to spot than
                // it is to hide behind.
                auto reveal = std::max<int>(seaLevel, ((2 * maxHeight) + minHeight) / 3);
                auto occlude = std::max<int>(seaLevel, (maxHeight + (2 * minHeight)) / 3);

                result.reveal.set(x, y, static_cast<unsigned char>(clampToByte(reveal)));
                result.occlude.set(x, y, static_cast<unsigned char>(clampToByte(occlude)));
            }
        }

        return result;
    }
}
