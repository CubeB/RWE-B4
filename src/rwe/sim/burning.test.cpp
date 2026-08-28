#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimTicksPerSecond.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 32, int height = 32)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        FeatureDefinition makeTreeDef(const std::string& name, bool flammable)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 20_ss;
            d.blocking = true;
            d.flamable = flammable;
            d.burnMin = 3;
            d.burnMax = 3;
            d.sparkTime = 1;
            d.spreadChance = 100;
            return d;
        }

        void tickFor(GameSimulation& sim, unsigned int ticks)
        {
            for (unsigned int i = 0; i < ticks; ++i)
            {
                sim.tick();
            }
        }
    }

    TEST_CASE("burning features", "[burning]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

        auto crispDef = sim.featureDefinitions.insert(makeTreeDef("crisp", false));
        auto treeDef = sim.featureDefinitions.insert(makeTreeDef("tree", true));
        sim.featureDefinitions.get(treeDef).featureBurnt = crispDef;
        auto rockDef = sim.featureDefinitions.insert(makeTreeDef("rock", false));

        auto treeId = sim.addFeature(treeDef, 8, 8).value();

        SECTION("only flammable features catch fire")
        {
            auto rockId = sim.addFeature(rockDef, 12, 12).value();
            sim.igniteFeature(rockId);
            REQUIRE_FALSE(sim.getFeature(rockId).burningUntil.has_value());

            sim.igniteFeature(treeId);
            REQUIRE(sim.getFeature(treeId).burningUntil.has_value());
        }

        SECTION("a burnt-out feature is replaced by its burnt form")
        {
            auto position = sim.getFeature(treeId).position;
            sim.igniteFeature(treeId);

            tickFor(sim, (3 * SimTicksPerSecond) + 1);

            REQUIRE_FALSE(sim.tryGetFeature(treeId).has_value());
            bool foundCrisp = false;
            for (const auto& [_, feature] : sim.features)
            {
                if (feature.featureName == crispDef)
                {
                    foundCrisp = true;
                    REQUIRE((feature.position == position));
                }
            }
            REQUIRE(foundCrisp);
        }

        SECTION("fire spreads to flammable neighbours at spark time")
        {
            // 2 tiles away: inside the 32-unit default reach.
            auto neighbourId = sim.addFeature(treeDef, 10, 8).value();
            // Far away: out of reach.
            auto farId = sim.addFeature(treeDef, 30, 30).value();

            sim.igniteFeature(treeId);
            REQUIRE_FALSE(sim.getFeature(neighbourId).burningUntil.has_value());

            tickFor(sim, SimTicksPerSecond);

            REQUIRE(sim.getFeature(neighbourId).burningUntil.has_value());
            REQUIRE_FALSE(sim.getFeature(farId).burningUntil.has_value());
        }

        SECTION("a fire-starting blast has a chance to ignite what it hits")
        {
            auto position = sim.getFeature(treeId).position;
            sim.tryIgniteFeaturesInRadius(position, 16_ss, 100);
            REQUIRE(sim.getFeature(treeId).burningUntil.has_value());
        }

        SECTION("a zero chance never ignites")
        {
            auto position = sim.getFeature(treeId).position;
            sim.tryIgniteFeaturesInRadius(position, 16_ss, 0);
            REQUIRE_FALSE(sim.getFeature(treeId).burningUntil.has_value());
        }
    }
}
