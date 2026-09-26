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

        SECTION("a dead end is backed out of, not given up on")
        {
            // A pocket open to the west whose east end the walker stands
            // against: neither quarter turn is open there, only the way back.
            // The walk must reverse out and go round (issue #309, a sea
            // transport tucked in beside its own shipyard).
            auto walkable = [](const Point& p) {
                auto inPocketWall = p.x >= 0 && p.x <= 6 && (p.y == -1 || p.y == 1);
                auto inEndWall = p.x == 7 && p.y >= -1 && p.y <= 1;
                return !inPocketWall && !inEndWall;
            };
            auto goal = Point(20, 0);
            auto result = bugWalk(
                Point(6, 0),
                goal,
                walkable,
                [&](const Point& p) { return octile(p, goal); },
                4000);

            REQUIRE(result.reachedGoal);
        }

        SECTION("a step back from the wall gets the same answer as the wall itself")
        {
            // What #309 needs: from the cell touching the obstacle and from
            // the one before it, the walk has to agree, or the search is
            // relaxed from one and not the other and the unit shuttles
            // between them.
            auto walkable = [](const Point& p) {
                auto inPocketWall = p.x >= 0 && p.x <= 6 && (p.y == -1 || p.y == 1);
                auto inEndWall = p.x == 7 && p.y >= -1 && p.y <= 1;
                return !inPocketWall && !inEndWall;
            };
            auto goal = Point(20, 0);
            auto distance = [&](const Point& p) { return octile(p, goal); };
            // PathFindingService's rule: relax to the walk's closest cell when
            // the walk fell short of the goal but got nearer than it started.
            auto relaxes = [&](const Point& start) {
                auto result = bugWalk(start, goal, walkable, distance, 4000);
                auto reachable = distance(result.closest);
                return !result.reachedGoal && reachable != 0 && reachable < distance(start);
            };

            REQUIRE(relaxes(Point(6, 0)) == relaxes(Point(5, 0)));
        }

        SECTION("round the short end of a wall, whichever side that is")
        {
            // A wall across the way that runs a hundred cells to one side and
            // three to the other. The tracers go both ways a step at a time,
            // so the short side is found in a handful of steps.
            auto goal = Point(10, 0);
            auto distance = [&](const Point& p) { return octile(p, goal); };
            auto longUp = [](const Point& p) { return !(p.x == 5 && p.y >= -100 && p.y <= 3); };
            auto longDown = [](const Point& p) { return !(p.x == 5 && p.y >= -3 && p.y <= 100); };

            auto up = bugWalk(Point(0, 0), goal, longUp, distance, 4000);
            auto down = bugWalk(Point(0, 0), goal, longDown, distance, 4000);

            REQUIRE(up.reachedGoal);
            REQUIRE(down.reachedGoal);
            REQUIRE(up.steps < 40);
            REQUIRE(down.steps < 40);
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
