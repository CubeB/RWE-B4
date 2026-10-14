#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        void registerModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        UnitDefinition makeTankDef()
        {
            UnitDefinition d{};
            d.objectName = "tankmodel";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.maxVelocity = 2_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            // Deciding whether to break off is a search at sight range over
            // things that ask to be shot at, so a fixture without either of
            // these is invisible to it.
            d.sightDistance = 300u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeTowerDef()
        {
            auto d = makeTankDef();
            d.isMobile = false;
            d.canMove = false;
            return d;
        }

        /**
         * Not sim_test_util.h's addUnitOfType: this one names the base piece
         * so a weapon has somewhere to aim from, sets fire orders to
         * FireAtWill so the unit will break off its route, and leaves
         * buildTimeCompleted alone. Patrol needs all three.
         */
        UnitId addFiringUnitOfType(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            const UnitId unitId(sim.units.emplace(pieces, std::move(env)));
            auto& unit = sim.getUnitState(unitId);
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = 100;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return unitId;
        }

        void armUnit(GameSimulation& sim, UnitId unitId, SimScalar range)
        {
            WeaponDefinition w{};
            w.maxRange = range;
            sim.weaponDefinitions["laser"] = w;
            UnitWeapon weapon;
            weapon.weaponType = "laser";
            // Aim but never actually fire; firing needs projectile data these tests don't set up.
            weapon.readyTime = GameTime(std::numeric_limits<unsigned int>::max());
            sim.getUnitState(unitId).weapons[0] = weapon;
        }
    }

    TEST_CASE("a patrol order re-queues its waypoint when reached", "[patrol]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto b = SimVector(400_ss, 0_ss, 400_ss);
        auto tankId = addFiringUnitOfType(sim, "tank", player, a, script);
        auto& tank = sim.getUnitState(tankId);
        tank.orders.push_back(PatrolOrder(a));
        tank.orders.push_back(PatrolOrder(b));

        sim.tick();

        // Already standing at A, so A rotates to the back and B is now current.
        REQUIRE(tank.orders.size() == 2);
        REQUIRE((std::get<PatrolOrder>(tank.orders.front()).destination == b));
        REQUIRE((std::get<PatrolOrder>(tank.orders.back()).destination == a));
    }

    TEST_CASE("a patrolling unit engages an enemy in weapon range instead of moving on", "[patrol]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto tankId = addFiringUnitOfType(sim, "tank", player, SimVector(100_ss, 0_ss, 100_ss), script);
        armUnit(sim, tankId, 200_ss);
        auto enemyId = addFiringUnitOfType(sim, "tank", enemy, SimVector(150_ss, 0_ss, 100_ss), script);

        // Visibility is recomputed at the end of a tick, so give it one to
        // settle before the patrol runs: nothing is visible to anyone on the
        // very first tick of a simulation.
        sim.tick();

        auto& tank = sim.getUnitState(tankId);
        tank.orders.push_back(PatrolOrder(SimVector(100_ss, 0_ss, 100_ss)));

        sim.tick();

        // The waypoint was under our feet, but the enemy takes priority. The
        // break-off is an order, not a nudge: an attack goes in front of the
        // patrol, which is left alone so the route resumes at the same
        // waypoint when the attack is over.
        REQUIRE(tank.orders.size() == 2);
        const auto* attack = std::get_if<AttackOrder>(&tank.orders.front());
        REQUIRE(attack != nullptr);
        REQUIRE((std::get<UnitId>(attack->target) == enemyId));
        REQUIRE(std::holds_alternative<PatrolOrder>(tank.orders.back()));

        // It is a chase the unit started itself, so it carries a leash.
        REQUIRE(attack->leash.has_value());

        // And the tick after, that order is doing what an attack order does.
        sim.tick();
        auto attacking = std::get_if<UnitWeaponStateAttacking>(&tank.weapons[0]->state);
        REQUIRE(attacking != nullptr);
        REQUIRE((std::get<UnitId>(attacking->target) == enemyId));
    }

    TEST_CASE("a patrolling unit on hold fire ignores enemies", "[patrol]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto tankId = addFiringUnitOfType(sim, "tank", player, a, script);
        armUnit(sim, tankId, 200_ss);
        addFiringUnitOfType(sim, "tank", enemy, SimVector(150_ss, 0_ss, 100_ss), script);

        auto& tank = sim.getUnitState(tankId);
        tank.fireOrders = UnitFireOrders::HoldFire;
        tank.orders.push_back(PatrolOrder(a));
        tank.orders.push_back(PatrolOrder(SimVector(400_ss, 0_ss, 400_ss)));

        sim.tick();

        REQUIRE((std::get<PatrolOrder>(tank.orders.back()).destination == a));
    }

    TEST_CASE("a static unit that cannot patrol drops a patrol order", "[patrol]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        sim.unitDefinitions["tower"] = makeTowerDef();
        registerModel(sim, "tankmodel");

        auto towerId = addFiringUnitOfType(sim, "tower", player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.getUnitState(towerId).orders.push_back(PatrolOrder(SimVector(400_ss, 0_ss, 400_ss)));

        sim.tick();

        REQUIRE(sim.getUnitState(towerId).orders.empty());
    }

    TEST_CASE("a patrol set on a factory is handed to what it builds", "[patrol]")
    {
        // The original gives a unit with no mover QPATROL (0x43F3B9), a
        // mission that never completes, so the waypoints sit in the factory's
        // list; GetBuilt (0x402DA0) then walks that list and issues each one
        // to the finished unit. RWE dropped the order from a static unit
        // within a tick, so the factory had nothing to hand over and the
        // product just got a BuggerOff (play-test 2026-10-10, an air plant).
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto player = addWellStockedPlayer(sim, "ARM");
        registerModel(sim, "model");

        UnitDefinition factoryDef{};
        factoryDef.objectName = "model";
        factoryDef.isMobile = false;
        factoryDef.canMove = true;
        factoryDef.canPatrol = true;
        factoryDef.builder = true;
        factoryDef.workerTimePerTick = 10;
        factoryDef.maxHitPoints = 1000;
        factoryDef.movementCollisionInfo = UnitDefinition::AdHocMovementClass{6u, 6u, 255u, 255u, 0u, 0u};
        factoryDef.yardMap = Grid<YardMapCell>(6, 6, YardMapCell::GroundPassable);
        sim.unitDefinitions["FACT"] = factoryDef;
        sim.unitScriptDefinitions["FACT"] = *script;

        auto productDef = makeTankDef();
        productDef.canPatrol = true;
        productDef.objectName = "model";
        productDef.buildTime = 100u;
        productDef.buildCostMetal = Metal(1.0f);
        productDef.buildCostEnergy = Energy(1.0f);
        sim.unitDefinitions["TANK"] = productDef;
        sim.unitScriptDefinitions["TANK"] = *script;

        auto a = SimVector(900_ss, 0_ss, 600_ss);
        auto b = SimVector(900_ss, 0_ss, 900_ss);

        auto factoryId = addUnitOfType(sim, "FACT", player, SimVector(600_ss, 0_ss, 600_ss), script);
        auto& factory = sim.getUnitState(factoryId);
        factory.inBuildStance = true;
        factory.buildQueue.emplace_back("TANK", 1);
        factory.orders.push_back(PatrolOrder(a));
        factory.orders.push_back(PatrolOrder(b));

        std::optional<UnitId> productId;
        for (int i = 0; i < 600 && !productId; ++i)
        {
            sim.tick();
            for (const auto& entry : sim.units)
            {
                if (entry.second.unitType == "TANK" && !entry.second.isBeingBuilt(sim.unitDefinitions.at("TANK")))
                {
                    productId = entry.first;
                }
            }
        }
        REQUIRE(productId.has_value());

        // The factory hands its orders over on the tick after the frame
        // completes, when it notices.
        tick(sim, 3);

        // The factory kept its waypoints through the whole build...
        REQUIRE(factory.orders.size() == 2);
        REQUIRE(std::holds_alternative<PatrolOrder>(factory.orders.front()));

        // ...and the unit it finished is on the route.
        const auto& orders = sim.getUnitState(*productId).orders;
        REQUIRE(orders.size() >= 2);
        for (const auto& order : orders)
        {
            REQUIRE(std::holds_alternative<PatrolOrder>(order));
        }
    }

    TEST_CASE("a builder on patrol does not go looking for a fight", "[patrol]")
    {
        // A unit that can repair is given RepairPatrol rather than Patrol
        // (0x43F3E6), and neither RepairPatrol handler calls the acquisition
        // search at all -- so a construction unit walking a route in the
        // original clears wreckage and mends things and never breaks off to
        // chase anything.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "builder");
        auto enemy = addPlayer(sim, "enemy");

        auto builderDef = makeTankDef();
        builderDef.canReclamate = true;
        builderDef.builder = true;
        sim.unitDefinitions["builder"] = builderDef;
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto builderId = addFiringUnitOfType(sim, "builder", player, SimVector(100_ss, 0_ss, 100_ss), script);
        armUnit(sim, builderId, 200_ss);
        addFiringUnitOfType(sim, "tank", enemy, SimVector(150_ss, 0_ss, 100_ss), script);

        sim.tick();

        auto& builder = sim.getUnitState(builderId);
        builder.orders.push_back(PatrolOrder(SimVector(100_ss, 0_ss, 100_ss)));

        sim.tick();
        sim.tick();

        // The patrol is still the only order it has: nothing was pushed in
        // front of it.
        REQUIRE(builder.orders.size() == 1);
        REQUIRE(std::holds_alternative<PatrolOrder>(builder.orders.front()));
    }

    TEST_CASE("a move followed by a patrol becomes two patrol waypoints", "[patrol]")
    {
        // The rule every route-composition path shares: a move immediately
        // followed by a patrol is a route, the move's destination its first
        // waypoint. A factory hands a whole queue to the unit it finishes,
        // which is why replaceOrders has to apply it too.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto b = SimVector(400_ss, 0_ss, 400_ss);
        auto tankId = addFiringUnitOfType(sim, "tank", player, SimVector(0_ss, 0_ss, 0_ss), script);

        std::deque<UnitOrder> route{MoveOrder(a), PatrolOrder(b)};
        sim.getUnitState(tankId).replaceOrders(route);

        const auto& orders = sim.getUnitState(tankId).orders;
        REQUIRE(orders.size() == 2);
        REQUIRE((std::get<PatrolOrder>(orders[0]).destination == a));
        REQUIRE((std::get<PatrolOrder>(orders[1]).destination == b));
    }
}
