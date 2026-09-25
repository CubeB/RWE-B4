#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <cmath>

namespace rwe
{
    namespace
    {
        /** A ramp: the ground rises one height unit per heightmap row going down the map. */
        MapTerrain makeRampTerrain()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(0));
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 32; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(y * 4));
                }
            }
            return MapTerrain(std::move(heights), 0_ss);
        }

        UnitDefinition makeTankDef(bool upright)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.upright = upright;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        const UnitPhysicsInfoGround& groundOf(const GameSimulation& sim, UnitId id)
        {
            return std::get<UnitPhysicsInfoGround>(sim.getUnitState(id).physics);
        }
    }

    TEST_CASE("computeGroundTilt reads the slope under the footprint", "[groundtilt]")
    {
        // 0x48A490: four rotated footprint corners, pitch from the front pair
        // against the back pair, roll from the right pair against the left.
        auto terrain = makeRampTerrain();
        auto centre = SimVector(0_ss, 0_ss, 0_ss);
        // Which way the ramp rises in world z, read off the terrain itself so
        // the case does not depend on which way the heightmap's rows run.
        auto ahead = terrain.getHeightAt(centre.x, centre.z + 32_ss);
        auto behind = terrain.getHeightAt(centre.x, centre.z - 32_ss);
        REQUIRE(ahead != behind);
        auto risesAhead = ahead > behind;

        SECTION("facing up or down the ramp is pitch and no roll")
        {
            auto tilt = computeGroundTilt(terrain, centre, SimAngle(0), 16_ss, 16_ss, false);
            REQUIRE((tilt.pitch > 0_ss) == risesAhead);
            REQUIRE(rweAbs(tilt.roll).value < 0.001f);

            // Turned round, the same slope is the other way up.
            auto back = computeGroundTilt(terrain, centre, SimAngle(0x8000), 16_ss, 16_ss, false);
            REQUIRE(back.pitch.value == Catch::Approx(-tilt.pitch.value).margin(0.001f));
            REQUIRE(rweAbs(back.roll).value < 0.001f);
        }

        SECTION("facing across the ramp is roll and no pitch")
        {
            // Heading +x: the unit's right hand is -z.
            auto tilt = computeGroundTilt(terrain, centre, SimAngle(0x4000), 16_ss, 16_ss, false);
            REQUIRE(rweAbs(tilt.pitch).value < 0.001f);
            // Right side down when the ground falls towards -z, i.e. when it rises ahead in +z.
            REQUIRE((tilt.roll < 0_ss) == risesAhead);
        }

        SECTION("flat ground is level")
        {
            auto flat = makeFlatTerrain(32, 32);
            auto tilt = computeGroundTilt(flat, centre, SimAngle(1234), 16_ss, 16_ss, false);
            REQUIRE(tilt.pitch == 0_ss);
            REQUIRE(tilt.roll == 0_ss);
        }
    }

    TEST_CASE("a ground unit tilts to the ground under it unless it is upright", "[groundtilt]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeRampTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["tank"] = makeTankDef(false);
        sim.unitDefinitions["mast"] = makeTankDef(true);
        auto tankId = addUnitOfType(sim, "tank", player, SimVector(0_ss, 0_ss, 0_ss), script);
        auto mastId = addUnitOfType(sim, "mast", player, SimVector(64_ss, 0_ss, 0_ss), script);

        sim.tick();

        // The placement runs every tick, moving or not (0x48A870).
        REQUIRE(groundOf(sim, tankId).pitch != 0_ss);
        REQUIRE(rweAbs(groundOf(sim, tankId).roll).value < 0.001f);

        // upright: one sample under the centre, no tilt (0x48A8BF).
        REQUIRE(groundOf(sim, mastId).pitch == 0_ss);
        REQUIRE(groundOf(sim, mastId).roll == 0_ss);

        // And the previous values follow a tick behind, for the drawing.
        auto before = groundOf(sim, tankId).pitch;
        sim.tick();
        REQUIRE(groundOf(sim, tankId).previousPitch == before);
    }
}
