#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <optional>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeTargetingTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addTargetingPlayer(GameSimulation& sim, const std::string& name, GamePlayerType type)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                type,
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

        std::shared_ptr<CobScript> makeTargetingScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        void registerTargetingModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** A gun emplacement: it shoots, it does not move, and it sees the whole of a test map. */
        void defineShooter(GameSimulation& sim, const std::string& type, const std::string& badTargetCategory)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = true;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            d.badTargetCategory = {badTargetCategory, std::string(), std::string()};
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        void defineTarget(GameSimulation& sim, const std::string& type, const std::string& category, bool shootMe)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = shootMe;
            d.category = category;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        void defineWeapon(GameSimulation& sim, const std::string& name, bool toAirWeapon)
        {
            WeaponDefinition w{};
            w.maxRange = 400_ss;
            // Long enough that nothing gets a second shot off during a test.
            w.reloadTime = 1000_ss;
            w.burst = 1;
            w.velocity = 450_ss / 30_ss;
            w.damageRadius = 4_ss;
            w.damage["DEFAULT"] = 1;
            w.toAirWeapon = toAirWeapon;
            sim.weaponDefinitions[name] = w;
        }

        UnitId spawnTargetingUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
        {
            auto env = std::make_unique<CobEnvironment>(script.get());
            UnitMesh base;
            base.name = "base";
            std::vector<UnitMesh> pieces{base};
            UnitState unit(pieces, std::move(env));
            unit.unitType = unitType;
            unit.owner = owner;
            unit.position = pos;
            unit.previousPosition = pos;
            unit.hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            unit.fireOrders = UnitFireOrders::FireAtWill;
            return sim.tryAddUnit(std::move(unit)).value();
        }

        void armWith(GameSimulation& sim, UnitId id, const std::string& weaponType)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            sim.getUnitState(id).weapons[0] = weapon;
        }

        void putInTheAir(GameSimulation& sim, UnitId id)
        {
            sim.getUnitState(id).physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            sim.flyingUnitsSet.insert(id);
        }

        std::optional<UnitId> weaponTarget(const GameSimulation& sim, UnitId id)
        {
            const auto& weapon = sim.getUnitState(id).weapons[0];
            if (!weapon)
            {
                return std::nullopt;
            }

            auto attacking = std::get_if<UnitWeaponStateAttacking>(&weapon->state);
            if (attacking == nullptr)
            {
                return std::nullopt;
            }

            auto target = std::get_if<UnitId>(&attacking->target);
            return target == nullptr ? std::nullopt : std::optional<UnitId>(*target);
        }

        /**
         * Visibility is computed at the end of a tick, so a unit spawned this
         * tick cannot be seen until the next one; acquisition therefore lands
         * on the second tick.
         */
        void tickTwice(GameSimulation& sim)
        {
            sim.tick();
            sim.tick();
        }
    }

    TEST_CASE("a weapon picks a target its category list does not object to", "[targeting]")
    {
        // The bad target category is per weapon slot and is matched against
        // the candidate's own Category list. A tank told that VTOL is a bad
        // target shoots the other tank two hundred units away rather than the
        // aircraft sitting on top of it -- and it does so however the units
        // are ordered, which is the whole of the old behaviour.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto us = addTargetingPlayer(sim, "us", GamePlayerType::Human);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "tank", "VTOL");
        defineTarget(sim, "enemyTank", "CORE TANK LEVEL1 NOTAIR NOTSUB", true);
        defineTarget(sim, "enemyPlane", "CORE VTOL LEVEL1 NOTSUB", true);
        defineWeapon(sim, "gun", false);

        auto shooterId = spawnTargetingUnit(sim, "tank", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWith(sim, shooterId, "gun");

        SECTION("the aircraft is passed over even though it is nearer and was spawned first")
        {
            auto planeId = spawnTargetingUnit(sim, "enemyPlane", them, SimVector(64_ss, 0_ss, 0_ss), script);
            auto tankId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(-192_ss, 0_ss, 0_ss), script);
            REQUIRE(planeId.value < tankId.value);

            tickTwice(sim);

            auto target = weaponTarget(sim, shooterId);
            REQUIRE(target.has_value());
            REQUIRE(target->value == tankId.value);
        }

        SECTION("a bad target category is a preference, not a veto")
        {
            auto planeId = spawnTargetingUnit(sim, "enemyPlane", them, SimVector(64_ss, 0_ss, 0_ss), script);

            tickTwice(sim);

            auto target = weaponTarget(sim, shooterId);
            REQUIRE(target.has_value());
            REQUIRE(target->value == planeId.value);
        }
    }

    TEST_CASE("nothing opens fire on a building that does not ask to be shot at", "[targeting]")
    {
        // Fifty-four of the shipped units leave ShootMe out, and they are
        // exactly the passive buildings. A tank standing next to an enemy
        // solar collector ignores it and shoots the tank across the field.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto us = addTargetingPlayer(sim, "us", GamePlayerType::Human);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "tank", std::string());
        defineTarget(sim, "enemyTank", "CORE TANK LEVEL1 NOTAIR NOTSUB", true);
        defineTarget(sim, "enemySolar", "CORE LEVEL1 ENERGY NOTAIR NOTSUB", false);
        defineWeapon(sim, "gun", false);

        auto shooterId = spawnTargetingUnit(sim, "tank", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWith(sim, shooterId, "gun");
        auto solarId = spawnTargetingUnit(sim, "enemySolar", them, SimVector(64_ss, 0_ss, 0_ss), script);

        SECTION("the collector is skipped in favour of something that shoots back")
        {
            auto tankId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(-192_ss, 0_ss, 0_ss), script);

            tickTwice(sim);

            auto target = weaponTarget(sim, shooterId);
            REQUIRE(target.has_value());
            REQUIRE(target->value == tankId.value);
        }

        SECTION("with nothing else about, the collector is still left alone")
        {
            tickTwice(sim);

            REQUIRE_FALSE(weaponTarget(sim, shooterId).has_value());
            REQUIRE(sim.getUnitState(solarId).hitPoints == sim.unitDefinitions.at("enemySolar").maxHitPoints);
        }

    }

    TEST_CASE("a computer player's units go after buildings anyway", "[targeting]")
    {
        // The original lets a player of the computer type ignore ShootMe, so
        // an AI's tanks do open fire on the buildings a human player's tanks
        // walk past.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto ai = addTargetingPlayer(sim, "ai", GamePlayerType::Computer);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "tank", std::string());
        defineTarget(sim, "enemySolar", "CORE LEVEL1 ENERGY NOTAIR NOTSUB", false);
        defineWeapon(sim, "gun", false);

        auto shooterId = spawnTargetingUnit(sim, "tank", ai, SimVector(0_ss, 0_ss, 0_ss), script);
        armWith(sim, shooterId, "gun");
        auto solarId = spawnTargetingUnit(sim, "enemySolar", them, SimVector(64_ss, 0_ss, 0_ss), script);

        tickTwice(sim);

        auto target = weaponTarget(sim, shooterId);
        REQUIRE(target.has_value());
        REQUIRE(target->value == solarId.value);
    }

    TEST_CASE("an anti-air weapon will not point itself at the ground", "[targeting]")
    {
        // The four flak guns are the only weapons in the shipped data with
        // toAirWeapon set, and the original refuses them a ground target in
        // the same routine that measures the range.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto us = addTargetingPlayer(sim, "us", GamePlayerType::Human);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "flakGun", std::string());
        defineTarget(sim, "enemyTank", "CORE TANK LEVEL1 NOTAIR NOTSUB", true);
        defineTarget(sim, "enemyPlane", "CORE VTOL LEVEL1 NOTSUB", true);
        defineWeapon(sim, "flak", true);

        auto shooterId = spawnTargetingUnit(sim, "flakGun", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWith(sim, shooterId, "flak");
        auto tankId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(64_ss, 0_ss, 0_ss), script);

        SECTION("it reaches past the nearer ground unit for the aircraft")
        {
            auto planeId = spawnTargetingUnit(sim, "enemyPlane", them, SimVector(-192_ss, 60_ss, 0_ss), script);
            putInTheAir(sim, planeId);

            tickTwice(sim);

            auto target = weaponTarget(sim, shooterId);
            REQUIRE(target.has_value());
            REQUIRE(target->value == planeId.value);
        }

        SECTION("with nothing in the air it holds its fire")
        {
            tickTwice(sim);

            REQUIRE_FALSE(weaponTarget(sim, shooterId).has_value());
            REQUIRE(sim.getUnitState(tankId).hitPoints == sim.unitDefinitions.at("enemyTank").maxHitPoints);
        }
    }

    TEST_CASE("a group spreads its fire instead of piling onto one unit", "[targeting]")
    {
        // The original scores every candidate with a random number drawn
        // between zero and the square of the distance to it and takes the
        // lowest. Twelve guns looking at two equally distant enemies
        // therefore split between them, while a near enemy still wins nearly
        // every time it is offered against a far one.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto us = addTargetingPlayer(sim, "us", GamePlayerType::Human);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "tank", std::string());
        defineTarget(sim, "enemyTank", "CORE TANK LEVEL1 NOTAIR NOTSUB", true);
        defineWeapon(sim, "gun", false);

        std::vector<UnitId> shooters;
        for (int i = 0; i < 12; ++i)
        {
            auto x = intToSimScalar((i * 32) - 176);
            auto id = spawnTargetingUnit(sim, "tank", us, SimVector(x, 0_ss, 0_ss), script);
            armWith(sim, id, "gun");
            shooters.push_back(id);
        }

        SECTION("two equally distant enemies both get shot at")
        {
            auto northId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(0_ss, 0_ss, -96_ss), script);
            auto southId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(0_ss, 0_ss, 96_ss), script);

            tickTwice(sim);

            int north = 0;
            int south = 0;
            for (auto id : shooters)
            {
                auto target = weaponTarget(sim, id);
                REQUIRE(target.has_value());
                if (target->value == northId.value)
                {
                    ++north;
                }
                else if (target->value == southId.value)
                {
                    ++south;
                }
            }

            CAPTURE(north);
            CAPTURE(south);
            REQUIRE(north + south == 12);
            REQUIRE(north > 0);
            REQUIRE(south > 0);
        }

        SECTION("a near enemy still wins nearly every time against a far one")
        {
            auto nearId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(0_ss, 0_ss, -32_ss), script);
            auto farId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(0_ss, 0_ss, 192_ss), script);

            tickTwice(sim);

            int near = 0;
            int far = 0;
            for (auto id : shooters)
            {
                auto target = weaponTarget(sim, id);
                REQUIRE(target.has_value());
                if (target->value == nearId.value)
                {
                    ++near;
                }
                else if (target->value == farId.value)
                {
                    ++far;
                }
            }

            CAPTURE(near);
            CAPTURE(far);
            REQUIRE(near + far == 12);
            REQUIRE(near >= 9);
        }
    }

    TEST_CASE("return fire shoots back at whatever hit it, and nothing else", "[targeting]")
    {
        // Return fire is not a quieter fire at will: the unit acquires
        // nothing on its own and only takes the target it is handed by
        // being shot. Hold fire does not even do that.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto us = addTargetingPlayer(sim, "us", GamePlayerType::Human);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "tank", std::string());
        defineTarget(sim, "enemyTank", "CORE TANK LEVEL1 NOTAIR NOTSUB", true);
        defineWeapon(sim, "gun", false);

        auto shooterId = spawnTargetingUnit(sim, "tank", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWith(sim, shooterId, "gun");
        auto bystanderId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(64_ss, 0_ss, 0_ss), script);
        auto attackerId = spawnTargetingUnit(sim, "enemyTank", them, SimVector(-192_ss, 0_ss, 0_ss), script);

        SECTION("it sits quiet with enemies all around until one of them shoots it")
        {
            sim.getUnitState(shooterId).setFireOrders(UnitFireOrders::ReturnFire);

            tickTwice(sim);
            REQUIRE_FALSE(weaponTarget(sim, shooterId).has_value());

            sim.applyDamage(shooterId, 1, attackerId);

            auto target = weaponTarget(sim, shooterId);
            REQUIRE(target.has_value());
            REQUIRE(target->value == attackerId.value);
            REQUIRE(target->value != bystanderId.value);
        }

        SECTION("it keeps the target it was handed once it has it")
        {
            sim.getUnitState(shooterId).setFireOrders(UnitFireOrders::ReturnFire);
            tickTwice(sim);
            sim.applyDamage(shooterId, 1, attackerId);

            tickTwice(sim);

            auto target = weaponTarget(sim, shooterId);
            REQUIRE(target.has_value());
            REQUIRE(target->value == attackerId.value);
        }

        SECTION("hold fire stays silent even when it is being shot")
        {
            sim.getUnitState(shooterId).setFireOrders(UnitFireOrders::HoldFire);

            tickTwice(sim);
            sim.applyDamage(shooterId, 1, attackerId);

            REQUIRE_FALSE(weaponTarget(sim, shooterId).has_value());

            tickTwice(sim);
            REQUIRE_FALSE(weaponTarget(sim, shooterId).has_value());
        }

        SECTION("dropping out of fire at will drops the target the unit chose for itself")
        {
            tickTwice(sim);
            REQUIRE(weaponTarget(sim, shooterId).has_value());

            sim.getUnitState(shooterId).setFireOrders(UnitFireOrders::ReturnFire);

            REQUIRE_FALSE(weaponTarget(sim, shooterId).has_value());
        }
    }

    TEST_CASE("a unit does not leave its post for something it is told not to chase", "[targeting]")
    {
        // NoChaseCategory decides what is worth breaking off for, not what is
        // worth shooting: a patrol that names VTOL there walks on past an
        // aircraft it could never catch anyway.
        auto script = makeTargetingScript();
        GameSimulation sim(makeTargetingTerrain(), 0u, 0, 0);
        auto us = addTargetingPlayer(sim, "us", GamePlayerType::Human);
        auto them = addTargetingPlayer(sim, "them", GamePlayerType::Human);
        registerTargetingModel(sim);
        defineShooter(sim, "tank", std::string());
        sim.unitDefinitions["tank"].canMove = true;
        defineTarget(sim, "enemyPlane", "CORE VTOL LEVEL1 NOTSUB", true);
        defineWeapon(sim, "gun", false);

        auto here = SimVector(0_ss, 0_ss, 0_ss);
        auto there = SimVector(256_ss, 0_ss, 0_ss);

        auto planeId = spawnTargetingUnit(sim, "enemyPlane", them, SimVector(64_ss, 60_ss, 0_ss), script);
        putInTheAir(sim, planeId);

        SECTION("without a NoChaseCategory it stops where it is and engages")
        {
            auto tankId = spawnTargetingUnit(sim, "tank", us, here, script);
            armWith(sim, tankId, "gun");
            auto& tank = sim.getUnitState(tankId);
            tank.orders.push_back(PatrolOrder(here));
            tank.orders.push_back(PatrolOrder(there));

            sim.tick();

            REQUIRE((std::get<PatrolOrder>(sim.getUnitState(tankId).orders.front()).destination == here));
        }

        SECTION("naming the aircraft's category carries the patrol on")
        {
            sim.unitDefinitions["tank"].noChaseCategory = "VTOL";
            auto tankId = spawnTargetingUnit(sim, "tank", us, here, script);
            armWith(sim, tankId, "gun");
            auto& tank = sim.getUnitState(tankId);
            tank.orders.push_back(PatrolOrder(here));
            tank.orders.push_back(PatrolOrder(there));

            sim.tick();

            REQUIRE((std::get<PatrolOrder>(sim.getUnitState(tankId).orders.front()).destination == there));
        }
    }
}
