#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/cob/CobOpCode.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <rwe/sim/sim_test_util.h>

/**
 * `SetMaxReloadTime`, and the aiming pose a unit holds between shots.
 *
 * The original hands the script the unit's longest reload as it initialises
 * the weapon slots. `0x49E070` walks the three of them, keeps the largest
 * `reloadtime` (`wdef+0xE4`, a word the parser has already multiplied by 30,
 * so a tick count), and at `0x49E14B`-`0x49E17A` turns it into milliseconds:
 * `ticks * 1000`, then a signed divide by 30 done with the magic constant
 * `0x88888889` and a shift of four. `0x49E186` starts `"SetMaxReloadTime"`
 * with the result through StartScriptByName (`0x4B0A70`), and the string at
 * `0x509704` has that one reference in the whole image.
 *
 * Forty-two of the 157 shipped scripts implement it, and every one does the
 * same thing with the number -- `restore_delay = time * n` -- where
 * `restore_delay` is the sleep at the top of `RestoreAfterDelay`, the thread
 * that stows whatever aiming opened up. `AimPrimary` signals its aim signal
 * on the way in, which kills any restore still sleeping, so the pose is held
 * for as long as the unit keeps aiming and for `restore_delay` after it
 * stops.
 *
 * A script that is never told keeps whatever `Create()` set, which for every
 * one of the forty-two is 3000 -- three seconds. Nine of the shipped weapons
 * take longer than that to reload.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeReloadTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        void registerReloadModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        /**
         * `CORMSHIP_ROCKET` out of `weapons/ROCKETS.TDF`: range 1300,
         * reloadtime 9, tolerance 4000, `vlaunch` (which still runs the aim
         * script -- S:11) and `lineofsight`.
         */
        WeaponDefinition makeShipRocket()
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 1300_ss;
            w.reloadTime = 9_ss;
            w.burst = 1;
            w.velocity = 400_ss / 30_ss;
            w.damageRadius = 48_ss;
            w.damage["DEFAULT"] = 500;
            w.verticalLaunch = true;
            w.tolerance = SimAngle(4000);
            w.pitchTolerance = SimAngle(4000);
            return w;
        }

        /** `CORSHIP_MISSILE` from `weapons/MISSILES.TDF`: reloadtime 2. */
        WeaponDefinition makeShipMissile()
        {
            auto w = makeShipRocket();
            w.reloadTime = 2_ss;
            w.maxRange = 700_ss;
            w.damage["DEFAULT"] = 105;
            return w;
        }

        /** `ARMMSHIP_ROCKET`: the same weapon on the Arm side, reloadtime 12. */
        WeaponDefinition makeArmShipRocket()
        {
            auto w = makeShipRocket();
            w.reloadTime = 12_ss;
            return w;
        }

        void defineShip(GameSimulation& sim, const std::string& type, const std::string& weapon1, const std::string& weapon2)
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
            d.standingFireOrder = UnitFireOrders::FireAtWill;
            d.category = "CORE SHIP LEVEL2 WEAPON NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            d.weapon1 = weapon1;
            d.weapon2 = weapon2;
            sim.unitDefinitions[type] = d;
        }

        void defineVictim(GameSimulation& sim)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = false;
            d.sightDistance = 100u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions["VICTIM"] = d;
        }

        void push(CobScript& script, OpCode op)
        {
            script.instructions.push_back(static_cast<uint32_t>(op));
        }

        void push(CobScript& script, uint32_t operand)
        {
            script.instructions.push_back(operand);
        }

        void beginFunction(CobScript& script, const std::string& name)
        {
            script.functions.push_back(CobFunctionInfo{name, static_cast<unsigned int>(script.instructions.size())});
        }

        void endFunction(CobScript& script, uint32_t returnValue)
        {
            push(script, OpCode::PUSH_CONSTANT);
            push(script, returnValue);
            push(script, OpCode::RETURN);
        }

        /**
         * The bare recorder: nothing but a `SetMaxReloadTime` that files the
         * number it was given in static 0, so a test can read back exactly
         * what the engine sent.
         */
        std::shared_ptr<CobScript> makeRecordingScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 1;
            script->pieces.push_back("base");

            beginFunction(*script, "SetMaxReloadTime");
            push(*script, OpCode::PUSH_LOCAL_VAR);
            push(*script, 0u);
            push(*script, OpCode::POP_STATIC);
            push(*script, 0u);
            endFunction(*script, 0u);

            return script;
        }

        constexpr unsigned int SigAim = 2;
        constexpr unsigned int StaticRestoreDelay = 0;
        constexpr unsigned int StaticHatchOpen = 1;

        /**
         * CORMSHIP.BOS in miniature. `Create()` sets `restore_delay=3000` and
         * `SetMaxReloadTime` doubles what it is told, both exactly as the
         * shipped script does; `AimPrimary` signals `SIG_AIM1`, masks itself
         * with it, opens the hatches and starts `RestoreAfterDelay`, which
         * sleeps `restore_delay` and shuts them again. Any fresh aim kills the
         * sleeping restore, which is what is supposed to hold the hatches open
         * between one target and the next.
         */
        std::shared_ptr<CobScript> makeMissileShipScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 2;
            script->pieces.push_back("base");

            // 0: Create
            beginFunction(*script, "Create");
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 3000u);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticRestoreDelay);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 0u);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticHatchOpen);
            endFunction(*script, 0u);

            // 1: SetMaxReloadTime(time) { restore_delay = time * 2; }
            beginFunction(*script, "SetMaxReloadTime");
            push(*script, OpCode::PUSH_LOCAL_VAR);
            push(*script, 0u);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 2u);
            push(*script, OpCode::MUL);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticRestoreDelay);
            endFunction(*script, 0u);

            // 2: RestoreAfterDelay() { sleep restore_delay; hatches shut; }
            const auto restoreAfterDelayId = static_cast<uint32_t>(script->functions.size());
            beginFunction(*script, "RestoreAfterDelay");
            push(*script, OpCode::PUSH_STATIC);
            push(*script, StaticRestoreDelay);
            push(*script, OpCode::SLEEP);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 0u);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticHatchOpen);
            endFunction(*script, 0u);

            // 3: AimPrimary(heading, pitch)
            beginFunction(*script, "AimPrimary");
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, SigAim);
            push(*script, OpCode::SIGNAL);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, SigAim);
            push(*script, OpCode::SET_SIGNAL_MASK);
            push(*script, OpCode::PUSH_CONSTANT);
            push(*script, 1u);
            push(*script, OpCode::POP_STATIC);
            push(*script, StaticHatchOpen);
            push(*script, OpCode::START_SCRIPT);
            push(*script, restoreAfterDelayId);
            push(*script, 0u);
            endFunction(*script, 1u);

            return script;
        }

        int readStatic(GameSimulation& sim, UnitId id, unsigned int index)
        {
            return sim.getUnitState(id).cobEnvironment->_statics.at(index);
        }
    }

    TEST_CASE("a unit is told its longest reload in milliseconds", "[weapon][cob]")
    {
        auto script = makeRecordingScript();
        GameSimulation sim(makeReloadTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        registerReloadModel(sim);

        sim.weaponDefinitions["CORMSHIP_ROCKET"] = makeShipRocket();
        sim.weaponDefinitions["CORSHIP_MISSILE"] = makeShipMissile();
        sim.weaponDefinitions["ARMMSHIP_ROCKET"] = makeArmShipRocket();

        SECTION("the Core Missile Frigate hears nine seconds, not two")
        {
            // CORMSHIP.FBI: Weapon1=CORMSHIP_ROCKET (reloadtime 9),
            // Weapon2=CORSHIP_MISSILE (reloadtime 2). The larger wins, and
            // 9 seconds is 270 ticks, which is 9000 ms.
            defineShip(sim, "CORMSHIP", "CORMSHIP_ROCKET", "CORSHIP_MISSILE");
            sim.unitScriptDefinitions["CORMSHIP"] = *script;

            auto id = sim.trySpawnUnit("CORMSHIP", us, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
            REQUIRE(id);
            REQUIRE(readStatic(sim, *id, 0) == 9000);
        }

        SECTION("the Arm Ranger hears twelve")
        {
            defineShip(sim, "ARMMSHIP", "ARMMSHIP_ROCKET", "CORSHIP_MISSILE");
            sim.unitScriptDefinitions["ARMMSHIP"] = *script;

            auto id = sim.trySpawnUnit("ARMMSHIP", us, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
            REQUIRE(id);
            REQUIRE(readStatic(sim, *id, 0) == 12000);
        }

        SECTION("a unit with no weapons at all is still told, and told zero")
        {
            // 0x49E186 is not guarded on there being a weapon: the maximum
            // simply stays at the zero 0x49E07D put there.
            defineShip(sim, "UNARMED", "", "");
            sim.unitScriptDefinitions["UNARMED"] = *script;

            auto id = sim.trySpawnUnit("UNARMED", us, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
            REQUIRE(id);
            REQUIRE(readStatic(sim, *id, 0) == 0);
        }
    }

    TEST_CASE("a missile ship holds its hatches open between targets", "[weapon][cob]")
    {
        // The reported fault, end to end. With `SetMaxReloadTime` never sent,
        // the script keeps the 3000 its Create() set, so three seconds after
        // a target dies -- long before the nine-second reload is up -- the
        // hatches shut, only to be opened again by the next engagement.
        auto script = makeMissileShipScript();
        GameSimulation sim(makeReloadTerrain(), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");
        registerReloadModel(sim);

        sim.weaponDefinitions["CORMSHIP_ROCKET"] = makeShipRocket();
        sim.weaponDefinitions["CORSHIP_MISSILE"] = makeShipMissile();
        defineShip(sim, "CORMSHIP", "CORMSHIP_ROCKET", "CORSHIP_MISSILE");
        defineVictim(sim);
        sim.unitScriptDefinitions["CORMSHIP"] = *script;
        sim.unitScriptDefinitions["VICTIM"] = *script;

        auto shipId = sim.trySpawnUnit("CORMSHIP", us, SimVector(0_ss, 0_ss, 0_ss), std::nullopt);
        REQUIRE(shipId);
        auto victimId = sim.trySpawnUnit("VICTIM", them, SimVector(0_ss, 0_ss, 128_ss), std::nullopt);
        REQUIRE(victimId);
        sim.getUnitState(*shipId).hitPoints = sim.unitDefinitions.at("CORMSHIP").maxHitPoints;
        sim.getUnitState(*victimId).hitPoints = sim.unitDefinitions.at("VICTIM").maxHitPoints;

        // Create ran at spawn and set the script's own three-second default;
        // the engine's call then doubled the nine-second reload over it.
        REQUIRE(readStatic(sim, *shipId, StaticRestoreDelay) == 18000);

        // Let it find the target and swing onto it.
        tick(sim, 10);
        REQUIRE(readStatic(sim, *shipId, StaticHatchOpen) == 1);

        // The target dies. Nothing aims after this.
        sim.quietlyKillUnit(*victimId);

        // Four seconds later -- past the script's three-second default, well
        // inside a nine-second reload -- the hatches are still open, waiting
        // for the next thing to shoot at.
        tick(sim, 120);
        REQUIRE(readStatic(sim, *shipId, StaticHatchOpen) == 1);
    }
}
