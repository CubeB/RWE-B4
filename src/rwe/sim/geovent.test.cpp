#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/UnitDefinition.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A thermal vent, as every one of them is written in the shipped
         * feature TDFs: a one by one animating sprite that is worth nothing,
         * cannot be shot and declares geothermal.
         */
        FeatureDefinition makeVentDef(const std::string& name)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 0_ss;
            d.geothermal = true;
            d.indestructible = true;
            d.blocking = false;
            d.hitDensity = 0;
            d.damage = 1;
            return d;
        }

        /** Scenery of the same shape that is not a vent, to make sure the vent is what is being found. */
        FeatureDefinition makeShrubDef(const std::string& name)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 0_ss;
            d.geothermal = false;
            d.indestructible = true;
            d.blocking = false;
            d.hitDensity = 0;
            d.damage = 1;
            return d;
        }

        MovementClassDefinition makeBuildingFootprint(unsigned int footprintX, unsigned int footprintZ)
        {
            MovementClassDefinition mc{};
            mc.name = "PLANT";
            mc.footprintX = footprintX;
            mc.footprintZ = footprintZ;
            mc.minWaterDepth = 0;
            mc.maxWaterDepth = 0;
            mc.maxSlope = 255;
            mc.maxWaterSlope = 255;
            return mc;
        }

        /**
         * A geothermal plant's yardmap: solid ground everywhere except the one
         * cell that has to sit over the vent. ARMGEO and CORGEO both write
         * theirs this way, one G in the middle of a three by three of o.
         */
        Grid<YardMapCell> makeGeoPlantYardMap()
        {
            Grid<YardMapCell> yardMap(3, 3, YardMapCell::Ground);
            yardMap.set(1, 1, YardMapCell::Geo);
            return yardMap;
        }
    }

    TEST_CASE("a thermal vent is a feature that declares geothermal", "[geovent]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto ventDef = sim.featureDefinitions.insert(makeVentDef("geovent"));
        auto shrubDef = sim.featureDefinitions.insert(makeShrubDef("shrub"));

        SECTION("steam comes off the vent and off nothing else")
        {
            sim.addFeature(ventDef, 8, 8).value();
            sim.addFeature(shrubDef, 12, 12).value();

            auto points = findGeoVentSteamPoints(sim);

            REQUIRE(points.size() == 1);
        }

        SECTION("the steam leaves from the middle of the vent, on the ground")
        {
            auto ventId = sim.addFeature(ventDef, 8, 8).value();
            const auto& vent = sim.getFeature(ventId);

            auto points = findGeoVentSteamPoints(sim);

            REQUIRE(points.size() == 1);
            REQUIRE(points[0].x == Catch::Approx(vent.position.x.value));
            REQUIRE(points[0].y == Catch::Approx(vent.position.y.value));
            REQUIRE(points[0].z == Catch::Approx(vent.position.z.value));
        }

        SECTION("a map with no vents on it steams not at all")
        {
            sim.addFeature(shrubDef, 12, 12).value();

            REQUIRE(findGeoVentSteamPoints(sim).empty());
        }

        SECTION("every vent on the map steams")
        {
            sim.addFeature(ventDef, 4, 4).value();
            sim.addFeature(ventDef, 8, 8).value();
            sim.addFeature(ventDef, 20, 20).value();

            REQUIRE(findGeoVentSteamPoints(sim).size() == 3);
        }
    }

    TEST_CASE("a vent steams on the original's schedule", "[geovent]")
    {
        // The emitter Init at 0x475150 takes five for its interval and the
        // is-it-finished at 0x475330 is `return 0`, so a vent puffs once every
        // five ticks and never stops. Its puffs rise four times as fast as a
        // damaged unit's smoke: sixteen times the map's gravity at 0x475640
        // against four at 0x475380.

        SECTION("one puff every five ticks")
        {
            REQUIRE(geoVentSteamIntervalTicks == 5);
        }

        SECTION("steam climbs four times as fast as damage smoke")
        {
            REQUIRE(geoVentSteamRiseRate == Catch::Approx(4.0f * 0.5f));
        }
    }

    TEST_CASE("a geothermal plant may only be built on a vent", "[geovent]")
    {
        // What the original checks, at 0x47D684 to 0x47D769: walking the
        // footprint, a yardmap cell marked geo notes that the unit wants a
        // vent, and notes separately whether the feature under that cell has
        // the geothermal bit. At the end, a unit that wanted one and did not
        // find one is refused.

        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto ventDef = sim.featureDefinitions.insert(makeVentDef("geovent"));
        auto shrubDef = sim.featureDefinitions.insert(makeShrubDef("shrub"));

        auto mc = makeBuildingFootprint(3, 3);
        auto yardMap = makeGeoPlantYardMap();

        SECTION("the plant goes down when its geo cell lands on the vent")
        {
            sim.addFeature(ventDef, 9, 9).value();

            REQUIRE(sim.canBeBuiltAt(mc, yardMap, true, 8, 8));
        }

        SECTION("it is refused a square off, where the geo cell misses the vent")
        {
            sim.addFeature(ventDef, 9, 9).value();

            REQUIRE(!sim.canBeBuiltAt(mc, yardMap, true, 7, 8));
            REQUIRE(!sim.canBeBuiltAt(mc, yardMap, true, 8, 7));
        }

        SECTION("it is refused on bare ground")
        {
            REQUIRE(!sim.canBeBuiltAt(mc, yardMap, true, 8, 8));
        }

        SECTION("scenery that is not a vent will not do")
        {
            sim.addFeature(shrubDef, 9, 9).value();

            REQUIRE(!sim.canBeBuiltAt(mc, yardMap, true, 8, 8));
        }

        SECTION("a building with no geo cell in its yardmap is not asked for a vent")
        {
            Grid<YardMapCell> plainYardMap(3, 3, YardMapCell::Ground);

            REQUIRE(sim.canBeBuiltAt(mc, plainYardMap, false, 8, 8));
        }
    }
}
