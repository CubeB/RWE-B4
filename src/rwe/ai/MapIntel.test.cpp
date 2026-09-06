#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/MapIntel.h>

namespace rwe
{
    namespace
    {
        /**
         * A heightmap that is `waterCells` cells of height 0 followed by land
         * at height 100, with the sea at 50. Laid out as one row per cell so
         * the fraction under test is exactly waterCells / cells.
         */
        MapTerrain terrainWithWater(std::size_t cells, std::size_t waterCells)
        {
            Grid<unsigned char> heights(cells, 1, static_cast<unsigned char>(100));
            for (std::size_t x = 0; x < waterCells; ++x)
            {
                heights.set(x, 0, static_cast<unsigned char>(0));
            }
            return MapTerrain(std::move(heights), 50_ss);
        }
    }

    TEST_CASE("analyseMap")
    {
        SECTION("a dry map is a land map")
        {
            auto intel = analyseMap(terrainWithWater(100, 0), {});
            REQUIRE(intel.valid);
            REQUIRE(intel.waterFraction == 0.0f);
            REQUIRE(intel.character == MapCharacter::Land);
        }

        SECTION("a puddle does not make a naval map")
        {
            // Five per cent water is a lake or a river. Building a navy for it
            // would be metal thrown away, which is the whole reason the
            // threshold is not "any water at all".
            auto intel = analyseMap(terrainWithWater(100, 5), {});
            REQUIRE(intel.character == MapCharacter::Land);
        }

        SECTION("a quarter under water is mixed")
        {
            auto intel = analyseMap(terrainWithWater(100, 25), {});
            REQUIRE(intel.waterFraction == 0.25f);
            REQUIRE(intel.character == MapCharacter::Mixed);
        }

        SECTION("mostly sea is a water map")
        {
            auto intel = analyseMap(terrainWithWater(100, 70), {});
            REQUIRE(intel.character == MapCharacter::Water);
        }

        SECTION("the thresholds are inclusive at their lower edge")
        {
            REQUIRE(analyseMap(terrainWithWater(100, 12), {}).character == MapCharacter::Mixed);
            REQUIRE(analyseMap(terrainWithWater(100, 11), {}).character == MapCharacter::Land);
            REQUIRE(analyseMap(terrainWithWater(100, 40), {}).character == MapCharacter::Water);
            REQUIRE(analyseMap(terrainWithWater(100, 39), {}).character == MapCharacter::Mixed);
        }

        SECTION("start positions are carried through in map order")
        {
            std::vector<SimVector> starts{
                SimVector(10_ss, 0_ss, 10_ss),
                SimVector(90_ss, 0_ss, 90_ss)};
            auto intel = analyseMap(terrainWithWater(10, 0), starts);
            REQUIRE(intel.startPositions.size() == 2);
            REQUIRE(intel.startPositions[0].x == 10_ss);
            REQUIRE(intel.startPositions[1].x == 90_ss);
        }
    }

    TEST_CASE("nearestStartPosition")
    {
        MapIntel intel;
        intel.valid = true;
        intel.startPositions = {
            SimVector(0_ss, 0_ss, 0_ss),
            SimVector(100_ss, 0_ss, 0_ss),
            SimVector(0_ss, 0_ss, 100_ss)};

        SECTION("picks the closest")
        {
            REQUIRE(*nearestStartPosition(intel, SimVector(90_ss, 0_ss, 10_ss)) == 1);
            REQUIRE(*nearestStartPosition(intel, SimVector(5_ss, 0_ss, 95_ss)) == 2);
        }

        SECTION("ignores height, because a commander that has walked is not at the stored one")
        {
            REQUIRE(*nearestStartPosition(intel, SimVector(95_ss, 900_ss, 0_ss)) == 1);
        }

        SECTION("has no answer on a map that declares none")
        {
            MapIntel empty;
            REQUIRE(!nearestStartPosition(empty, SimVector(0_ss, 0_ss, 0_ss)));
        }
    }
}
