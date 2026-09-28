#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/weapontdf/WeaponTdf.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    namespace
    {
        // MINDGUN out of the shipped WEAPONS.TDF, verbatim. It is the only
        // weapon in the data that actually flies -- the other two weapons
        // carrying `unitsonly` are the burning-feature damage definitions in
        // FIRES.TDF, which produce no round at all -- and it is a beam, so it
        // is the case that matters: a 100-damage `unitsonly` round that in RWE
        // used to bury itself in the first hill it met.
        //
        // `lineofsight=1` with no `selfprop`, `tracks`, `dropped` or
        // `ballistic` makes it a straight flier at a fixed 91/30 units a tick,
        // and `weapontimer=2` gives it sixty ticks, which is a hundred and
        // eighty units of travel.
        const char* MindGunTdf = R"TDF(
[MINDGUN]
	{
	ID=7;
	name=Mind Gun;
	rendertype=2;
	lineofsight=1;

	range=500;
	reloadtime=1;
	weapontimer=2;
	weaponvelocity=91;
	areaofeffect=16;
	soundstart=phaser;
	soundhit=explode;
	unitsonly=1;

	[DAMAGE]
		{
		default=100;
		}
	}
)TDF";

        WeaponDefinition shippedMindGun(bool unitsOnly)
        {
            auto tdf = parseTdfFromString(MindGunTdf);
            auto block = tdf.findBlock("MINDGUN");
            REQUIRE(block.has_value());
            auto weapon = parseWeaponDefinition(parseWeaponBlock(block->get()));
            weapon.unitsOnly = unitsOnly;
            return weapon;
        }

        /** Flat ground at `ground` with the sea at `seaLevel`, dry unless they differ. */
        MapTerrain makeFlatGroundAt(unsigned char ground, SimScalar seaLevel)
        {
            Grid<unsigned char> heights(16, 16, ground);
            return MapTerrain(std::move(heights), seaLevel);
        }

        /**
         * A land unit standing on the flat ground, one square across, tall
         * enough that a round dropping from high up crosses it. The Mind Gun
         * is a units-only weapon that must still stop on one of these.
         */
        UnitId addStandingTarget(GameSimulation& sim, PlayerId owner)
        {
            UnitDefinition d{};
            d.objectName = "ARMADV";
            d.isMobile = true;
            d.maxHitPoints = 1000;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            sim.unitDefinitions["ARMADV"] = d;

            UnitModelDefinition model{};
            model.height = 32_ss;
            sim.unitModelDefinitions["ARMADV"] = model;

            auto script = makeEmptyCobScript();
            auto env = std::make_unique<CobEnvironment>(script.get());
            std::vector<UnitMesh> pieces;
            UnitState unit(pieces, std::move(env));
            unit.unitType = "ARMADV";
            unit.owner = owner;
            unit.position = SimVector(0_ss, 16_ss, 0_ss);
            unit.previousPosition = unit.position;
            unit.hitPoints = 1000;
            unit.buildTimeCompleted = 0;
            auto id = sim.tryAddUnit(std::move(unit));
            REQUIRE(id.has_value());
            return *id;
        }

        struct Flight
        {
            /** Every death the round caused, in the order the simulation reported them. */
            std::vector<ProjectileDiedEvent> deaths;
            /** Whether it was still in the air when the ticks ran out. */
            bool flying{false};
            /** Where it ended up, if it is still there. */
            SimScalar y{0};
        };

        /**
         * Drops a Mind Gun round straight down from `from` over terrain flat at
         * `ground` with the sea at `seaLevel`, and runs it for `ticks`.
         *
         * The round is the shipped definition read through the TDF parser, so
         * this pins the whole path: the key in the file, the parser, the
         * weapon definition and the stop tests.
         */
        Flight dropMindGun(unsigned char ground, SimScalar seaLevel, SimScalar from, bool unitsOnly, int ticks)
        {
            GameSimulation sim(makeFlatGroundAt(ground, seaLevel), 0u, 0, 0);
            sim.weaponDefinitions["MINDGUN"] = shippedMindGun(unitsOnly);

            auto gunner = addPlayer(sim, "gunner");
            sim.spawnProjectile(ProjectileSpawn{
                .owner = gunner,
                .weaponType = "MINDGUN",
                .position = SimVector(0_ss, from, 0_ss),
                .direction = SimVector(0_ss, -1_ss, 0_ss),
                .distanceToTarget = 100_ss});
            REQUIRE(sim.projectiles.begin() != sim.projectiles.end());

            Flight flight;
            for (int i = 0; i < ticks; ++i)
            {
                sim.events.clear();
                sim.tick();
                for (const auto& e : sim.events)
                {
                    if (const auto* died = std::get_if<ProjectileDiedEvent>(&e); died != nullptr)
                    {
                        flight.deaths.push_back(*died);
                    }
                }
            }

            for (const auto& p : sim.projectiles)
            {
                if (!p.second.isDead)
                {
                    flight.flying = true;
                    flight.y = p.second.position.y;
                }
            }
            return flight;
        }
    }

    TEST_CASE("a unitsonly round goes through the ground", "[weapon]")
    {
        // Issue #405. Dry ground at 16, the round dropped from 30 and left to
        // fall for twenty ticks -- sixty units, well past the surface. An
        // ordinary round stops at the ground (0x49B36D); a round with
        // `unitsonly` comes back out at 0x49B294 before it ever gets there.
        auto mindGun = dropMindGun(16, 0_ss, 30_ss, true, 20);
        CHECK(mindGun.deaths.empty());
        REQUIRE(mindGun.flying);
        CHECK(mindGun.y < 16_ss);

        // The same round without the key, in the same place, does not.
        auto ordinary = dropMindGun(16, 0_ss, 30_ss, false, 20);
        REQUIRE(ordinary.deaths.size() == 1u);
        CHECK(ordinary.deaths.front().deathType == ProjectileDiedEvent::DeathType::NormalImpact);
    }

    TEST_CASE("a unitsonly round goes through the water", "[weapon]")
    {
        // A seabed at 0 under twenty units of water, dropped from 30. The sea
        // test is at 0x49B3A1, behind the same early return.
        auto mindGun = dropMindGun(0, 20_ss, 30_ss, true, 20);
        CHECK(mindGun.deaths.empty());
        REQUIRE(mindGun.flying);
        CHECK(mindGun.y < 20_ss);

        auto ordinary = dropMindGun(0, 20_ss, 30_ss, false, 20);
        REQUIRE(ordinary.deaths.size() == 1u);
        CHECK(ordinary.deaths.front().deathType == ProjectileDiedEvent::DeathType::WaterImpact);
    }

    TEST_CASE("a unitsonly round still stops on a unit", "[weapon]")
    {
        // The early return sits *after* every unit test, so the flag buys a
        // Mind Gun round the terrain and the water and nothing else. An
        // Advanced Rocket Bot standing on the ground at 16 with a 32-high
        // model is in the round's path from 60 down.
        GameSimulation sim(makeFlatGroundAt(16, 0_ss), 0u, 0, 0);
        sim.weaponDefinitions["MINDGUN"] = shippedMindGun(true);

        auto gunner = addPlayer(sim, "gunner");
        auto victim = addPlayer(sim, "victim");
        auto target = addStandingTarget(sim, victim);

        sim.spawnProjectile(ProjectileSpawn{
            .owner = gunner,
            .weaponType = "MINDGUN",
            .position = SimVector(0_ss, 60_ss, 0_ss),
            .direction = SimVector(0_ss, -1_ss, 0_ss),
            .distanceToTarget = 100_ss});

        for (int i = 0; i < 20; ++i)
        {
            sim.events.clear();
            sim.tick();
            for (const auto& e : sim.events)
            {
                if (const auto* died = std::get_if<ProjectileDiedEvent>(&e); died != nullptr)
                {
                    // It stopped on the unit, not on the ground under it, and
                    // the shot's own 100 landed at the blast's centre.
                    CHECK(died->deathType == ProjectileDiedEvent::DeathType::NormalImpact);
                    CHECK(sim.getUnitState(target).hitPoints == 900);
                    return;
                }
            }
        }

        FAIL("the round went past the unit and never detonated");
    }

    TEST_CASE("a weapon TDF's unitsonly key reaches the weapon definition", "[weapon]")
    {
        // The shipped Mind Gun carries the key, and it survives the parser
        // onto the definition the simulation reads. The other two shipped
        // weapons with it, TREEBURN and SHRUBBURN in FIRES.TDF, are feature
        // damage entries and fire nothing, so this is the whole of the
        // shipped data that reaches the stop tests with the bit set.
        auto tdf = parseTdfFromString(MindGunTdf);
        auto block = tdf.findBlock("MINDGUN");
        REQUIRE(block.has_value());

        auto withKey = parseWeaponDefinition(parseWeaponBlock(block->get()));
        CHECK(withKey.unitsOnly);

        // TDF keys are case-insensitive -- WEAPONS.TDF spells it in lower case.
        TdfBlock lowerCase;
        lowerCase.insertOrAssignProperty("unitsonly", "1");
        CHECK(parseWeaponBlock(lowerCase).unitsOnly);

        // A weapon that says nothing does not skip the ground and the sea.
        CHECK_FALSE(parseWeaponBlock(TdfBlock{}).unitsOnly);
    }
}
