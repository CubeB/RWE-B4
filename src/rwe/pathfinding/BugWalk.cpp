#include "BugWalk.h"

#include <array>
#include <optional>

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

        /**
         * Whether a tracer standing at `p` is back on the greedy walk's own
         * line further along than where it met the wall (0x40E3D5-0x40E445,
         * and again for the second tracer at 0x40E56B). The axes are flipped
         * so the goal lies in the positive quadrant from the hit point; the
         * line is then the hit point's row out to the goal's column, and that
         * column on to the goal, which is the x-first route the greedy walk
         * would have taken.
         */
        bool rejoinsGreedyLine(const Point& hit, const Point& p, const Point& goal)
        {
            auto goalX = goal.x - hit.x;
            auto goalZ = goal.y - hit.y;
            auto x = p.x - hit.x;
            auto z = p.y - hit.y;
            if (goalX < 0)
            {
                goalX = -goalX;
                x = -x;
            }
            if (goalZ < 0)
            {
                goalZ = -goalZ;
                z = -z;
            }

            if (z == 0 && x > 0 && x <= goalX)
            {
                return true;
            }
            return x == goalX && z > 0 && z <= goalZ;
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
        auto improve = [&](const Point& p) {
            auto distance = distanceToGoal(p);
            if (distance < closestDistance)
            {
                closestDistance = distance;
                result.closest = p;
            }
        };
        auto arrive = [&](const Point& p) {
            if (p == goal)
            {
                result.reachedGoal = true;
                result.closest = p;
            }
            return result.reachedGoal;
        };

        auto walker = start;
        while (!result.reachedGoal && result.steps < stepLimit)
        {
            ++result.steps;

            auto direction = greedyDirection(walker, goal);
            auto next = step(walker, direction);
            if (walkable(next))
            {
                walker = next;
                if (!arrive(walker))
                {
                    improve(walker);
                }
                continue;
            }

            // The wall (0x40E2AC). Two tracers set off from here round it in
            // opposite directions, a step each in turn, and whichever gets
            // back onto the greedy line first takes the walk on from there.
            // The first keeps the wall on the side it is scanned from and
            // turns one way; the second is its mirror image, held as the
            // reverse of the way it faces, which is how the original stores
            // it. Both start a quarter turn from the blocked direction and
            // scan all eight from the wall round, so a dead end is backed out
            // of rather than given up on.
            const auto hit = walker;
            auto first = hit;
            auto firstHeading = (direction + 2) & 7;
            auto second = hit;
            auto secondHeading = (direction + 2) & 7;
            bool firstHasMoved = false;
            bool tracing = true;

            while (tracing && result.steps < stepLimit)
            {
                // A round of both tracers is one step, as the original counts
                // it (0x40E2F5).
                ++result.steps;

                // The first tracer (0x40E2D6-0x40E350): from the wall side,
                // turning one way.
                std::optional<int> firstMove;
                for (int i = 0; i < 8; ++i)
                {
                    auto d = (firstHeading - 2 + i) & 7;
                    if (walkable(step(first, d)))
                    {
                        firstMove = d;
                        break;
                    }
                }
                if (!firstMove)
                {
                    // Walled in on all eight sides.
                    return result;
                }

                // The two have met head on: the obstacle has been traced all
                // the way round without a way off it (0x40E352-0x40E376).
                if (firstHasMoved && first == second && *firstMove == secondHeading)
                {
                    return result;
                }

                first = step(first, *firstMove);
                firstHeading = *firstMove;
                firstHasMoved = true;
                if (arrive(first))
                {
                    return result;
                }
                if (rejoinsGreedyLine(hit, first, goal))
                {
                    walker = first;
                    tracing = false;
                    break;
                }
                improve(first);

                // The second tracer (0x40E46E-0x40E4F4): the mirror image.
                // Its heading is the reverse of the way it moves.
                std::optional<int> secondTurn;
                for (int i = 0; i < 8; ++i)
                {
                    auto h = (secondHeading + 2 - i) & 7;
                    if (walkable(step(second, (h + 4) & 7)))
                    {
                        secondTurn = h;
                        break;
                    }
                }
                if (!secondTurn)
                {
                    return result;
                }

                // 0x40E4F6-0x40E512: the same meeting, seen from this side.
                if (second == first && firstHeading == *secondTurn)
                {
                    return result;
                }

                second = step(second, (*secondTurn + 4) & 7);
                secondHeading = *secondTurn;
                if (arrive(second))
                {
                    return result;
                }
                if (rejoinsGreedyLine(hit, second, goal))
                {
                    walker = second;
                    tracing = false;
                    break;
                }
                improve(second);
            }
        }

        return result;
    }
}
