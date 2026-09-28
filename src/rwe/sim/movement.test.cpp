#include <catch2/catch_test_macros.hpp>
#include <rwe/grid/Grid.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/sim_test_util.h>
#include <algorithm>
#include <vector>

/**
 * The terrain caches, held against the arithmetic they hold.
 *
 * MapTerrain builds a per-cell slope grid and a per-cell water-depth grid in
 * its constructor, and isGridPointWalkable -- and so canBeBuiltAt, the
 * reachability labelling, the movement-class collision grids and MapIntel's
 * shipyard sweep -- answer from them. Every one of those is a decision the
 * simulation goes on to make, so a cache that disagreed with the heightmap
 * about one cell would be a desync rather than an optimisation, and what
 * stands between those two outcomes is this file.
 *
 * So the tests are all one shape: walk every cell of a map that has variety
 * in it, and require the cached value to equal what the geometric function
 * computes from the heightmap. A flat map would pass a cache that answered
 * every slope with zero and every depth with zero, which is why none of the
 * fixtures here is flat.
 */
namespace rwe
{
    namespace
    {
        /**
         * A map whose heights cover the whole byte range in a fixed pattern,
         * so that steep ground, gentle ground, deep water and dry land are
         * all present rather than whichever one a constant happened to be.
         *
         * Not makeFlatTerrain, which is the all-zero control and the wrong
         * shape for this: on it, a cache that returned nothing at all would
         * agree with the heightmap everywhere.
         *
         * The pattern comes from its own generator rather than a random one
         * because a failure has to be reproducible -- a test that fails on
         * some runs and not others is worse than no test.
         */
        MapTerrain makeVariedTerrain(int width, int height, SimScalar seaLevel)
        {
            Grid<unsigned char> heights(width, height, static_cast<unsigned char>(0));
            unsigned int state = 12345u;
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    // A pseudorandom byte plus two ramps, so most cells
                    // differ from their neighbour by a little and some by a
                    // lot, and the map's extremes are 0 and 255.
                    state = (state * 1103515245u) + 12345u;
                    const auto n = (state >> 16u) & 0xFFu;
                    heights.set(x, y, static_cast<unsigned char>((n + (x * 7u) + (y * 13u)) & 0xFFu));
                }
            }
            return MapTerrain(std::move(heights), seaLevel);
        }

        /**
         * The rule isGridPointWalkable applies, written out from the
         * heightmap rather than called.
         *
         * Deliberately independent of the functions under test: were this
         * the same code, agreeing with itself would prove nothing. The
         * per-cell checks in the first two cases are the load-bearing part
         * of this file; this one is the end of the chain, and the only place
         * that has to be a restatement rather than a call.
         */
        bool walkableByHand(
            const Grid<unsigned char>& heights,
            unsigned int waterLevel,
            const MovementClassDefinition& mc,
            unsigned int x,
            unsigned int y)
        {
            // The dry or wet slope limit, decided over the (w+1)x(h+1) block
            // the way isAreaUnderWater decides it.
            bool underWater = false;
            for (unsigned int dy = 0; dy < mc.footprintZ + 1 && !underWater; ++dy)
            {
                for (unsigned int dx = 0; dx < mc.footprintX + 1; ++dx)
                {
                    if (heights.get(static_cast<int>(x + dx), static_cast<int>(y + dy)) < waterLevel)
                    {
                        underWater = true;
                        break;
                    }
                }
            }
            const auto slopeLimit = underWater ? mc.maxWaterSlope : mc.maxSlope;

            for (unsigned int dy = 0; dy < mc.footprintZ; ++dy)
            {
                for (unsigned int dx = 0; dx < mc.footprintX; ++dx)
                {
                    unsigned int lowest = 255;
                    unsigned int highest = 0;
                    for (unsigned int cy = 0; cy < 2; ++cy)
                    {
                        for (unsigned int cx = 0; cx < 2; ++cx)
                        {
                            const auto h = static_cast<unsigned int>(heights.get(static_cast<int>(x + dx + cx), static_cast<int>(y + dy + cy)));
                            lowest = std::min(lowest, h);
                            highest = std::max(highest, h);
                        }
                    }
                    if (highest - lowest > slopeLimit)
                    {
                        return false;
                    }

                    const auto depth = getWaterDepth(heights, waterLevel, x + dx, y + dy);
                    if (depth < mc.minWaterDepth || depth > mc.maxWaterDepth)
                    {
                        return false;
                    }
                }
            }

            return true;
        }
    }

    TEST_CASE("the cached slope grid is the slope getSlope computes")
    {
        SECTION("over a map with heights across the whole byte range")
        {
            auto terrain = makeVariedTerrain(64, 48, 40_ss);
            const auto& heights = terrain.getHeightMap();
            const auto& slopes = terrain.getSlopeMap();

            // One cell narrower than the heightmap on each axis, because the
            // 2x2 block at the last row or column would read past the map.
            REQUIRE(slopes.getWidth() == heights.getWidth() - 1);
            REQUIRE(slopes.getHeight() == heights.getHeight() - 1);

            for (int y = 0; y < slopes.getHeight(); ++y)
            {
                for (int x = 0; x < slopes.getWidth(); ++x)
                {
                    REQUIRE(slopes.get(x, y) == getSlope(heights, static_cast<unsigned int>(x), static_cast<unsigned int>(y)));
                }
            }
        }

        SECTION("including on a map that is flat everywhere but for one cliff")
        {
            // The cells either side of the step are the interesting ones, and
            // a uniformly noisy map only reaches them by luck.
            Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(0));
            for (int y = 0; y < 16; ++y)
            {
                for (int x = 8; x < 16; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(255));
                }
            }
            auto terrain = MapTerrain(std::move(heights), 0_ss);

            const auto& cached = terrain.getSlopeMap();
            for (int y = 0; y < cached.getHeight(); ++y)
            {
                for (int x = 0; x < cached.getWidth(); ++x)
                {
                    REQUIRE(cached.get(x, y) == getSlope(terrain.getHeightMap(), static_cast<unsigned int>(x), static_cast<unsigned int>(y)));
                }
            }
            // And the cliff really is in there, so the loop above was not
            // satisfied by both sides reading zero.
            REQUIRE(cached.get(7, 0) == 255);
            REQUIRE(cached.get(6, 0) == 0);
        }

        SECTION("and on a map too small to hold a 2x2 block at all")
        {
            // Nothing can ask a slope question of this map, and the grid says
            // so by being empty rather than by being one cell wide and
            // answering a question that would read past the end.
            auto terrain = makeFlatTerrain(1, 1);
            REQUIRE(terrain.getSlopeMap().getWidth() == 0);
            REQUIRE(terrain.getSlopeMap().getHeight() == 0);
        }
    }

    TEST_CASE("the cached water depth is the depth getWaterDepth computes")
    {
        SECTION("across sea levels from dry to over the tallest cell")
        {
            for (auto seaLevel : {0_ss, 1_ss, 40_ss, 128_ss, 254_ss, 255_ss})
            {
                auto terrain = makeVariedTerrain(40, 32, seaLevel);
                const auto& heights = terrain.getHeightMap();
                const auto& depths = terrain.getWaterDepthMap();

                REQUIRE(depths.getWidth() == heights.getWidth());
                REQUIRE(depths.getHeight() == heights.getHeight());

                for (int y = 0; y < heights.getHeight(); ++y)
                {
                    for (int x = 0; x < heights.getWidth(); ++x)
                    {
                        const auto expected = getWaterDepth(heights, simScalarToUInt(seaLevel), static_cast<unsigned int>(x), static_cast<unsigned int>(y));
                        REQUIRE(depths.get(x, y) == expected);
                        REQUIRE(terrain.getWaterDepthAt(x, y) == expected);
                    }
                }
            }
        }

        SECTION("and with no cache at all when the sea level is above a byte")
        {
            // A depth of 300 does not fit in one byte, and a movement class
            // compares a depth against MinWaterDepth and MaxWaterDepth rather
            // than merely asking whether a cell is wet -- so a truncated 44
            // would be a different answer, not a slower one. The grid is empty
            // instead, and the accessor still gives the true depth.
            auto terrain = makeVariedTerrain(24, 24, 300_ss);
            REQUIRE(terrain.getWaterDepthMap().getWidth() == 0);

            for (int y = 0; y < 24; ++y)
            {
                for (int x = 0; x < 24; ++x)
                {
                    REQUIRE(terrain.getWaterDepthAt(x, y) == getWaterDepth(terrain.getHeightMap(), 300u, static_cast<unsigned int>(x), static_cast<unsigned int>(y)));
                }
            }
        }
    }

    TEST_CASE("isGridPointWalkable decides the same cells with the cache as without")
    {
        // The end of the chain: what the AI's site searches, the reachability
        // labelling and the collision grids all call, over every top-left tile
        // of a map with real shape in it. Held against the rule written out
        // from the heightmap in walkableByHand.
        auto terrain = makeVariedTerrain(48, 40, 60_ss);
        const auto& heights = terrain.getHeightMap();
        const auto waterLevel = simScalarToUInt(terrain.getSeaLevel());

        /**
         * A movement class, and what this map should do with it: whether some
         * tile is walkable for it, or whether it is one of the two degenerate
         * classes that stand everywhere or nowhere.
         */
        struct ClassCase
        {
            MovementClassDefinition definition;
            /** 0 = some tiles walkable, 1 = every tile, 2 = no tile. */
            int expectation;
        };

        const ClassCase cases[] = {
            // Ground: dry only, and gentle about it.
            {{"TANKSH2", 3u, 3u, 0u, 0u, 12u, 15u}, 0},
            // A constructor: a bigger footprint and a tighter dry slope.
            {{"CORCRAP", 4u, 4u, 0u, 0u, 10u, 20u}, 0},
            // A commander: wades to depth 100 and climbs to 32.
            {{"TANKDS2", 2u, 2u, 0u, 100u, 32u, 32u}, 0},
            // A hull: must float on at least 30 of depth, and cannot climb.
            {{"some-ship", 4u, 4u, 30u, 255u, 0u, 10u}, 0},
            // A hovercraft: dry or wet, indifferent to either.
            {{"TANKHOVER3", 3u, 3u, 0u, 255u, 12u, 12u}, 0},
            // Not shipped classes, and the two degenerate answers. One wants
            // a depth nothing on a sea-level-60 map can have, so nowhere; one
            // wants nothing at all, so everywhere. Both are worth having: a
            // cached grid read one cell out of bounds shows up on exactly
            // these, and nothing else.
            {{"too-deep", 1u, 1u, 200u, 255u, 255u, 255u}, 2},
            {{"anything-goes", 1u, 1u, 0u, 255u, 255u, 255u}, 1}};

        for (const auto& testCase : cases)
        {
            const auto& mc = testCase.definition;

            // isMaxSlopeGreaterThan reads one cell past the footprint on each
            // axis, through isAreaUnderWater, so the scan stops a tile short
            // of the edge for the same reason ReachabilityMap's does.
            const int maxX = heights.getWidth() - static_cast<int>(mc.footprintX) - 1;
            const int maxY = heights.getHeight() - static_cast<int>(mc.footprintZ) - 1;

            int walkable = 0;
            for (int y = 0; y <= maxY; ++y)
            {
                for (int x = 0; x <= maxX; ++x)
                {
                    const auto ux = static_cast<unsigned int>(x);
                    const auto uy = static_cast<unsigned int>(y);

                    const auto expected = walkableByHand(heights, waterLevel, mc, ux, uy);
                    REQUIRE(isGridPointWalkable(terrain, mc, ux, uy) == expected);
                    if (expected)
                    {
                        ++walkable;
                    }
                }
            }

            // So the loop above cannot have been satisfied by a function
            // that always refuses, nor by one that always agrees.
            const int tiles = (maxX + 1) * (maxY + 1);
            if (testCase.expectation == 0)
            {
                REQUIRE(walkable > 0);
                REQUIRE(walkable < tiles);
            }
            else if (testCase.expectation == 1)
            {
                REQUIRE(walkable == tiles);
            }
            else
            {
                REQUIRE(walkable == 0);
            }
        }
    }
}
