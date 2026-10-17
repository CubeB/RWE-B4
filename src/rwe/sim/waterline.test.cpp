#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobOpCode.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    // Real numbers out of the shipped unit files: a submarine rides 20 world
    // units down, a tidal generator 8, a shipyard 1. Twenty-one units set the
    // key at all and none of them also sets Floater.
    static const unsigned int SubmarineWaterLine = 20;
    static const unsigned int TidalWaterLine = 8;
    static const unsigned int ShipyardWaterLine = 1;

    // A model so tall that state 3 can never fire, for the cases about the other four.
    static const int TallModel = 100000;

    TEST_CASE("computeSfxOccupyState: an aircraft is never in the water", "[waterline]")
    {
        // The original gates the whole thing on the movement mode being 1 or
        // 2, which is to say a thing that moves over ground or water.
        REQUIRE(computeSfxOccupyState(100, 100, 0, false, 2, TallModel) == 0);
        REQUIRE(computeSfxOccupyState(-50, 100, 0, false, 3, TallModel) == 0);
    }

    TEST_CASE("computeSfxOccupyState: above the surface is state 4", "[waterline]")
    {
        REQUIRE(computeSfxOccupyState(101, 100, 0, true, 0, TallModel) == 4);
        REQUIRE(computeSfxOccupyState(500, 100, SubmarineWaterLine, true, 2, TallModel) == 4);
    }

    TEST_CASE("computeSfxOccupyState: a unit floating at zero waterline reads as afloat", "[waterline]")
    {
        // RWE's own ships sit with their origin exactly on the surface and
        // carry no waterline, so unitY + 0 == seaLevel and they report 2 --
        // which is the state a ship's script waits for.
        REQUIRE(computeSfxOccupyState(100, 100, 0, true, 0, TallModel) == 2);
    }

    TEST_CASE("computeSfxOccupyState: waterline decides where afloat actually is", "[waterline]")
    {
        // This is the whole point of the key. A submarine is afloat when its
        // origin is 20 below the surface, not when it is level with it.
        REQUIRE(computeSfxOccupyState(80, 100, SubmarineWaterLine, true, 0, TallModel) == 2);
        REQUIRE(computeSfxOccupyState(92, 100, TidalWaterLine, true, 0, TallModel) == 2);
        REQUIRE(computeSfxOccupyState(99, 100, ShipyardWaterLine, true, 0, TallModel) == 2);

        // And a submarine level with the surface is not at its waterline.
        REQUIRE(computeSfxOccupyState(100, 100, SubmarineWaterLine, true, 0, TallModel) != 2);
    }

    TEST_CASE("computeSfxOccupyState: just under the surface is state 1", "[waterline]")
    {
        // The original's window is unitY - seaLevel > -5, so four units down
        // still counts as breaking the surface and five does not.
        REQUIRE(computeSfxOccupyState(96, 100, SubmarineWaterLine, true, 0, TallModel) == 1);
        REQUIRE(computeSfxOccupyState(99, 100, SubmarineWaterLine, true, 0, TallModel) == 1);

        // Five down falls through every test, and the original leaves the
        // state alone rather than resetting it.
        REQUIRE(computeSfxOccupyState(95, 100, SubmarineWaterLine, true, 3, TallModel) == 3);
        REQUIRE(computeSfxOccupyState(95, 100, SubmarineWaterLine, true, 1, TallModel) == 1);
    }

    TEST_CASE("computeSfxOccupyState: the surface window loses to the waterline test", "[waterline]")
    {
        // The tests run in order and the later ones win. A shipyard one unit
        // down is inside the -5 window, so state 1 is set first and then
        // overwritten by state 2 because that is exactly its waterline.
        REQUIRE(computeSfxOccupyState(99, 100, ShipyardWaterLine, true, 0, TallModel) == 2);

        // A tidal generator eight down is outside the window, so only the
        // waterline test fires.
        REQUIRE(computeSfxOccupyState(92, 100, TidalWaterLine, true, 0, TallModel) == 2);
    }

    TEST_CASE("computeSfxOccupyState: the state is sticky where nothing matches", "[waterline]")
    {
        // Deep water with a waterline that does not line up matches none of
        // the tests, and the original seeds its answer with the previous
        // state rather than with zero.
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 2, TallModel) == 2);
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 4, TallModel) == 4);
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 0, TallModel) == 0);
    }

    TEST_CASE("computeSfxOccupyState: a unit climbing out of the water walks up the states", "[waterline]")
    {
        // A submarine surfacing: deep, then at its waterline, then breaking
        // the surface, then clear of it.
        auto state = 0;
        state = computeSfxOccupyState(50, 100, SubmarineWaterLine, true, state, TallModel);
        REQUIRE(state == 0);

        state = computeSfxOccupyState(80, 100, SubmarineWaterLine, true, state, TallModel);
        REQUIRE(state == 2);

        state = computeSfxOccupyState(98, 100, SubmarineWaterLine, true, state, TallModel);
        REQUIRE(state == 1);

        state = computeSfxOccupyState(105, 100, SubmarineWaterLine, true, state, TallModel);
        REQUIRE(state == 4);
    }

    TEST_CASE("computeSfxOccupyState: a hull wholly under the surface is state 3", "[waterline]")
    {
        // BT-7, BT-11: `unitY + modelTop < seaLevel`, strictly, tested last so
        // that it overrides states 1 and 2.
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 0, 10) == 3);

        // Exactly at the surface is not under it.
        REQUIRE(computeSfxOccupyState(90, 100, SubmarineWaterLine, true, 0, 10) == 0);

        // At its waterline a submarine whose top is below the surface reads 3,
        // not the 2 the waterline test would have given it.
        REQUIRE(computeSfxOccupyState(80, 100, SubmarineWaterLine, true, 0, 10) == 3);

        // The same boat with a tall conning tower is merely afloat.
        REQUIRE(computeSfxOccupyState(80, 100, SubmarineWaterLine, true, 0, 30) == 2);

        // Above the surface is 4 whatever the model says.
        REQUIRE(computeSfxOccupyState(101, 100, SubmarineWaterLine, true, 3, 10) == 4);
    }

    namespace
    {
        /**
         * Puts a unit that is never ordered anywhere into a world with the sea
         * at zero and says what `setSFXoccupy` it was told after one tick.
         */
        struct Occupancy
        {
            int stored;
            int threadsStarted;
            int argument;
        };

        Occupancy tellOneIdleUnit(int waterLine, int modelHeight, SimScalar y)
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            auto owner = addPlayer(sim, "us");

            UnitDefinition d{};
            d.objectName = "HULL";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.waterLine = static_cast<unsigned int>(waterLine);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["HULL"] = d;

            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["HULL"] = createUnitModelDefinition(intToSimScalar(modelHeight), std::move(pieces));

            // A script that keeps its setSFXoccupy thread alive to be counted.
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces = {"base"};
            script->functions = {CobFunctionInfo{"setSFXoccupy", 0}};
            script->instructions = {
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT), 1000000u, static_cast<uint32_t>(OpCode::SLEEP), static_cast<uint32_t>(OpCode::RETURN)};

            auto id = addUnitOfType(sim, "HULL", owner, SimVector(100_ss, y, 100_ss), script);
            sim.tick();

            const auto& state = sim.getUnitState(id);
            Occupancy result{state.sfxOccupyState, 0, -1};
            for (const auto& t : state.cobEnvironment->threads)
            {
                if (t->name == "setSFXoccupy")
                {
                    ++result.threadsStarted;
                    result.argument = t->callStack.top().locals.at(0);
                }
            }
            return result;
        }
    }

    TEST_CASE("a mobile unit that was never ordered anywhere is told its water state on its first tick", "[waterline]")
    {
        // BT-11. The occupancy routine is the last step of the mover's update
        // and has no condition of its own, so it runs every tick for every
        // mobile unit, moving or not. RWE asked only from the step that
        // applied a movement.

        SECTION("a hull wholly under the surface is told 3")
        {
            auto told = tellOneIdleUnit(20, 10, -20_ss);
            REQUIRE(told.stored == 3);
            REQUIRE(told.threadsStarted == 1);
            REQUIRE(told.argument == 3);
        }

        SECTION("a ship riding at its waterline with its top out of the water is told 2")
        {
            auto told = tellOneIdleUnit(20, 30, -20_ss);
            REQUIRE(told.stored == 2);
            REQUIRE(told.threadsStarted == 1);
            REQUIRE(told.argument == 2);
        }

        SECTION("a unit on dry land is told 4")
        {
            auto told = tellOneIdleUnit(0, 10, 50_ss);
            REQUIRE(told.stored == 4);
            REQUIRE(told.threadsStarted == 1);
            REQUIRE(told.argument == 4);
        }
    }
}
