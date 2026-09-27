#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>

/**
 * The acid in the sea of the Core Contingency's acid worlds (issue #378):
 * the map's `waterdamage` off every unit in the sea once a second, when the
 * map sets `waterdoesdamage` (0x48AED3-0x48AF32).
 */
namespace rwe
{
    namespace
    {
        const SimScalar SeaLevel = 20_ss;

        MapTerrain makeFloodedTerrain()
        {
            Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), SeaLevel);
        }

        struct AcidWorld
        {
            GameSimulation sim{makeFloodedTerrain(), 0u, 0, 0};
            PlayerId owner = addPlayer(sim);
            std::shared_ptr<CobScript> script = makeEmptyCobScript();

            AcidWorld()
            {
                sim.waterDamage = 10;
                UnitDefinition wader{};
                wader.maxHitPoints = 100;
                wader.isMobile = true;
                wader.buildTime = 1;
                sim.unitDefinitions["WADER"] = wader;
                auto hover = wader;
                hover.canHover = true;
                sim.unitDefinitions["HOVER"] = hover;
            }

            UnitId add(const std::string& type, SimScalar y)
            {
                return addUnitOfType(sim, type, owner, SimVector(0_ss, y, 0_ss), script);
            }

            unsigned int hitPoints(UnitId id) const
            {
                return sim.getUnitState(id).hitPoints;
            }

            void runTick(unsigned int tick)
            {
                sim.gameTime = GameTime(tick);
                sim.updateWaterDamage();
            }
        };
    }

    TEST_CASE("the acid takes waterdamage off a unit in the sea once a second", "[water][acid]")
    {
        AcidWorld world;
        auto wader = world.add("WADER", 0_ss);

        world.runTick(30);
        CHECK(world.hitPoints(wader) == 90u);

        // The game's tick counter a multiple of thirty, and no other tick.
        for (unsigned int tick = 31; tick < 60; ++tick)
        {
            world.runTick(tick);
        }
        CHECK(world.hitPoints(wader) == 90u);

        world.runTick(60);
        CHECK(world.hitPoints(wader) == 80u);
    }

    TEST_CASE("a hovercraft rides over the acid", "[water][acid]")
    {
        AcidWorld world;
        auto hover = world.add("HOVER", SeaLevel);
        world.runTick(30);
        CHECK(world.hitPoints(hover) == 100u);
    }

    TEST_CASE("the acid reaches a unit whose height is at sea level in whole units", "[water][acid]")
    {
        // The original compares the integer part of the unit's height with
        // sea level, so anything short of a whole unit above it is in.
        AcidWorld world;
        auto justUnder = world.add("WADER", SeaLevel + SimScalar(0.9f));
        auto above = world.add("WADER", SeaLevel + 1_ss);
        world.runTick(30);
        CHECK(world.hitPoints(justUnder) == 90u);
        CHECK(world.hitPoints(above) == 100u);
    }

    TEST_CASE("a map that does not set waterdoesdamage has no acid", "[water][acid]")
    {
        AcidWorld world;
        world.sim.waterDamage = 0;
        auto wader = world.add("WADER", 0_ss);
        world.runTick(30);
        CHECK(world.hitPoints(wader) == 100u);
    }

    TEST_CASE("the acid is a hit like any other to armour", "[water][acid]")
    {
        // Through 0x489BB0, so an armoured unit takes it at its
        // DamageModifier: half, here.
        AcidWorld world;
        world.sim.unitDefinitions["WADER"].damageModifier = 0x8000;
        auto wader = world.add("WADER", 0_ss);
        world.sim.getUnitState(wader).armored = true;
        world.runTick(30);
        CHECK(world.hitPoints(wader) == 95u);
    }
}
