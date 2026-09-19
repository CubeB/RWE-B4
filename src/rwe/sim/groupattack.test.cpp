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
#include <rwe/sim/sim_test_util.h>
#include <memory>
#include <vector>

/**
 * Regression coverage for issue #66: a large group ordered to attack a
 * single unit had most of its members go idle instead of engaging.
 *
 * The cause was never the dispatch -- every unit really did get its own
 * AttackOrder -- but that attackTarget (UnitBehaviorService.cpp) sent every
 * one of them at the target's bare centre. PathFindingService::beginSearch
 * then centred every attacker's A* goal on that one cell, so they queued
 * for the pathfinder's single request FIFO one at a time: the first few
 * arrivals parked in range and fired, and everyone else walked a naive
 * straight line into the back of whoever got there first, collided,
 * re-requested a path, and rejoined the tail of the same queue. With a
 * large group that reads as most of it standing still.
 *
 * attackApproachGoal gives each attacker a stand-off point of its own,
 * spread around the target rather than stacked on it, so this is a direct
 * check that most of a large group actually gets into a firing state
 * instead of being lost in that queue.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeGroupAttackTerrain()
        {
            Grid<unsigned char> heights(128, 128, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerGroupAttackModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /** A short-to-medium-ranged ground attacker, modest but mobile. */
        UnitDefinition makeAttackerDef()
        {
            UnitDefinition d{};
            d.objectName = "attackermodel";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.maxVelocity = 5_ss;
            d.acceleration = 2_ss;
            d.brakeRate = 2_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 1000u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        /**
         * A stationary punching bag, tough enough that the fight is still
         * going once the test asserts on it rather than already finished.
         */
        UnitDefinition makeTargetDef()
        {
            UnitDefinition d{};
            d.objectName = "targetmodel";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = false;
            d.maxHitPoints = 100000;
            d.buildTime = 0u;
            d.sightDistance = 1000u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** Line-of-sight, turreted so hull facing (a separate concern, see turret.test.cpp) cannot mask the result. */
        void defineAttackerWeapon(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 200_ss;
            w.reloadTime = 1_ss;
            w.burst = 1;
            w.turret = true;
            w.velocity = 300_ss / 30_ss;
            w.damageRadius = 0_ss;
            w.damage["DEFAULT"] = 5;
            sim.weaponDefinitions["attackergun"] = w;
        }

        void armAttacker(GameSimulation& sim, UnitId id)
        {
            UnitWeapon weapon;
            weapon.weaponType = "attackergun";
            sim.getUnitState(id).weapons[0] = weapon;
        }

        /**
         * A "base" mesh piece, not just a script that names one: weapon
         * aiming resolves a firing point through the unit's own piece list
         * (getPieceLocalPosition), and sim_test_util's addUnitOfType leaves
         * that empty (it is meant for tests with nothing to aim). Every
         * other combat test (turret.test.cpp, dogfight.test.cpp) spawns its
         * own way for the same reason.
         */
        UnitId spawnGroupAttackUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        /**
         * tryGetUnitState rather than getUnitState: over a long enough run
         * an attacker can be gone by the time this runs, and that is not what
         * this test is checking for -- it should just count as not engaging.
         *
         * Gone because it was SHOT, by its own side. This was filed as a
         * unit vanishing from the simulation (#73) and guessed to be a
         * footprint collision; a probe that logged every attacker's hit
         * points settled it. The ones at the back fire line-of-sight shots
         * through the ones in front, five points a hit from tick 36, and the
         * first of four dies at tick 592 on exactly a hundred points of it.
         * Friendly fire is unconditional in the original (TOTALA-EXE.md,
         * "Friendly fire is unconditional"), so that is the engine being
         * right, and a dead unit with no corpse leaves the unit list.
         */
        bool isEngaging(GameSimulation& sim, UnitId attackerId, UnitId targetId)
        {
            auto unitRef = sim.tryGetUnitState(attackerId);
            if (!unitRef)
            {
                return false;
            }
            const auto& weapon = unitRef->get().weapons[0];
            if (!weapon)
            {
                return false;
            }
            const auto* attacking = std::get_if<UnitWeaponStateAttacking>(&weapon->state);
            if (!attacking)
            {
                return false;
            }
            const auto* target = std::get_if<UnitId>(&attacking->target);
            return target != nullptr && *target == targetId;
        }
    }

    TEST_CASE("a large group ordered to attack one unit mostly engages rather than going idle", "[groupattack]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeGroupAttackTerrain(), 0u, 0, 0);
        auto attackerOwner = addPlayer(sim, "attackers");
        auto targetOwner = addPlayer(sim, "target");

        sim.unitDefinitions["attacker"] = makeAttackerDef();
        sim.unitDefinitions["target"] = makeTargetDef();
        registerGroupAttackModel(sim, "attackermodel");
        registerGroupAttackModel(sim, "targetmodel");
        defineAttackerWeapon(sim);

        // A stationary target well clear of the attackers' starting cluster.
        // The terrain's coordinate system is centred on (0, 0, 0), not on its
        // own corner, so these stay comfortably inside a 128x128 heightmap
        // rather than clipping its edge.
        auto targetPos = SimVector(300_ss, 0_ss, 300_ss);
        auto targetId = spawnGroupAttackUnit(sim, "target", targetOwner, targetPos, script);

        // Twenty-six attackers, clustered together the way a player's
        // control group usually is, all comfortably outside weapon range
        // (200) of the target so every one of them has to navigate in.
        constexpr int AttackerCount = 26;
        constexpr int Columns = 6;
        std::vector<UnitId> attackerIds;
        attackerIds.reserve(AttackerCount);
        for (int i = 0; i < AttackerCount; ++i)
        {
            auto col = i % Columns;
            auto row = i / Columns;
            auto pos = SimVector(intToSimScalar(100 + col * 70), 0_ss, intToSimScalar(-150 + row * 70));
            auto id = spawnGroupAttackUnit(sim, "attacker", attackerOwner, pos, script);
            armAttacker(sim, id);
            attackerIds.push_back(id);
        }

        for (auto id : attackerIds)
        {
            sim.getUnitState(id).orders.push_back(AttackOrder(targetId));
        }

        // Long enough for the pathfinder (4000 expansions/tick budget) to
        // route all of them and for them to walk in and start firing, short
        // enough that the target -- a hundred thousand hit points against
        // twenty-six attackers doing five damage on a one-second reload --
        // is still standing when the assertion runs.
        tick(sim, 500);

        auto& target = sim.getUnitState(targetId);
        REQUIRE_FALSE(target.isDead());

        int engaging = 0;
        for (auto id : attackerIds)
        {
            if (isEngaging(sim, id, targetId))
            {
                ++engaging;
            }
        }

        // Before the fix, all but the first handful of arrivals stayed
        // queued behind a single contested cell and never got into range at
        // all. Most of the group reaching a firing state is the direct
        // opposite of the reported symptom.
        REQUIRE(engaging >= (AttackerCount * 3) / 4);

        // A second, independent signal: with most of the group actually
        // shooting, the target should have taken real damage rather than
        // the fight being an artifact of the state check above.
        REQUIRE(target.hitPoints < 100000);
    }
}
