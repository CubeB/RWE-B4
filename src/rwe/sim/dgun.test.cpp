#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/cob/Cob.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/WeaponDefinition.h>
#include <memory>
#include <vector>
#include <rwe/sim/sim_test_util.h>

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
            // The shipped weapon carries it, and it is what stops a detonation
            // consuming the round -- see the noexplode cases below.
            w.noExplode = true;
            sim.weaponDefinitions[name] = w;
        }

        /**
         * ARM_DISINTEGRATOR exactly as rev31's WEAPONS.TDF ships it, less the
         * sound and explosion art. Parsed rather than transcribed: the cases
         * below lean on range, weaponvelocity, areaofeffect, reloadtime and
         * the damage figure, and a fixture that invented any of them would be
         * testing the fixture. Note `weapontimer=4` sitting in the file
         * alongside `weaponvelocity` -- it is there, and it is not what times
         * the shot.
         */
        const char* const ArmDisintegratorTdf = R"TDF(
[ARM_DISINTEGRATOR]
	{
	ID=22;
	name=Disintegrator;
	rendertype=3;
	lineofsight=1;
	turret=1;

	model=dgun;

	range=240;
	reloadtime=1.2;
	weapontimer=4;
	energypershot=400;
	weaponvelocity=200;
	areaofeffect=48;
	soundtrigger=1;
	firestarter=70;
	beamweapon=1;
	noexplode=1;
	commandfire=1;

	startsmoke=1;

	[DAMAGE]
		{
		default=30000;
		}
	}
)TDF";

        WeaponDefinition shippedDisintegrator()
        {
            auto tdf = parseTdfFromString(ArmDisintegratorTdf);
            auto block = tdf.findBlock("ARM_DISINTEGRATOR");
            REQUIRE(block.has_value());
            return parseWeaponDefinition(parseWeaponBlock(block->get()));
        }

        /**
         * The same weapon with `commandfire` taken off, so the shooter will
         * pick its own target and the case does not have to drive an order as
         * well as a projectile. What the order does is settled separately in
         * dgunorder.test.cpp and in section 85; none of it touches the round
         * once it has left the barrel, which is what these cases are about.
         */
        void giveShippedDisintegrator(GameSimulation& sim, const std::string& name)
        {
            auto w = shippedDisintegrator();
            w.commandFire = false;
            sim.weaponDefinitions[name] = w;
        }

        /**
         * SMALL_UNITEX out of UNITS.TDF -- what a Peewee's ExplodeAs names, and
         * the smallest death explosion in the game: areaofeffect 30, so a
         * radius of fifteen, and thirty damage.
         */
        void defineSmallUnitEx(GameSimulation& sim, const std::string& name)
        {
            WeaponDefinition w{};
            w.physicsType = ProjectilePhysicsTypeBallistic();
            w.maxRange = 480_ss;
            w.reloadTime = SimScalar(3.6f);
            w.burst = 1;
            w.velocity = 250_ss / 30_ss;
            w.damageRadius = 15_ss;
            w.damage["DEFAULT"] = 30;
            sim.weaponDefinitions[name] = w;
        }

        /** Every live projectile's position, in id order. */
        std::vector<SimVector> projectilePositions(const GameSimulation& sim)
        {
            std::vector<SimVector> out;
            for (const auto& p : sim.projectiles)
            {
                out.push_back(p.second.position);
            }
            return out;
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

    TEST_CASE("a command-fire weapon picks its own target only for the computer", "[dgun]")
    {
        // The auto-target scan skips any weapon carrying `commandfire`
        // (0x40643F, 0x407131, 0x40FDE5), and so does the return-fire path
        // (0x408A90). It is what keeps a human's commander from
        // disintegrating the first thing that wanders past.
        //
        // With one exception, from the scan at 0x4089A0: "commandfire weapons
        // do not either, unless the player is of type 2". Type 2 is the
        // computer player -- the same player+0x73 byte that exempts an AI from
        // the ShootMe rule -- so an AI's commander does D-gun what comes at
        // it. Reported from a play-test, where one stood and died under fire
        // from several units with that weapon unused.
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

        SECTION("a computer player's commander fires it unbidden")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto duel = setUpDuel(sim, script, 10000.0f, 10000.0f, 1000000u, 0x10000);
            sim.players[duel.us.value].type = GamePlayerType::Computer;
            defineDgunWeapon(sim, "disintegrator", true, 400.0f, 0.0f, 1.2f, 30000u);
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

    TEST_CASE("the shipped weapon parses to the numbers the rest of this file uses", "[dgun]")
    {
        // Everything below is worked out from these, so they are checked once
        // against the file rather than trusted in eight places.
        auto w = shippedDisintegrator();

        REQUIRE(w.maxRange == 240_ss);
        REQUIRE(std::holds_alternative<ProjectilePhysicsTypeLineOfSight>(w.physicsType));

        // weaponvelocity is per second in the TDF and per tick in here.
        REQUIRE(w.velocity == 200_ss / 30_ss);

        // areaofeffect is 48 and the blast radius is half of it (0x49A150's
        // shr eax,1). Getting this wrong doubles the weapon's reach.
        REQUIRE(w.damageRadius == 24_ss);

        // No edgeeffectiveness in the file, so the falloff runs all the way
        // down to nothing at the rim.
        REQUIRE(w.edgeEffectiveness == 0_ss);

        REQUIRE(w.noExplode);
        REQUIRE(w.commandFire);
        REQUIRE(w.energyPerShot == Energy(400.0f));
        REQUIRE(w.reloadTime == SimScalar(1.2f));
        REQUIRE(w.damage.at("DEFAULT") == 30000u);
        REQUIRE(w.fireStarter == 70u);
    }

    TEST_CASE("a line-of-sight round lives long enough to fly its whole range", "[dgun]")
    {
        // 0x49C942: the life is (range << 16) / weaponvelocity, which is just
        // "how many ticks to cover the full range at this speed". For the
        // disintegrator that is 240 / (200/30) = 36 ticks, exactly 240 world
        // units. Nothing about the target enters into it -- the round is fired
        // along a direction fixed at the muzzle and flies the lot.
        GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
        auto us = addDgunPlayer(sim, "us", 10000.0f, 10000.0f);
        giveShippedDisintegrator(sim, "disintegrator");

        UnitWeapon weapon;
        weapon.weaponType = "disintegrator";

        auto lifeOfShotAt = [&](SimScalar distanceToTarget) {
            sim.spawnProjectile(us, weapon, SimVector(0_ss, 100_ss, 0_ss), SimVector(0_ss, 0_ss, 1_ss), distanceToTarget, std::nullopt, std::nullopt);
            GameTime life(0);
            for (const auto& p : sim.projectiles)
            {
                REQUIRE(p.second.dieOnFrame.has_value());
                life = GameTime(p.second.dieOnFrame->value - p.second.createdAt.value);
            }
            for (auto& p : sim.projectiles)
            {
                p.second.isDead = true;
            }
            sim.deleteDeadProjectiles();
            return life;
        };

        SECTION("thirty-six ticks, whatever it was aimed at")
        {
            // A target at arm's length and one at the far edge of the range
            // give the same answer, because the range is the only input.
            REQUIRE(lifeOfShotAt(30_ss) == GameTime(36));
            REQUIRE(lifeOfShotAt(239_ss) == GameTime(36));
        }

        SECTION("weapontimer is a fallback, not an override")
        {
            // ARM_DISINTEGRATOR declares weapontimer=4, which is 120 ticks.
            // The original only reaches that branch for a weapon with no
            // weaponvelocity to divide the range by, and RWE had the two the
            // wrong way round: the D-gun used to live for four seconds.
            REQUIRE(sim.weaponDefinitions.at("disintegrator").weaponTimer == GameTime(120));
            REQUIRE(lifeOfShotAt(100_ss) == GameTime(36));
        }
    }

    TEST_CASE("noexplode: the round goes off without being spent, and leaves a trail", "[dgun]")
    {
        // The headline behaviour, and the one a play-test reported as "the
        // trail continues past the target and damages things along it". The
        // detonation routine's first act is to mark the round dead (0x499EDE);
        // noexplode skips that line and nothing else. So the round keeps its
        // position and velocity, moves on, and tests the next cell -- once a
        // tick for the rest of its 36-tick life.
        //
        // The shooter's muzzle is twenty units up and the target is on the
        // ground two hundred away, so the beam comes in almost flat, goes off
        // on the target, and then ploughs into the dirt and goes off again
        // every tick until its life runs out.
        auto script = makeDgunScript();

        auto runShot = [&script](bool noExplode, std::vector<UnitId>& victims, GameSimulation& sim) {
            auto us = addDgunPlayer(sim, "us", 100000.0f, 100000.0f);
            auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
            registerDgunModel(sim);
            defineDgunUnit(sim, "commander", true, 3000);
            defineDgunUnit(sim, "victim", false, 500);
            giveShippedDisintegrator(sim, "disintegrator");
            sim.weaponDefinitions.at("disintegrator").noExplode = noExplode;
            // Only the reload is stretched, so the case reads as one shot.
            sim.weaponDefinitions.at("disintegrator").reloadTime = SimScalar(100.0f);

            auto shooterId = spawnDgunUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithDgun(sim, shooterId, "disintegrator");

            // The aimed target, then two more strung out behind it, then one
            // past the end of the range as a control.
            //
            // The distances are measured rather than picked. The beam leaves
            // the muzzle twenty up and comes down across two hundred units, so
            // it first goes off on the target's own bounding box at z=192 and
            // then ploughs on, detonating every tick, out to z=239 -- the last
            // of its thirty-six ticks, and exactly two hundred and forty units
            // of travel. One blast at z=192 reaches 216; the two behind the
            // target are therefore put at 240 and 256, out of reach of the
            // first detonation and inside the trail, which is what makes the
            // case tell the two behaviours apart. The control at 288 is beyond
            // even the last blast's rim.
            for (auto z : {200, 240, 256, 288})
            {
                victims.push_back(spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, SimScalar(static_cast<float>(z))), script));
            }

            tick(sim, 80);
        };

        SECTION("with it, everything along the line goes, out to the end of the range")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            std::vector<UnitId> victims;
            runShot(true, victims, sim);

            // The one it was aimed at, and the two behind it that the beam
            // never stopped for. One shot, three kills.
            REQUIRE(!sim.units.tryGet(victims[0]).has_value());
            REQUIRE(!sim.units.tryGet(victims[1]).has_value());
            REQUIRE(!sim.units.tryGet(victims[2]).has_value());

            // And the range is still a range: 288 is past 240 and the trail
            // does not reach it.
            REQUIRE(sim.units.tryGet(victims[3]).has_value());
        }

        SECTION("without it, the first thing it touches is the last")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            std::vector<UnitId> victims;
            runShot(false, victims, sim);

            REQUIRE(!sim.units.tryGet(victims[0]).has_value());
            REQUIRE(sim.units.tryGet(victims[1]).has_value());
            REQUIRE(sim.units.tryGet(victims[2]).has_value());
            REQUIRE(sim.units.tryGet(victims[3]).has_value());
        }

        SECTION("the round is still in the air after it has gone off")
        {
            // The same thing said directly: it detonates and survives.
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto us = addDgunPlayer(sim, "us", 100000.0f, 100000.0f);
            auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
            registerDgunModel(sim);
            defineDgunUnit(sim, "commander", true, 3000);
            defineDgunUnit(sim, "victim", false, 500);
            giveShippedDisintegrator(sim, "disintegrator");
            sim.weaponDefinitions.at("disintegrator").reloadTime = SimScalar(100.0f);

            auto shooterId = spawnDgunUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithDgun(sim, shooterId, "disintegrator");
            auto victimId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 200_ss), script);

            bool sawItFlyOnAfterTheKill = false;
            int detonations = 0;
            SimScalar furthestFlown(0.0f);
            for (int i = 0; i < 80; ++i)
            {
                sim.events.clear();
                sim.tick();
                for (const auto& e : sim.events)
                {
                    if (std::holds_alternative<ProjectileDetonatedEvent>(e))
                    {
                        ++detonations;
                    }
                }
                for (const auto& p : sim.projectiles)
                {
                    furthestFlown = rweMax(furthestFlown, (p.second.position - p.second.origin).length());
                }
                if (!sim.units.tryGet(victimId).has_value() && !projectilePositions(sim).empty())
                {
                    sawItFlyOnAfterTheKill = true;
                }
            }
            REQUIRE(sawItFlyOnAfterTheKill);

            // Not one blast but a run of them: the round goes off on the
            // target and then once a tick, all the way to the end of its life.
            REQUIRE(detonations > 1);

            // And the end of its life is its full range, to the world unit.
            // Thirty-six ticks at 200/30 apiece is 240, whatever it hit on the
            // way and however far away the thing it was aimed at was.
            REQUIRE(furthestFlown > 239.5_ssf);
            REQUIRE(furthestFlown < 240.5_ssf);
        }
    }

    TEST_CASE("the blast is twenty-four wide and falls away as the square", "[dgun]")
    {
        // areaofeffect 48 halved (0x49A150), and scale = (1 - d/R)^2 with no
        // edgeeffectiveness to put a floor under it (0x49A3B6). Distance is
        // measured to the unit's bounding box, so a unit standing on the blast
        // is at zero and takes the lot.
        auto script = makeDgunScript();
        GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
        auto us = addDgunPlayer(sim, "us", 1000.0f, 1000.0f);
        auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
        registerDgunModel(sim);
        defineDgunUnit(sim, "victim", false, 1000000);
        giveShippedDisintegrator(sim, "disintegrator");

        auto weapon = sim.weaponDefinitions.at("disintegrator");

        // Where the box edge actually is, measured rather than assumed.
        auto probeId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 0_ss), script);
        auto probeBox = sim.createBoundingBox(sim.getUnitState(probeId));
        auto boxEdge = probeBox.center.x + probeBox.extents.x;
        sim.quietlyKillUnit(probeId);
        sim.deleteDeadUnits();

        struct Case
        {
            SimScalar distance;
            unsigned int expected;
        };

        const Case cases[] = {
            {0_ss, 30000},  // on the box: the whole 30000
            {12_ss, 7500},  // half way out: a quarter, not a half
            {18_ss, 1875},  // three quarters out: a sixteenth
            {24_ss, 0},     // at the rim
            {30_ss, 0},     // and outside it, where areaofeffect 48 would have reached
        };

        for (const auto& c : cases)
        {
            auto victimId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 0_ss), script);
            auto blastPosition = SimVector(boxEdge + c.distance, 0_ss, 0_ss);
            REQUIRE(rweSqrt(sim.createBoundingBox(sim.getUnitState(victimId)).distanceSquared(blastPosition)) == c.distance);

            Projectile p{};
            p.owner = us;
            p.position = blastPosition;
            p.previousPosition = blastPosition;
            p.origin = blastPosition;
            p.weaponType = "disintegrator";
            p.damage = weapon.damage;
            p.damageRadius = weapon.damageRadius;
            p.edgeEffectiveness = weapon.edgeEffectiveness;

            sim.applyDamageInRadius(blastPosition, p.damageRadius, p);
            CAPTURE(simScalarToFloat(c.distance));
            REQUIRE(damageTaken(sim, victimId) == c.expected);

            sim.quietlyKillUnit(victimId);
            sim.deleteDeadUnits();
        }
    }

    TEST_CASE("the firer is exempt from its own blast and nobody else is", "[dgun]")
    {
        // 0x49A259 compares each candidate against the projectile's stored
        // firing unit and skips on a match. That is the only exemption on the
        // whole damage path: there is no allegiance test anywhere, so an ally
        // standing in the blast takes it at full strength, and the original
        // merely books the result into two tallies by owner -- which would be
        // pointless if own-damage did not happen.
        auto script = makeDgunScript();
        GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
        auto us = addDgunPlayer(sim, "us", 1000.0f, 1000.0f);
        auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
        registerDgunModel(sim);
        defineDgunUnit(sim, "victim", false, 1000000);
        giveShippedDisintegrator(sim, "disintegrator");
        auto weapon = sim.weaponDefinitions.at("disintegrator");

        auto blastPosition = SimVector(0_ss, 0_ss, 0_ss);

        auto firerId = spawnDgunUnit(sim, "victim", us, blastPosition, script);
        auto alliedId = spawnDgunUnit(sim, "victim", us, SimVector(16_ss, 0_ss, 0_ss), script);
        auto enemyId = spawnDgunUnit(sim, "victim", them, SimVector(-16_ss, 0_ss, 0_ss), script);

        Projectile p{};
        p.owner = us;
        p.attacker = firerId;
        p.position = blastPosition;
        p.previousPosition = blastPosition;
        p.origin = blastPosition;
        p.weaponType = "disintegrator";
        p.damage = weapon.damage;
        p.damageRadius = weapon.damageRadius;
        p.edgeEffectiveness = weapon.edgeEffectiveness;

        sim.applyDamageInRadius(blastPosition, p.damageRadius, p);

        REQUIRE(damageTaken(sim, firerId) == 0);
        REQUIRE(damageTaken(sim, alliedId) > 0);
        REQUIRE(damageTaken(sim, enemyId) > 0);

        SECTION("a blast with no firer exempts nobody")
        {
            // A dying unit's explodeAs, or a feature going up: the original
            // has no unit to compare against, so everything in reach takes it.
            auto before = damageTaken(sim, firerId);
            p.attacker = std::nullopt;
            sim.applyDamageInRadius(blastPosition, p.damageRadius, p);
            REQUIRE(damageTaken(sim, firerId) > before);
        }
    }

    TEST_CASE("the beam flies through your own units and blasts them anyway", "[dgun]")
    {
        // Two halves of one asymmetry. The collision test skips a unit of the
        // firing player's own side (0x49B1F4), so the round is not stopped or
        // triggered by a friendly standing in the way. But nothing on the
        // damage path cares whose the unit is, so a detonation that does
        // happen hurts a friendly at full strength.
        auto script = makeDgunScript();
        GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
        auto us = addDgunPlayer(sim, "us", 100000.0f, 100000.0f);
        auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
        registerDgunModel(sim);
        defineDgunUnit(sim, "commander", true, 3000);
        defineDgunUnit(sim, "victim", false, 500);
        giveShippedDisintegrator(sim, "disintegrator");
        sim.weaponDefinitions.at("disintegrator").reloadTime = SimScalar(100.0f);

        auto shooterId = spawnDgunUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
        armWithDgun(sim, shooterId, "disintegrator");

        // One of ours in the line of fire, halfway to the target.
        auto bystanderId = spawnDgunUnit(sim, "victim", us, SimVector(0_ss, 0_ss, 100_ss), script);
        // One of ours right beside the target.
        auto neighbourId = spawnDgunUnit(sim, "victim", us, SimVector(0_ss, 0_ss, 216_ss), script);
        auto enemyId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 200_ss), script);

        tick(sim, 80);

        // It reached the enemy: the friendly halfway there neither stopped it
        // nor set it off.
        REQUIRE(!sim.units.tryGet(enemyId).has_value());
        REQUIRE(sim.units.tryGet(bystanderId).has_value());
        REQUIRE(damageTaken(sim, bystanderId) == 0);

        // And the one standing beside the target is gone, because friendly
        // fire is the rule rather than the exception.
        REQUIRE(!sim.units.tryGet(neighbourId).has_value());
    }

    TEST_CASE("reload time is scaled by the shooter's health and kills", "[dgun]")
    {
        // TotalA.exe 0x49E468, which RWE had never had:
        //
        //   reloadTicks = ((120 - 20*hp/maxdamage) * ((100 - 6*tier)*reloadTicks/100)) / 100
        //
        // The commander's disintegrator is the case with a round number behind
        // it: reloadtime 1.2 is 36 ticks, and a healthy commander with no kills
        // gets exactly 36 back -- the two terms cancel. That is what makes this
        // safe to apply to every weapon in the game.
        const auto reload = SimScalar(1.2f);
        const unsigned int commanderHitPoints = 3000; // ARMCOM MaxDamage

        SECTION("an undamaged rookie gets the TDF number back untouched")
        {
            REQUIRE(computeReloadTicks(reload, commanderHitPoints, commanderHitPoints, 0) == GameTime(36));
        }

        SECTION("damage slows it down, to 120% at the point of death")
        {
            // Half health: (120 - 10) * 36 / 100 = 39.
            REQUIRE(computeReloadTicks(reload, 1500, commanderHitPoints, 0) == GameTime(39));
            // Nothing left: 120 * 36 / 100 = 43, which is 1.43 seconds.
            REQUIRE(computeReloadTicks(reload, 0, commanderHitPoints, 0) == GameTime(43));
        }

        SECTION("kills speed it up, in tiers of five, and stop at five tiers")
        {
            // Four kills is still tier zero.
            REQUIRE(computeReloadTicks(reload, commanderHitPoints, commanderHitPoints, 4) == GameTime(36));
            // Five is tier one: (100 - 6) * 36 / 100 = 33.
            REQUIRE(computeReloadTicks(reload, commanderHitPoints, commanderHitPoints, 5) == GameTime(33));
            // Twenty-five is tier five and the end of it: 70 * 36 / 100 = 25.
            REQUIRE(computeReloadTicks(reload, commanderHitPoints, commanderHitPoints, 25) == GameTime(25));
            REQUIRE(computeReloadTicks(reload, commanderHitPoints, commanderHitPoints, 100) == GameTime(25));
        }

        SECTION("and the weapon really waits that long")
        {
            auto script = makeDgunScript();
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto us = addDgunPlayer(sim, "us", 1000000.0f, 1000000.0f);
            auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
            registerDgunModel(sim);
            defineDgunUnit(sim, "commander", true, commanderHitPoints);
            defineDgunUnit(sim, "victim", false, 100000000);
            giveShippedDisintegrator(sim, "disintegrator");

            auto shooterId = spawnDgunUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithDgun(sim, shooterId, "disintegrator");
            spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 200_ss), script);

            // The tick the first shot leaves on, then the tick the next one
            // does. Thirty-six ticks apart is 1.2 seconds.
            std::optional<int> first;
            std::optional<int> second;
            for (int i = 0; i < 200 && !second; ++i)
            {
                sim.events.clear();
                sim.tick();

                bool fired = false;
                for (const auto& e : sim.events)
                {
                    if (std::holds_alternative<FireWeaponEvent>(e))
                    {
                        fired = true;
                    }
                }
                if (!fired)
                {
                    continue;
                }
                if (!first)
                {
                    first = i;
                }
                else
                {
                    second = i;
                }
            }

            REQUIRE(first.has_value());
            REQUIRE(second.has_value());
            REQUIRE(*second - *first == 36);
        }
    }

    TEST_CASE("what actually hurts a commander that D-guns something at arm's length", "[dgun]")
    {
        // The play-test said the D-gun damages the firer. The decode says the
        // opposite in as many words -- the firer is the one unit exempt from
        // the blast (0x49A259) -- so this case is the empirical half of that
        // argument, run at the range where the question is sharpest: close
        // enough that the very first detonation is standing on the shooter.
        auto script = makeDgunScript();

        auto setUp = [&script](GameSimulation& sim, bool victimExplodes) {
            auto us = addDgunPlayer(sim, "us", 1000000.0f, 1000000.0f);
            auto them = addDgunPlayer(sim, "them", 1000.0f, 1000.0f);
            registerDgunModel(sim);
            defineDgunUnit(sim, "commander", true, 3000);
            defineDgunUnit(sim, "victim", false, 500);
            giveShippedDisintegrator(sim, "disintegrator");
            sim.weaponDefinitions.at("disintegrator").reloadTime = SimScalar(100.0f);
            defineSmallUnitEx(sim, "blast");
            if (victimExplodes)
            {
                // What a Peewee leaves behind when it dies.
                sim.unitDefinitions.at("victim").explodeAs = "blast";
            }

            auto shooterId = spawnDgunUnit(sim, "commander", us, SimVector(0_ss, 0_ss, 0_ss), script);
            armWithDgun(sim, shooterId, "disintegrator");
            // One cell away, which is as close as two units get.
            auto victimId = spawnDgunUnit(sim, "victim", them, SimVector(0_ss, 0_ss, 16_ss), script);
            return std::make_pair(shooterId, victimId);
        };

        SECTION("not its own beam, however close the target is")
        {
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto ids = setUp(sim, false);

            tick(sim, 80);

            REQUIRE(!sim.units.tryGet(ids.second).has_value());
            // Thirty thousand damage went off a few units from the commander's
            // own bounding box, and went on doing it. Without the exemption
            // this is a commander that kills itself every time it fires.
            REQUIRE(damageTaken(sim, ids.first) == 0);
        }

        SECTION("but the death explosion of what it killed does")
        {
            // The untraced half of the play-test report, and the one thing on
            // the path that has no firer to exempt: the corpse's own blast is
            // spawned with no attacker, so it hurts whatever is standing over
            // it -- which, at this range, is the commander.
            GameSimulation sim(makeDgunTerrain(), 0u, 0, 0);
            auto ids = setUp(sim, true);

            tick(sim, 80);

            REQUIRE(!sim.units.tryGet(ids.second).has_value());
            auto taken = damageTaken(sim, ids.first);
            REQUIRE(taken > 0);
            // And it is unmistakably the little thirty-point corpse blast
            // rather than anything the disintegrator did: thirty thousand
            // would have taken the commander with it several times over.
            // At this range it is the whole thirty, because the corpse blast
            // is centred on the victim's position, which is a hair's breadth
            // from the commander's own bounding box, and a unit whose box
            // contains the blast point is at distance zero.
            REQUIRE(taken <= 30);
        }
    }
}
