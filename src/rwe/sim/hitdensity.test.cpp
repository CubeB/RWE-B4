#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/WeaponDefinition.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeHitDensityTerrain()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** A flat-flying gun round, of the kind that has nothing but its own path. */
        void defineFlatShot(GameSimulation& sim)
        {
            WeaponDefinition w{};
            w.maxRange = 600_ss;
            w.velocity = 10_ss;
            w.damageRadius = 8_ss;
            w.damage["DEFAULT"] = 50;
            w.physicsType = ProjectilePhysicsTypeLineOfSight();
            sim.weaponDefinitions["flatshot"] = w;
        }

        /**
         * A rock: two cells across, twenty units tall, and blocking.
         */
        FeatureDefinition makeRock(const std::string& name)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 20_ss;
            d.blocking = true;
            d.damage = 2000;
            return d;
        }

        /**
         * Fire a level shot at the given height, from a hundred units west of
         * `through` and aimed straight across it. Answers whether the shot got
         * to the far side.
         */
        bool shotGetsThrough(GameSimulation& sim, const SimVector& through, SimScalar height)
        {
            Projectile p{};
            p.weaponType = "flatshot";
            p.owner = PlayerId(0);
            p.position = SimVector(through.x - 100_ss, height, through.z);
            p.previousPosition = p.position;
            p.origin = p.position;
            p.velocity = SimVector(10_ss, 0_ss, 0_ss);
            p.damage.insert_or_assign("DEFAULT", 50u);
            p.damageRadius = 8_ss;

            auto targetX = through.x + 100_ss;
            auto id = ProjectileId(sim.projectiles.emplace(std::move(p)));

            for (int i = 0; i < 100; ++i)
            {
                sim.updateProjectiles();

                auto shot = sim.projectiles.tryGet(id);
                if (!shot || shot->get().isDead)
                {
                    return false;
                }

                if (shot->get().position.x >= targetX)
                {
                    return true;
                }
            }

            return false;
        }
    }

    // Priority 17. `hitDensity` is set by 559 of the shipped features. The
    // long-standing guess -- recorded as a guess -- was that it is the chance a
    // shot passes through the feature rather than stopping on it. The string
    // `hitdensity` does not occur anywhere in TotalA.exe, so the original never
    // reads the key at all; its projectile-versus-feature test at 0x49B2B3 is
    // purely geometric.
    //
    // The key is no longer represented anywhere in RWE: it is not parsed into
    // FeatureTdf and FeatureDefinition has no field for it (#108). That is a
    // stronger guard than this file used to give -- the guess cannot be
    // implemented without deliberately putting the value back -- so what is
    // left here is the geometry the collision actually uses.
    TEST_CASE("hit density does not decide whether a shot stops on a feature", "[hitdensity]")
    {
        SECTION("a shot stops on a rock in the way")
        {
            // This used to run once for each of the four values the shipped
            // feature files use -- 100 for solid things, 5 and 10 for foliage,
            // 0 for smudges. With no field to carry them the four runs were the
            // same run, so what is pinned now is the stop itself.
            GameSimulation sim(makeHitDensityTerrain(), 0u, 0, 0);
            defineFlatShot(sim);

            auto rockDef = sim.featureDefinitions.insert(makeRock("rock"));
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto rock = sim.getFeature(rockId).position;

            REQUIRE_FALSE(shotGetsThrough(sim, rock, rock.y + 10_ss));
        }

        SECTION("with nothing in the way the same shot crosses the map")
        {
            GameSimulation sim(makeHitDensityTerrain(), 0u, 0, 0);
            defineFlatShot(sim);

            auto rockDef = sim.featureDefinitions.insert(makeRock("rock"));
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto rock = sim.getFeature(rockId).position;
            sim.deleteFeature(rockId);

            REQUIRE(shotGetsThrough(sim, rock, rock.y + 10_ss));
        }

        SECTION("a shot passing over the top of a feature is not stopped by it")
        {
            // The original's only test is the vertical one at 0x49B32E: the
            // feature's top is its map square's ground height plus the
            // definition's height byte, and a shot above that misses. A shot at
            // thirty units clears a rock twenty units tall standing on flat ground.
            GameSimulation sim(makeHitDensityTerrain(), 0u, 0, 0);
            defineFlatShot(sim);

            auto rockDef = sim.featureDefinitions.insert(makeRock("rock"));
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto rock = sim.getFeature(rockId).position;

            REQUIRE(shotGetsThrough(sim, rock, rock.y + 30_ss));
        }
    }
}
