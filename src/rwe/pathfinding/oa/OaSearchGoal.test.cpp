#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rwe/pathfinding/oa/OaSearchGoal.h>
#include <vector>

namespace rwe
{
    TEST_CASE("a circular goal contains cells within its squared radius", "[oa]")
    {
        oa::SearchGoal goal{};
        goal.cellX = 2;
        goal.cellZ = 0;
        goal.radiusSquared = 4;

        CHECK(oa::goalContains(goal, 2, 0));
        CHECK(oa::goalContains(goal, 0, 0));
        CHECK(oa::goalContains(goal, 4, 0));
        CHECK(oa::goalContains(goal, 2, 2));
        CHECK_FALSE(oa::goalContains(goal, 5, 0));
        CHECK_FALSE(oa::goalContains(goal, 2, 3));
        CHECK_FALSE(oa::goalContains(goal, 3, 2));
    }

    TEST_CASE("circle goal cost is the octile distance less the tolerance", "[oa]")
    {
        oa::SearchGoal goal{};
        goal.cellX = 0;
        goal.cellZ = 0;
        CHECK(oa::goalCost(goal, 0, 0) == 0);
        CHECK(oa::goalCost(goal, 1, 0) == 18);
        CHECK(oa::goalCost(goal, 0, 1) == 18);
        CHECK(oa::goalCost(goal, 1, 1) == 25);
        CHECK(oa::goalCost(goal, 2, 1) == 43);
        CHECK(oa::goalCost(goal, 2, 2) == 50);

        oa::SearchGoal tolerant{};
        tolerant.cellX = 0;
        tolerant.cellZ = 0;
        tolerant.tolerance = 20;
        CHECK(oa::goalCost(tolerant, 1, 0) == 0);
        CHECK(oa::goalCost(tolerant, 2, 0) == 16);
        CHECK(oa::goalCost(tolerant, 1, 1) == 5);
    }

    TEST_CASE("a circular goal visits only its centre cell", "[oa]")
    {
        oa::SearchGoal goal{};
        goal.cellX = 3;
        goal.cellZ = 4;

        std::vector<std::array<int32_t, 2>> visited;
        oa::forEachGoalCell(goal, [&](int32_t x, int32_t z) { visited.push_back({x, z}); });

        REQUIRE(visited.size() == 1);
        REQUIRE(visited[0][0] == 3);
        REQUIRE(visited[0][1] == 4);
    }
}
