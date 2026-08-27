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

    void PlayerVisibility::radarCircle(const Point& center, int radius)
    {
        forEachCellInCircle(radar, center, radius, [&](int x, int y) {
            radar.set(x, y, 1);
        });
    }
}
