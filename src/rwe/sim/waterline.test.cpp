#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/UnitBehaviorService_util.h>

namespace rwe
{
    // Real numbers out of the shipped unit files: a submarine rides 20 world
    // units down, a tidal generator 8, a shipyard 1. Twenty-one units set the
    // key at all and none of them also sets Floater.
    static const unsigned int SubmarineWaterLine = 20;
    static const unsigned int TidalWaterLine = 8;
    static const unsigned int ShipyardWaterLine = 1;

    TEST_CASE("computeSfxOccupyState: an aircraft is never in the water", "[waterline]")
    {
        // The original gates the whole thing on the movement mode being 1 or
        // 2, which is to say a thing that moves over ground or water.
        REQUIRE(computeSfxOccupyState(100, 100, 0, false, 2) == 0);
        REQUIRE(computeSfxOccupyState(-50, 100, 0, false, 3) == 0);
    }

    TEST_CASE("computeSfxOccupyState: above the surface is state 4", "[waterline]")
    {
        REQUIRE(computeSfxOccupyState(101, 100, 0, true, 0) == 4);
        REQUIRE(computeSfxOccupyState(500, 100, SubmarineWaterLine, true, 2) == 4);
    }

    TEST_CASE("computeSfxOccupyState: a unit floating at zero waterline reads as afloat", "[waterline]")
    {
        // RWE's own ships sit with their origin exactly on the surface and
        // carry no waterline, so unitY + 0 == seaLevel and they report 2 --
        // which is the state a ship's script waits for.
        REQUIRE(computeSfxOccupyState(100, 100, 0, true, 0) == 2);
    }

    TEST_CASE("computeSfxOccupyState: waterline decides where afloat actually is", "[waterline]")
    {
        // This is the whole point of the key. A submarine is afloat when its
        // origin is 20 below the surface, not when it is level with it.
        REQUIRE(computeSfxOccupyState(80, 100, SubmarineWaterLine, true, 0) == 2);
        REQUIRE(computeSfxOccupyState(92, 100, TidalWaterLine, true, 0) == 2);
        REQUIRE(computeSfxOccupyState(99, 100, ShipyardWaterLine, true, 0) == 2);

        // And a submarine level with the surface is not at its waterline.
        REQUIRE(computeSfxOccupyState(100, 100, SubmarineWaterLine, true, 0) != 2);
    }

    TEST_CASE("computeSfxOccupyState: just under the surface is state 1", "[waterline]")
    {
        // The original's window is unitY - seaLevel > -5, so four units down
        // still counts as breaking the surface and five does not.
        REQUIRE(computeSfxOccupyState(96, 100, SubmarineWaterLine, true, 0) == 1);
        REQUIRE(computeSfxOccupyState(99, 100, SubmarineWaterLine, true, 0) == 1);

        // Five down falls through every test, and the original leaves the
        // state alone rather than resetting it.
        REQUIRE(computeSfxOccupyState(95, 100, SubmarineWaterLine, true, 3) == 3);
        REQUIRE(computeSfxOccupyState(95, 100, SubmarineWaterLine, true, 1) == 1);
    }

    TEST_CASE("computeSfxOccupyState: the surface window loses to the waterline test", "[waterline]")
    {
        // The tests run in order and the later ones win. A shipyard one unit
        // down is inside the -5 window, so state 1 is set first and then
        // overwritten by state 2 because that is exactly its waterline.
        REQUIRE(computeSfxOccupyState(99, 100, ShipyardWaterLine, true, 0) == 2);

        // A tidal generator eight down is outside the window, so only the
        // waterline test fires.
        REQUIRE(computeSfxOccupyState(92, 100, TidalWaterLine, true, 0) == 2);
    }

    TEST_CASE("computeSfxOccupyState: the state is sticky where nothing matches", "[waterline]")
    {
        // Deep water with a waterline that does not line up matches none of
        // the tests, and the original seeds its answer with the previous
        // state rather than with zero.
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 2) == 2);
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 4) == 4);
        REQUIRE(computeSfxOccupyState(50, 100, SubmarineWaterLine, true, 0) == 0);
    }

    TEST_CASE("computeSfxOccupyState: a unit climbing out of the water walks up the states", "[waterline]")
    {
        // A submarine surfacing: deep, then at its waterline, then breaking
        // the surface, then clear of it.
        auto state = 0;
        state = computeSfxOccupyState(50, 100, SubmarineWaterLine, true, state);
        REQUIRE(state == 0);

        state = computeSfxOccupyState(80, 100, SubmarineWaterLine, true, state);
        REQUIRE(state == 2);

        state = computeSfxOccupyState(98, 100, SubmarineWaterLine, true, state);
        REQUIRE(state == 1);

        state = computeSfxOccupyState(105, 100, SubmarineWaterLine, true, state);
        REQUIRE(state == 4);
    }
}
