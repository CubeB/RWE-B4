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
#include <set>
#include <vector>
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

        /** The ARM Thunder, straight out of ARMTHUND.FBI. */
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
            d.acceleration = 0.08_ssf;
            d.brakeRate = 0.4_ssf;
            d.turnRate = 356_ss;
            d.attackRunLength = 120_ss;
            d.maneuverLeashLength = 1280_ss;
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

    TEST_CASE("a bomber keeps hitting on pass after pass", "[bomber]")
    {
        // The failure this guards against: after the first run the aircraft
        // settles into a circle, going round and round without ever lining up
        // again, and every later pass misses.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(128, 128), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["bomber"] = makeBomberDef();
        auto targetDef = makeTargetDef();
        // Tough enough to survive the whole engagement so the run keeps going.
        targetDef.maxHitPoints = 1000000;
        sim.unitDefinitions["target"] = targetDef;
        registerModel(sim, "model");
        defineBomb(sim);

        auto targetPosition = SimVector(600_ss, 0_ss, 0_ss);
        auto targetId = spawnUnit(sim, "target", them, targetPosition, script);
        sim.getUnitState(targetId).hitPoints = 1000000;

        auto bomberId = spawnUnit(sim, "bomber", us, SimVector(-900_ss, 200_ss, 0_ss), script);
        {
            auto& bomber = sim.getUnitState(bomberId);
            bomber.physics = UnitPhysicsInfoAir{AirMovementStateFlying{}};
            UnitWeapon weapon;
            weapon.weaponType = "bomb";
            bomber.weapons[0] = weapon;
            bomber.orders.push_back(createAttackOrder(targetId));
        }
        sim.flyingUnitsSet.insert(bomberId);

        std::map<ProjectileId, SimVector> lastSeen;
        std::vector<int> hitTicks;
        std::set<ProjectileId> counted;
        auto slowestSpeed = SimScalar(1000.0f);

        for (int tick = 0; tick < 3600; ++tick)
        {
            sim.tick();

            // Watch for a bomb that has just come down near the target.
            for (const auto& [id, projectile] : sim.projectiles)
            {
                lastSeen[id] = projectile.position;
            }
            for (auto it = lastSeen.begin(); it != lastSeen.end();)
            {
                if (sim.projectiles.tryGet(it->first))
                {
                    ++it;
                    continue;
                }
                if (counted.insert(it->first).second && flatDistance(it->second, targetPosition) < 64_ss)
                {
                    hitTicks.push_back(tick);
                }
                it = lastSeen.erase(it);
            }

            // While manoeuvring it should never come close to stopping.
            const auto& bomber = sim.getUnitState(bomberId);
            if (auto air = std::get_if<UnitPhysicsInfoAir>(&bomber.physics))
            {
                if (auto run = std::get_if<AirMovementStateAttackRun>(&air->movementState))
                {
                    if (tick > 60)
                    {
                        slowestSpeed = rweMin(slowestSpeed, run->currentVelocity.length());
                    }
                }
            }
        }

        INFO("hits at ticks: " << hitTicks.size());
        // Several separate passes connect over the two minutes...
        REQUIRE(hitTicks.size() >= 3);
        // ...and they keep coming: the last one is in the closing stretch,
        // not all bunched into the opening run.
        REQUIRE(hitTicks.back() > 1800);
        // The aircraft flew the whole engagement, never stalling to turn.
        REQUIRE(slowestSpeed > 4_ss);
    }

    TEST_CASE("a bomber that comes in off the target line still lands its bombs", "[bomber]")
    {
        // Approaching straight down the target line is the easy case, and the
        // test above already covers it. This is the one the player actually
        // sees: an aircraft that has to turn onto its target, come round after
        // a run-out, or attack something behind it. If the run commits to
        // whatever heading the aircraft happened to have rather than to the
        // line through the target, it flies a track parallel to the one it
        // wanted, the bombsight never opens, and it crosses over, loops and
        // tries again for as long as you let it.
        auto script = makeEmptyCobScript();
        GameSimulation sim(makeFlatTerrain(256, 256), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        sim.unitDefinitions["bomber"] = makeBomberDef();
        auto targetDef = makeTargetDef();
        // Tough enough to sit there for the whole engagement.
        targetDef.maxHitPoints = 1000000;
        sim.unitDefinitions["target"] = targetDef;
        registerModel(sim, "model");
        defineBomb(sim);

        auto targetPosition = SimVector(0_ss, 0_ss, 0_ss);

        // Bearings around the target, all at the same range, so no section is
        // luckier than another on distance.
        auto start = SimVector(-1000_ss, 200_ss, 0_ss);
        SECTION("from the north-west") { start = SimVector(-700_ss, 200_ss, -700_ss); }
        SECTION("from the north") { start = SimVector(0_ss, 200_ss, -1000_ss); }
        SECTION("from the north-east") { start = SimVector(700_ss, 200_ss, -700_ss); }
        SECTION("from the east") { start = SimVector(1000_ss, 200_ss, 0_ss); }
        SECTION("from the south") { start = SimVector(0_ss, 200_ss, 1000_ss); }

        auto targetId = spawnUnit(sim, "target", them, targetPosition, script);
        sim.getUnitState(targetId).hitPoints = 1000000;

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

        std::map<ProjectileId, SimVector> lastSeen;
        std::set<ProjectileId> counted;
        int hits = 0;
        int dropped = 0;
        auto closest = SimScalar(100000.0f);

        // Two minutes: long enough for several passes at any bearing.
        for (int tick = 0; tick < 3600; ++tick)
        {
            sim.tick();

            if (std::getenv("RWE_TRACE_BOMBER") && tick % 30 == 0)
            {
                const auto& b = sim.getUnitState(bomberId);
                if (auto air = std::get_if<UnitPhysicsInfoAir>(&b.physics))
                {
                    const char* st = "other";
                    int ph = -1;
                    SimVector vel(0_ss, 0_ss, 0_ss);
                    if (auto run = std::get_if<AirMovementStateAttackRun>(&air->movementState)) { st = "run"; ph = static_cast<int>(run->phase); vel = run->currentVelocity; }
                    else if (auto f = std::get_if<AirMovementStateFlying>(&air->movementState)) { st = "flying"; vel = f->currentVelocity; }
                    std::cout << "t=" << tick << " " << st << " ph=" << ph
                              << " pos=" << simScalarToFloat(b.position.x) << "," << simScalarToFloat(b.position.z)
                              << " d=" << simScalarToFloat(flatDistance(b.position, targetPosition))
                              << " spd=" << simScalarToFloat(vel.length()) << std::endl;
                }
            }

            for (const auto& [id, projectile] : sim.projectiles)
            {
                lastSeen[id] = projectile.position;
            }
            for (auto it = lastSeen.begin(); it != lastSeen.end();)
            {
                if (sim.projectiles.tryGet(it->first))
                {
                    ++it;
                    continue;
                }
                if (counted.insert(it->first).second)
                {
                    ++dropped;
                    auto miss = flatDistance(it->second, targetPosition);
                    closest = rweMin(closest, miss);
                    if (miss < 64_ss)
                    {
                        ++hits;
                    }
                }
                it = lastSeen.erase(it);
            }
        }

        CAPTURE(dropped);
        CAPTURE(hits);
        CAPTURE(simScalarToFloat(closest));
        // It has to actually let go of bombs...
        REQUIRE(dropped > 0);
        // ...and put several of them on the target rather than parading past it.
        REQUIRE(hits >= 3);
    }

}
