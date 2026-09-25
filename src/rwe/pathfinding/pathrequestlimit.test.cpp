#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        UnitDefinition makeWalkerDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.maxVelocity = SimScalar(1.5f);
            d.acceleration = 0.1_ssf;
            d.brakeRate = 0.1_ssf;
            d.turnRate = 360_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawn(GameSimulation& sim, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = "walker";
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        GameSimulation makeSim()
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            auto player = addPlayer(sim);
            (void)player;
            sim.unitDefinitions["walker"] = makeWalkerDef();
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
            return sim;
        }

        NavigationStateMoving* movingOf(GameSimulation& sim, UnitId id)
        {
            return std::get_if<NavigationStateMoving>(&sim.getUnitState(id).navigationState.state);
        }

        bool onStandIn(GameSimulation& sim, UnitId id)
        {
            auto* moving = movingOf(sim, id);
            return moving != nullptr && moving->pathIsStandIn;
        }
    }

    TEST_CASE("a goal that drifts within the rate limit keeps the route it has", "[pathfinding]")
    {
        // The original polls a navigator for "wants a path" at most once every
        // 60 ticks (WantsPath, 0x44F260). Without that limit an attacker whose
        // stand-off point followed a target re-asked every few ticks, and each
        // ask threw away the search already in flight. A goal that has only
        // drifted is the same goal, so the route in hand is kept and no fresh
        // ask goes in until the limit runs out.
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeSim();
        auto unitId = spawn(sim, PlayerId(0), SimVector(-400_ss, 0_ss, 0_ss), script);
        sim.getUnitState(unitId).orders.push_back(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));

        // Tick until the real route replaces the stand-in, so what follows has
        // a route worth keeping.
        for (int i = 0; i < 120; ++i)
        {
            sim.tick();
            if (!onStandIn(sim, unitId))
            {
                break;
            }
        }
        auto* moving = movingOf(sim, unitId);
        REQUIRE(moving != nullptr);
        REQUIRE(moving->path.has_value());
        REQUIRE_FALSE(moving->pathIsStandIn);

        auto routeTime = moving->path->pathCreationTime;
        auto searchesBefore = sim.pathFindingService.counters.searches;

        // A goal 32 units away: an approach point following a target.
        sim.getUnitState(unitId).orders.clear();
        sim.getUnitState(unitId).orders.push_back(MoveOrder(SimVector(432_ss, 0_ss, 32_ss)));
        sim.tick();

        moving = movingOf(sim, unitId);
        REQUIRE(moving != nullptr);
        REQUIRE(moving->path.has_value());
        REQUIRE(moving->path->pathCreationTime == routeTime);
        REQUIRE_FALSE(moving->pathIsStandIn);
        REQUIRE(sim.pathFindingService.counters.searches == searchesBefore);

        // And once the limit has run out it is asked for after all.
        for (int i = 0; i < 61; ++i)
        {
            sim.tick();
        }
        REQUIRE(sim.pathFindingService.counters.searches > searchesBefore);
    }

    TEST_CASE("a goal somewhere else takes the stand-in at once", "[pathfinding]")
    {
        // A new order is not a drift, and must not be held behind the rate
        // limit: the unit heads for it on the tick it was given, as it always
        // did.
        auto script = makeEmptyCobScript({"base"});
        auto sim = makeSim();
        auto unitId = spawn(sim, PlayerId(0), SimVector(-400_ss, 0_ss, 0_ss), script);
        sim.getUnitState(unitId).orders.push_back(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));

        for (int i = 0; i < 120; ++i)
        {
            sim.tick();
            if (!onStandIn(sim, unitId))
            {
                break;
            }
        }
        REQUIRE(movingOf(sim, unitId) != nullptr);
        REQUIRE_FALSE(movingOf(sim, unitId)->pathIsStandIn);

        sim.getUnitState(unitId).orders.clear();
        sim.getUnitState(unitId).orders.push_back(MoveOrder(SimVector(-400_ss, 0_ss, 400_ss)));
        sim.tick();

        auto* moving = movingOf(sim, unitId);
        REQUIRE(moving != nullptr);
        REQUIRE(moving->path.has_value());
        REQUIRE(moving->pathIsStandIn);
    }
}
