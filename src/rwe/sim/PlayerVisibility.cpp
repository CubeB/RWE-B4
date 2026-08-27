#include "PlayerVisibility.h"
#include <algorithm>

namespace rwe
{
    namespace
    {
        template <typename Func>
        void forEachCellInCircle(const Grid<unsigned char>& grid, const Point& center, int radius, Func f)
        {
            auto minX = std::max(0, center.x - radius);
            auto maxX = std::min(grid.getWidth() - 1, center.x + radius);
            auto minY = std::max(0, center.y - radius);
            auto maxY = std::min(grid.getHeight() - 1, center.y + radius);
            auto radiusSquared = radius * radius;

            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    auto dx = x - center.x;
                    auto dy = y - center.y;
                    if ((dx * dx) + (dy * dy) <= radiusSquared)
                    {
                        f(x, y);
                    }
                }
            }
        }
    }

    PlayerVisibility::PlayerVisibility(int width, int height)
        : explored(width, height, static_cast<unsigned char>(0)),
          visible(width, height, static_cast<unsigned char>(0)),
          radar(width, height, static_cast<unsigned char>(0))
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

    bool PlayerVisibility::isOnRadar(const Point& cell) const
    {
        return contains(cell) && radar.get(cell.x, cell.y) != 0;
    }

    void PlayerVisibility::clearCurrent()
    {
        for (int y = 0; y < visible.getHeight(); ++y)
        {
            for (int x = 0; x < visible.getWidth(); ++x)
            {
                visible.set(x, y, 0);
                radar.set(x, y, 0);
            }
        }
    }

    void PlayerVisibility::revealCircle(const Point& center, int radius)
    {
        forEachCellInCircle(visible, center, radius, [&](int x, int y) {
            visible.set(x, y, 1);
            explored.set(x, y, 1);
        });
    }

    void PlayerVisibility::revealCircleWithLineOfSight(const Point& center, int radius, const Grid<unsigned char>& groundHeights, int eyeHeight, int targetHeight)
    {
        if (!contains(center))
        {
            return;
        }

        auto groundAt = [&](int x, int y) {
            return static_cast<int>(groundHeights.get(x, y));
        };
        auto eye = groundAt(center.x, center.y) + eyeHeight;

        forEachCellInCircle(visible, center, radius, [&](int x, int y) {
            auto dx = x - center.x;
            auto dy = y - center.y;
            auto steps = std::max(std::abs(dx), std::abs(dy));
            auto target = groundAt(x, y) + targetHeight;

            // Walk the line of sight one cell at a time; the ground must stay
            // below the sight line the whole way.
            bool clear = true;
            for (int t = 1; t < steps; ++t)
            {
                // Round to nearest cell along the line (integer arithmetic, deterministic).
                auto sx = center.x + ((dx * t * 2 + (dx >= 0 ? steps : -steps)) / (2 * steps));
                auto sy = center.y + ((dy * t * 2 + (dy >= 0 ? steps : -steps)) / (2 * steps));
                // Height of the sight line at this step, in 1/steps units.
                auto lineHeightScaled = (eye * (steps - t)) + (target * t);
                if (groundAt(sx, sy) * steps > lineHeightScaled)
                {
                    clear = false;
                    break;
                }
            }

            if (clear)
            {
                visible.set(x, y, 1);
                explored.set(x, y, 1);
            }
        });
    }

    void PlayerVisibility::radarCircle(const Point& center, int radius)
    {
        forEachCellInCircle(radar, center, radius, [&](int x, int y) {
            radar.set(x, y, 1);
        });
    }

    Grid<unsigned char> computeVisionHeights(const Grid<unsigned char>& heightmap)
    {
        auto cells = PlayerVisibility::VisionCellSizeInTiles;
        auto width = (heightmap.getWidth() + cells - 1) / cells;
        auto height = (heightmap.getHeight() + cells - 1) / cells;
        Grid<unsigned char> result(width, height, static_cast<unsigned char>(0));
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                unsigned char highest = 0;
                for (int dy = 0; dy < cells; ++dy)
                {
                    for (int dx = 0; dx < cells; ++dx)
                    {
                        auto hx = (x * cells) + dx;
                        auto hy = (y * cells) + dy;
                        if (hx < heightmap.getWidth() && hy < heightmap.getHeight())
                        {
                            highest = std::max(highest, heightmap.get(hx, hy));
                        }
                    }
                }
                result.set(x, y, highest);
            }
        }
        return result;
    }
}
