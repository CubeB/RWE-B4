#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <optional>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * Placing a structure on top of an enemy building nobody here has discovered.
 *
 * Reported from a play-test, and settled by testing the original directly: TA
 * allows the placement over an enemy structure that is not in view. RWE
 * refused it, which told the player something was standing there -- see
 * TOTALA-EXE.md §27's correction, which also records that the static reading
 * of the click gate had this wrong.
 *
 * The rule has two halves and they must not be confused. The simulation's own
 * occupancy test answers the same for every peer and still refuses, because
 * lockstep depends on it; what becomes fog-aware is the interface's question,
 * and the build then fails on arrival with the constructor message.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFogTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerFogModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * A building: it needs a yardmap, and it occupies the ground.
         *
         * Registered under an upper-case name because createUnit upper-cases
         * the type before tryAddUnit looks it up again, so a lower-case key is
         * invisible to the spawn path -- and throws rather than failing the
         * assertion you were looking at.
         */
        UnitDefinition makeHutDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = false;
            d.canMove = false;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            d.yardMap = Grid<YardMapCell>(2, 2, YardMapCell::Ground);
            return d;
        }

        UnitDefinition makeScoutDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 300u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }
    }

    TEST_CASE("an enemy building nobody here has seen does not refuse the placement", "[fogplacement]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFogTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        registerFogModel(sim);

        sim.unitDefinitions["HUT"] = makeHutDef();
        sim.unitScriptDefinitions["HUT"] = *script;
        sim.unitDefinitions["SCOUT"] = makeScoutDef();
        sim.unitScriptDefinitions["SCOUT"] = *script;

        // Spawned the real way, so that it stamps the occupancy grid: the
        // shared test helper puts a unit in the list without doing that, and
        // a building that does not occupy anything would make this test
        // measure nothing at all.
        SimVector theirSite(200_ss, 0_ss, 200_ss);
        auto theirsId = sim.trySpawnUnit("HUT", them, theirSite, std::nullopt).value();

        sim.tick();

        const auto& hutDefinition = sim.unitDefinitions.at("HUT");
        auto mc = sim.getAdHocMovementClass(hutDefinition.movementCollisionInfo);
        auto rect = sim.computeFootprintRegion(theirSite, hutDefinition.movementCollisionInfo);

        REQUIRE_FALSE(sim.canSeeUnit(us, theirsId));

        // The simulation knows the ground is taken, and answers the same for
        // every peer. That is what a build attempt runs into when it arrives.
        REQUIRE_FALSE(sim.canBeBuiltAt(mc, hutDefinition.yardMap, hutDefinition.yardMapContainsGeo, rect.x, rect.y));

        // What we are allowed to see is another matter: refusing the box here
        // would tell us something is standing there.
        REQUIRE(sim.canBeBuiltAtAsSeenBy(mc, hutDefinition.yardMap, hutDefinition.yardMapContainsGeo, rect.x, rect.y, us));

        // Its owner was never blind to it.
        REQUIRE_FALSE(sim.canBeBuiltAtAsSeenBy(mc, hutDefinition.yardMap, hutDefinition.yardMapContainsGeo, rect.x, rect.y, them));

        SECTION("and once it has been seen it refuses like anything else")
        {
            sim.trySpawnUnit("SCOUT", us, SimVector(120_ss, 0_ss, 200_ss), std::nullopt).value();
            sim.tick();

            REQUIRE(sim.canSeeUnit(us, theirsId));
            REQUIRE_FALSE(sim.canBeBuiltAtAsSeenBy(mc, hutDefinition.yardMap, hutDefinition.yardMapContainsGeo, rect.x, rect.y, us));
        }
    }
}
