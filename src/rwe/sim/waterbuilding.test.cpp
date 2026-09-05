#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <vector>

namespace rwe
{
    namespace
    {
        /** Sea bed at 0 and a surface at 60: sixty units of water. */
        MapTerrain makeSeaTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 60_ss);
        }

        Grid<YardMapCell> yardMapOf(unsigned int w, unsigned int h, YardMapCell cell)
        {
            std::vector<YardMapCell> cells(static_cast<std::size_t>(w) * h, cell);
            return Grid<YardMapCell>(w, h, std::move(cells));
        }

        UnitDefinition makeWaterBuilding(unsigned int waterLine, unsigned int footprint)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.maxHitPoints = 500;
            d.buildTime = 0u;
            d.waterLine = waterLine;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{footprint, footprint, 255u, 255u, 0u, 0u};
            d.yardMap = yardMapOf(footprint, footprint, YardMapCell::Water);
            return d;
        }
    }

    TEST_CASE("a building whose yardmap is all water stands at its waterline", "[building]")
    {
        // 0x47D820: walk the footprint, and if any cell is one the building
        // stands on -- the yardmap characters o O c f y G -- take the lowest
        // terrain corner under those. If none of them is, the building is a
        // water building and stands at `seaLevel - waterline` instead.
        //
        // Sixteen shipped buildings take that branch and every one of them
        // declares a Waterline: the shipyards, both tidal generators, the
        // sonar stations, the torpedo launchers, the floating radar and the
        // floating metal makers. It is what puts them in the water rather
        // than on top of it, which a play-test reported them sitting on.
        GameSimulation sim(makeSeaTerrain(), 0u, 0, 0);
        auto footprint = DiscreteRect(20, 20, 3, 3);

        SECTION("a tidal generator sits eight below the surface")
        {
            // ARMTIDE: Waterline 8, three by three.
            auto tide = makeWaterBuilding(8, 3);
            REQUIRE(sim.computeBuildHeight(tide, footprint) == 60_ss - 8_ss);
        }

        SECTION("a Core shipyard sits twenty-two below it")
        {
            // CORSY's Waterline is 22 against ARMSY's 1 -- the asymmetry is
            // in the shipped data, and it is why the two yards look so
            // different in the water.
            auto yard = makeWaterBuilding(22, 3);
            REQUIRE(sim.computeBuildHeight(yard, footprint) == 60_ss - 22_ss);
        }

        SECTION("a waterline of zero puts it exactly at the surface")
        {
            // ARMFRT declares Waterline=0.3, and the original parses that key
            // as an integer, so it is zero: the Arm floating radar sits right
            // on the waterline while the Core one sits four down.
            auto radar = makeWaterBuilding(0, 3);
            REQUIRE(sim.computeBuildHeight(radar, footprint) == 60_ss);
        }

        SECTION("one land cell anywhere in the footprint changes the rule")
        {
            // The switch is not a flag on the unit: it is whether any cell of
            // the yardmap is one the building stands on. With one, the
            // building is levelled onto the ground and the waterline is not
            // consulted at all.
            auto mixed = makeWaterBuilding(8, 3);
            mixed.yardMap->set(1, 1, YardMapCell::Ground);
            REQUIRE(sim.computeBuildHeight(mixed, footprint) == 0_ss);
        }

        SECTION("a building with no yardmap at all stands on the ground")
        {
            auto plain = makeWaterBuilding(8, 3);
            plain.yardMap = std::nullopt;
            REQUIRE(sim.computeBuildHeight(plain, footprint) == 0_ss);
        }
    }
}
