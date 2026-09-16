#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/MapIntel.h>
#include <rwe/sim/GameSimulation.h>
#include <string>
#include <vector>

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

        /**
         * A 2D heightmap drawn as rows of characters: 'w' is a water cell at
         * `waterHeight`, anything else is land at `landHeight`. Every row
         * must be the same length. This is for tests that need actual shape
         * -- two separate lakes, a basin big enough for a shipyard -- which a
         * single row cannot show.
         */
        MapTerrain terrainFromRows(const std::vector<std::string>& rows, unsigned char landHeight, unsigned char waterHeight, SimScalar seaLevel)
        {
            auto height = rows.size();
            auto width = rows.empty() ? std::size_t(0) : rows[0].size();
            Grid<unsigned char> heights(static_cast<int>(width), static_cast<int>(height), landHeight);
            for (std::size_t y = 0; y < height; ++y)
            {
                for (std::size_t x = 0; x < width; ++x)
                {
                    heights.set(static_cast<int>(x), static_cast<int>(y), rows[y][x] == 'w' ? waterHeight : landHeight);
                }
            }
            return MapTerrain(std::move(heights), seaLevel);
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

    TEST_CASE("analyseMap water connectivity")
    {
        // Two lakes either side of a strip of land, three rows deep so a
        // flood fill has more than one neighbour to consider.
        auto terrain = terrainFromRows(
            {
                "wwwLLLwww",
                "wwwLLLwww",
                "wwwLLLwww",
            },
            100, 0, 50_ss);
        auto intel = analyseMap(terrain, {});

        SECTION("two separated lakes are two connected bodies, not one")
        {
            REQUIRE(intel.waterRegionSizes.size() == 2);
        }

        SECTION("two points in the same lake are the same water body")
        {
            auto a = terrain.heightmapIndexToWorldCenter(0, 1);
            auto b = terrain.heightmapIndexToWorldCenter(2, 1);
            REQUIRE(sameWaterBody(intel, terrain, a, b));
        }

        SECTION("two points in different lakes are not the same water body")
        {
            auto a = terrain.heightmapIndexToWorldCenter(0, 1);
            auto b = terrain.heightmapIndexToWorldCenter(7, 1);
            REQUIRE_FALSE(sameWaterBody(intel, terrain, a, b));
        }

        SECTION("dry land is not part of any water body")
        {
            auto land = terrain.heightmapIndexToWorldCenter(4, 1);
            REQUIRE(waterRegionAt(intel, terrain, land) == 0);

            auto lake = terrain.heightmapIndexToWorldCenter(0, 1);
            REQUIRE_FALSE(sameWaterBody(intel, terrain, land, lake));
        }
    }

    TEST_CASE("analyseMap shipyard sites")
    {
        SECTION("a basin deep enough for a shipyard gets a nominated site")
        {
            // Land for the top half, water deep enough (depth 50, past the
            // MinWaterDepth=30 both ARMSY and CORSY need) for the bottom
            // half -- an 8x8 footprint fits with room to spare.
            std::vector<std::string> rows(16, std::string(16, 'L'));
            for (std::size_t y = 8; y < 16; ++y)
            {
                rows[y] = std::string(16, 'w');
            }
            auto terrain = terrainFromRows(rows, 100, 0, 50_ss);
            auto intel = analyseMap(terrain, {});

            REQUIRE_FALSE(intel.shipyardSites.empty());

            // The only row of top-left tiles that clears the full 8x8 depth
            // requirement is y=8 (rows 8-15); every x from 0 to 8 fits
            // within the 16-wide basin.
            bool foundExpected = false;
            for (const auto& site : intel.shipyardSites)
            {
                REQUIRE(site.tile.y == 8);
                REQUIRE(site.waterRegion != 0);
                if (site.tile.x == 0)
                {
                    foundExpected = true;
                    // The footprint spans heightmap tiles [0,8) x [8,16), so
                    // its centre is exactly tile (4, 12).
                    auto expectedCorner = terrain.heightmapIndexToWorldCorner(4, 12);
                    REQUIRE(site.position.x == expectedCorner.x);
                    REQUIRE(site.position.z == expectedCorner.z);
                    REQUIRE(site.position.y == terrain.getSeaLevel());
                }
            }
            REQUIRE(foundExpected);
        }

        SECTION("water too shallow for a shipyard gets no site at all")
        {
            // Same shape as above, but the water is only 20 deep against a
            // MinWaterDepth=30 requirement.
            std::vector<std::string> rows(16, std::string(16, 'L'));
            for (std::size_t y = 8; y < 16; ++y)
            {
                rows[y] = std::string(16, 'w');
            }
            auto terrain = terrainFromRows(rows, 100, 30, 50_ss);
            auto intel = analyseMap(terrain, {});

            REQUIRE(intel.shipyardSites.empty());
        }

        SECTION("a map too small for the footprint nominates nothing, rather than reading out of bounds")
        {
            auto terrain = terrainFromRows({"wwww", "wwww", "wwww", "wwww"}, 100, 0, 50_ss);
            auto intel = analyseMap(terrain, {});
            REQUIRE(intel.shipyardSites.empty());
        }
    }

    TEST_CASE("resolveAiSideUnits naval roles")
    {
        MapTerrain terrain(Grid<unsigned char>(4, 4, static_cast<unsigned char>(0)), 0_ss);
        GameSimulation sim(std::move(terrain), 0u, 0, 0);

        SECTION("resolves the real ARM naval unit names when the data defines them")
        {
            sim.unitDefinitions["ARMSY"] = UnitDefinition{};
            sim.unitDefinitions["ARMCS"] = UnitDefinition{};
            sim.unitDefinitions["ARMPT"] = UnitDefinition{};
            sim.unitDefinitions["ARMROY"] = UnitDefinition{};
            sim.unitDefinitions["ARMTSHIP"] = UnitDefinition{};
            sim.unitDefinitions["ARMSUB"] = UnitDefinition{};

            auto units = resolveAiSideUnits(sim, "ARM");
            REQUIRE(units.shipyard == "ARMSY");
            REQUIRE(units.constructionShip == "ARMCS");
            REQUIRE(units.scoutShip == "ARMPT");
            REQUIRE(units.destroyer == "ARMROY");
            REQUIRE(units.seaTransport == "ARMTSHIP");
            REQUIRE(units.submarine == "ARMSUB");
        }

        SECTION("resolves the real CORE naval unit names when the data defines them")
        {
            sim.unitDefinitions["CORSY"] = UnitDefinition{};
            sim.unitDefinitions["CORCS"] = UnitDefinition{};
            sim.unitDefinitions["CORPT"] = UnitDefinition{};
            sim.unitDefinitions["CORROY"] = UnitDefinition{};
            sim.unitDefinitions["CORTSHIP"] = UnitDefinition{};
            sim.unitDefinitions["CORSUB"] = UnitDefinition{};

            auto units = resolveAiSideUnits(sim, "CORE");
            REQUIRE(units.shipyard == "CORSY");
            REQUIRE(units.constructionShip == "CORCS");
            REQUIRE(units.scoutShip == "CORPT");
            REQUIRE(units.destroyer == "CORROY");
            REQUIRE(units.seaTransport == "CORTSHIP");
            REQUIRE(units.submarine == "CORSUB");
        }

        SECTION("a naval unit type the loaded data does not define is left empty, not guessed at")
        {
            // Only the shipyard is defined -- a land-locked mod, or a map
            // pack that stripped the rest -- so nothing else should resolve.
            sim.unitDefinitions["ARMSY"] = UnitDefinition{};

            auto units = resolveAiSideUnits(sim, "ARM");
            REQUIRE(units.shipyard == "ARMSY");
            REQUIRE(units.constructionShip.empty());
            REQUIRE(units.scoutShip.empty());
            REQUIRE(units.destroyer.empty());
            REQUIRE(units.seaTransport.empty());
            REQUIRE(units.submarine.empty());
        }

        SECTION("with no naval data defined at all, every naval field is empty")
        {
            auto units = resolveAiSideUnits(sim, "ARM");
            REQUIRE(units.shipyard.empty());
            REQUIRE(units.constructionShip.empty());
            REQUIRE(units.scoutShip.empty());
            REQUIRE(units.destroyer.empty());
            REQUIRE(units.seaTransport.empty());
            REQUIRE(units.submarine.empty());
        }
    }
}
