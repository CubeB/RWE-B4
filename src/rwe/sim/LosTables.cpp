#include "LosTables.h"
#include <algorithm>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        /**
         * The cells a straight line from the origin to (endX, endY) passes
         * through, one per step, excluding the origin. Integer only: the
         * position at step t is rounded to the nearest cell.
         */
        LosRay traceRay(int endX, int endY)
        {
            LosRay ray;
            auto steps = std::max(endX, endY);
            if (steps <= 0)
            {
                return ray;
            }

            ray.reserve(steps);
            for (int t = 1; t <= steps; ++t)
            {
                auto x = ((endX * t * 2) + steps) / (2 * steps);
                auto y = ((endY * t * 2) + steps) / (2 * steps);
                ray.emplace_back(x, y);
            }
            return ray;
        }
    }

    int LosTables::maxRadius() const
    {
        return tables.empty() ? 0 : static_cast<int>(tables.size()) - 1;
    }

    const LosTable& LosTables::tableForRadius(int radius) const
    {
        if (radius < 0 || radius >= static_cast<int>(tables.size()))
        {
            throw std::logic_error("no line of sight table for the requested radius");
        }

        return tables[radius];
    }

    LosTables generateLosTables(int maxRadius)
    {
        LosTables result;
        result.tables.resize(std::max(0, maxRadius) + 1);

        for (int r = 1; r <= maxRadius; ++r)
        {
            auto& table = result.tables[r];
            auto rSquared = r * r;

            // The ray endpoints are the cells on the rim of the quadrant's
            // disc: the furthest cell in each column, then the furthest cell
            // in each row. Fixed iteration order, and duplicates dropped, so
            // the fan is identical on every machine.
            std::vector<Point> endpoints;
            auto addEndpoint = [&](int x, int y) {
                if (x == 0 && y == 0)
                {
                    return;
                }
                for (const auto& p : endpoints)
                {
                    if (p.x == x && p.y == y)
                    {
                        return;
                    }
                }
                endpoints.emplace_back(x, y);
            };

            for (int x = 0; x <= r; ++x)
            {
                for (int y = r; y >= 0; --y)
                {
                    if ((x * x) + (y * y) <= rSquared)
                    {
                        addEndpoint(x, y);
                        break;
                    }
                }
            }
            for (int y = 0; y <= r; ++y)
            {
                for (int x = r; x >= 0; --x)
                {
                    if ((x * x) + (y * y) <= rSquared)
                    {
                        addEndpoint(x, y);
                        break;
                    }
                }
            }

            table.rays.reserve(endpoints.size());
            for (const auto& e : endpoints)
            {
                table.rays.push_back(traceRay(e.x, e.y));
            }
        }

        return result;
    }
}
