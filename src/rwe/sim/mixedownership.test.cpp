#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/puppet_test_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MixedOwnership.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /** A ground unit that walks, and can hold a gun when one is named. */
        UnitDefinition makeMixedUnitDef()
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

        /**
         * A script whose `Killed` ladder asks for a particular corpse level:
         * `Killed(severity, corpsetype) { corpsetype = level; return 0; }`.
         * The shipped ones are three-band ladders off the severity; what they
         * write into the second parameter is all the spawner reads.
         */
        std::shared_ptr<CobScript> makeKilledLevelScript(int level)
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            script->instructions = {
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                static_cast<uint32_t>(level),
                static_cast<uint32_t>(OpCode::POP_LOCAL_VAR),
                1u,
                static_cast<uint32_t>(OpCode::PUSH_CONSTANT),
                0u,
                static_cast<uint32_t>(OpCode::RETURN)};
            script->functions.push_back(CobFunctionInfo{"Killed", 0u});
            return script;
        }

        void defineMixedWorld(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 0_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));

            auto walker = makeMixedUnitDef();
            sim.unitDefinitions["WALKER"] = walker;

            auto shooter = makeMixedUnitDef();
            shooter.weapon1 = "LASER";
            shooter.standingFireOrder = UnitFireOrders::FireAtWill;
            shooter.category = "ARM ALL";
            sim.unitDefinitions["SHOOTER"] = shooter;

            auto victim = makeMixedUnitDef();
            victim.canAttack = false;
            victim.category = "CORE ALL";
            sim.unitDefinitions["VICTIM"] = victim;

            auto commander = makeMixedUnitDef();
            commander.commander = true;
            commander.category = "ARM ALL";
            sim.unitDefinitions["COMMANDER"] = commander;

            auto bomber = makeMixedUnitDef();
            bomber.explodeAs = "BLAST";
            bomber.category = "ARM ALL";
            sim.unitDefinitions["BOMBER"] = bomber;

            auto builder = makeMixedUnitDef();
            builder.builder = true;
            builder.canReclamate = true;
            builder.workerTimePerTick = 30u;
            builder.buildDistance = 200_ss;
            builder.energyStorage = Energy(10000.0f);
            builder.metalStorage = Metal(10000.0f);
            builder.category = "ARM ALL";
            sim.unitDefinitions["BUILDER"] = builder;

            auto captor = makeMixedUnitDef();
            captor.builder = true;
            captor.canCapture = true;
            captor.buildDistance = 200_ss;
            captor.workerTimePerTick = 30u;
            captor.category = "ARM ALL";
            sim.unitDefinitions["CAPTOR"] = captor;

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

            // A death explosion: no turret, a wide enough blast to matter.
            WeaponDefinition blast{};
            blast.physicsType = ProjectilePhysicsTypeLineOfSight();
            blast.velocity = 960_ss / 30_ss;
            blast.damageRadius = 96_ss;
            blast.damage["DEFAULT"] = 500;
            sim.weaponDefinitions["BLAST"] = blast;

            FeatureDefinition rock{};
            rock.name = "ROCK";
            rock.footprintX = 2;
            rock.footprintZ = 2;
            rock.height = 20_ss;
            rock.blocking = true;
            rock.reclaimable = true;
            rock.damage = 1000;
            auto rockId = sim.featureDefinitions.insert(std::move(rock));
            sim.featureNameIndex.insert_or_assign("ROCK", rockId);

            auto plain = *makeEmptyCobScript({"base"});
            sim.unitScriptDefinitions["WALKER"] = plain;
            sim.unitScriptDefinitions["SHOOTER"] = plain;
            sim.unitScriptDefinitions["COMMANDER"] = plain;
            sim.unitScriptDefinitions["BUILDER"] = plain;
            sim.unitScriptDefinitions["CAPTOR"] = plain;
            sim.unitScriptDefinitions["BOMBER"] = plain;
            sim.unitScriptDefinitions["VICTIM"] = *makeKilledLevelScript(2);
        }

        /** A shell at `position`, owned by `owner`, big enough to matter. */
        Projectile makeMixedBlast(PlayerId owner, const SimVector& position, unsigned int damage)
        {
            Projectile p{};
            p.weaponType = "BLAST";
            p.owner = owner;
            p.position = position;
            p.previousPosition = position;
            p.origin = position;
            p.damage["DEFAULT"] = damage;
            p.damageRadius = 96_ss;
            p.edgeEffectiveness = 0_ss;
            return p;
        }

        UnitId spawnMixedUnit(GameSimulation& sim, const std::string& type, PlayerId owner, const SimVector& pos)
        {
            auto id = sim.trySpawnCompletedUnit(type, owner, pos, std::nullopt);
            REQUIRE(id.has_value());
            return *id;
        }

        void markRemote(GameSimulation& sim, PlayerId player)
        {
            sim.getPlayer(player).simulation = PlayerSimulation::Remote;
        }

        /**
         * Whether a shot is fired within the window, read off the firing event
         * as the other weapon tests do -- a round can leave the world on the
         * tick it is born.
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

        int countCaptures(const GameSimulation& sim)
        {
            int n = 0;
            for (const auto& e : sim.events)
            {
                if (std::holds_alternative<UnitCapturedEvent>(e))
                {
                    ++n;
                }
            }
            return n;
        }
    }

    TEST_CASE("a local hit shows on a remote unit and is recorded, but never kills it", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto attacker = spawnMixedUnit(sim, "SHOOTER", local, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = spawnMixedUnit(sim, "VICTIM", remote, SimVector(200_ss, 0_ss, 240_ss));
        auto health = sim.unitDefinitions.at("VICTIM").maxHitPoints;

        sim.applyDamage(victim, 30, attacker);
        REQUIRE(sim.getUnitState(victim).hitPoints == health - 30);
        REQUIRE(sim.getUnitState(victim).isAlive());

        auto& outbox = mixedOwnershipOutboxOf(sim);
        REQUIRE(outbox.damage.size() == 1);
        REQUIRE(outbox.damage[0].victim == victim);
        REQUIRE(outbox.damage[0].attacker == attacker);
        REQUIRE(outbox.damage[0].damage == 30u);
        REQUIRE(outbox.deaths.empty());

        // However hard it is hit, the display floors at one hit point and the
        // record goes out; only the owner's 0x0c can kill.
        sim.applyDamage(victim, 100000, attacker);
        REQUIRE(sim.getUnitState(victim).hitPoints == 1);
        REQUIRE(sim.getUnitState(victim).isAlive());
        REQUIRE(outbox.damage.size() == 2);
    }

    TEST_CASE("a remote player's own shot does not become an outgoing record", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        [[maybe_unused]] auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto shooter = spawnMixedUnit(sim, "SHOOTER", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = spawnMixedUnit(sim, "VICTIM", remote, SimVector(200_ss, 0_ss, 240_ss));

        // The blast names the remote player as its source, so this machine is
        // applying someone else's result, not reporting its own.
        Projectile blast{};
        blast.owner = remote;
        blast.position = sim.getUnitState(victim).position;
        blast.previousPosition = blast.position;
        blast.origin = blast.position;
        blast.damage["DEFAULT"] = 100;
        blast.damageRadius = 100_ss;
        blast.edgeEffectiveness = 0_ss;
        blast.attacker = shooter;
        sim.applyDamageInRadius(blast.position, blast.damageRadius, blast);

        REQUIRE(mixedOwnershipOutboxOf(sim).damage.empty());
    }

    TEST_CASE("an incoming 0x0b kills a local unit and records its 0x0c", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto attacker = spawnMixedUnit(sim, "SHOOTER", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = spawnMixedUnit(sim, "VICTIM", local, SimVector(200_ss, 0_ss, 240_ss));
        sim.getUnitState(victim).hitPoints = 10;

        applyIncomingDamage(sim, victim, attacker, 50);

        REQUIRE(sim.getUnitState(victim).isDead());

        auto& outbox = mixedOwnershipOutboxOf(sim);
        REQUIRE(outbox.deaths.size() == 1);
        REQUIRE(outbox.deaths[0].unit == victim);
        REQUIRE(outbox.deaths[0].killer == attacker);
        REQUIRE(outbox.deaths[0].cause() == 1u);
        // The local Killed ladder wrote "corpse level 2".
        REQUIRE(outbox.deaths[0].corpseLevel() == 2u);

        // The remote attacker is credited with the kill.
        REQUIRE(sim.getUnitState(attacker).kills == 1u);
        REQUIRE(sim.getPlayer(remote).unitsKilled == 1u);
    }

    TEST_CASE("an incoming 0x0b is ignored for a unit this machine does not own", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        [[maybe_unused]] auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto victim = spawnMixedUnit(sim, "VICTIM", remote, SimVector(200_ss, 0_ss, 240_ss));
        auto health = sim.unitDefinitions.at("VICTIM").maxHitPoints;

        applyIncomingDamage(sim, victim, std::nullopt, 500);

        REQUIRE(sim.getUnitState(victim).hitPoints == health);
        REQUIRE(mixedOwnershipOutboxOf(sim).deaths.empty());
    }

    TEST_CASE("a local weapon targets a remote unit and a remote weapon targets nothing", "[mixed]")
    {
        auto fireAt = [](bool shooterIsRemote) {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
            sim.lineOfSightMode = LineOfSightMode::Circular;
            auto local = addPlayer(sim, "local");
            auto remote = addPlayer(sim, "remote");
            defineMixedWorld(sim);

            auto shooterOwner = shooterIsRemote ? remote : local;
            auto victimOwner = shooterIsRemote ? local : remote;
            spawnMixedUnit(sim, "SHOOTER", shooterOwner, SimVector(100_ss, 0_ss, 100_ss));
            spawnMixedUnit(sim, "VICTIM", victimOwner, SimVector(100_ss, 0_ss, 140_ss));

            markRemote(sim, remote);
            return firesWithin(sim, 200);
        };

        REQUIRE(fireAt(false));
        REQUIRE_FALSE(fireAt(true));
    }

    TEST_CASE("a remote unit's death explosion leaves a local unit alone", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto neighbour = spawnMixedUnit(sim, "WALKER", local, SimVector(200_ss, 0_ss, 200_ss));
        auto health = sim.unitDefinitions.at("WALKER").maxHitPoints;

        // The dying unit's explodeAs is a record of what its owner's machine
        // decided; it reaches the neighbour as that owner's 0x0b.
        auto remoteBomber = spawnMixedUnit(sim, "BOMBER", remote, SimVector(200_ss, 0_ss, 240_ss));
        sim.killUnit(remoteBomber);
        REQUIRE(sim.getUnitState(neighbour).hitPoints == health);
        REQUIRE(mixedOwnershipOutboxOf(sim).deaths.empty());

        // A Local player's own explosion is its to resolve, and does.
        tick(sim, 1);
        auto localBomber = spawnMixedUnit(sim, "BOMBER", local, SimVector(200_ss, 0_ss, 240_ss));
        sim.killUnit(localBomber);
        REQUIRE(sim.getUnitState(neighbour).hitPoints < health);
    }

    TEST_CASE("a projectile owned by a remote player leaves a local unit alone", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto victim = spawnMixedUnit(sim, "WALKER", local, SimVector(200_ss, 0_ss, 200_ss));
        auto health = sim.unitDefinitions.at("WALKER").maxHitPoints;
        auto at = sim.getUnitState(victim).position;

        // What a puppet's display round does here is show an explosion; the
        // owner's 0x0b is the only thing that costs the unit anything.
        sim.doProjectileImpact(makeMixedBlast(remote, at, 500), ImpactType::Normal);
        REQUIRE(sim.getUnitState(victim).hitPoints == health);

        sim.doProjectileImpact(makeMixedBlast(local, at, 500), ImpactType::Normal);
        REQUIRE(sim.getUnitState(victim).hitPoints < health);
    }

    TEST_CASE("an incoming 0x0b is what does apply a remote player's damage", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto attacker = spawnMixedUnit(sim, "SHOOTER", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto victim = spawnMixedUnit(sim, "WALKER", local, SimVector(200_ss, 0_ss, 240_ss));
        auto health = sim.unitDefinitions.at("WALKER").maxHitPoints;

        sim.doProjectileImpact(makeMixedBlast(remote, sim.getUnitState(victim).position, 500), ImpactType::Normal);
        REQUIRE(sim.getUnitState(victim).hitPoints == health);

        applyIncomingDamage(sim, victim, attacker, 500);
        REQUIRE(sim.getUnitState(victim).hitPoints == health - 500);
    }

    TEST_CASE("a remote player's round leaves a feature alone", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto rockId = sim.featureNameIndex.at("ROCK");
        auto rock = sim.addFeature(rockId, 10, 10);
        REQUIRE(rock.has_value());
        auto health = sim.getFeature(*rock).hitPoints;
        auto at = sim.getFeature(*rock).position;

        sim.doProjectileImpact(makeMixedBlast(remote, at, 5000), ImpactType::Normal);
        REQUIRE(sim.tryGetFeature(*rock).has_value());
        REQUIRE(sim.getFeature(*rock).hitPoints == health);

        // A Local player's round breaks it up as it always did.
        sim.doProjectileImpact(makeMixedBlast(local, at, 5000), ImpactType::Normal);
        REQUIRE_FALSE(sim.tryGetFeature(*rock).has_value());
    }

    TEST_CASE("a local unit sees and collides with a remote unit", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        sim.lineOfSightMode = LineOfSightMode::Circular;
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto localUnit = spawnMixedUnit(sim, "WALKER", local, SimVector(200_ss, 0_ss, 200_ss));
        auto remoteUnit = spawnMixedUnit(sim, "WALKER", remote, SimVector(200_ss, 0_ss, 240_ss));
        tick(sim, 1);

        REQUIRE(sim.canSeeUnit(local, remoteUnit));

        // A remote unit's footprint blocks a local unit exactly as another
        // local unit's would.
        const auto& remoteDefinition = sim.unitDefinitions.at("WALKER");
        auto remoteRegion = sim.computeFootprintRegion(sim.getUnitState(remoteUnit).position, remoteDefinition.movementCollisionInfo);
        REQUIRE(sim.isCollisionAt(remoteRegion, localUnit));
    }

    TEST_CASE("a local builder cannot reclaim, repair or capture a remote unit", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto victim = spawnMixedUnit(sim, "VICTIM", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto health = sim.unitDefinitions.at("VICTIM").maxHitPoints;

        // Reclaim, straight through the step the builder's mission would call.
        REQUIRE(sim.reclaimUnitStep(victim, local, 50));
        REQUIRE(sim.getUnitState(victim).hitPoints == health);

        // Repair: the arm is refused before anything is mended.
        auto builder = spawnMixedUnit(sim, "BUILDER", local, SimVector(200_ss, 0_ss, 240_ss));
        sim.getUnitState(victim).hitPoints = health / 2;
        sim.getUnitState(builder).orders.push_back(RepairOrder(victim));
        tick(sim, 5);
        REQUIRE(sim.getUnitState(victim).hitPoints == health / 2);
        REQUIRE(sim.getUnitState(builder).orders.empty());

        // Capture: the owner does not change and no capture event fires.
        sim.events.clear();
        auto captor = spawnMixedUnit(sim, "CAPTOR", local, SimVector(240_ss, 0_ss, 200_ss));
        sim.getUnitState(captor).orders.push_back(CaptureOrder(victim));
        tick(sim, 5);
        REQUIRE(sim.getUnitState(victim).owner == remote);
        REQUIRE(sim.getUnitState(captor).orders.empty());
        REQUIRE(countCaptures(sim) == 0);
    }

    TEST_CASE("a remote player is out when the stream kills its last unit", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        [[maybe_unused]] auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);
        sim.commanderDeathMode = CommanderDeathMode::GameContinues;

        auto first = spawnMixedUnit(sim, "WALKER", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto last = spawnMixedUnit(sim, "WALKER", remote, SimVector(240_ss, 0_ss, 200_ss));

        sim.killUnit(first);
        tick(sim, 1);
        REQUIRE(sim.getPlayer(remote).status == GamePlayerStatus::Alive);

        sim.killUnit(last);
        tick(sim, 1);
        REQUIRE(sim.getPlayer(remote).status == GamePlayerStatus::Dead);
    }

    TEST_CASE("DELETEPLAYER removes a departed peer's units with the commander's quit cause", "[mixed]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        [[maybe_unused]] auto local = addPlayer(sim, "local");
        auto remote = addPlayer(sim, "remote");
        defineMixedWorld(sim);
        markRemote(sim, remote);

        auto commander = spawnMixedUnit(sim, "COMMANDER", remote, SimVector(200_ss, 0_ss, 200_ss));
        auto other = spawnMixedUnit(sim, "WALKER", remote, SimVector(240_ss, 0_ss, 200_ss));

        removeRemotePlayer(sim, remote);

        auto observation = sim.unitDeathObservations.find(commander.value);
        REQUIRE(observation != sim.unitDeathObservations.end());
        REQUIRE(observation->second.cause == "quit");

        REQUIRE(sim.getPlayer(remote).status == GamePlayerStatus::Dead);

        tick(sim, 1);
        REQUIRE_FALSE(sim.tryGetUnitState(commander).has_value());
        REQUIRE_FALSE(sim.tryGetUnitState(other).has_value());
    }

    TEST_CASE("a puppet placed on a local unit's cell leaves the local unit standing", "[mixed][puppet]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        auto local = addWellStockedPlayer(sim, "ARM");
        auto remote = addPlayer(sim, "remote");

        // TA's coordinates put this at a spot the driver will place the puppet
        // on; RWE's own coordinate is the same conversion the driver does.
        auto position = sim.terrain.topLeftCoordinateToWorld(SimVector(0_ss, 0_ss, 0_ss));
        auto localUnit = sim.trySpawnCompletedUnit("TANK", local, position, std::nullopt);
        REQUIRE(localUnit);

        TadPuppetDriver driver(sim, 8, puppetTestLoadOrder());
        driver.addPlayer(1, remote);
        driver.setExternalClock(true);

        auto layout = tadUnitStateLayout(std::vector<bool>{false, false, false}, 8);
        TadUnitState place;
        place.tick = 0;
        place.sync = TadUnitSync{0, 3, 100, 0, 0, 0, std::nullopt, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}, std::nullopt};
        driver.onPacket(TadPacket{0, 1}, {tadEncodeUnitState(place, layout)});
        driver.applyTick(0);

        // The puppet could not take a local unit's cells, so it was refused.
        REQUIRE(sim.tryGetUnitState(*localUnit).has_value());
        REQUIRE(driver.stats().spawnsRefused == 1);
    }

    TEST_CASE("a puppet placed on a remote occupant's cell takes it", "[mixed][puppet]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        definePuppetTestWorld(sim);
        addWellStockedPlayer(sim, "ARM");
        auto remote = addPlayer(sim, "remote");

        auto position = sim.terrain.topLeftCoordinateToWorld(SimVector(0_ss, 0_ss, 0_ss));
        auto occupant = sim.trySpawnCompletedUnit("TANK", remote, position, std::nullopt);
        REQUIRE(occupant);

        TadPuppetDriver driver(sim, 8, puppetTestLoadOrder());
        driver.addPlayer(1, remote);
        driver.setExternalClock(true);

        auto layout = tadUnitStateLayout(std::vector<bool>{false, false, false}, 8);
        TadUnitState place;
        place.tick = 0;
        place.sync = TadUnitSync{0, 3, 100, 0, 0, 0, std::nullopt, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}, std::nullopt};
        driver.onPacket(TadPacket{0, 1}, {tadEncodeUnitState(place, layout)});
        driver.applyTick(0);

        // The remote occupant's cells are the puppet's to claim.
        REQUIRE(driver.stats().spawnsRefused == 0);
    }
}
