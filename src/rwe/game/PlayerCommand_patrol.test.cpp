#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        UnitDefinition makeTankDef()
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.canPatrol = true;
            d.maxVelocity = 2_ss;
            d.acceleration = 0.5_ssf;
            d.brakeRate = 0.5_ssf;
            d.turnRate = 2000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        using Kind = PlayerUnitCommand::IssueOrder::IssueKind;

        void issue(GameSimulation& sim, UnitId unit, const UnitOrder& order, Kind kind)
        {
            REQUIRE(applyUnitCommandToSimulation(sim, PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(order, kind))));
        }
    }

    TEST_CASE("a move then a patrol patrols between the two points", "[game][patrol]")
    {
        // A lone patrol waypoint parks the unit where it stands (see
        // UnitState::addOrder). A move the unit is walking has to become the
        // route's first waypoint instead of being consumed before the patrol
        // runs, or a bomber told to move and then patrol freezes in mid-air.
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "player");
        sim.unitDefinitions["tank"] = makeTankDef();
        auto unit = addUnitOfType(sim, "tank", player, SimVector(0_ss, 0_ss, 0_ss), makeEmptyCobScript());

        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto b = SimVector(400_ss, 0_ss, 400_ss);

        SECTION("a fresh patrol keeps the move it replaces")
        {
            issue(sim, unit, MoveOrder(a), Kind::Immediate);
            issue(sim, unit, PatrolOrder(b), Kind::Immediate);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE(orders.size() == 2);
            REQUIRE((std::get<PatrolOrder>(orders[0]).destination == a));
            REQUIRE((std::get<PatrolOrder>(orders[1]).destination == b));
        }

        SECTION("a shift-queued patrol turns the move into a waypoint")
        {
            issue(sim, unit, MoveOrder(a), Kind::Immediate);
            issue(sim, unit, PatrolOrder(b), Kind::Queued);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE(orders.size() == 2);
            REQUIRE((std::get<PatrolOrder>(orders[0]).destination == a));
            REQUIRE((std::get<PatrolOrder>(orders[1]).destination == b));
        }

        SECTION("a patrol shift-queued on an idle unit loops between the point and the unit")
        {
            issue(sim, unit, PatrolOrder(b), Kind::Queued);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE(orders.size() == 2);
            REQUIRE((std::get<PatrolOrder>(orders[0]).destination == b));
            REQUIRE((std::get<PatrolOrder>(orders[1]).destination == SimVector(0_ss, 0_ss, 0_ss)));
        }

        SECTION("a patrol with no move loops between the point and the unit")
        {
            issue(sim, unit, PatrolOrder(b), Kind::Immediate);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE(orders.size() == 2);
            REQUIRE((std::get<PatrolOrder>(orders[0]).destination == b));
            REQUIRE((std::get<PatrolOrder>(orders[1]).destination == SimVector(0_ss, 0_ss, 0_ss)));
        }
    }
}
