#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <memory>
#include <optional>
#include <rwe/sim/sim_test_util.h>
#include <vector>

/**
 * A builder on the ground faces its work before it starts on it.
 *
 * Reported from a play-test: a construction kbot "should still turn to face or
 * walk towards (if needed) whatever it is, before it begins, repairing,
 * reclaiming". It did neither, because `prepareBuilderForWork` returned at once
 * for anything that could not fly -- every turn it did was for aircraft. The
 * original turns in all nine of its work missions: the bearing to the job
 * (0x48A980), less the unit's own heading (unit+0x66), handed to 0x438590.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFacingTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerFacingModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }
    }

    TEST_CASE("a builder on the ground turns to face its work before starting", "[builderfacing]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFacingTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        registerFacingModel(sim);

        UnitDefinition builder{};
        builder.objectName = "model";
        builder.isMobile = true;
        builder.canMove = false;
        builder.builder = true;
        // Well within reach from where it stands: the only thing it can be
        // waiting on is the turn.
        builder.buildDistance = 200_ss;
        builder.workerTimePerTick = 2u;
        builder.maxHitPoints = 100;
        builder.buildTime = 0u;
        // Slow enough that the turn plainly takes more than a tick.
        builder.turnRate = 500_ss;
        builder.metalStorage = Metal(10000.0f);
        builder.energyStorage = Energy(10000.0f);
        builder.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["builder"] = builder;

        UnitDefinition frameDef{};
        frameDef.objectName = "model";
        // Mobile though it never moves: a building would want a yardmap, and
        // this is only ever something to lathe at.
        frameDef.isMobile = true;
        frameDef.canMove = false;
        frameDef.maxHitPoints = 100;
        // Long enough to still be unfinished when the turn is done.
        frameDef.buildTime = 6000u;
        frameDef.buildCostMetal = Metal(1.0f);
        frameDef.buildCostEnergy = Energy(1.0f);
        frameDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
        sim.unitDefinitions["frame"] = frameDef;

        auto builderId = addUnitOfType(sim, "builder", player, SimVector(200_ss, 0_ss, 200_ss), script);
        auto frameId = addUnitOfType(sim, "frame", player, SimVector(260_ss, 0_ss, 200_ss), script);
        sim.getUnitState(frameId).buildTimeCompleted = 0u;
        sim.getUnitState(builderId).inBuildStance = true;

        // Pointing the opposite way from the job. The bearing is taken from
        // the same function the engine uses, so the test says nothing about
        // which way any particular angle points.
        auto toJob = sim.getUnitState(frameId).position - sim.getUnitState(builderId).position;
        auto facingJob = UnitState::toRotation(toJob);
        auto facingAway = facingJob + HalfTurn;
        sim.getUnitState(builderId).rotation = facingAway;

        sim.getUnitState(builderId).orders.push_back(CompleteBuildOrder(frameId));

        tick(sim, 5);

        // Not working yet -- it is still coming round.
        REQUIRE(std::get_if<UnitBehaviorStateBuilding>(&sim.getUnitState(builderId).behaviourState) == nullptr);

        // But it is coming round: the work facing is asserted every tick it
        // works, and the rotation has moved off where it started.
        REQUIRE(sim.getUnitState(builderId).slowFacePoint.has_value());
        REQUIRE(sim.getUnitState(builderId).rotation != facingAway);

        // Half a turn at five hundred a tick wants about sixty-five of them.
        tick(sim, 90);

        REQUIRE(angleBetweenIsLessOrEqual(sim.getUnitState(builderId).rotation, facingJob, SimAngle(1u << 12u)));
        REQUIRE(std::get_if<UnitBehaviorStateBuilding>(&sim.getUnitState(builderId).behaviourState) != nullptr);
    }
}
