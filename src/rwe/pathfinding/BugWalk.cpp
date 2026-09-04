#include "BugWalk.h"

#include <array>

namespace rwe
{
    namespace
    {
        /**
         * The original's own direction tables, at 0x4FD670 and 0x4FD678.
         * Direction 0 is -z and they run round in eighths, so 2 is -x, 4 is
         * +z and 6 is +x; the odd numbers are the diagonals.
         */
        constexpr std::array<int, 8> DirectionX{0, -1, -1, -1, 0, 1, 1, 1};
        constexpr std::array<int, 8> DirectionZ{-1, -1, 0, 1, 1, 1, 0, -1};

        Point step(const Point& from, int direction)
        {
            auto d = static_cast<std::size_t>(direction & 7);
            return Point(from.x + DirectionX[d], from.y + DirectionZ[d]);
        }

        /**
         * Which way the greedy walk wants to go: along x while there is any x
         * left to cover, along z otherwise. 0x40E1CF works x first in exactly
         * this way, which is why the original's units tend to square off a
         * detour rather than cut the diagonal.
         */
        int greedyDirection(const Point& from, const Point& goal)
        {
            auto dx = goal.x - from.x;
            if (dx != 0)
            {
                return dx < 0 ? 2 : 6;
            }

            auto dz = goal.y - from.y;
            return dz < 0 ? 0 : 4;
        }
    }

    BugWalkResult bugWalk(
        const Point& start,
        const Point& goal,
        const std::function<bool(const Point&)>& walkable,
        const std::function<unsigned int(const Point&)>& distanceToGoal,
        unsigned int stepLimit)
    {
        BugWalkResult result;
        result.closest = start;
        result.reachedGoal = start == goal;

        auto closestDistance = distanceToGoal(start);
        auto current = start;

        // Wall-following state. The walk is either running straight at the
        // goal or tracing an obstacle. Which way round it traces is settled at
        // the moment of contact and then held: `wallOffset` is where the wall
        // sits relative to the way the walk is now facing, and every step
        // afterwards starts its scan there and sweeps away from it. Getting
        // that wrong is what makes a tracer drift off the obstacle and wander
        // -- it has to try the wall first and take the first thing that is
        // not the wall.
        bool followingWall = false;
        Point wallHitPoint = start;
        unsigned int wallHitDistance = 0;
        int heading = 0;
        int wallOffset = 2;
        unsigned int wallSteps = 0;

        while (result.steps < stepLimit && !result.reachedGoal)
        {
            if (!followingWall)
            {
                auto direction = greedyDirection(current, goal);
                if (walkable(step(current, direction)))
                {
                    current = step(current, direction);
                }
                else
                {
                    // Meet the wall. Turn a quarter and remember which side of
                    // us the wall ended up on.
                    bool turned = false;
                    for (int side : {2, -2})
                    {
                        auto d = (direction + side) & 7;
                        if (walkable(step(current, d)))
                        {
                            heading = d;
                            wallOffset = -side;
                            turned = true;
                            break;
                        }
                    }

                    if (!turned)
                    {
                        // Neither quarter turn is open: there is no wall to
                        // trace, only a corner to be stuck in.
                        break;
                    }

                    followingWall = true;
                    wallHitPoint = current;
                    wallHitDistance = distanceToGoal(current);
                    wallSteps = 0;
                    current = step(current, heading);
                }
            }
            else
            {
                // Scan from the wall and sweep away from it, taking the first
                // cell that is open: hard against the obstacle first, then
                // progressively further from it.
                bool moved = false;
                auto sweep = wallOffset > 0 ? -1 : 1;
                for (int i = 0; i < 8; ++i)
                {
                    auto d = (heading + wallOffset + (i * sweep)) & 7;
                    if (walkable(step(current, d)))
                    {
                        current = step(current, d);
                        heading = d;
                        moved = true;
                        break;
                    }
                }

                if (!moved)
                {
                    // Walled in on all eight sides: nothing more to learn.
                    break;
                }

                ++wallSteps;

                // Leave the wall once the walk is back on an axis-aligned line
                // to the target and closer to it than where it met the wall.
                // The original works this out by flipping the axes so the
                // target sits in the positive quadrant and comparing against
                // the hit point (0x40E3D5-0x40E445); this is that test written
                // plainly. Any difference between the two changes only how far
                // the goal is relaxed afterwards, never the route the search
                // then finds.
                auto onAxis = (current.x == goal.x) || (current.y == goal.y);
                if (onAxis && distanceToGoal(current) < wallHitDistance)
                {
                    followingWall = false;
                }

                // All the way round and back where it started: the target
                // cannot be reached this way (0x40E370).
                if (current == wallHitPoint && wallSteps > 1)
                {
                    break;
                }
            }

            ++result.steps;

            auto distance = distanceToGoal(current);
            if (distance < closestDistance)
            {
                closestDistance = distance;
                result.closest = current;
            }

            if (current == goal)
            {
                result.reachedGoal = true;
                result.closest = current;
            }
        }

        return result;
    }
}
