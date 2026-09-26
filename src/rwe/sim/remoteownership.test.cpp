#include <catch2/catch_test_macros.hpp>
#include <rwe/game/save_util.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /** A ground unit that walks, and can hold a gun when one is named. */
        UnitDefinition makeOwnershipUnitDef()
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = true;
            d.canAttack = true;
            d.maxVelocity = 2_ss;
            d.acceleration = 0.2_ssf;
            d.brakeRate = 0.2_ssf;
            d.turnRate = 2000_ss;
            d.maxHitPoints = 1000;
            d.buildTime = 0u;
            d.sightDistance = 512u;
            d.shootMe = true;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 17u, 255u, 0u, 12u};
            return d;
        }

        void defineOwnershipWorld(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            auto walker = makeOwnershipUnitDef();
            sim.unitDefinitions["WALKER"] = walker;

            auto shooter = makeOwnershipUnitDef();
            shooter.weapon1 = "LASER";
            shooter.standingFireOrder = UnitFireOrders::FireAtWill;
            shooter.category = "ARM ALL";
            sim.unitDefinitions["SHOOTER"] = shooter;

            auto victim = makeOwnershipUnitDef();
            victim.canAttack = false;
            victim.category = "CORE ALL";
            sim.unitDefinitions["VICTIM"] = victim;

            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 600_ss;
            w.reloadTime = SimScalar(0.2f);
            w.burst = 1;
            w.velocity = 960_ss / 30_ss;
            w.damageRadius = 16_ss;
            w.damage["DEFAULT"] = 30;
            w.turret = true;
            w.tolerance = SimAngle(1000);
            w.pitchTolerance = SimAngle(1000);
            w.energyPerShot = Energy(0.0f);
            sim.weaponDefinitions["LASER"] = w;

            auto script = *makeEmptyCobScript({"base"});
            sim.unitScriptDefinitions["WALKER"] = script;
            sim.unitScriptDefinitions["SHOOTER"] = script;
            sim.unitScriptDefinitions["VICTIM"] = script;
        }

        UnitId spawnOwnershipUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos)
        {
            auto id = sim.trySpawnUnit(type, owner, pos, std::nullopt);
            REQUIRE(id.has_value());
            sim.getUnitState(*id).hitPoints = sim.unitDefinitions.at(type).maxHitPoints;
            return *id;
        }

        void markRemote(GameSimulation& sim, PlayerId player)
        {
            sim.getPlayer(player).simulation = PlayerSimulation::Remote;
        }

        /**
         * Whether a shot is fired within the window. The projectile itself can
         * leave the world on the tick it is born -- a round that hits the
         * ground it was fired into -- so the firing event is the reliable
         * signal, as in weaponfiretick.test.cpp.
         */
        bool firesWithin(GameSimulation& sim, int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                sim.events.clear();
                sim.tick();
                for (const auto& e : sim.events)
                {
                    if (std::get_if<FireWeaponEvent>(&e) != nullptr)
                    {
                        return true;
                    }
                }
            }
            return false;
        }
    }

    TEST_CASE("a remote player's unit takes no orders and a local one does", "[remote]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineOwnershipWorld(sim);

        auto localUnit = spawnOwnershipUnit(sim, "WALKER", local, SimVector(200_ss, 0_ss, 200_ss));
        auto remoteUnit = spawnOwnershipUnit(sim, "WALKER", remote, SimVector(200_ss, 0_ss, 300_ss));
        markRemote(sim, remote);

        auto target = SimVector(600_ss, 0_ss, 200_ss);
        sim.getUnitState(localUnit).orders.push_back(MoveOrder(target));
        sim.getUnitState(remoteUnit).orders.push_back(MoveOrder(target));

        auto localStart = sim.getUnitState(localUnit).position;
        auto remoteStart = sim.getUnitState(remoteUnit).position;

        tick(sim, 120);

        auto localMoved = sim.getUnitState(localUnit).position != localStart;
        auto remoteStayed = sim.getUnitState(remoteUnit).position == remoteStart;
        REQUIRE(localMoved);
        REQUIRE(remoteStayed);
    }

    TEST_CASE("a remote unit with a weapon never fires and a local one does", "[remote]")
    {
        auto setup = [](bool shooterIsRemote) {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Circular;
            auto local = addPlayer(sim, "local");
            auto remote = addPlayer(sim, "remote");
            defineOwnershipWorld(sim);

            auto shooterOwner = shooterIsRemote ? remote : local;
            auto victimOwner = shooterIsRemote ? local : remote;
            spawnOwnershipUnit(sim, "SHOOTER", shooterOwner, SimVector(100_ss, 0_ss, 100_ss));
            spawnOwnershipUnit(sim, "VICTIM", victimOwner, SimVector(100_ss, 0_ss, 140_ss));

            markRemote(sim, remote);

            return firesWithin(sim, 200);
        };

        REQUIRE(setup(false));
        REQUIRE_FALSE(setup(true));
    }

    TEST_CASE("local damage cannot touch a remote unit but recorded damage can", "[remote]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineOwnershipWorld(sim);
        markRemote(sim, remote);

        auto id = spawnOwnershipUnit(sim, "WALKER", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto health = sim.unitDefinitions.at("WALKER").maxHitPoints;

        sim.applyDamage(id, 100);
        REQUIRE(sim.getUnitState(id).hitPoints == health);

        Projectile blast{};
        blast.owner = local;
        blast.position = sim.getUnitState(id).position;
        blast.previousPosition = blast.position;
        blast.origin = blast.position;
        blast.damage["DEFAULT"] = 100000;
        blast.damageRadius = 100_ss;
        blast.edgeEffectiveness = 0_ss;
        sim.applyDamageInRadius(blast.position, blast.damageRadius, blast);
        REQUIRE(sim.getUnitState(id).hitPoints == health);

        applyRemoteDamage(sim, id, 100);
        REQUIRE(sim.getUnitState(id).hitPoints == health - 100);

        applyRemoteDamage(sim, id, health);
        REQUIRE(sim.getUnitState(id).hitPoints == 1);
        REQUIRE(sim.getUnitState(id).isAlive());

        applyRemoteDamage(sim, UnitId(9999), 100);
    }

    TEST_CASE("a remote unit can still be killed by the owner's death record", "[remote]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto remote = addPlayer(sim, "remote");
        defineOwnershipWorld(sim);
        markRemote(sim, remote);

        auto id = spawnOwnershipUnit(sim, "WALKER", remote, SimVector(200_ss, 0_ss, 200_ss));
        sim.killUnit(id);
        tick(sim, 1);

        REQUIRE_FALSE(sim.tryGetUnitState(id).has_value());
    }

    TEST_CASE("a remote player's resources are not settled and a local one's are", "[remote]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineOwnershipWorld(sim);

        auto maker = makeOwnershipUnitDef();
        maker.isMobile = false;
        maker.canMove = false;
        maker.makesMetal = Metal(1.0f);
        maker.metalStorage = Metal(500.0f);
        maker.movementCollisionInfo = UnitDefinition::AdHocMovementClass{2u, 2u, 255u, 255u, 0u, 255u};
        sim.unitDefinitions["MAKER"] = maker;
        sim.unitScriptDefinitions["MAKER"] = *makeEmptyCobScript({"base"});

        spawnOwnershipUnit(sim, "MAKER", local, SimVector(100_ss, 0_ss, 100_ss));
        spawnOwnershipUnit(sim, "MAKER", remote, SimVector(300_ss, 0_ss, 300_ss));
        markRemote(sim, remote);

        auto remoteMetal = sim.getPlayer(remote).metal;
        auto localMetal = sim.getPlayer(local).metal;

        tick(sim, 90);

        REQUIRE(sim.getPlayer(remote).metal == remoteMetal);
        REQUIRE(sim.getPlayer(local).metal != localMetal);
    }

    TEST_CASE("a save refuses a remote player and accepts an all-local game", "[remote]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        addPlayer(sim, "other");
        defineOwnershipWorld(sim);
        spawnOwnershipUnit(sim, "WALKER", local, SimVector(200_ss, 0_ss, 200_ss));

        REQUIRE_FALSE(hasRemotePlayer(sim));
        REQUIRE_NOTHROW(saveSimulationToJson(sim));

        markRemote(sim, local);
        REQUIRE(hasRemotePlayer(sim));
        REQUIRE_THROWS(saveSimulationToJson(sim));
    }

    TEST_CASE("the sync hash does not depend on the simulation flag", "[remote]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "player");
        defineOwnershipWorld(sim);
        spawnOwnershipUnit(sim, "WALKER", player, SimVector(200_ss, 0_ss, 200_ss));

        auto before = computeHashOf(sim);
        markRemote(sim, player);
        auto after = computeHashOf(sim);

        REQUIRE(before == after);
    }
}
