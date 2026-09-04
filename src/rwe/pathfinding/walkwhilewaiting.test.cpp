#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
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
        MapTerrain makeFlatTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

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
    }

    TEST_CASE("a unit ordered to move sets off before its route arrives", "[pathfinding]")
    {
        // The original never lets a unit wait. Navigator::SetGoal (0x44F2A0)
        // installs a two-point path -- where the unit stands, then the goal --
        // and raises the "I have a path" flag alongside the "I want a path"
        // one, so the unit is walking on the tick it was ordered and the real
        // route replaces the straight line whenever the scheduler reaches it.
        //
        // RWE used to install nothing and stand still until its search came
        // back. With four hundred units ordered at once and four searches a
        // tick, that was seconds of an army not moving, and it is most of what
        // looked like a pathfinding budget problem.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        // Nothing may be spent on searching, so no real route can ever arrive
        // and anything the unit does is the stand-in's doing.
        sim.pathFindingService.expansionBudgetPerTick = 0;

        auto start = SimVector(-400_ss, 0_ss, 0_ss);
        auto unitId = spawn(sim, player, start, script);
        sim.getUnitState(unitId).orders.push_back(MoveOrder(SimVector(400_ss, 0_ss, 0_ss)));

        for (int tick = 0; tick < 60; ++tick)
        {
            sim.tick();
        }

        const auto& unit = sim.getUnitState(unitId);

        // It has gone somewhere, and it has gone the right way.
        REQUIRE(unit.position.x > start.x);
        REQUIRE(unit.position.distanceSquared(start) > 100_ss);

        // And it is still asking for the route it never got.
        auto* movingState = std::get_if<NavigationStateMoving>(&unit.navigationState.state);
        REQUIRE(movingState != nullptr);
        REQUIRE(movingState->pathRequested);
    }

    TEST_CASE("a reachable destination is still reached exactly", "[pathfinding]")
    {
        // The first pass relaxes the goal when it cannot reach it, so the
        // safety property is this one: when the goal *is* reachable, nothing
        // is relaxed and the unit finishes where it was sent rather than near
        // it. A walk that failed spuriously would show up here as a unit
        // parked short of its destination with its order still in hand.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        sim.unitDefinitions["walker"] = makeWalkerDef();
        std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

        auto destination = SimVector(200_ss, 0_ss, 120_ss);
        auto unitId = spawn(sim, player, SimVector(-200_ss, 0_ss, -120_ss), script);
        sim.getUnitState(unitId).orders.push_back(MoveOrder(destination));

        for (int tick = 0; tick < 1200; ++tick)
        {
            sim.tick();
            if (sim.getUnitState(unitId).orders.empty())
            {
                break;
            }
        }

        const auto& unit = sim.getUnitState(unitId);
        REQUIRE(unit.orders.empty());

        // Arrival tolerance rather than an exact cell: a unit stops when it
        // is close enough, and the point here is that it is close to the
        // place it was sent and not to somewhere the first pass settled for.
        REQUIRE(unit.position.distanceSquared(destination) < (32_ss * 32_ss));
    }
}
