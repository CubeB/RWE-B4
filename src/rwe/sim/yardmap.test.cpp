#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitMesh.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace rwe
{
    namespace
    {
        UnitDefinition makeBuilding(
            const std::string& yardMap,
            unsigned int footprintX,
            unsigned int footprintZ,
            unsigned int maxSlope = 10u,
            unsigned int minWaterDepth = 0u,
            unsigned int maxWaterDepth = 255u,
            unsigned int waterLine = 0u,
            bool minWaterDepthPresent = false)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.waterLine = waterLine;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{
                footprintX,
                footprintZ,
                maxSlope,
                maxSlope,
                minWaterDepth,
                maxWaterDepth,
                minWaterDepthPresent};
            d.yardMap = parseYardMap(footprintX, footprintZ, yardMap);
            d.yardMapContainsGeo = d.yardMap->any(isGeo);
            return d;
        }

        SimVector cellCenter(const GameSimulation& sim, int x, int y)
        {
            return sim.terrain.heightmapIndexToWorldCorner(x, y) + SimVector(8_ss, 0_ss, 8_ss);
        }

        /**
         * A mobile unit registered through tryAddUnit, so it is in the
         * occupancy grid. The shared addUnitOfType deliberately does not do
         * that; the point of this one is the grid.
         */
        UnitId addMobileUnitAt(GameSimulation& sim, PlayerId owner, int x, int y, const std::shared_ptr<CobScript>& script)
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["TANK"] = d;

            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "TANK";
            unit.owner = owner;
            unit.position = cellCenter(sim, x, y);
            unit.previousPosition = unit.position;
            unit.hitPoints = 100;
            auto unitId = sim.tryAddUnit(std::move(unit));
            REQUIRE(unitId.has_value());
            return *unitId;
        }

        /** A two-unit rise per column, so each cell is gentle but the footprint is not. */
        MapTerrain makeRampTerrain()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(0));
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 32; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(std::min(255, x * 2)));
                }
            }
            return MapTerrain(std::move(heights), 0_ss);
        }

        FeatureDefinition makeFeature(const std::string& name, bool blocking, bool indestructible, bool geothermal)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.geothermal = geothermal;
            d.indestructible = indestructible;
            d.blocking = blocking;
            return d;
        }
    }

    TEST_CASE("BT-51 maps each yard letter to the original mask byte", "[placement][bt51]")
    {
        auto mask = [](char c) {
            auto cell = parseYardMapCell(c);
            REQUIRE(cell.has_value());
            return yardMapMask(*cell);
        };

        REQUIRE(mask('.') == 0x00u);
        REQUIRE(mask('f') == 0x6Fu);
        REQUIRE(mask('o') == 0x2Fu);
        REQUIRE(mask('c') == 0x2Du);
        REQUIRE(mask('O') == 0x2Bu);
        REQUIRE(mask('w') == 0x37u);
        REQUIRE(mask('C') == 0x35u);
        REQUIRE(mask('y') == 0x29u);
        REQUIRE(mask('Y') == 0x31u);
        REQUIRE(mask('G') == 0x8Fu);

        // The loader skips anything else without counting it as a cell.
        REQUIRE_FALSE(parseYardMapCell('g').has_value());
        REQUIRE_FALSE(parseYardMapCell('x').has_value());
        REQUIRE_FALSE(parseYardMapCell(' ').has_value());
    }

    TEST_CASE("BT-51 a mobile unit blocks a solid yard cell but not a passable one", "[placement][bt51]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayer(sim);
        addMobileUnitAt(sim, player, 9, 9, script);

        auto check = [&](const std::string& yardMap, YardMapCell cell) {
            auto building = makeBuilding(yardMap, 3, 3);
            auto mc = sim.getAdHocMovementClass(building.movementCollisionInfo);
            CAPTURE(yardMap, static_cast<int>(cell));
            return sim.canBeBuiltAt(mc, building.yardMap, building.yardMapContainsGeo, 8, 8, building.waterLine);
        };

        // o, O and c all ask about the occupant; y does not.
        REQUIRE_FALSE(check("ooo ooo ooo", YardMapCell::Ground));
        REQUIRE_FALSE(check("OOO OOO OOO", YardMapCell::GroundPassableWhenClosed));
        REQUIRE_FALSE(check("ccc ccc ccc", YardMapCell::GroundGeoPassableWhenOpen));
        REQUIRE(check("yyy yyy yyy", YardMapCell::GroundPassable));
    }

    TEST_CASE("BT-51 a blocking or indestructible feature is tested only where the letter asks", "[placement][bt51]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto blocking = sim.featureDefinitions.insert(makeFeature("blocking", true, false, false));
        auto indestructible = sim.featureDefinitions.insert(makeFeature("indestructible", false, true, false));
        sim.addFeature(blocking, 9, 9).value();
        sim.addFeature(indestructible, 15, 15).value();

        auto check = [&](const std::string& yardMap, unsigned int x, unsigned int y) {
            auto building = makeBuilding(yardMap, 1, 1);
            auto mc = sim.getAdHocMovementClass(building.movementCollisionInfo);
            CAPTURE(yardMap, x, y);
            return sim.canBeBuiltAt(mc, building.yardMap, building.yardMapContainsGeo, x, y, building.waterLine);
        };

        // 0x20 is on o and f, but not on a gap.
        REQUIRE_FALSE(check("o", 9, 9));
        REQUIRE_FALSE(check("f", 9, 9));
        REQUIRE(check(".", 9, 9));

        // 0x40 is only on f.
        REQUIRE(check("o", 15, 15));
        REQUIRE_FALSE(check("f", 15, 15));
    }

    TEST_CASE("BT-51 the support spread is measured over the whole footprint", "[placement][bt51]")
    {
        // Every cell's own 2x2 block has slope 2, but the rise across the
        // 3x3 footprint is 6: a per-cell rule passes, the support spread does
        // not.
        GameSimulation sim(makeRampTerrain(), 0u, 0, 0);

        auto check = [&](unsigned int maxSlope) {
            auto building = makeBuilding("yyy yyy yyy", 3, 3, maxSlope);
            auto mc = sim.getAdHocMovementClass(building.movementCollisionInfo);
            return sim.canBeBuiltAt(mc, building.yardMap, building.yardMapContainsGeo, 8, 8, building.waterLine);
        };

        REQUIRE_FALSE(check(3u));
        REQUIRE(check(6u));
    }

    TEST_CASE("BT-51 a geothermal letter wants a vent and ignores the feature's blocking flag", "[placement][bt51]")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto vent = sim.featureDefinitions.insert(makeFeature("vent", true, true, true));
        auto shrub = sim.featureDefinitions.insert(makeFeature("shrub", false, true, false));
        sim.addFeature(vent, 9, 9).value();
        sim.addFeature(shrub, 15, 15).value();

        auto check = [&](const std::string& yardMap, unsigned int x, unsigned int y) {
            auto building = makeBuilding(yardMap, 1, 1);
            auto mc = sim.getAdHocMovementClass(building.movementCollisionInfo);
            CAPTURE(yardMap, x, y);
            return sim.canBeBuiltAt(mc, building.yardMap, building.yardMapContainsGeo, x, y, building.waterLine);
        };

        // G has no 0x20/0x40, so a blocking vent is exactly what it wants.
        REQUIRE(check("G", 9, 9));
        REQUIRE_FALSE(check("G", 10, 10));
        REQUIRE_FALSE(check("G", 15, 15));
    }

    TEST_CASE("BT-51 water cells use the clamp and depth limits, not a support spread", "[placement][bt51]")
    {
        auto makeSea = [](unsigned char seabed) {
            Grid<unsigned char> heights(32, 32, seabed);
            return MapTerrain(std::move(heights), 60_ss);
        };

        auto check = [](GameSimulation& sim, const std::string& yardMap, unsigned int minDepth, unsigned int maxDepth, unsigned int waterLine) {
            auto building = makeBuilding(yardMap, 3, 3, 255u, minDepth, maxDepth, waterLine, true);
            auto mc = sim.getAdHocMovementClass(building.movementCollisionInfo);
            return sim.canBeBuiltAt(mc, building.yardMap, building.yardMapContainsGeo, 8, 8, building.waterLine);
        };

        SECTION("deep enough water is accepted")
        {
            GameSimulation sim(makeSea(0), 0u, 0, 0);
            REQUIRE(check(sim, "www www www", 5u, 100u, 8u));
        }

        SECTION("the seabed above the standing height is refused by the clamp")
        {
            GameSimulation sim(makeSea(56), 0u, 0, 0);
            REQUIRE_FALSE(check(sim, "www www www", 5u, 100u, 8u));
        }

        SECTION("too little depth is refused by the minimum-depth test")
        {
            GameSimulation sim(makeSea(50), 0u, 0, 0);
            REQUIRE_FALSE(check(sim, "www www www", 15u, 100u, 8u));
        }

        SECTION("a support cell uses the maximum-depth test")
        {
            GameSimulation sim(makeSea(0), 0u, 0, 0);
            REQUIRE_FALSE(check(sim, "yyy yyy yyy", 0u, 50u, 0u));
            REQUIRE(check(sim, "yyy yyy yyy", 0u, 100u, 0u));
        }
    }
}
