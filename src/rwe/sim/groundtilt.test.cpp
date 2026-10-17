#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitModelVertices.h>
#include <rwe/sim/UnitPieceDefinition.h>
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

    TEST_CASE("computeSelectionAlignment samples the model's selection rectangle (BT-41)", "[groundtilt][bt41]")
    {
        // 0x48A490: four vertices of the root's selection primitive, the mean
        // of their heights for the unit's own, and a pitch and bank over the
        // rectangle's length and width. The ramp rises with world z where
        // makeRampTerrain says it does; read off the terrain itself.
        auto terrain = makeRampTerrain();
        auto centre = SimVector(0_ss, 0_ss, 0_ss);
        auto ahead = terrain.getHeightAt(centre.x, centre.z + 32_ss);
        auto behind = terrain.getHeightAt(centre.x, centre.z - 32_ss);
        REQUIRE(ahead != behind);
        auto risesAhead = ahead > behind;

        // A rectangle that is not the footprint: 20 across, 60 along, and not
        // centred on the model's origin (it runs from -10 to +50 along the nose).
        const std::array<SimVector, 4> usual{
            SimVector(-10_ss, 0_ss, -10_ss),
            SimVector(10_ss, 0_ss, -10_ss),
            SimVector(10_ss, 0_ss, 50_ss),
            SimVector(-10_ss, 0_ss, 50_ss)};

        SECTION("the height is the mean of the four, which on a plane is the height at their centre")
        {
            auto aligned = computeSelectionAlignment(terrain, centre, SimAngle(0), usual, false);
            REQUIRE(aligned.has_value());
            auto expected = terrain.getHeightAt(0_ss, 20_ss);
            REQUIRE(aligned->y.value == Catch::Approx(expected.value).margin(0.01f));
            REQUIRE((aligned->pitch > 0_ss) == risesAhead);
            REQUIRE(rweAbs(aligned->roll).value < 0.001f);
        }

        SECTION("the order the corners are listed in does not turn the hull about the wrong axis")
        {
            const std::array<SimVector, 4> shuffled{usual[1], usual[3], usual[0], usual[2]};
            auto a = computeSelectionAlignment(terrain, centre, SimAngle(0), usual, false);
            auto b = computeSelectionAlignment(terrain, centre, SimAngle(0), shuffled, false);
            REQUIRE(a.has_value());
            REQUIRE(b.has_value());
            REQUIRE(b->pitch.value == Catch::Approx(a->pitch.value).margin(0.0001f));
            REQUIRE(b->roll.value == Catch::Approx(a->roll.value).margin(0.0001f));
            REQUIRE(b->y.value == Catch::Approx(a->y.value).margin(0.0001f));
        }

        SECTION("turned across the ramp it is roll, not pitch")
        {
            auto aligned = computeSelectionAlignment(terrain, centre, SimAngle(0x4000), usual, false);
            REQUIRE(aligned.has_value());
            REQUIRE(rweAbs(aligned->pitch).value < 0.001f);
            REQUIRE((aligned->roll < 0_ss) == risesAhead);
        }

        SECTION("the last column or row of the map leaves the unit as it was")
        {
            auto edge = terrain.heightmapIndexToWorldCenter(31, 10);
            REQUIRE_FALSE(computeSelectionAlignment(terrain, edge, SimAngle(0), usual, false).has_value());
            auto inside = terrain.heightmapIndexToWorldCenter(10, 10);
            REQUIRE(computeSelectionAlignment(terrain, inside, SimAngle(0), usual, false).has_value());
        }
    }

    TEST_CASE("a ground unit is aligned to its model's selection rectangle, and left alone without one (BT-41)", "[groundtilt][bt41]")
    {
        auto script = makeEmptyCobScript({"base"});

        auto spawn = [&](GameSimulation& sim, PlayerId owner, const SimVector& pos) {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "tank";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            return sim.tryAddUnit(std::move(unit)).value();
        };
        auto registerModel = [&](GameSimulation& sim, std::optional<std::array<SimVector, 4>> rectangle) {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            auto definition = createUnitModelDefinition(10_ss, std::move(pieces));
            auto vertices = std::make_shared<UnitModelVertices>();
            vertices->perPiece.emplace_back();
            vertices->selectionRectangle = rectangle;
            definition.vertices = vertices;
            sim.unitModelDefinitions["model"] = std::move(definition);
        };

        const std::array<SimVector, 4> rectangle{
            SimVector(-8_ss, 0_ss, -30_ss),
            SimVector(8_ss, 0_ss, -30_ss),
            SimVector(8_ss, 0_ss, 30_ss),
            SimVector(-8_ss, 0_ss, 30_ss)};

        SECTION("height and tilt come from the rectangle")
        {
            GameSimulation sim(makeRampTerrain(), 0u, 0, 0);
            auto player = addPlayer(sim);
            sim.unitDefinitions["tank"] = makeTankDef(false);
            registerModel(sim, rectangle);
            auto id = spawn(sim, player, SimVector(0_ss, 0_ss, 0_ss));
            sim.tick();

            auto aligned = computeSelectionAlignment(sim.terrain, sim.getUnitState(id).position, sim.getUnitState(id).rotation, rectangle, false);
            REQUIRE(aligned.has_value());
            REQUIRE(groundOf(sim, id).pitch == aligned->pitch);
            REQUIRE(sim.getUnitState(id).position.y.value == Catch::Approx(aligned->y.value).margin(0.01f));
        }

        SECTION("a model read from a 3DO with no selection primitive keeps its height and tilt")
        {
            GameSimulation sim(makeRampTerrain(), 0u, 0, 0);
            auto player = addPlayer(sim);
            sim.unitDefinitions["tank"] = makeTankDef(false);
            registerModel(sim, std::nullopt);
            auto id = spawn(sim, player, SimVector(0_ss, 7_ss, 0_ss));
            sim.tick();

            REQUIRE(groundOf(sim, id).pitch == 0_ss);
            REQUIRE(sim.getUnitState(id).position.y == 7_ss);
        }
    }
}
