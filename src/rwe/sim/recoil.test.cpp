#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobAngle.h>
#include <rwe/cob/CobAxis.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/util.h>
#include <memory>

namespace rwe
{
    namespace
    {
        // The one angle the original ever passes, so the numbers the exact
        // cases below assert are the ones a real shot produces. It is also
        // big enough that the rock is well clear of the noise in converting a
        // 16-bit turn to radians and back.
        const SimScalar RockAngle = intToSimScalar(rockUnitAngle);

        MapTerrain makeMinimalTerrain()
        {
            Grid<unsigned char> heights(2, 2, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        struct Rock
        {
            /** Where the hull's nose ends up, in the unit's own frame. */
            SimVector nose;

            /** Where the hull's left flank ends up, in the unit's own frame. */
            SimVector leftFlank;

            /** Where the shot went, in the unit's own frame. */
            SimVector shot;

            /** The angles handed to the RockUnit script. */
            std::pair<int, int> angles;
        };

        /**
         * Fires a shot in the given world direction from a unit sitting at the
         * origin at the given rotation, and reports where the recoil leaves the
         * hull. The two turns go through the same handler the COB VM uses, so
         * the axis and sign conventions under test are the engine's own rather
         * than a copy of them.
         */
        Rock fireAndRock(SimAngle unitRotation, const SimVector& shotDirection)
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces = {"base"};

            GameSimulation sim(makeMinimalTerrain(), 0u, 0, 0);
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces{UnitMesh{"base"}};
            UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            sim.getUnitState(unitId).position = SimVector(0_ss, 0_ss, 0_ss);
            sim.getUnitState(unitId).rotation = unitRotation;
            sim.getUnitState(unitId).previousRotation = unitRotation;

            auto angles = computeRockUnitAngles(unitRotation, shotDirection, RockAngle);

            // What rockunit.h does with its two arguments: turn the base piece
            // about x by the first and about z by the second.
            const auto& cobEnv = *sim.getUnitState(unitId).cobEnvironment;
            handlePieceCommand(
                sim,
                cobEnv,
                unitId,
                CobEnvironment::PieceCommandStatus{
                    0u,
                    CobEnvironment::PieceCommandStatus::Turn{CobAxis::X, CobAngle(static_cast<uint16_t>(angles.first)), std::nullopt}});
            handlePieceCommand(
                sim,
                cobEnv,
                unitId,
                CobEnvironment::PieceCommandStatus{
                    0u,
                    CobEnvironment::PieceCommandStatus::Turn{CobAxis::Z, CobAngle(static_cast<uint16_t>(angles.second)), std::nullopt}});

            std::vector<UnitPieceDefinition> pieceDefs{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss)}};
            auto modelDef = createUnitModelDefinition(0_ss, std::move(pieceDefs));
            auto hull = getPieceTransform("base", modelDef, sim.getUnitState(unitId).pieces);

            // The unit is at the origin, so its inverse transform is a plain
            // rotation and carries the world shot direction into hull space.
            return Rock{
                hull * SimVector(0_ss, 0_ss, 1_ss),
                hull * SimVector(1_ss, 0_ss, 0_ss),
                sim.getUnitState(unitId).getInverseTransform() * shotDirection,
                angles};
        }
    }

    TEST_CASE("recoil rocks the hull away from the shot", "[recoil]")
    {
        // The unit's nose is its local +z and its left flank its local +x, so
        // a unit at rotation zero faces world +z and its right is world -x.

        SECTION("firing forward lifts the nose and squats the tail")
        {
            auto rock = fireAndRock(SimAngle(0), SimVector(0_ss, 0_ss, 1_ss));

            REQUIRE(rock.nose.y.value > 0.0f);
            REQUIRE(rock.leftFlank.y.value == Catch::Approx(0.0f).margin(1e-4));

            // Dead ahead is the one case where the numbers are exact: the
            // whole rock is a backwards pitch about the x axis and nothing
            // about the z.
            REQUIRE(rock.angles.first == -rockUnitAngle);
            REQUIRE(rock.angles.second == 0);
        }

        SECTION("firing right rolls the hull left, lifting the right flank")
        {
            auto rock = fireAndRock(SimAngle(0), SimVector(-1_ss, 0_ss, 0_ss));

            // Rolling to the left means the left flank drops and the right,
            // the side the round left by, comes up.
            REQUIRE(rock.leftFlank.y.value < 0.0f);
            REQUIRE(rock.nose.y.value == Catch::Approx(0.0f).margin(1e-4));
        }

        SECTION("firing backwards squats the nose")
        {
            auto rock = fireAndRock(SimAngle(0), SimVector(0_ss, 0_ss, -1_ss));

            REQUIRE(rock.nose.y.value < 0.0f);
        }

        SECTION("a unit turned side on to the shot pitches, not rolls")
        {
            // Facing world +x, so firing along world +x is firing straight
            // ahead and must rock the hull exactly as the first case did. A
            // rock computed in world space instead of the unit's own frame
            // would roll this one over onto its side instead.
            auto rock = fireAndRock(QuarterTurn, SimVector(1_ss, 0_ss, 0_ss));

            REQUIRE(rock.nose.y.value > 0.0f);
            REQUIRE(rock.leftFlank.y.value == Catch::Approx(0.0f).margin(1e-4));
        }

        SECTION("a unit turned side on firing across itself rolls, not pitches")
        {
            // Facing world +x with the shot going to world +z, which is its
            // right, so the right flank should come up as it did above.
            auto rock = fireAndRock(QuarterTurn, SimVector(0_ss, 0_ss, 1_ss));

            REQUIRE(rock.leftFlank.y.value < 0.0f);
            REQUIRE(rock.nose.y.value == Catch::Approx(0.0f).margin(1e-4));
        }

        SECTION("the hull always heels the way the round went, whatever the heading")
        {
            // The general statement the cases above are instances of: the
            // flank the shot left by is the one that rises. Sweep every
            // sixteenth of a turn of hull heading against every sixteenth of
            // a turn of shot bearing.
            for (int heading = 0; heading < 16; ++heading)
            {
                for (int bearing = 0; bearing < 16; ++bearing)
                {
                    auto unitRotation = SimAngle(static_cast<uint16_t>(heading * 4096));
                    auto shotDirection = UnitState::toDirection(SimAngle(static_cast<uint16_t>(bearing * 4096)));
                    auto rock = fireAndRock(unitRotation, shotDirection);

                    // Take the shot direction along with the hull and see
                    // which way it went: away from the shot means up.
                    auto rocked = (rock.nose * rock.shot.z) + (rock.leftFlank * rock.shot.x);
                    REQUIRE(rocked.y.value > 0.0f);
                }
            }
        }
    }

    TEST_CASE("recoil is the same size for every weapon", "[recoil]")
    {
        // The original pushes the literal 0x320 at all three of its projectile
        // spawn routines and passes it straight to RockUnit, so a Peewee's
        // machine gun heaves its hull exactly as far as a Bulldog's cannon
        // does. RWE used to invent an angle from the weapon's damage instead,
        // which put a light gun at 141 units and capped a heavy one at 900.

        SECTION("the angle is the constant the original passes")
        {
            REQUIRE(rockUnitAngle == 800);
        }

        SECTION("a shot dead ahead pitches by exactly that much and rolls not at all")
        {
            auto angles = computeRockUnitAngles(SimAngle(0), SimVector(0_ss, 0_ss, 1_ss), RockAngle);

            REQUIRE(angles.first == -800);
            REQUIRE(angles.second == 0);
        }

        SECTION("a shot off the right beam rolls by exactly that much and pitches not at all")
        {
            // The unit faces world +z, so its right is world -x.
            auto angles = computeRockUnitAngles(SimAngle(0), SimVector(-1_ss, 0_ss, 0_ss), RockAngle);

            REQUIRE(angles.first == 0);
            REQUIRE(angles.second == 800);
        }
    }

    TEST_CASE("only a unit whose script defines RockUnit recoils", "[recoil]")
    {
        // The engine asks for RockUnit on every unit that fires; what decides
        // whether anything happens is whether the script has one. The
        // original looks the name up in the script's own name table
        // (0x4B0A70), gets -1 when it is absent, and 0x4B0B00 returns without
        // starting a thread. Only seventeen of the two hundred shipped
        // scripts include rockunit.h, which is why a Peewee stands still and
        // a Stumpy rocks.

        SECTION("a script with no RockUnit starts nothing")
        {
            CobScript script;
            script.staticVariableCount = 0;
            script.pieces = {"base"};
            script.functions = {CobFunctionInfo{"FirePrimary", 0}};
            script.instructions = {0};

            CobEnvironment env(&script);
            auto thread = env.createThread("RockUnit", {-800, 0});

            REQUIRE(!thread.has_value());
            REQUIRE(env.threads.empty());
        }

        SECTION("a script that defines one gets a thread with both angles")
        {
            CobScript script;
            script.staticVariableCount = 0;
            script.pieces = {"base"};
            script.functions = {CobFunctionInfo{"FirePrimary", 0}, CobFunctionInfo{"RockUnit", 1}};
            script.instructions = {0, 0};

            CobEnvironment env(&script);
            auto thread = env.createThread("RockUnit", {-800, 0});

            REQUIRE(thread.has_value());
            REQUIRE(env.threads.size() == 1);
            REQUIRE((*thread)->callStack.top().locals.at(0) == -800);
            REQUIRE((*thread)->callStack.top().locals.at(1) == 0);
        }
    }
}
