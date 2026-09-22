#include "MovementClassCollisionService.h"
#include <rwe/grid/Point.h>
#include <rwe/sim/movement.h>
#include <rwe/util/collection_util.h>
#include <vector>

namespace rwe
{
    namespace
    {
        /**
         * Labels every walkable cell with the id of the region it belongs to,
         * using the connectivity the pathfinder's successors use: a diagonal
         * edge exists only when both of the orthogonal cells between its ends
         * are walkable, so a region boundary cannot be crossed by squeezing
         * between two touching obstacles.
         */
        Grid<int> labelComponents(const Grid<char>& walkable)
        {
            const int width = walkable.getWidth();
            const int height = walkable.getHeight();
            Grid<int> components(width, height, 0);

            const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
            const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

            auto walkableAt = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < width && y < height && walkable.get(x, y) != 0;
            };

            std::vector<Point> open;
            int nextId = 0;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    if (!walkableAt(x, y) || components.get(x, y) != 0)
                    {
                        continue;
                    }

                    ++nextId;
                    components.set(x, y, nextId);
                    open.clear();
                    open.push_back(Point(x, y));
                    while (!open.empty())
                    {
                        auto p = open.back();
                        open.pop_back();
                        for (int i = 0; i < 8; ++i)
                        {
                            auto nx = p.x + dx[i];
                            auto ny = p.y + dy[i];
                            if (!walkableAt(nx, ny) || components.get(nx, ny) != 0)
                            {
                                continue;
                            }
                            // The two orthogonal cells between p and a
                            // diagonal neighbour must both be clear.
                            if (dx[i] != 0 && dy[i] != 0
                                && (!walkableAt(p.x + dx[i], p.y) || !walkableAt(p.x, p.y + dy[i])))
                            {
                                continue;
                            }
                            components.set(nx, ny, nextId);
                            open.push_back(Point(nx, ny));
                        }
                    }
                }
            }

            return components;
        }
    }

    void MovementClassCollisionService::registerMovementClass(MovementClassId id, Grid<char>&& walkableGrid)
    {
        walkableGrids.insert({id, std::move(walkableGrid)});
    }

    bool MovementClassCollisionService::isWalkable(MovementClassId movementClass, const Point& position) const
    {
        return walkableGrids.at(movementClass).tryGetValue(position).value_or(false);
    }

    const Grid<char>& MovementClassCollisionService::getGrid(MovementClassId movementClass) const
    {
        return walkableGrids.at(movementClass);
    }

    const Grid<int>* MovementClassCollisionService::tryGetComponentGrid(MovementClassId movementClass) const
    {
        auto existing = componentGrids.find(movementClass);
        if (existing != componentGrids.end())
        {
            return &existing->second;
        }

        auto grid = walkableGrids.find(movementClass);
        if (grid == walkableGrids.end())
        {
            return nullptr;
        }

        auto inserted = componentGrids.emplace(movementClass, labelComponents(grid->second));
        return &inserted.first->second;
    }
    Grid<char> computeWalkableGrid(const MapTerrain& terrain, const MovementClassDefinition& movementClass)
    {
        const auto footprintX = movementClass.footprintX;
        const auto footprintY = movementClass.footprintZ;

        const auto width = terrain.getHeightMap().getWidth() - footprintX;
        const auto height = terrain.getHeightMap().getHeight() - footprintY;

        return Grid<char>::from(
            width,
            height,
            [&](const auto& c) { return isGridPointWalkable(terrain, movementClass, c.x, c.y); });
    }
}
