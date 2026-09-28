#include <catch2/catch_test_macros.hpp>
#include <rwe/pathfinding/PathfindingBackend.h>

namespace rwe
{
    TEST_CASE("a pathfinding backend named in rwe.cfg parses to its enum", "[pathing]")
    {
        REQUIRE(pathfindingBackendFromString("rwe", PathfindingBackend::OpenAnnihilation) == PathfindingBackend::RweAStar);
        REQUIRE(pathfindingBackendFromString("oa", PathfindingBackend::RweAStar) == PathfindingBackend::OpenAnnihilation);
        REQUIRE(pathfindingBackendFromString("open-annihilation", PathfindingBackend::RweAStar) == PathfindingBackend::OpenAnnihilation);
        REQUIRE(pathfindingBackendFromString("openannihilation", PathfindingBackend::RweAStar) == PathfindingBackend::OpenAnnihilation);
    }

    TEST_CASE("an unknown pathfinding backend keeps the caller's fallback", "[pathing]")
    {
        REQUIRE(pathfindingBackendFromString("nonsense", PathfindingBackend::RweAStar) == PathfindingBackend::RweAStar);
        REQUIRE(pathfindingBackendFromString("", PathfindingBackend::OpenAnnihilation) == PathfindingBackend::OpenAnnihilation);
    }

    TEST_CASE("a pathfinding backend writes the spelling its config and headers share", "[pathing]")
    {
        REQUIRE(std::string(pathfindingBackendName(PathfindingBackend::RweAStar)) == "rwe");
        REQUIRE(std::string(pathfindingBackendName(PathfindingBackend::OpenAnnihilation)) == "oa");
    }
}
