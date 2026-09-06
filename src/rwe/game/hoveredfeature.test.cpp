#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>

/**
 * The feature under the cursor outlives the feature.
 *
 * GameScene picks the hovered feature's id while it updates and reads it again
 * when the panel draws and when the cursor is resolved. A feature can be
 * reclaimed between the two, and GameSimulation::getFeature asserts on an id
 * that no longer resolves -- so hovering a tree while a builder reclaimed it
 * took the game down with an assertion inside renderUi.
 */
namespace rwe
{
    namespace
    {
        MapTerrain makeFlatTerrain(int width = 64, int height = 64)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /** A tree: the thing you hover over and a builder reclaims. */
        FeatureDefinition makeTreeDef()
        {
            FeatureDefinition d{};
            d.name = "treetype1";
            d.footprintX = 2;
            d.footprintZ = 2;
            d.height = 40_ss;
            d.reclaimable = true;
            d.autoreclaimable = true;
            d.metal = 0;
            d.energy = 100;
            d.damage = 10;
            d.blocking = true;
            return d;
        }

        /** A fortification wall: there to be looked at, not reclaimed. */
        FeatureDefinition makeWallDef()
        {
            auto d = makeTreeDef();
            d.name = "arm_wall";
            d.reclaimable = false;
            d.autoreclaimable = false;
            return d;
        }
    }

    TEST_CASE("tryGetHoveredFeature", "[hoveredfeature]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);
        auto treeDef = sim.featureDefinitions.insert(makeTreeDef());
        auto treeId = sim.addFeature(treeDef, 20, 20).value();

        SECTION("nothing hovered resolves to nothing")
        {
            REQUIRE_FALSE(tryGetHoveredFeature(sim, std::nullopt).has_value());
        }

        SECTION("a feature that is there resolves to it")
        {
            auto feature = tryGetHoveredFeature(sim, treeId);
            REQUIRE(feature.has_value());
            REQUIRE(feature->get().featureName == treeDef);
        }

        SECTION("a feature reclaimed since it was hovered resolves to nothing")
        {
            // The crash: the id was picked during update and the feature was
            // gone by the time the panel drew it.
            sim.deleteFeature(treeId);
            REQUIRE_FALSE(tryGetHoveredFeature(sim, treeId).has_value());
        }
    }

    TEST_CASE("featureCanBeReclaimed", "[hoveredfeature]")
    {
        GameSimulation sim(makeFlatTerrain(), 0u, 0, 0);

        SECTION("a reclaimable feature can be")
        {
            auto treeId = sim.addFeature(sim.featureDefinitions.insert(makeTreeDef()), 20, 20).value();
            REQUIRE(featureCanBeReclaimed(sim, treeId));
        }

        SECTION("an unreclaimable one cannot")
        {
            auto wallId = sim.addFeature(sim.featureDefinitions.insert(makeWallDef()), 30, 30).value();
            REQUIRE_FALSE(featureCanBeReclaimed(sim, wallId));
        }

        SECTION("a feature that has already gone cannot be reclaimed either")
        {
            // Reached from the cursor and order paths with a hovered id, which
            // is stale in exactly the same window as the panel's.
            auto treeId = sim.addFeature(sim.featureDefinitions.insert(makeTreeDef()), 20, 20).value();
            sim.deleteFeature(treeId);
            REQUIRE_FALSE(featureCanBeReclaimed(sim, treeId));
        }
    }
}
