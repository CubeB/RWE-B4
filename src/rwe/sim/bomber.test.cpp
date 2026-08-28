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
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width, int height)
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
            script->pieces.push_back("base");
            return script;
        }

        void registerModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** The ARM Thunder's numbers: 9 units a tick at 200 up. */
        UnitDefinition makeBomberDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.canFly = true;
            d.cruiseAltitude = 200_ss;
            d.maxVelocity = 9_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 200_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
            return d;
        }

        UnitDefinition makeTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            // Mobile but unable to move: avoids needing a yardmap.
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 10000;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** The ARM bomb: dropped, range 1280, 48 blast, no launch speed of its own. */
        void defineBomb(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeBomb();
            w.maxRange = 1280_ss;
            w.reloadTime = SimScalar(0.18f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = 0_ss;
            w.damageRadius = 24_ss;
            w.damage["DEFAULT"] = 100;
            sim.weaponDefinitions["bomb"] = w;
        }

        UnitId spawnUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        SimScalar flatDistance(const SimVector& a, const SimVector& b)
        {
            auto dx = a.x - b.x;
            auto dz = a.z - b.z;
            return rweSqrt((dx * dx) + (dz * dz));
        }
    }

    TEST_CASE("a bomber releases its bomb on the approach and it lands on the target", "[bomber]")
    {
        auto script = makeEmptyCobScript();
        // 128 tiles = 2048 world units, so there is room for a long approach.
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["bomber"] = makeBomberDef();
        sim.unitDefinitions["target"] = makeTargetDef();
        registerModel(sim, "model");
        defineBomb(sim);

        auto targetPosition = SimVector(600_ss, 0_ss, 0_ss);
        auto targetId = spawnUnit(sim, "target", them, targetPosition, script);

        // Straight in from the west, or diagonally so the bomber has to turn onto the target.
        auto start = SimVector(-900_ss, 200_ss, 0_ss);
        SECTION("straight approach") { start = SimVector(-900_ss, 200_ss, 0_ss); }
        SECTION("diagonal approach") { start = SimVector(-600_ss, 200_ss, -900_ss); }
        SECTION("from close by, forcing a run-out and a second pass") { start = SimVector(400_ss, 200_ss, 0_ss); }

        auto bomberId = spawnUnit(sim, "bomber", us, start, script);
        {
            auto& bomber = sim.getUnitState(bomberId);
            bomber.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "bomb";
            bomber.weapons[0] = weapon;
            bomber.orders.push_back(createAttackOrder(targetId));
        }
        sim.flyingUnitsSet.insert(bomberId);

        struct BombRecord
        {
            SimVector spawnPosition;
            SimVector bomberPosition;
            SimVector lastPosition;
        };
        std::map<ProjectileId, BombRecord> bombs;

        for (int tick = 0; tick < 1200; ++tick)
        {
            sim.tick();
            if (std::getenv("RWE_TRACE_BOMBER") && tick % 20 == 0)
            {
                const auto& b = sim.getUnitState(bomberId);
                std::string phase = "n/a";
                if (auto air = std::get_if<UnitPhysicsInfoAir>(&b.physics))
                {
                    if (auto run = std::get_if<AirMovementStateAttackRun>(&air->movementState))
                    {
                        phase = std::to_string(static_cast<int>(run->phase)) + " v=" + std::to_string(simScalarToFloat(run->currentVelocity.x)) + "," + std::to_string(simScalarToFloat(run->currentVelocity.z));
                    }
                    else if (std::holds_alternative<AirMovementStateFlying>(air->movementState)) phase = "flying";
                }
                bool attacking = b.weapons[0] && std::holds_alternative<UnitWeaponStateAttacking>(b.weapons[0]->state);
                std::cout << "t=" << tick << " pos=" << simScalarToFloat(b.position.x) << "," << simScalarToFloat(b.position.y) << "," << simScalarToFloat(b.position.z) << " phase=" << phase << " attacking=" << attacking << " orders=" << b.orders.size() << " bombs=" << bombs.size() << "\n";
            }
            for (const auto& [id, projectile] : sim.projectiles)
            {
                auto it = bombs.find(id);
                if (it == bombs.end())
                {
                    bombs.emplace(id, BombRecord{projectile.position, sim.getUnitState(bomberId).position, projectile.position});
                }
                else
                {
                    it->second.lastPosition = projectile.position;
                }
            }
        }

        REQUIRE_FALSE(bombs.empty());
        const auto& first = bombs.begin()->second;

        INFO("bomb spawned at " << simScalarToFloat(first.spawnPosition.x) << "," << simScalarToFloat(first.spawnPosition.y) << "," << simScalarToFloat(first.spawnPosition.z)
                                << " with bomber at " << simScalarToFloat(first.bomberPosition.x) << "," << simScalarToFloat(first.bomberPosition.z)
                                << ", last seen at " << simScalarToFloat(first.lastPosition.x) << "," << simScalarToFloat(first.lastPosition.y) << "," << simScalarToFloat(first.lastPosition.z));

        // It leaves from the bomber itself...
        REQUIRE(flatDistance(first.spawnPosition, first.bomberPosition) < 40_ss);
        // ...while the bomber is still short of the target, on the way in...
        REQUIRE(flatDistance(first.bomberPosition, targetPosition) > 100_ss);
        REQUIRE(flatDistance(first.bomberPosition, targetPosition) < 900_ss);
        // ...and comes down close to the target.
        REQUIRE(flatDistance(first.lastPosition, targetPosition) < 64_ss);
    }
}
