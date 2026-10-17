#include <catch2/catch_test_macros.hpp>
#include <rwe/game/OrderDispatch.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitModelDefinition.h>
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

    namespace
    {
        MapTerrain makeFloodedTerrain(int size)
        {
            Grid<unsigned char> heights(size, size, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 20_ss);
        }

        /** A repairer: builder with the canreclamate mirror, at the given depth, flying or not. */
        UnitDefinition makeRepairerDef(bool flies, unsigned int maxWaterDepth, bool amphibious = false)
        {
            UnitDefinition d{};
            d.isMobile = true;
            d.canMove = true;
            d.builder = true;
            d.canReclamate = true;
            d.canFly = flies;
            d.amphibious = amphibious;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, maxWaterDepth};
            return d;
        }

        /** A damaged hull whose top is `top` above its origin. */
        UnitId addHull(GameSimulation& sim, PlayerId owner, int top, SimScalar y)
        {
            UnitDefinition d{};
            d.objectName = "HULL";
            d.isMobile = true;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["HULL"] = d;
            sim.unitModelDefinitions["HULL"] = createUnitModelDefinition(intToSimScalar(top), {UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}});
            auto id = addUnitOfType(sim, "HULL", owner, SimVector(300_ss, y, 300_ss), makeEmptyCobScript());
            sim.getUnitState(id).hitPoints = 50;
            return id;
        }
    }

    TEST_CASE("a repair order is refused across the water as the original's dispatcher does (BT-13)", "[game][repair][water]")
    {
        // 0x4899B0's last test, in whole units, applied where the order is
        // chosen: a flier that is not amphibious cannot be sent to a unit
        // whose top is under the surface; a ground or sea repairer is limited
        // by its own maximum water depth below it. The sea is at 20.
        GameSimulation sim(makeFloodedTerrain(64), 0u, 0, 0);
        auto player = addPlayer(sim, "player");
        sim.unitDefinitions["conaircraft"] = makeRepairerDef(true, 0u);
        sim.unitDefinitions["amphibiousaircraft"] = makeRepairerDef(true, 0u, true);
        sim.unitDefinitions["wader"] = makeRepairerDef(false, 12u);
        sim.unitDefinitions["conship"] = makeRepairerDef(false, 10000u);

        auto spawn = [&](const std::string& type) { return addUnitOfType(sim, type, player, SimVector(0_ss, 0_ss, 0_ss), makeEmptyCobScript()); };
        auto tryRepair = [&](UnitId repairer, UnitId target) {
            return applyUnitCommandToSimulation(sim, player, PlayerUnitCommand(repairer, PlayerUnitCommand::IssueOrder(RepairOrder(target), Kind::Immediate)));
        };

        // A submarine with its whole hull under the surface: origin at 0, top at 10.
        auto submerged = addHull(sim, player, 10, 0_ss);

        SECTION("a construction aircraft cannot be sent to a submerged hull")
        {
            auto aircraft = spawn("conaircraft");
            REQUIRE_FALSE(tryRepair(aircraft, submerged));
            REQUIRE(sim.getUnitState(aircraft).orders.empty());
        }

        SECTION("an amphibious one can")
        {
            auto aircraft = spawn("amphibiousaircraft");
            REQUIRE(tryRepair(aircraft, submerged));
            REQUIRE(sim.getUnitState(aircraft).orders.size() == 1);
        }

        SECTION("a ship with the default depth can, at any depth")
        {
            auto ship = spawn("conship");
            REQUIRE(tryRepair(ship, submerged));
            REQUIRE(sim.getUnitState(ship).orders.size() == 1);
        }

        SECTION("a land builder wades twelve below the surface and no more")
        {
            auto wader = spawn("wader");
            // Top at 10 against 20 - 12 = 8: within reach.
            REQUIRE(tryRepair(wader, submerged));

            // A hull whose top is at 5: out of reach, and the queue it
            // already has is left alone.
            auto deep = addHull(sim, player, 5, 0_ss);
            REQUIRE_FALSE(tryRepair(wader, deep));
            REQUIRE(sim.getUnitState(wader).orders.size() == 1);
        }

        SECTION("a hull that has surfaced is reachable by anything")
        {
            auto surfaced = addHull(sim, player, 30, 0_ss);
            auto aircraft = spawn("conaircraft");
            REQUIRE(tryRepair(aircraft, surfaced));
        }
    }

    TEST_CASE("an attack order the unit cannot carry out is refused for every sender and leaves its queue alone (BT-63)", "[game][attackeligibility]")
    {
        // The interface refuses these before it sends them; the computer
        // player and a peer do not go through the interface, so the command
        // refuses them too and every peer agrees.
        GameSimulation sim(makeFloodedTerrain(64), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");

        auto gunnerDef = makeTankDef();
        gunnerDef.canAttack = true;
        sim.unitDefinitions["gunner"] = gunnerDef;
        sim.unitDefinitions["target"] = makeTankDef();
        sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, {UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}});
        sim.unitDefinitions["target"].objectName = "model";

        WeaponDefinition gun{};
        gun.maxRange = 300_ss;
        sim.weaponDefinitions["GUN"] = gun;
        WeaponDefinition torpedo{};
        torpedo.maxRange = 400_ss;
        torpedo.waterWeapon = true;
        sim.weaponDefinitions["TORPEDO"] = torpedo;
        WeaponDefinition flak{};
        flak.maxRange = 680_ss;
        flak.toAirWeapon = true;
        sim.weaponDefinitions["FLAK"] = flak;

        auto armed = [&](const char* weaponType) {
            auto id = addUnitOfType(sim, "gunner", us, SimVector(0_ss, 20_ss, 0_ss), makeEmptyCobScript());
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            sim.getUnitState(id).weapons[0] = weapon;
            return id;
        };

        // Its whole hull is under the sea at 20: origin 0, top 10.
        auto submerged = addUnitOfType(sim, "target", them, SimVector(500_ss, 0_ss, 0_ss), makeEmptyCobScript());

        auto send = [&](UnitId attacker, const AttackOrder& order) {
            return applyUnitCommandToSimulation(sim, us, PlayerUnitCommand(attacker, PlayerUnitCommand::IssueOrder(order, Kind::Immediate)));
        };
        auto attack = [&](UnitId attacker, UnitId target) { return send(attacker, AttackOrder(target)); };
        auto attackPlace = [&](UnitId attacker, const SimVector& place) { return send(attacker, AttackOrder(place)); };

        SECTION("a gun against a submerged unit is refused and the queue it has is kept")
        {
            auto id = armed("GUN");
            issue(sim, id, MoveOrder(SimVector(50_ss, 20_ss, 50_ss)), Kind::Immediate);
            REQUIRE_FALSE(attack(id, submerged));
            REQUIRE(sim.getUnitState(id).orders.size() == 1);
            REQUIRE(std::holds_alternative<MoveOrder>(sim.getUnitState(id).orders.front()));
        }

        SECTION("a torpedo against a submerged unit is accepted")
        {
            auto id = armed("TORPEDO");
            REQUIRE(attack(id, submerged));
            REQUIRE(std::holds_alternative<AttackOrder>(sim.getUnitState(id).orders.front()));
        }

        SECTION("an anti-air first weapon is refused a place and accepted nothing on the ground")
        {
            auto id = armed("FLAK");
            REQUIRE_FALSE(attackPlace(id, SimVector(300_ss, 20_ss, 300_ss)));
            REQUIRE(sim.getUnitState(id).orders.empty());

            auto gunnerId = armed("GUN");
            REQUIRE(attackPlace(gunnerId, SimVector(300_ss, 20_ss, 300_ss)));
        }

        SECTION("a unit with no weapon in either slot is not judged")
        {
            auto id = addUnitOfType(sim, "gunner", us, SimVector(0_ss, 20_ss, 0_ss), makeEmptyCobScript());
            REQUIRE(attack(id, submerged));
        }
    }

}
