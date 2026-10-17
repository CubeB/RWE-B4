#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <optional>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * How far a builder can reclaim from.
 *
 * Reported from a play-test: "check distances to reclaim, it seems very far".
 * It was: `reclaimTarget` measured centre to centre against a flat 300 under a
 * comment admitting the number was a guess, where an ARMCK's Builddistance is
 * 40 and an air repair pad's is 6.
 *
 * The original uses two different rules, in two different handlers. A unit is
 * reached by the reclaimer's Builddistance -- ReclaimUnit state 4 loads
 * def+0x212 at 0x4048F4 and squares it against dx^2 + dz^2. A feature has no
 * reach test at all: Reclaim state 0 (0x404B5B) installs a move goal on the
 * feature's square (0x438AD0) and the work begins once the move is done.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeReachTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerReachModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** ARMCK's numbers: it reaches 40, and it cannot walk in these cases. */
        UnitDefinition makeShortArmedReclaimerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.builder = true;
            d.canReclamate = true;
            d.buildDistance = 40_ss;
            d.workerTimePerTick = 80u;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.metalStorage = Metal(10000.0f);
            d.energyStorage = Energy(10000.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeScrapDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 100;
            // Expensive enough that the job is still going when the test
            // looks: a reclaimer doing eighty a tick eats a cheap unit in two,
            // and finishing puts the builder back to idle.
            d.buildTime = 6000u;
            d.buildCostMetal = Metal(100.0f);
            d.buildCostEnergy = Energy(50.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        FeatureDefinition makeWreckDef()
        {
            FeatureDefinition d{};
            d.name = "wreck";
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 10_ss;
            d.reclaimable = true;
            d.autoreclaimable = false;
            d.metal = 50;
            d.energy = 0;
            d.blocking = true;
            return d;
        }

        bool isReclaiming(GameSimulation& sim, UnitId id)
        {
            return std::get_if<UnitBehaviorStateReclaiming>(&sim.getUnitState(id).behaviourState) != nullptr;
        }
    }

    TEST_CASE("a builder reclaims a unit only from within its build distance", "[reclaimreach]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeReachTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerReachModel(sim);

        sim.unitDefinitions["builder"] = makeShortArmedReclaimerDef();
        sim.unitDefinitions["scrap"] = makeScrapDef();

        auto scrapId = addUnitOfType(sim, "scrap", player, SimVector(200_ss, 0_ss, 200_ss), script);

        SECTION("two hundred away it does not, though the old flat 300 let it")
        {
            auto builderId = addUnitOfType(sim, "builder", player, SimVector(400_ss, 0_ss, 200_ss), script);
            sim.getUnitState(builderId).inBuildStance = true;
            sim.getUnitState(builderId).orders.push_back(ReclaimOrder(scrapId));

            tick(sim, 10);

            REQUIRE_FALSE(isReclaiming(sim, builderId));
            REQUIRE(sim.getUnitState(scrapId).isAlive());
        }

        SECTION("alongside it, it does")
        {
            auto builderId = addUnitOfType(sim, "builder", player, SimVector(230_ss, 0_ss, 200_ss), script);
            sim.getUnitState(builderId).inBuildStance = true;
            sim.getUnitState(builderId).orders.push_back(ReclaimOrder(scrapId));

            tick(sim, 10);

            REQUIRE(isReclaiming(sim, builderId));
        }
    }

    TEST_CASE("a builder reclaims a feature only once it has reached it", "[reclaimreach]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeReachTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerReachModel(sim);

        sim.unitDefinitions["builder"] = makeShortArmedReclaimerDef();

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto wreckId = sim.addFeature(wreckDef, 20, 20).value();
        auto wreckPosition = sim.getFeature(wreckId).position;

        // Rooted well short of it, and unable to walk: arriving is the test,
        // so it never starts.
        auto builderId = addUnitOfType(sim, "builder", player, wreckPosition + SimVector(200_ss, 0_ss, 0_ss), script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(wreckId));

        tick(sim, 10);

        REQUIRE_FALSE(isReclaiming(sim, builderId));
        REQUIRE(sim.tryGetFeature(wreckId).has_value());
    }

    TEST_CASE("a land builder reclaims a wreck on the sea floor from the shore", "[reclaimreach]")
    {
        // The pathfinder cannot give a walker a way onto deep water, so it
        // settles for the closest it can get, and navigateTo calls that
        // arrival. A feature has no reach test in the original -- arriving is
        // the whole of it -- so the beam goes out from there. RWE used to
        // wait for Builddistance to the footprint, which a wreck out over the
        // deep never satisfied: no beam, no complaint, the order never
        // finished (play-test 2026-10-10).
        auto script = makeEmptyCobScript({"base"});
        Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(100));
        for (int y = 0; y < 64; ++y)
        {
            for (int x = 32; x < 64; ++x)
            {
                heights.set(x, y, static_cast<unsigned char>(0));
            }
        }
        GameSimulation sim(MapTerrain(std::move(heights), 50_ss), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerReachModel(sim);

        auto walkerDef = makeShortArmedReclaimerDef();
        walkerDef.canMove = true;
        walkerDef.maxVelocity = 2_ss;
        walkerDef.acceleration = 1_ss;
        walkerDef.brakeRate = 1_ss;
        walkerDef.turnRate = 1000_ss;
        sim.unitDefinitions["builder"] = walkerDef;

        auto wreckDef = sim.featureDefinitions.insert(makeWreckDef());
        auto wreckId = sim.addFeature(wreckDef, 50, 32).value();

        auto builderId = addUnitOfType(sim, "builder", player, SimVector(400_ss, 100_ss, 520_ss), script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(wreckId));

        bool reclaimed = false;
        for (int i = 0; i < 900 && !reclaimed; ++i)
        {
            sim.tick();
            reclaimed = isReclaiming(sim, builderId) || !sim.tryGetFeature(wreckId).has_value();
        }
        REQUIRE(reclaimed);
    }

    TEST_CASE("a ground builder's reach to a unit adds a third of the target's two extents (BT-92)", "[reclaimreach][bt92]")
    {
        // 0x404730, work check at 0x4048FB: the reach is the build distance
        // plus the integer part of (X extent + Z extent) / 3 of the target,
        // which for the 2x2 scrap here is (32 + 32) / 3 = 21.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeReachTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerReachModel(sim);

        sim.unitDefinitions["builder"] = makeShortArmedReclaimerDef();
        sim.unitDefinitions["scrap"] = makeScrapDef();
        auto scrapId = addUnitOfType(sim, "scrap", player, SimVector(200_ss, 0_ss, 200_ss), script);

        auto reclaimsFrom = [&](float x) {
            auto builderId = addUnitOfType(sim, "builder", player, SimVector(SimScalar(x), 0_ss, 200_ss), script);
            sim.getUnitState(builderId).inBuildStance = true;
            sim.getUnitState(builderId).orders.push_back(ReclaimOrder(scrapId));
            tick(sim, 10);
            return isReclaiming(sim, builderId);
        };

        // 46 from the edge (cells 12 and 13, so x = 224): past 40, inside 40 + 21.
        CHECK(reclaimsFrom(270.0f));
    }

    TEST_CASE("a ground builder's reach to a unit still ends at the extent term (BT-92)", "[reclaimreach][bt92]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeReachTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerReachModel(sim);

        sim.unitDefinitions["builder"] = makeShortArmedReclaimerDef();
        sim.unitDefinitions["scrap"] = makeScrapDef();
        auto scrapId = addUnitOfType(sim, "scrap", player, SimVector(200_ss, 0_ss, 200_ss), script);

        // 66 from the edge (cells 12 and 13, so x = 224): past 40 + 21 = 61.
        auto builderId = addUnitOfType(sim, "builder", player, SimVector(290_ss, 0_ss, 200_ss), script);
        sim.getUnitState(builderId).inBuildStance = true;
        sim.getUnitState(builderId).orders.push_back(ReclaimOrder(scrapId));
        tick(sim, 10);
        CHECK_FALSE(isReclaiming(sim, builderId));
    }
}
