#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <optional>
#include <vector>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeTurretTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addTurretPlayer(GameSimulation& sim, const std::string& name)
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

        /**
         * A script with no functions in it. The aiming code asks the COB
         * environment for an AimPrimary and gets nothing back, which is the
         * case the fixture wants: a turreted weapon then goes straight to
         * firing, so anything that stops a shot here is the hull check and
         * not the script.
         */
        std::shared_ptr<CobScript> makeTurretScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        /**
         * The one piece sits twenty units up, because it doubles as the firing
         * point: a muzzle at ground level puts the round into the dirt on the
         * tick it is created and there is then nothing left to observe.
         */
        void registerTurretModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineTurretUnit(GameSimulation& sim, const std::string& type, bool canAttack)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = canAttack;
            d.sightDistance = 1000u;
            d.maxHitPoints = 1000000;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        void defineTurretWeapon(GameSimulation& sim, const std::string& name, bool turret, bool verticalLaunch, unsigned int tolerance)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 400_ss;
            w.reloadTime = 1000_ss;
            w.burst = 1;
            w.velocity = 450_ss / 30_ss;
            w.damageRadius = 4_ss;
            w.damage["DEFAULT"] = 1;
            w.turret = turret;
            w.verticalLaunch = verticalLaunch;
            w.tolerance = SimAngle(tolerance);
            w.pitchTolerance = SimAngle(tolerance);
            sim.weaponDefinitions[name] = w;
        }

        UnitId spawnTurretUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        void armWithTurretWeapon(GameSimulation& sim, UnitId id, const std::string& weaponType)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            sim.getUnitState(id).weapons[0] = weapon;
        }

    }

    TEST_CASE("a weapon bolted to the hull waits for the hull to come round", "[turret]")
    {
        // Every weapon in the shipped data that leaves turret out is mounted
        // rigidly: aircraft cannon, torpedo tubes, gunship rockets. The
        // original never runs an aim script for them (0x49E205 sends them
        // past the call), and the fire handler it picks for them measures the
        // bearing to the target against the unit's own heading before letting
        // the shot go (0x49DA59 into 0x49D880). Rotation zero faces +Z, so a
        // target at -Z is directly behind.
        auto script = makeTurretScript();
        GameSimulation sim(makeTurretTerrain(), 0u, 0, 0);
        auto us = addTurretPlayer(sim, "us");
        auto them = addTurretPlayer(sim, "them");
        registerTurretModel(sim);
        defineTurretUnit(sim, "gunship", true);
        defineTurretUnit(sim, "victim", false);
        // 8000 of 65536 is a shade under 44 degrees, which is what the four
        // gunship rocket definitions in the shipped data ask for.
        defineTurretWeapon(sim, "hullRocket", false, false, 8000u);
        defineTurretWeapon(sim, "turretGun", true, false, 8000u);

        SECTION("facing away it holds its fire, and fires once turned around")
        {
            auto shooterId = spawnTurretUnit(sim, "gunship", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithTurretWeapon(sim, shooterId, "hullRocket");
            spawnTurretUnit(sim, "victim", them, SimVector(0_ss, 0_ss, -128_ss), script);

            REQUIRE_FALSE(everFires(sim, 30));

            // It has a target -- the hull check is about shooting, not about
            // choosing -- but nothing has left the barrel.
            REQUIRE(std::holds_alternative<UnitWeaponStateAttacking>(sim.getUnitState(shooterId).weapons[0]->state));

            // Bring the nose round to the target.
            sim.getUnitState(shooterId).rotation = HalfTurn;

            REQUIRE(everFires(sim, 5));
        }

        SECTION("a turreted gun on the same hull shoots over its shoulder straight away")
        {
            auto shooterId = spawnTurretUnit(sim, "gunship", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithTurretWeapon(sim, shooterId, "turretGun");
            spawnTurretUnit(sim, "victim", them, SimVector(0_ss, 0_ss, -128_ss), script);

            REQUIRE(everFires(sim, 10));
        }

        SECTION("dead ahead is fine without turning at all")
        {
            auto shooterId = spawnTurretUnit(sim, "gunship", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithTurretWeapon(sim, shooterId, "hullRocket");
            spawnTurretUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 128_ss), script);

            REQUIRE(everFires(sim, 10));
        }
    }

    TEST_CASE("the hull only has to be within tolerance, not exactly on", "[turret]")
    {
        // The check is an absolute difference against tolerance, so the
        // window is symmetric about the nose and its width is the weapon's
        // own figure rather than anything fixed.
        auto script = makeTurretScript();

        auto fires = [&](unsigned int tolerance, SimAngle hullRotation) {
            GameSimulation sim(makeTurretTerrain(), 0u, 0, 0);
            auto us = addTurretPlayer(sim, "us");
            auto them = addTurretPlayer(sim, "them");
            registerTurretModel(sim);
            defineTurretUnit(sim, "gunship", true);
            defineTurretUnit(sim, "victim", false);
            defineTurretWeapon(sim, "hullRocket", false, false, tolerance);

            auto shooterId = spawnTurretUnit(sim, "gunship", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithTurretWeapon(sim, shooterId, "hullRocket");
            sim.getUnitState(shooterId).rotation = hullRotation;
            spawnTurretUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 128_ss), script);

            return everFires(sim, 10);
        };

        SECTION("a nose off by less than tolerance still fires")
        {
            REQUIRE(fires(8000u, SimAngle(4000)));
            REQUIRE(fires(8000u, SimAngle(65536u - 4000u)));
        }

        SECTION("a nose off by more than tolerance does not")
        {
            REQUIRE_FALSE(fires(8000u, SimAngle(12000)));
            REQUIRE_FALSE(fires(8000u, SimAngle(65536u - 12000u)));
        }

        SECTION("a torpedo tube's 32767 lets it fire very nearly astern")
        {
            // Six of the shipped torpedoes ask for 32767, one short of a half
            // circle: everything except dead astern, which misses by the odd
            // unit in the original too, since the difference is taken as a
            // signed 16-bit quantity and 32768 negates to itself.
            REQUIRE(fires(32767u, SimAngle(32000)));
            REQUIRE_FALSE(fires(32767u, HalfTurn));
        }
    }

    TEST_CASE("a vertical launch does not care which way the launcher faces", "[turret]")
    {
        // vlaunch weapons are turret=0 too, but the original hands them a
        // different fire handler (0x49DB70, chosen at 0x49E030) and that one
        // never looks at the unit's heading -- the missile leaves the tube
        // upwards and turns onto the target afterwards.
        auto script = makeTurretScript();
        GameSimulation sim(makeTurretTerrain(), 0u, 0, 0);
        auto us = addTurretPlayer(sim, "us");
        auto them = addTurretPlayer(sim, "them");
        registerTurretModel(sim);
        defineTurretUnit(sim, "silo", true);
        defineTurretUnit(sim, "victim", false);
        defineTurretWeapon(sim, "siloRocket", false, true, 4000u);

        auto shooterId = spawnTurretUnit(sim, "silo", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWithTurretWeapon(sim, shooterId, "siloRocket");
        spawnTurretUnit(sim, "victim", them, SimVector(0_ss, 0_ss, -128_ss), script);

        REQUIRE(everFires(sim, 10));
    }

    TEST_CASE("pitch tolerance falls back to the weapon's own tolerance", "[turret]")
    {
        // Sixty of the shipped weapons name a tolerance and say nothing about
        // pitch. The original reads pitchtolerance, finds it zero and uses
        // tolerance instead (0x49D8B4-0x49D8CF) rather than any fixed figure,
        // which matters most for the aircraft weapons whose tolerances run to
        // tens of degrees.
        TdfBlock block;
        block.insertOrAssignProperty("tolerance", "8000");
        auto w = parseWeaponBlock(block);

        REQUIRE(w.tolerance == 8000u);
        REQUIRE(w.pitchTolerance == 8000u);

        TdfBlock blockBoth;
        blockBoth.insertOrAssignProperty("tolerance", "6000");
        blockBoth.insertOrAssignProperty("pitchtolerance", "12000");
        auto both = parseWeaponBlock(blockBoth);

        REQUIRE(both.tolerance == 6000u);
        REQUIRE(both.pitchTolerance == 12000u);

        TdfBlock empty;
        auto neither = parseWeaponBlock(empty);

        REQUIRE(neither.tolerance == 256u);
        REQUIRE(neither.pitchTolerance == 256u);
    }
}
