#include <catch2/catch_test_macros.hpp>
#include <rwe/pathfinding/BugWalk.h>
#include <algorithm>
#include <cstdlib>
#include <set>

namespace rwe
{
    namespace
    {
        /** The octile distance the unit pathfinder orders cells by, in whole steps. */
        unsigned int octile(const Point& a, const Point& b)
        {
            auto dx = static_cast<unsigned int>(std::abs(a.x - b.x));
            auto dy = static_cast<unsigned int>(std::abs(a.y - b.y));
            auto diagonal = std::min(dx, dy);
            auto straight = std::max(dx, dy) - diagonal;
            // 14/10 is the usual integer stand-in for sqrt(2).
            return (14 * diagonal) + (10 * straight);
        }
    }

    TEST_CASE("the bug walk answers how close it is possible to get", "[pathfinding]")
    {
        SECTION("open ground: it reaches the goal")
        {
            auto walkable = [](const Point&) { return true; };
            auto result = bugWalk(
                Point(0, 0),
                Point(10, 4),
                walkable,
                [&](const Point& p) { return octile(p, Point(10, 4)); },
                1000);

            REQUIRE(result.reachedGoal);
            REQUIRE(result.closest == Point(10, 4));
        }

        SECTION("a wall with a way round it: it still reaches the goal")
        {
            // A vertical wall at x = 5 from y = -3 up to y = 3, so the walk
            // has to trace round one end.
            auto walkable = [](const Point& p) {
                return !(p.x == 5 && p.y >= -3 && p.y <= 3);
            };
            auto goal = Point(10, 0);
            auto result = bugWalk(
                Point(0, 0),
                goal,
                walkable,
                [&](const Point& p) { return octile(p, goal); },
                4000);

            REQUIRE(result.reachedGoal);
        }

        SECTION("a goal sealed inside a box: it gets close and gives up")
        {
            // A closed room around the goal. Nothing can reach it, and the
            // point of the walk is to establish that cheaply rather than by
            // exhausting the map.
            auto goal = Point(10, 0);
            auto walkable = [&](const Point& p) {
                bool onBoxEdge =
                    (std::abs(p.x - goal.x) <= 2 && std::abs(p.y - goal.y) <= 2)
                    && (std::abs(p.x - goal.x) == 2 || std::abs(p.y - goal.y) == 2);
                return !onBoxEdge;
            };

            auto result = bugWalk(
                Point(0, 0),
                goal,
                walkable,
                [&](const Point& p) { return octile(p, goal); },
                4000);

            REQUIRE_FALSE(result.reachedGoal);

            // It got nearer than it started -- which is what tells the search
            // there is something worth aiming at -- but not to the goal.
            REQUIRE(octile(result.closest, goal) < octile(Point(0, 0), goal));
            REQUIRE(octile(result.closest, goal) > 0);
        }

        SECTION("the walker sealed in instead: it learns nothing and says so")
        {
            // Walled in where it stands. The original abandons the search
            // outright in this case rather than running an A* that can only
            // exhaust itself (0x40E979).
            auto start = Point(0, 0);
            auto walkable = [&](const Point& p) { return p == start; };
            auto goal = Point(10, 0);

            auto result = bugWalk(
                start,
                goal,
                walkable,
                [&](const Point& p) { return octile(p, goal); },
                4000);

            REQUIRE_FALSE(result.reachedGoal);
            REQUIRE(result.closest == start);
        }

        SECTION("it never spends more than its step limit")
        {
            // A spiral of walls would trap a naive walk for a very long time.
            // The original has no limit at all; this one reports the closest
            // it reached and stops, which is a safe answer rather than a
            // wrong one.
            auto goal = Point(400, 400);
            auto walkable = [](const Point& p) { return p.x % 7 != 3; };

            auto result = bugWalk(
                Point(0, 0),
                goal,
                walkable,
                [&](const Point& p) { return octile(p, goal); },
                50);

            REQUIRE(result.steps <= 50);
        }
    }
}
