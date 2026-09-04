#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        constexpr int RegrowthMapSize = 16;

        MapTerrain makeRegrowthTerrain()
        {
            Grid<unsigned char> heights(RegrowthMapSize, RegrowthMapSize, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        /**
         * A tree out of greenworld's TREES.TDF: one square, reclaimable, and with
         * the `reproducearea=6` every shipped plant carries. The `reproduce`
         * chance is the thing under test, so it is the argument.
         */
        FeatureDefinition makeTree(const std::string& name, unsigned int reproduce)
        {
            FeatureDefinition d{};
            d.name = name;
            d.footprintX = 1;
            d.footprintZ = 1;
            d.height = 30_ss;
            d.blocking = true;
            d.reclaimable = true;
            d.energy = 100;
            d.reproduce = reproduce;
            d.reproduceArea = 6;
            d.damage = 1;
            return d;
        }

        std::size_t countFeatures(const GameSimulation& sim)
        {
            std::size_t n = 0;
            for (const auto& entry : sim.features)
            {
                (void)entry;
                ++n;
            }
            return n;
        }
    }

    // Priority 16. The sweep is 0x424050's tail, 0x4240A3-0x4241A3: one map square
    // per tick, walking the grid backwards, `rand(100) < reproduce` on whatever
    // stands there, and a seed dropped in a square drawn from a `reproduceArea` box.
    TEST_CASE("features regrow into empty ground", "[regrowth]")
    {
        SECTION("a plant with no reproduce chance never seeds")
        {
            // This is every feature in the shipped game: 83 of them name the key
            // and all 83 set it to zero, so a stock forest does not grow back.
            GameSimulation sim(makeRegrowthTerrain(), 0u, 0, 0);
            auto treeDef = sim.featureDefinitions.insert(makeTree("tree", 0));
            REQUIRE(sim.addFeature(treeDef, 8, 8).has_value());

            // Four full sweeps of a sixteen-square map.
            tick(sim, RegrowthMapSize * RegrowthMapSize * 4);

            REQUIRE(countFeatures(sim) == 1);
        }

        SECTION("a plant that always takes seeds into the ground beside it")
        {
            GameSimulation sim(makeRegrowthTerrain(), 0u, 0, 0);
            auto treeDef = sim.featureDefinitions.insert(makeTree("tree", 100));
            REQUIRE(sim.addFeature(treeDef, 8, 8).has_value());

            tick(sim, RegrowthMapSize * RegrowthMapSize * 2);

            REQUIRE(countFeatures(sim) > 1);
        }

        SECTION("a seed lands inside the parent's reproduce area")
        {
            // `reproducearea=6` draws an offset of rand(6) - 3 on each axis, so a
            // child is never more than three squares from its parent.
            GameSimulation sim(makeRegrowthTerrain(), 0u, 0, 0);
            auto treeDef = sim.featureDefinitions.insert(makeTree("tree", 100));
            auto parentId = sim.addFeature(treeDef, 8, 8).value();
            auto parent = sim.terrain.worldToHeightmapCoordinate(sim.getFeature(parentId).position);

            // Stop on the tick the first seed lands: after that the child is in
            // the sweep too and its own children would be two hops from the parent.
            for (int i = 0; i < RegrowthMapSize * RegrowthMapSize * 2 && countFeatures(sim) == 1; ++i)
            {
                sim.tick();
            }
            REQUIRE(countFeatures(sim) == 2);

            for (const auto& [id, feature] : sim.features)
            {
                if (id == parentId)
                {
                    continue;
                }
                auto cell = sim.terrain.worldToHeightmapCoordinate(feature.position);
                REQUIRE(std::abs(cell.x - parent.x) <= 3);
                REQUIRE(std::abs(cell.y - parent.y) <= 3);
            }
        }

        SECTION("a square waits its turn in the sweep")
        {
            // The cursor visits one square a tick, so a tree in the middle of a
            // sixteen-square map does not get its first roll for a hundred and
            // twenty ticks however certain that roll is. This is what keeps
            // regrowth to a crawl instead of a chain reaction.
            GameSimulation sim(makeRegrowthTerrain(), 0u, 0, 0);
            auto treeDef = sim.featureDefinitions.insert(makeTree("tree", 100));
            REQUIRE(sim.addFeature(treeDef, 8, 8).has_value());

            tick(sim, 10);
            REQUIRE(countFeatures(sim) == 1);

            tick(sim, (RegrowthMapSize * RegrowthMapSize) - 10);
            REQUIRE(countFeatures(sim) > 1);
        }

        SECTION("nothing grows where there is no empty ground")
        {
            // Pack the map solid. Every candidate square already holds something,
            // and the original refuses any destination whose feature slot is not
            // empty, so the wood stays exactly the size it is.
            GameSimulation sim(makeRegrowthTerrain(), 0u, 0, 0);
            auto treeDef = sim.featureDefinitions.insert(makeTree("tree", 100));

            for (int x = 0; x < RegrowthMapSize; ++x)
            {
                for (int z = 0; z < RegrowthMapSize; ++z)
                {
                    sim.addFeature(treeDef, x, z);
                }
            }
            auto planted = countFeatures(sim);
            REQUIRE(planted > 0);

            tick(sim, RegrowthMapSize * RegrowthMapSize * 2);

            REQUIRE(countFeatures(sim) == planted);
        }
    }
}
