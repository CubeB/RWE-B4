#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rwe/pathfinding/oa/OaSearchTrace.h>
#include <stdexcept>
#include <vector>

namespace rwe
{
    namespace
    {
        constexpr uint8_t dirNorth = 0; // (0, -1)
        constexpr uint8_t dirWest = 2;  // (-1, 0)
        constexpr uint8_t dirSouth = 4; // (0, 1)
        constexpr uint8_t dirEast = 6;  // (1, 0)
    }

    TEST_CASE("a straight search path keeps only the start and finish", "[oa]")
    {
        std::vector<uint8_t> predecessor(5, dirEast);
        predecessor[0] = dirNorth;

        const auto route = oa::reconstructSearchPath(5, 1, {0, 0}, {4, 0}, 1, 1, predecessor);
        REQUIRE(route.size() == 2);
        REQUIRE(route.front() == std::array<int16_t, 2>{8, 8});
        REQUIRE(route.back() == std::array<int16_t, 2>{72, 8});
    }

    TEST_CASE("an L-shaped search path keeps its corner", "[oa]")
    {
        // Go east along row 0 from (0, 0) to (4, 0), then south to (4, 4).
        std::vector<uint8_t> predecessor(25, dirNorth);
        for (int16_t x = 1; x <= 4; ++x)
        {
            predecessor[x] = dirEast;
        }
        for (int16_t z = 1; z <= 4; ++z)
        {
            predecessor[static_cast<std::size_t>(z) * 5 + 4] = dirSouth;
        }

        const auto route = oa::reconstructSearchPath(5, 5, {0, 0}, {4, 4}, 1, 1, predecessor);
        REQUIRE(route.size() == 3);
        REQUIRE(route[0] == std::array<int16_t, 2>{8, 8});
        REQUIRE(route[1] == std::array<int16_t, 2>{72, 8});
        REQUIRE(route[2] == std::array<int16_t, 2>{72, 72});
    }

    TEST_CASE("search route points are footprint centres", "[oa]")
    {
        std::vector<uint8_t> predecessor(5, dirEast);
        predecessor[0] = dirNorth;

        const auto route = oa::reconstructSearchPath(5, 1, {0, 0}, {4, 0}, 2, 3, predecessor);
        REQUIRE(route.front() == std::array<int16_t, 2>{16, 24});
        REQUIRE(route.back() == std::array<int16_t, 2>{80, 24});
    }

    TEST_CASE("a truncated or invalid predecessor map is rejected", "[oa]")
    {
        const std::vector<uint8_t> truncated(4, dirEast);
        REQUIRE_THROWS_AS(
            oa::reconstructSearchPath(5, 1, {0, 0}, {4, 0}, 1, 1, truncated), std::invalid_argument);

        std::vector<uint8_t> badDirection(5, dirEast);
        badDirection[4] = 8;
        REQUIRE_THROWS_AS(
            oa::reconstructSearchPath(5, 1, {0, 0}, {4, 0}, 1, 1, badDirection),
            std::invalid_argument);

        // (2, 0) and (1, 0) point at each other, so the start is never reached.
        std::vector<uint8_t> cycle(5, dirNorth);
        cycle[2] = dirEast;
        cycle[1] = dirWest;
        REQUIRE_THROWS_AS(
            oa::reconstructSearchPath(5, 1, {0, 0}, {2, 0}, 1, 1, cycle), std::invalid_argument);
    }
}
