#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/Projectile.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A solid, blocking feature in the shape of a TA rock or unit wreck:
         * worth something to reclaim, and with hit points of its own.
         */
        FeatureDefinition makeWreckDef(const std::string& name, unsigned int metal, unsigned int hitPoints)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 20_ss;
            d.blocking = true;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = metal;
            d.hitDensity = 100;
            d.damage = hitPoints;
            return d;
        }

        PlayerId addPlayer(GameSimulation& sim)
        {
            GamePlayerInfo p{
                std::optional<std::string>("salvager"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(0.0f),
                Energy(0.0f),
                Metal(10000.0f),
                Energy(10000.0f),
                Metal(10000.0f),
                Energy(10000.0f),
            };
            return sim.addPlayer(p);
        }

        /** A shell that does `damage` points inside `radius`, centred wherever you drop it. */
        Projectile makeShell(const SimVector& position, unsigned int damage, SimScalar radius)
        {
            Projectile p{};
            p.weaponType = "TESTSHELL";
            p.owner = PlayerId(0);
            p.position = position;
            p.previousPosition = position;
            p.origin = position;
            p.damage.insert_or_assign("DEFAULT", damage);
            p.damageRadius = radius;
            return p;
        }
    }

    TEST_CASE("feature hit points", "[wreckage]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

        // Modelled on TA's greenworld [Rock]: metal=100, damage=2000,
        // featuredead=Rock1a (a more battered rock worth the same metal).
        auto rubbleDef = sim.featureDefinitions.insert(makeWreckDef("rubble", 100u, 0u));
        auto rockDef = sim.featureDefinitions.insert(makeWreckDef("rock", 100u, 2000u));
        sim.featureDefinitions.get(rockDef).featureDead = rubbleDef;

        SECTION("a feature is placed at the hit points its definition declares")
        {
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);
        }

        SECTION("gunfire does not touch a feature")
        {
            // Weapons damage units only. A wreck or a rock sitting under a barrage
            // keeps every hit point and stays exactly where it is: the only ways a
            // feature leaves the map are reclaim and fire.
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            // Squarely on top of it, and with far more damage than it has health.
            sim.doProjectileImpact(makeShell(position, 100000u, 64_ss), ImpactType::Normal);

            REQUIRE(sim.tryGetFeature(rockId).has_value());
            REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);
            REQUIRE((sim.getFeature(rockId).position == position));

            // No featureDead rubble was left behind in its place either.
            for (const auto& [_, feature] : sim.features)
            {
                REQUIRE(feature.featureName != rubbleDef);
            }
        }
    }

    TEST_CASE("reclaim work scales with what is left of the feature", "[wreckage]")
    {
        SECTION("a substantial rock takes far longer to clear than a bush")
        {
            // Straight out of TA's greenworld data: [Rock] is metal=100 damage=2000,
            // [Shrub1] is energy=20 with no damage key at all.
            auto rock = makeWreckDef("rock", 100u, 2000u);
            auto bush = makeWreckDef("bush", 0u, 0u);
            bush.metal = 0u;
            bush.energy = 20u;

            REQUIRE(computeFeatureReclaimWork(rock, rock.damage) == 600u);
            REQUIRE(computeFeatureReclaimWork(bush, bush.damage) == 20u);
        }

        SECTION("fewer hit points means less to clear away")
        {
            auto rock = makeWreckDef("rock", 100u, 2000u);
            REQUIRE(computeFeatureReclaimWork(rock, 400u) < computeFeatureReclaimWork(rock, 2000u));
        }

        SECTION("work is never zero, so valueless scenery can still be cleared")
        {
            auto smudge = makeWreckDef("smudge", 0u, 0u);
            REQUIRE(computeFeatureReclaimWork(smudge, 0u) == 1u);
        }
    }

    TEST_CASE("a feature yields its full metal when reclaimed", "[wreckage]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto rockDef = sim.featureDefinitions.insert(makeWreckDef("rock", 100u, 2000u));
        auto rockId = sim.addFeature(rockDef, 8, 8).value();

        // Nothing has worn the rock down -- weapons cannot -- so it is reclaimed
        // at the full hit points it was placed with.
        REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);

        // 100 metal + 2000/4 hit points = 600 work, taken in thirty bites of 20.
        for (int i = 0; i < 29; ++i)
        {
            REQUIRE_FALSE(sim.reclaimFeature(rockId, player, 20u));
        }
        REQUIRE(sim.reclaimFeature(rockId, player, 20u));

        REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());

        const auto& info = sim.getPlayer(player);
        REQUIRE(info.metal.value + info.metalProductionBuffer.value == Catch::Approx(100.0f));
    }
}
