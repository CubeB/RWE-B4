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

        SECTION("a feature loses hit points when a weapon lands on it")
        {
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            sim.doProjectileImpact(makeShell(position, 500u, 64_ss), ImpactType::Normal);

            REQUIRE(sim.tryGetFeature(rockId).has_value());
            REQUIRE(sim.getFeature(rockId).hitPoints == 1500u);
        }

        SECTION("damage falls off with distance, as it does for units")
        {
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            // The rock's 2x2 footprint reaches 16 world units either side of its
            // centre, so a blast 48 units out is 32 units clear of it: half of a
            // 64-unit radius, hence half damage.
            auto blastPosition = SimVector(position.x + 48_ss, position.y, position.z);
            sim.doProjectileImpact(makeShell(blastPosition, 500u, 64_ss), ImpactType::Normal);

            auto lost = 2000u - sim.getFeature(rockId).hitPoints;
            REQUIRE(lost > 0u);
            REQUIRE(lost < 500u);
        }

        SECTION("a shot out of range leaves the feature alone")
        {
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            auto blastPosition = SimVector(position.x + 200_ss, position.y, position.z);
            sim.doProjectileImpact(makeShell(blastPosition, 500u, 64_ss), ImpactType::Normal);

            REQUIRE(sim.getFeature(rockId).hitPoints == 2000u);
        }

        SECTION("a destroyed feature becomes its featureDead form")
        {
            auto rockId = sim.addFeature(rockDef, 8, 8).value();
            auto position = sim.getFeature(rockId).position;

            sim.doProjectileImpact(makeShell(position, 3000u, 64_ss), ImpactType::Normal);

            REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());

            bool foundRubble = false;
            for (const auto& [_, feature] : sim.features)
            {
                if (feature.featureName == rubbleDef)
                {
                    foundRubble = true;
                    REQUIRE((feature.position == position));
                }
            }
            REQUIRE(foundRubble);
        }

        SECTION("a destroyed feature with no featureDead simply disappears")
        {
            auto stumpDef = sim.featureDefinitions.insert(makeWreckDef("stump", 0u, 100u));
            auto stumpId = sim.addFeature(stumpDef, 12, 12).value();
            auto position = sim.getFeature(stumpId).position;

            sim.doProjectileImpact(makeShell(position, 500u, 64_ss), ImpactType::Normal);

            REQUIRE_FALSE(sim.tryGetFeature(stumpId).has_value());

            int remaining = 0;
            for ([[maybe_unused]] const auto& entry : sim.features)
            {
                ++remaining;
            }
            REQUIRE(remaining == 0);
        }

        SECTION("an indestructible feature is unharmed")
        {
            auto bedrockDef = sim.featureDefinitions.insert(makeWreckDef("bedrock", 100u, 2000u));
            sim.featureDefinitions.get(bedrockDef).indestructible = true;
            sim.featureDefinitions.get(bedrockDef).featureDead = rubbleDef;
            auto bedrockId = sim.addFeature(bedrockDef, 8, 8).value();

            sim.doProjectileImpact(makeShell(sim.getFeature(bedrockId).position, 100000u, 64_ss), ImpactType::Normal);

            REQUIRE(sim.tryGetFeature(bedrockId).has_value());
            REQUIRE(sim.getFeature(bedrockId).hitPoints == 2000u);
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

        SECTION("a damaged feature needs less work than a fresh one")
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

    TEST_CASE("shooting a feature makes it quicker to reclaim", "[wreckage]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto rockDef = sim.featureDefinitions.insert(makeWreckDef("rock", 100u, 2000u));

        auto freshId = sim.addFeature(rockDef, 4, 4).value();
        auto shotId = sim.addFeature(rockDef, 16, 16).value();

        sim.doProjectileImpact(makeShell(sim.getFeature(shotId).position, 1600u, 64_ss), ImpactType::Normal);
        REQUIRE(sim.getFeature(shotId).hitPoints == 400u);

        const auto& definition = sim.getFeatureDefinition(rockDef);
        auto freshWork = computeFeatureReclaimWork(definition, sim.getFeature(freshId).hitPoints);
        auto shotWork = computeFeatureReclaimWork(definition, sim.getFeature(shotId).hitPoints);

        REQUIRE(freshWork == 600u);
        REQUIRE(shotWork == 200u);
        REQUIRE(shotWork < freshWork);
    }

    TEST_CASE("a damaged feature still yields its full metal", "[wreckage]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto player = addPlayer(sim);
        auto rockDef = sim.featureDefinitions.insert(makeWreckDef("rock", 100u, 2000u));
        auto rockId = sim.addFeature(rockDef, 8, 8).value();

        sim.doProjectileImpact(makeShell(sim.getFeature(rockId).position, 1600u, 64_ss), ImpactType::Normal);
        REQUIRE(sim.getFeature(rockId).hitPoints == 400u);

        // 200 work in ten bites, which is exactly the reduced total.
        for (int i = 0; i < 9; ++i)
        {
            REQUIRE_FALSE(sim.reclaimFeature(rockId, player, 20u));
        }
        REQUIRE(sim.reclaimFeature(rockId, player, 20u));

        REQUIRE_FALSE(sim.tryGetFeature(rockId).has_value());

        const auto& info = sim.getPlayer(player);
        REQUIRE(info.metal.value + info.metalProductionBuffer.value == Catch::Approx(100.0f));
    }
}
