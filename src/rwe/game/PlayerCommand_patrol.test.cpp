#include <catch2/catch_test_macros.hpp>
#include <rwe/game/OrderDispatch.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/sim/FeatureDefinition.h>
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
            REQUIRE((applyUnitCommandToSimulation(sim, sim.getUnitState(unit).owner, PlayerUnitCommand(unit, PlayerUnitCommand::IssueOrder(order, kind)))));
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
            REQUIRE((orders.size() == 2));
            REQUIRE(((std::get<PatrolOrder>(orders[0]).destination == a)));
            REQUIRE(((std::get<PatrolOrder>(orders[1]).destination == b)));
        }

        SECTION("a shift-queued patrol turns the move into a waypoint")
        {
            issue(sim, unit, MoveOrder(a), Kind::Immediate);
            issue(sim, unit, PatrolOrder(b), Kind::Queued);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE((orders.size() == 2));
            REQUIRE(((std::get<PatrolOrder>(orders[0]).destination == a)));
            REQUIRE(((std::get<PatrolOrder>(orders[1]).destination == b)));
        }

        SECTION("a patrol shift-queued on an idle unit loops between the point and the unit")
        {
            issue(sim, unit, PatrolOrder(b), Kind::Queued);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE((orders.size() == 2));
            REQUIRE(((std::get<PatrolOrder>(orders[0]).destination == b)));
            REQUIRE(((std::get<PatrolOrder>(orders[1]).destination == SimVector(0_ss, 0_ss, 0_ss))));
        }

        SECTION("a patrol with no move loops between the point and the unit")
        {
            issue(sim, unit, PatrolOrder(b), Kind::Immediate);

            const auto& orders = sim.getUnitState(unit).orders;
            REQUIRE((orders.size() == 2));
            REQUIRE(((std::get<PatrolOrder>(orders[0]).destination == b)));
            REQUIRE(((std::get<PatrolOrder>(orders[1]).destination == SimVector(0_ss, 0_ss, 0_ss))));
        }
    }

    TEST_CASE("Shift orders remove the first matching mission, within sixteen on both axes", "[queuetoggle]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "player");
        sim.unitDefinitions["tank"] = makeTankDef();
        auto id = addUnitOfType(sim, "tank", player, SimVector(0_ss, 0_ss, 0_ss), makeEmptyCobScript());
        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto b = SimVector(400_ss, 0_ss, 400_ss);
        auto& unit = sim.getUnitState(id);
        unit.orders = {MoveOrder(a), MoveOrder(b), MoveOrder(a)};
        issue(sim, id, MoveOrder(SimVector(116_ss, 900_ss, 84_ss)), Kind::Queued);
        REQUIRE((unit.orders.size() == 2));
        REQUIRE((std::get<MoveOrder>(unit.orders.front()).destination == b));
        REQUIRE((std::get<MoveOrder>(unit.orders.back()).destination == a));
        issue(sim, id, MoveOrder(SimVector(117_ss, 0_ss, 100_ss)), Kind::Queued);
        REQUIRE((unit.orders.size() == 3));
        issue(sim, id, MoveOrder(a), Kind::Immediate);
        REQUIRE((unit.orders.size() == 1));
        REQUIRE((std::get<MoveOrder>(unit.orders.front()).destination == a));
        REQUIRE((ordersMatchForQueueToggle(BuildOrder("A", a), BuildOrder("B", a))));
        REQUIRE_FALSE(ordersMatchForQueueToggle(AttackOrder(UnitId(2)), AttackOrder(UnitId(3))));
    }

    TEST_CASE("a queued reclaim toggles against any feature within sixteen units (BT-86)", "[game][patrol]")
    {
        // 0x43AFC0 matches a reclaim on its type and a sixteen-unit box (each
        // axis, inclusive), since a feature is a position and not a unit
        // target. Two features a heightmap cell apart are sixteen apart.
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 0, 0);
        auto player = addPlayer(sim, "player");
        sim.unitDefinitions["tank"] = makeTankDef();
        auto id = addUnitOfType(sim, "tank", player, SimVector(0_ss, 0_ss, 0_ss), makeEmptyCobScript());

        FeatureDefinition rock{};
        rock.name = "rock";
        rock.footprintX = 1;
        rock.footprintZ = 1;
        rock.height = 10_ss;
        rock.reclaimable = true;
        rock.metal = 10u;
        rock.blocking = false;
        auto rockDef = sim.featureDefinitions.insert(rock);
        auto first = sim.addFeature(rockDef, 4, 4).value();
        auto neighbour = sim.addFeature(rockDef, 5, 4).value();
        auto far = sim.addFeature(rockDef, 7, 4).value();

        auto& orders = sim.getUnitState(id).orders;
        SECTION("a feature sixteen units away is the same order, and the second click removes the first")
        {
            issue(sim, id, ReclaimOrder(first), Kind::Queued);
            REQUIRE((orders.size() == 1));
            issue(sim, id, ReclaimOrder(neighbour), Kind::Queued);
            REQUIRE(orders.empty());
        }

        SECTION("a feature further than that is another order")
        {
            issue(sim, id, ReclaimOrder(first), Kind::Queued);
            issue(sim, id, ReclaimOrder(far), Kind::Queued);
            REQUIRE((orders.size() == 2));
        }

        SECTION("the same feature still toggles")
        {
            issue(sim, id, ReclaimOrder(first), Kind::Queued);
            issue(sim, id, ReclaimOrder(first), Kind::Queued);
            REQUIRE(orders.empty());
        }
    }

    TEST_CASE("formation keeps fractional offsets and height, while distant outliers get the click", "[formation]")
    {
        auto click = SimVector(500_ss, 23_ss, 500_ss);
        std::vector<SimVector> positions = {SimVector(0.5_ssf, 0_ss, 0_ss), SimVector(20.5_ssf, 0_ss, 0_ss)};
        REQUIRE((formationDestination(positions, positions[0], click) == SimVector(490.5_ssf, 23_ss, 500_ss)));
        REQUIRE((formationDestination(positions, positions[1], click) == SimVector(510.5_ssf, 23_ss, 500_ss)));
        REQUIRE((formationDestination(positions, SimVector(1000_ss, 0_ss, 0_ss), click) == click));
        positions = {SimVector(-1.5_ssf, 0_ss, 0_ss), SimVector(0.5_ssf, 0_ss, 0_ss)};
        REQUIRE((formationDestination(positions, positions[0], click) == SimVector(499.5_ssf, 23_ss, 500_ss)));
    }

}
