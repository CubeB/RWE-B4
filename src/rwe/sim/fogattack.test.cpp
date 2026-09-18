#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <optional>
#include <rwe/sim/sim_test_util.h>
#include <variant>
#include <vector>

/**
 * An attack order on a unit that goes into the fog (B4 #74).
 *
 * The original keeps the order on the last position the attacker's owner
 * actually saw the target at, and picks it up again -- moved, if it has moved
 * -- once that ground is visible once more. RWE followed the target's live
 * position the whole time, which is omniscient: an ordered attacker walked to
 * where its quarry really was rather than to where it was last seen.
 *
 * Settled by testing the original directly rather than read out of the
 * binary; see TOTALA-EXE.md S:9's correction, which says as much.
 *
 * The question asked is canSeeUnit and deliberately not canDetectUnit: a
 * radar contact is a blip and not a target, and the radar picture is
 * recomputed for one player a tick, so it could not feed a deterministic
 * decision even if the original wanted it to.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFogAttackTerrain()
        {
            // 64x64 tiles of 16 world units, centred on the origin, so the map
            // runs from -512 to +512.
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerFogAttackModel(GameSimulation& sim, const std::string& objectName)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions[objectName] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * Blind on its own account, so that every scrap of vision in these
         * tests comes from the scout and can be taken away again by killing
         * it. Slow enough that it is still walking in when the assertions run.
         */
        UnitDefinition makeBlindAttackerDef()
        {
            UnitDefinition d{};
            d.objectName = "attackermodel";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.maxVelocity = 1_ss;
            d.acceleration = 1_ss;
            d.brakeRate = 1_ss;
            d.turnRate = 1000_ss;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** Stands still and does nothing but look. */
        UnitDefinition makeWatcherDef()
        {
            UnitDefinition d{};
            d.objectName = "scoutmodel";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 100;
            d.buildTime = 0u;
            d.sightDistance = 200u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitDefinition makeQuarryDef()
        {
            UnitDefinition d{};
            d.objectName = "quarrymodel";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = 10000;
            d.buildTime = 0u;
            d.sightDistance = 0u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        /** Short-ranged, so the attacker has to walk and never arrives. */
        void defineAttackerWeapon(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 100_ss;
            w.reloadTime = 1_ss;
            w.burst = 1;
            w.turret = true;
            w.velocity = 300_ss / 30_ss;
            w.damageRadius = 0_ss;
            w.damage["DEFAULT"] = 5;
            sim.weaponDefinitions["attackergun"] = w;
        }

        UnitId spawnFogAttackUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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
            return sim.tryAddUnit(std::move(unit)).value();
        }

        /** The remembered position carried on the unit's leading attack order. */
        std::optional<SimVector> rememberedPosition(GameSimulation& sim, UnitId attackerId)
        {
            const auto& orders = sim.getUnitState(attackerId).orders;
            if (orders.empty())
            {
                return std::nullopt;
            }
            const auto* attack = std::get_if<AttackOrder>(&orders.front());
            if (attack == nullptr)
            {
                return std::nullopt;
            }
            return attack->lastSeenPosition;
        }
    }

    TEST_CASE("an attack order holds the last seen position while the target is in fog", "[fogattack]")
    {
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFogAttackTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");

        registerFogAttackModel(sim, "attackermodel");
        registerFogAttackModel(sim, "scoutmodel");
        registerFogAttackModel(sim, "quarrymodel");
        sim.unitDefinitions["attacker"] = makeBlindAttackerDef();
        sim.unitDefinitions["watcher"] = makeWatcherDef();
        sim.unitDefinitions["quarry"] = makeQuarryDef();
        defineAttackerWeapon(sim);

        auto firstSpot = SimVector(0_ss, 0_ss, 0_ss);
        auto quarryId = spawnFogAttackUnit(sim, "quarry", them, firstSpot, script);

        // Our eyes, close enough to see the quarry where it stands. Sight is
        // capped at 256 world units in True mode whatever the FBI says, so
        // this has to be genuinely near.
        auto watcherId = spawnFogAttackUnit(sim, "watcher", us, SimVector(100_ss, 0_ss, 0_ss), script);

        // Far out of weapon range to the west, so it spends the whole test
        // walking rather than arriving and aiming.
        auto attackerId = spawnFogAttackUnit(sim, "attacker", us, SimVector(-400_ss, 0_ss, 0_ss), script);
        UnitWeapon weapon;
        weapon.weaponType = "attackergun";
        sim.getUnitState(attackerId).weapons[0] = weapon;
        sim.getUnitState(attackerId).orders.push_back(AttackOrder(quarryId));

        // Two ticks, not one: updateVisibility runs at the *end* of a tick,
        // after unit behaviour, so on the first tick after everything is
        // spawned together the attack order is handled against a visibility
        // grid that is still empty. In play the order is given by clicking
        // something already visible, from units that have been ticking for a
        // while, so the lag is a fixture artifact rather than a delay anyone
        // would see.
        tick(sim, 2);

        REQUIRE(sim.canSeeUnit(us, quarryId));
        auto seen = rememberedPosition(sim, attackerId);
        REQUIRE(seen.has_value());
        REQUIRE(seen->x == firstSpot.x);
        REQUIRE(seen->z == firstSpot.z);

        SECTION("it freezes once the ground goes dark, and the quarry's real position stops mattering")
        {
            sim.getUnitState(watcherId).markAsDeadNoCorpse();
            sim.tick();
            REQUIRE_FALSE(sim.canSeeUnit(us, quarryId));

            // It slips away northward while nobody is looking.
            auto secondSpot = SimVector(0_ss, 0_ss, 300_ss);
            sim.getUnitState(quarryId).position = secondSpot;
            sim.getUnitState(quarryId).previousPosition = secondSpot;

            tick(sim, 30);

            auto stillSeen = rememberedPosition(sim, attackerId);
            REQUIRE(stillSeen.has_value());
            REQUIRE(stillSeen->x == firstSpot.x);
            REQUIRE(stillSeen->z == firstSpot.z);

            // And the order is still standing rather than having been dropped.
            REQUIRE_FALSE(sim.getUnitState(attackerId).orders.empty());

            // What the attacker is walking at is the old place, not the new
            // one: the stand-off point sits beside the remembered spot, so its
            // z stays down at the first spot rather than following north.
            const auto& goal = sim.getUnitState(attackerId).navigationState.desiredDestination;
            REQUIRE(goal.has_value());
            const auto* goalPoint = std::get_if<SimVector>(&*goal);
            REQUIRE(goalPoint != nullptr);
            REQUIRE(goalPoint->z < 150_ss);
        }

        SECTION("and picks the target up again, where it now is, once the ground is visible")
        {
            sim.getUnitState(watcherId).markAsDeadNoCorpse();
            sim.tick();

            auto secondSpot = SimVector(0_ss, 0_ss, 300_ss);
            sim.getUnitState(quarryId).position = secondSpot;
            sim.getUnitState(quarryId).previousPosition = secondSpot;
            tick(sim, 5);

            // Fresh eyes on the place it moved to, and again two ticks so the
            // behaviour phase runs against a visibility grid that has had the
            // new watcher stamped into it.
            spawnFogAttackUnit(sim, "watcher", us, SimVector(100_ss, 0_ss, 300_ss), script);
            tick(sim, 2);

            REQUIRE(sim.canSeeUnit(us, quarryId));
            auto seenAgain = rememberedPosition(sim, attackerId);
            REQUIRE(seenAgain.has_value());
            REQUIRE(seenAgain->x == secondSpot.x);
            REQUIRE(seenAgain->z == secondSpot.z);
        }
    }

    TEST_CASE("a target that was never seen is not lost, and a ground target never hides", "[fogattack]")
    {
        // An attack order is issued in play by clicking something visible, so
        // the remembered position is set on the first tick and the never-seen
        // case does not arise. Where it does -- an order from something with
        // its own notion of what it knows -- the live position is kept rather
        // than the order being quietly dropped, which is what RWE did before
        // and is the conservative half of this change.
        auto script = makeEmptyCobScript({"base"});
        GameSimulation sim(makeFogAttackTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");

        registerFogAttackModel(sim, "attackermodel");
        registerFogAttackModel(sim, "quarrymodel");
        sim.unitDefinitions["attacker"] = makeBlindAttackerDef();
        sim.unitDefinitions["quarry"] = makeQuarryDef();
        defineAttackerWeapon(sim);

        auto quarryId = spawnFogAttackUnit(sim, "quarry", them, SimVector(0_ss, 0_ss, 0_ss), script);
        auto attackerId = spawnFogAttackUnit(sim, "attacker", us, SimVector(-400_ss, 0_ss, 0_ss), script);
        UnitWeapon weapon;
        weapon.weaponType = "attackergun";
        sim.getUnitState(attackerId).weapons[0] = weapon;
        sim.getUnitState(attackerId).orders.push_back(AttackOrder(quarryId));

        tick(sim, 10);

        // Nobody ever saw it, so nothing was remembered ...
        REQUIRE_FALSE(sim.canSeeUnit(us, quarryId));
        REQUIRE_FALSE(rememberedPosition(sim, attackerId).has_value());

        // ... and the order is still being carried out rather than dropped.
        REQUIRE_FALSE(sim.getUnitState(attackerId).orders.empty());
        const auto& goal = sim.getUnitState(attackerId).navigationState.desiredDestination;
        REQUIRE(goal.has_value());

        SECTION("a place is a place whether or not anyone is looking at it")
        {
            auto& state = sim.getUnitState(attackerId);
            state.orders.clear();
            state.orders.push_back(AttackOrder(SimVector(200_ss, 0_ss, 200_ss)));

            tick(sim, 5);

            // A ground target keeps no remembered position: there is nothing
            // to lose sight of.
            REQUIRE_FALSE(rememberedPosition(sim, attackerId).has_value());
            REQUIRE_FALSE(sim.getUnitState(attackerId).orders.empty());
        }
    }
}
