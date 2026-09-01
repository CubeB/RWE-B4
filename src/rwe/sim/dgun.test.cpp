#include <catch2/catch_test_macros.hpp>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <vector>

namespace rwe
{
    namespace
    {
        MapTerrain makeDgunTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addDgunPlayer(GameSimulation& sim, const std::string& name, float metal, float energy)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(1000000.0f),
                Energy(1000000.0f),
                Metal(metal),
                Energy(energy),
            };
            return sim.addPlayer(p);
        }

        std::shared_ptr<CobScript> makeDgunScript()
        {
            auto script = std::make_shared<CobScript>();
            script->staticVariableCount = 0;
            script->pieces.push_back("base");
            return script;
        }

        /**
         * Twenty units up, because the piece doubles as the muzzle: a shot
         * spawned at ground level buries itself on the tick it is created.
         */
        void registerDgunModel(GameSimulation& sim)
        {
            std::vector<UnitPieceDefinition> pieces{UnitPieceDefinition{"base", SimVector(0_ss, 20_ss, 0_ss), std::nullopt}};
            sim.unitModelDefinitions["model"] = createUnitModelDefinition(10_ss, std::move(pieces));
        }

        void defineDgunUnit(GameSimulation& sim, const std::string& type, bool canAttack, unsigned int hitPoints)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.canAttack = canAttack;
            d.sightDistance = 1000u;
            d.maxHitPoints = hitPoints;
            d.buildTime = 0u;
            d.shootMe = true;
            d.category = "ARM LEVEL1 WEAPON NOTAIR NOTSUB";
            // Storage is recomputed from the units on the field every second,
            // so without it the player's stores would be clamped to nothing.
            d.metalStorage = Metal(1000000.0f);
            d.energyStorage = Energy(1000000.0f);
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 255u};
            sim.unitDefinitions[type] = d;
        }

        /**
         * ARM_DISINTEGRATOR as it ships, less the parts that do not bear on the
         * simulation: range 240, energypershot 400, areaofeffect 48, commandfire,
         * and a DAMAGE table whose only entry is 30000. The reload and the damage
         * are left open because the cases below want to vary them.
         */
        void defineDgunWeapon(GameSimulation& sim, const std::string& name, bool commandFire, float energy, float metal, float reloadSeconds, unsigned int damage)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            w.maxRange = 240_ss;
            w.reloadTime = SimScalar(reloadSeconds);
            w.burst = 1;
            w.velocity = 200_ss / 30_ss;
            w.damageRadius = 24_ss;
            w.damage["DEFAULT"] = damage;
            w.tolerance = SimAngle(8000u);
            w.pitchTolerance = SimAngle(8000u);
            w.energyPerShot = Energy(energy);
            w.metalPerShot = Metal(metal);
            w.commandFire = commandFire;
            sim.weaponDefinitions[name] = w;
        }

        UnitId spawnDgunUnit(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& pos, const std::shared_ptr<CobScript>& script)
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

        void armWithDgun(GameSimulation& sim, UnitId id, const std::string& weaponType)
        {
            UnitWeapon weapon;
            weapon.weaponType = weaponType;
            sim.getUnitState(id).weapons[0] = weapon;
        }

        bool anyProjectiles(const GameSimulation& sim)
        {
            for ([[maybe_unused]] const auto& p : sim.projectiles)
            {
                return true;
            }
            return false;
        }

        bool everFires(GameSimulation& sim, int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                sim.tick();
                if (anyProjectiles(sim))
                {
                    return true;
                }
            }
            return false;
        }

        void tick(GameSimulation& sim, int ticks)
        {
            for (int i = 0; i < ticks; ++i)
            {
                sim.tick();
            }
        }

        /**
         * A shooter and something to shoot at, a hundred and twenty-eight units
         * apart, which is well inside the D-gun's 240.
         */
        struct Duel
        {
            PlayerId us;
            PlayerId them;
            UnitId shooter;
            UnitId victim;
        };

        Duel setUpDuel(GameSimulation& sim, const std::shared_ptr<CobScript>& script, float metal, float energy, unsigned int victimHitPoints, int victimDamageModifier)
        {
            auto us = addDgunPlayer(sim, "us", metal, energy);
            auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
            registerDgunModel(sim);
            defineDgunUnit(sim, "commander", true, 3000);
            defineDgunUnit(sim, "victim", false, victimHitPoints);
            sim.unitDefinitions["victim"].damageModifier = victimDamageModifier;

            auto shooterId = spawnDgunUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            auto victimId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 128_ss), script);
            return Duel{us, them, shooterId, victimId};
        }

        unsigned int damageTaken(const GameSimulation& sim, UnitId unitId)
        {
            const auto& unit = sim.getUnitState(unitId);
            return sim.unitDefinitions.at(unit.unitType).maxHitPoints - unit.hitPoints;
        }
    }

    TEST_CASE("a weapon that cannot pay for its shot does not fire", "[dgun]")
    {
        // TotalA.exe 0x49E3ED weighs energypershot and metalpershot against what
        // the player actually has in store and skips the weapon outright if
        // either is short; only afterwards, at 0x49E51F, does it take them. A
        // commander on a flat battery therefore cannot D-gun anything, which is
        // the whole reason the 400-energy price on the shot means anything.
        //
        // A hundred seconds of reload keeps every case below to one shot, so
        // what leaves the stores can be read off directly.
        auto script = makeDgunScript();

        SECTION("with less than energypershot in store nothing leaves the barrel")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto duel = setUpDuel(sim, script, 1000.0f, 100.0f, 1000000u, 0x10000);
            defineDgunWeapon(sim, "disintegrator", false, 400.0f, 0.0f, 100.0f, 30000u);
            armWithDgun(sim, duel.shooter, "disintegrator");

            REQUIRE(!everFires(sim, 60));
        }

        SECTION("with the energy in store it fires, and the energy goes")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto duel = setUpDuel(sim, script, 1000.0f, 10000.0f, 1000000u, 0x10000);
            defineDgunWeapon(sim, "disintegrator", false, 400.0f, 0.0f, 100.0f, 30000u);
            armWithDgun(sim, duel.shooter, "disintegrator");

            auto energyBefore = sim.getPlayer(duel.us).energy;
            REQUIRE(everFires(sim, 60));

            // Long enough for the second to settle, and the reload is far too
            // long for a second shot to muddy the figure.
            tick(sim, 60);
            REQUIRE((energyBefore - sim.getPlayer(duel.us).energy).value == 400.0f);
        }

        SECTION("metalpershot is weighed and taken alongside energypershot")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto duel = setUpDuel(sim, script, 50.0f, 10000.0f, 1000000u, 0x10000);
            defineDgunWeapon(sim, "disintegrator", false, 400.0f, 200.0f, 100.0f, 30000u);
            armWithDgun(sim, duel.shooter, "disintegrator");

            // Energy enough, metal short: still nothing. This is the half RWE
            // did not have -- metalpershot was parsed and then dropped.
            REQUIRE(!everFires(sim, 60));

            sim.getPlayer(duel.us).metal = Metal(1000.0f);
            auto metalBefore = sim.getPlayer(duel.us).metal;
            REQUIRE(everFires(sim, 60));
            tick(sim, 60);
            REQUIRE((metalBefore - sim.getPlayer(duel.us).metal).value == 200.0f);
        }
    }

    TEST_CASE("a command-fire weapon never picks its own target", "[dgun]")
    {
        // The auto-target scan skips any weapon carrying `commandfire`
        // (0x40643F, 0x407131, 0x40FDE5), and so does the return-fire path
        // (0x408A90). It is what keeps a commander from disintegrating the
        // first thing that wanders past.
        auto script = makeDgunScript();

        SECTION("with commandfire it sits there at fire-at-will")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto duel = setUpDuel(sim, script, 10000.0f, 10000.0f, 1000000u, 0x10000);
            defineDgunWeapon(sim, "disintegrator", true, 400.0f, 0.0f, 1.2f, 30000u);
            armWithDgun(sim, duel.shooter, "disintegrator");

            REQUIRE(!everFires(sim, 60));
        }

        SECTION("the same weapon without it shoots on sight")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto duel = setUpDuel(sim, script, 10000.0f, 10000.0f, 1000000u, 0x10000);
            defineDgunWeapon(sim, "disintegrator", false, 400.0f, 0.0f, 1.2f, 30000u);
            armWithDgun(sim, duel.shooter, "disintegrator");

            REQUIRE(everFires(sim, 60));
        }
    }

    TEST_CASE("the D-gun is an ordinary weapon whose damage number happens to beat armour", "[dgun]")
    {
        // There is nothing else to the thing. `[DAMAGE] default=30000` in the
        // shipped ARM_DISINTEGRATOR is exactly the figure the armour cut-out at
        // 0x489BD1 tests against, so it arrives whole through a DamageModifier
        // that would otherwise halve it, and a target that would have survived
        // does not. One point less and it would: these two are the same hit
        // either side of the line.
        //
        // A direct hit, because the blast falloff would scale the number below
        // the threshold before it ever reached the armour step and there would
        // be nothing left to see.
        auto script = makeDgunScript();
        GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
        auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
        registerDgunModel(sim);
        defineDgunUnit(sim, "victim", false, 20000u);
        sim.unitDefinitions["victim"].damageModifier = simScalarToFixed(0.5_ssf);

        SECTION("thirty thousand ignores the armour and kills outright")
        {
            auto victimId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(victimId).armored = true;

            sim.applyDamage(victimId, 30000);
            REQUIRE(sim.getUnitState(victimId).isDead());
        }

        SECTION("one point short of it is halved and leaves the target standing")
        {
            auto victimId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 0_ss), script);
            sim.getUnitState(victimId).armored = true;

            sim.applyDamage(victimId, 29999);
            REQUIRE(sim.getUnitState(victimId).isAlive());
            REQUIRE(damageTaken(sim, victimId) == 14999);
        }
    }
}
