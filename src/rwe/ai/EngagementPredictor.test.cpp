#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/EngagementPredictor.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

/**
 * combatStats and engagementMargin, pinned against real shipped Total
 * Annihilation data rather than invented numbers (per CLAUDE.md: a fixture
 * that makes its own values up has hidden bugs here before).
 *
 * Every figure below was read out of the real game data with the engine's
 * own tools -- `hpi_test.exe list`/`extract` against D:/RWE-Data/totala1.hpi
 * -- not guessed at: `units/<NAME>.FBI` for MaxDamage, MaxVelocity and
 * BuildCostMetal, and the matching TDF block under weapons (grouped by
 * weapon family, not one file per weapon: EMG and CORE_CANLASER are two
 * blocks in different files) for range, reloadtime, burst and the [DAMAGE]
 * default entry. Each unit's comment below names its two source files.
 * CORMIST is the doctrine's "Slasher" -- its FBI's own Name= key says so;
 * there is no unit literally spelled CORSLASH in the shipped data.
 */
namespace rwe
{
    namespace
    {
        void addWeapon(GameSimulation& sim, const std::string& name, float range, float reload, unsigned int damage, int burst = 1)
        {
            WeaponDefinition w{};
            w.maxRange = SimScalar(range);
            w.reloadTime = SimScalar(reload);
            w.burst = burst;
            w.damage["DEFAULT"] = damage;
            sim.weaponDefinitions[name] = w;
        }

        void addUnit(GameSimulation& sim, const std::string& type, unsigned int hp, float speed, float metal, const std::string& weapon)
        {
            UnitDefinition d{};
            d.maxHitPoints = hp;
            d.maxVelocity = SimScalar(speed);
            d.buildCostMetal = Metal(metal);
            d.weapon1 = weapon;
            sim.unitDefinitions[type] = d;
        }

        /**
         * The nine units #389 names, with the weapon each one's Weapon1 key
         * actually points to. See the file comment above for where every
         * number came from.
         */
        GameSimulation makeRealUnitWorld()
        {
            GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);

            // weapons/WEAPONS.TDF [EMG]: range=180 reloadtime=.4 burst=3, [DAMAGE] default=8.
            addWeapon(sim, "EMG", 180.0f, 0.4f, 8u, 3);
            // weapons/LASERS.TDF [CORE_LASER]: range=180 reloadtime=.865 (no Burst key), [DAMAGE] default=30.
            addWeapon(sim, "CORE_LASER", 180.0f, 0.865f, 30u);
            // weapons/ROCKETS.TDF [CORKBOT_ROCKET]: range=400 reloadtime=3.7, [DAMAGE] default=100.
            addWeapon(sim, "CORKBOT_ROCKET", 400.0f, 3.7f, 100u);
            // weapons/CANNONS.TDF [ARM_HAM]: range=320 reloadtime=1.95, [DAMAGE] default=85.
            addWeapon(sim, "ARM_HAM", 320.0f, 1.95f, 85u);
            // weapons/CANNONS.TDF [CORE_THUD]: range=230 reloadtime=1.9, [DAMAGE] default=80.
            addWeapon(sim, "CORE_THUD", 230.0f, 1.9f, 80u);
            // weapons/MISSILES.TDF [ARMTRUCK_MISSILE]: range=600 reloadtime=2.5, [DAMAGE] default=40.
            addWeapon(sim, "ARMTRUCK_MISSILE", 600.0f, 2.5f, 40u);
            // weapons/LASERS.TDF [CORE_CANLASER]: range=200 reloadtime=.95, [DAMAGE] default=220.
            addWeapon(sim, "CORE_CANLASER", 200.0f, 0.95f, 220u);
            // weapons/MISSILES.TDF [CORTRUCK_MISSILE]: range=600 reloadtime=2.5, [DAMAGE] default=41.
            addWeapon(sim, "CORTRUCK_MISSILE", 600.0f, 2.5f, 41u);

            // units/ARMPW.FBI (Peewee): MaxDamage=250 MaxVelocity=1.8 BuildCostMetal=53 Weapon1=EMG.
            addUnit(sim, "ARMPW", 250u, 1.8f, 53.0f, "EMG");
            // units/ARMFLASH.FBI (Flash): MaxDamage=625 MaxVelocity=2 BuildCostMetal=106 Weapon1=EMG.
            addUnit(sim, "ARMFLASH", 625u, 2.0f, 106.0f, "EMG");
            // units/CORAK.FBI (A.K.): MaxDamage=265 MaxVelocity=1.72 BuildCostMetal=56 Weapon1=CORE_LASER.
            addUnit(sim, "CORAK", 265u, 1.72f, 56.0f, "CORE_LASER");
            // units/CORSTORM.FBI (Storm): MaxDamage=620 MaxVelocity=1.25 BuildCostMetal=118 Weapon1=CORKBOT_ROCKET.
            addUnit(sim, "CORSTORM", 620u, 1.25f, 118.0f, "CORKBOT_ROCKET");
            // units/ARMHAM.FBI (Hammer): MaxDamage=800 MaxVelocity=1.1 BuildCostMetal=151 Weapon1=ARM_HAM.
            addUnit(sim, "ARMHAM", 800u, 1.1f, 151.0f, "ARM_HAM");
            // units/CORTHUD.FBI (Thud): MaxDamage=800 MaxVelocity=1.13 BuildCostMetal=147 Weapon1=CORE_THUD.
            addUnit(sim, "CORTHUD", 800u, 1.13f, 147.0f, "CORE_THUD");
            // units/ARMSAM.FBI (Samson): MaxDamage=650 MaxVelocity=1.5 BuildCostMetal=119 Weapon1=ARMTRUCK_MISSILE.
            addUnit(sim, "ARMSAM", 650u, 1.5f, 119.0f, "ARMTRUCK_MISSILE");
            // units/CORCAN.FBI (Can): MaxDamage=2800 MaxVelocity=0.5 BuildCostMetal=420 Weapon1=CORE_CANLASER.
            addUnit(sim, "CORCAN", 2800u, 0.5f, 420.0f, "CORE_CANLASER");
            // units/CORMIST.FBI (Name=Slasher): MaxDamage=655 MaxVelocity=1.45 BuildCostMetal=116 Weapon1=CORTRUCK_MISSILE.
            addUnit(sim, "CORMIST", 655u, 1.45f, 116.0f, "CORTRUCK_MISSILE");

            return sim;
        }

        std::vector<UnitCombatStats> statsOf(const GameSimulation& sim, const std::string& type, int count)
        {
            std::vector<UnitCombatStats> v;
            v.reserve(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i)
            {
                v.push_back(combatStats(sim, type));
            }
            return v;
        }
    }

    TEST_CASE("combatStats reads hp, dps, range, speed and metal from a unit's real FBI and weapon TDF")
    {
        auto sim = makeRealUnitWorld();
        auto pw = combatStats(sim, "ARMPW");
        REQUIRE(pw.hp == 250.0f);
        REQUIRE(pw.metal == 53.0f);
        REQUIRE(pw.speed == 1.8f);
        REQUIRE(pw.range == 180.0f);
        // 8 damage * 3-round burst / 0.4s reload = 60 dps, matching the
        // shipped-data table in docs/ai-architecture-proposal.md S:15.7.
        REQUIRE(pw.dps == Catch::Approx(60.0f));
    }

    TEST_CASE("combatStats is all-zero for a unit type that is not known")
    {
        auto sim = makeRealUnitWorld();
        auto stats = combatStats(sim, "NOSUCHUNIT");
        REQUIRE(stats.hp == 0.0f);
        REQUIRE(stats.dps == 0.0f);
        REQUIRE(stats.range == 0.0f);
        REQUIRE(stats.speed == 0.0f);
        REQUIRE(stats.metal == 0.0f);
    }

    TEST_CASE("engagementMargin is exactly zero between two identical mirrored forces")
    {
        auto sim = makeRealUnitWorld();
        auto a = statsOf(sim, "ARMPW", 5);
        auto b = statsOf(sim, "ARMPW", 5);
        REQUIRE(engagementMargin(a, b) == 0.0f);
    }

    TEST_CASE("more of the same unit wins")
    {
        auto sim = makeRealUnitWorld();
        auto more = statsOf(sim, "ARMPW", 6);
        auto fewer = statsOf(sim, "ARMPW", 4);
        REQUIRE(engagementMargin(more, fewer) > 0.0f);
    }

    TEST_CASE("a few Storms beat more Peewees once range is accounted for")
    {
        // CORSTORM outranges ARMPW better than two to one (400 vs 180) and
        // the user's own doctrine (core-anti-rush-doctrine memory) is that
        // Storm is the answer to early Peewee pressure precisely because of
        // that standoff. Unboosted, Storm's hp*dps per metal
        // (620*27.03/118 =~ 142) is well below Peewee's (250*60/53 =~ 283)
        // -- the same "no term for range" gap issue #389 names -- so this
        // only passes because the range-advantage term is doing real work.
        auto sim = makeRealUnitWorld();
        auto storms = statsOf(sim, "CORSTORM", 4);
        auto peewees = statsOf(sim, "ARMPW", 6);
        REQUIRE(engagementMargin(storms, peewees) > 0.0f);
    }

    TEST_CASE("enough Peewees still beat a few Storms")
    {
        // The range term is bounded, not a hard "cannot be touched": with
        // enough of a numbers disadvantage against it, Storm still loses.
        // Guards against a formula that favours the longer-ranged side
        // regardless of how outnumbered it is.
        auto sim = makeRealUnitWorld();
        auto storms = statsOf(sim, "CORSTORM", 2);
        auto peewees = statsOf(sim, "ARMPW", 20);
        REQUIRE(engagementMargin(storms, peewees) < 0.0f);
    }

    TEST_CASE("the Slasher outranges the Flash")
    {
        // CORMIST ("Slasher") reaches to 600 against ARMFLASH's 180; the
        // doctrine names the Slasher as the main Flash counter specifically
        // because it "far outranges it". A single Slasher's unboosted
        // hp*dps (655*16.4 =~ 10742) is well under a single Flash's
        // (625*60 =~ 37500), so a 1-for-1 margin in the Slasher's favour
        // only comes from the range term.
        auto sim = makeRealUnitWorld();
        auto slashers = statsOf(sim, "CORMIST", 1);
        auto flashes = statsOf(sim, "ARMFLASH", 1);
        REQUIRE(engagementMargin(slashers, flashes) > 0.0f);
    }

    TEST_CASE("the Can's high hp times dps does not automatically win against a faster, longer-ranged raid group")
    {
        // CORCAN's per-metal hp*dps (2800*231.6/420 =~ 1544) dwarfs
        // ARMHAM's (800*43.6/151 =~ 231), the same shape of gap the
        // Crystal Maze finding names for Can against its own faction's
        // level-one line (core-ai-deficit-investigation memory) -- "the
        // formula has no term for range, speed or role" is the diagnosis
        // this predictor exists to fix. ARMHAM outranges the Can (320 vs
        // 200) and is more than twice as fast (1.1 vs 0.5), so a big enough
        // Hammer raid still beats a couple of Cans once that is accounted
        // for.
        auto sim = makeRealUnitWorld();
        auto cans = statsOf(sim, "CORCAN", 2);
        auto hammers = statsOf(sim, "ARMHAM", 16);
        REQUIRE(engagementMargin(hammers, cans) > 0.0f);
    }

    TEST_CASE("but a couple of Cans do beat a modest number of Hammers")
    {
        // The same pair the test above uses, at a raid size too small to
        // close the gap: the Can's advantage is real, just not unconditional.
        auto sim = makeRealUnitWorld();
        auto cans = statsOf(sim, "CORCAN", 2);
        auto hammers = statsOf(sim, "ARMHAM", 4);
        REQUIRE(engagementMargin(cans, hammers) > 0.0f);
    }
}
