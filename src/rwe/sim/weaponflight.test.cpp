#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <rwe/grid/Grid.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitWeapon.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/sim/tad_weapon_episodes.h>
#include <string>

// Conformance cases: how long a projectile takes to reach what it was aimed at,
// against how long it took in a real Total Annihilation game.
//
// The episodes are in tad_weapon_episodes.h, mined by tad_episodes
// --emit-weapon-cpp and regenerated rather than edited. The neighbouring
// hand-written cases -- missile.test.cpp, ballisticair.test.cpp,
// interception.test.cpp, accuracy.test.cpp -- choose their numbers to exercise
// a behaviour. EVERY NUMBER HERE CAME OUT OF A GAME. That is the difference to
// keep in mind when reading a failure: a hand-written case failing means the
// behaviour moved, and one of these failing means the behaviour no longer
// matches what the original did.
//
// WHAT IS DRIVEN. The real GameSimulation and the real Projectile, spawned by
// spawnProjectile and stepped by tick(), because what the corpus recorded is a
// projectile crossing a distance and not a unit deciding to fire. The unit
// behaviour that picks a target, the aim, the reload and the hit test are all
// out of scope here and have their own tests; putting a corpus number in one of
// those would be asserting something the corpus did not measure.
//
// Arrival is the first tick the projectile has covered the distance from where
// it was fired to where it was aimed. That is what the demo's damage record
// marks, to within the tick it lands on: a projectile arrives partway through
// its final tick, and over the corpus the overshoot past the aim point is always
// less than one step.

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain()
        {
            // Large enough that no episode's geometry leaves it, and flat at
            // zero so nothing intersects the ground. A shot's real map is not
            // reproducible here and is not what is being measured.
            Grid<unsigned char> heights(1024, 1024, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addShooter(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("us"),
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
         * The weapon as the loader would build it from the episode's own TDF
         * values -- the same weaponVelocity / 30 conversion as
         * LoadingScene_util.cpp, which is the one number this test is about.
         */
        void defineWeapon(GameSimulation& sim, const TadWeaponEpisode& episode)
        {
            WeaponDefinition w{};
            w.maxRange = 32000_ss;
            w.reloadTime = SimScalar(1.0f);
            w.burst = 1;
            w.burstInterval = 0_ss;
            w.velocity = SimScalar(static_cast<float>(episode.weaponVelocity)) / 30_ss;
            w.damageRadius = 16_ss;
            w.damage["DEFAULT"] = episode.weaponDamage;
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            sim.weaponDefinitions["corpus"] = w;
        }

        SimVector originOf(const TadWeaponEpisode& e)
        {
            return SimVector(
                SimScalar(static_cast<float>(tadFixedToDouble(e.originX))),
                SimScalar(static_cast<float>(tadFixedToDouble(e.originY))),
                SimScalar(static_cast<float>(tadFixedToDouble(e.originZ))));
        }

        SimVector targetOf(const TadWeaponEpisode& e)
        {
            return SimVector(
                SimScalar(static_cast<float>(tadFixedToDouble(e.targetX))),
                SimScalar(static_cast<float>(tadFixedToDouble(e.targetY))),
                SimScalar(static_cast<float>(tadFixedToDouble(e.targetZ))));
        }

        /**
         * How many ticks it takes the projectile to cover the distance, which is
         * the flight time plus one: like the build accumulator's first increment
         * landing on the nanoframe's own tick, a projectile takes its first step
         * on the tick it is fired.
         */
        unsigned int ticksToArrive(const TadWeaponEpisode& episode)
        {
            GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
            addShooter(sim);
            defineWeapon(sim, episode);

            auto from = originOf(episode);
            auto at = targetOf(episode);
            auto toTarget = at - from;
            auto distance = toTarget.length();

            UnitWeapon weapon;
            weapon.weaponType = "corpus";
            sim.spawnProjectile(
                PlayerId(0), weapon, from, toTarget.normalized(), distance, std::nullopt, std::nullopt, std::nullopt, at);

            for (unsigned int ticks = 1; ticks <= 4000u; ++ticks)
            {
                sim.tick();
                if (sim.projectiles.begin() == sim.projectiles.end())
                {
                    return 0;
                }
                const auto& p = sim.projectiles.begin()->second;
                if ((p.position - from).length() >= distance)
                {
                    return ticks;
                }
            }

            return 0;
        }

        std::string episodeName(const TadWeaponEpisode& episode)
        {
            return std::string(episode.shooterName) + " slot " + std::to_string(episode.weaponSlot)
                + " (" + episode.weaponName + ", " + episode.demo + " tick "
                + std::to_string(episode.shotTick) + "-" + std::to_string(episode.damageTick) + ")";
        }
    }

    TEST_CASE("a shot takes as long to arrive as it did in a real game", "[weapon][corpus]")
    {
        for (const auto& episode : tadWeaponEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                auto ticks = ticksToArrive(episode);
                REQUIRE(ticks > 0u);

                auto flight = static_cast<int>(ticks) - 1;
                REQUIRE(flight
                    == static_cast<int>(episode.flightTicks) + episode.expectedFlightDelta);
            }
        }
    }

    TEST_CASE("every weapon episode is a shot with damage the weapon can explain", "[weapon][corpus]")
    {
        // Not about RWE at all: this guards the fixture. The pairing that
        // produced these episodes is a filter over unlinked events, and the one
        // piece of evidence that it pairs the right damage with the right shot
        // is that each cell's modal damage is the firing weapon's own [DAMAGE]
        // default. A regeneration that broke the pairing would still emit
        // plausible-looking rows, and this is what would notice.
        for (const auto& episode : tadWeaponEpisodes)
        {
            DYNAMIC_SECTION(episodeName(episode))
            {
                REQUIRE(episode.weaponVelocity > 0u);
                REQUIRE(episode.weaponDamage > 0u);
                REQUIRE(episode.damageTick > episode.shotTick);
                REQUIRE(episode.flightTicks == episode.damageTick - episode.shotTick);
                REQUIRE(episode.pairingsAtMode <= episode.pairings);
                REQUIRE(episode.pairings >= 30u);

                // A representative that stood alone in its cell would be an
                // anecdote rather than a mode.
                REQUIRE(episode.pairingsAtMode > 1u);
            }
        }
    }
}
