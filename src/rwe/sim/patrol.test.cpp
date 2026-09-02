#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <limits>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addPlayer(GameSimulation& sim, const std::string& name)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeEmptyCobScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            // One piece so weapon aim/fire points can be resolved.
            script->pieces.push_back("base");
            return script;
        }

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

        UnitId addUnitOfType(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto b = SimVector(400_ss, 0_ss, 400_ss);
        auto tankId = addUnitOfType(sim, "tank", player, a, script);
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
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto tankId = addUnitOfType(sim, "tank", player, SimVector(100_ss, 0_ss, 100_ss), script);
        armUnit(sim, tankId, 200_ss);
        auto enemyId = addUnitOfType(sim, "tank", enemy, SimVector(150_ss, 0_ss, 100_ss), script);

        // Visibility is recomputed at the end of a tick, so give it one to
        // settle before the patrol runs: nothing is visible to anyone on the
        // very first tick of a simulation.
        sim.tick();

        auto& tank = sim.getUnitState(tankId);
        tank.orders.push_back(PatrolOrder(SimVector(100_ss, 0_ss, 100_ss)));

        sim.tick();

        // The waypoint was under our feet, but the enemy takes priority: the
        // order is neither completed nor rotated, and the weapon is aimed.
        REQUIRE(tank.orders.size() == 1);
        auto attacking = std::get_if<UnitWeaponStateAttacking>(&tank.weapons[0]->state);
        REQUIRE(attacking != nullptr);
        REQUIRE((std::get<UnitId>(attacking->target) == enemyId));
    }

    TEST_CASE("a patrolling unit on hold fire ignores enemies", "[patrol]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        auto enemy = addPlayer(sim, "enemy");
        sim.unitDefinitions["tank"] = makeTankDef();
        registerModel(sim, "tankmodel");

        auto a = SimVector(100_ss, 0_ss, 100_ss);
        auto tankId = addUnitOfType(sim, "tank", player, a, script);
        armUnit(sim, tankId, 200_ss);
        addUnitOfType(sim, "tank", enemy, SimVector(150_ss, 0_ss, 100_ss), script);

        auto& tank = sim.getUnitState(tankId);
        tank.fireOrders = UnitFireOrders::HoldFire;
        tank.orders.push_back(PatrolOrder(a));
        tank.orders.push_back(PatrolOrder(SimVector(400_ss, 0_ss, 400_ss)));

        sim.tick();

        REQUIRE((std::get<PatrolOrder>(tank.orders.back()).destination == a));
    }

    TEST_CASE("a static unit drops a patrol order", "[patrol]")
    {
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim, "patroller");
        sim.unitDefinitions["tower"] = makeTowerDef();
        registerModel(sim, "tankmodel");

        auto towerId = addUnitOfType(sim, "tower", player, SimVector(100_ss, 0_ss, 100_ss), script);
        sim.getUnitState(towerId).orders.push_back(PatrolOrder(SimVector(400_ss, 0_ss, 400_ss)));

        sim.tick();

        REQUIRE(sim.getUnitState(towerId).orders.empty());
    }
}
