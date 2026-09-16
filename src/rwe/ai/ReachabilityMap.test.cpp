#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitDefinition.h>
#include <string>
#include <vector>

/**
 * ReachabilityMap now carries two independent labellings -- ground and
 * naval -- so that a tank's answer and a ship's answer can both be current
 * at once without one rebuild evicting the other. These tests build small
 * synthetic strips (a fixture with real map shape, not the shipped data --
 * ReachabilityMap only ever reads the raw heightmap and a movement class,
 * neither of which has a "real" version worth reaching for here) and drive
 * both the ground and naval sides of the same instance across them.
 */
namespace rwe
{
    namespace
    {
        /** A 2D heightmap drawn as rows: 'w' is water at `waterHeight`, anything else is land at `landHeight`. */
        MapTerrain terrainFromRows(const std::vector<std::string>& rows, unsigned char landHeight, unsigned char waterHeight, SimScalar seaLevel)
        {
            auto height = rows.size();
            auto width = rows.empty() ? std::size_t(0) : rows[0].size();
            Grid<unsigned char> heights(static_cast<int>(width), static_cast<int>(height), landHeight);
            for (std::size_t y = 0; y < height; ++y)
            {
                for (std::size_t x = 0; x < width; ++x)
                {
                    heights.set(static_cast<int>(x), static_cast<int>(y), rows[y][x] == 'w' ? waterHeight : landHeight);
                }
            }
            return MapTerrain(std::move(heights), seaLevel);
        }

        /** A 1x1-footprint mover confined to dry land: any wet cell is out of bounds for it. */
        UnitDefinition::MovementCollisionInfo groundMover()
        {
            return UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
        }

        /** A 1x1-footprint mover that floats: needs at least one unit of depth, and tolerates any amount of it. */
        UnitDefinition::MovementCollisionInfo floatingMover()
        {
            return UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 1u, 255u};
        }
    }

    TEST_CASE("ReachabilityMap ground and naval layers")
    {
        SECTION("a lake blocks a tank from crossing it, but a ship can cross the same lake")
        {
            // Land - lake - land, three rows deep. Sea level 50, land height
            // 100 (depth 0), lake height 0 (depth 50): the ground mover
            // above needs depth exactly 0, the floating one needs depth at
            // least 1, so each is confined to exactly the side that suits it.
            auto terrain = terrainFromRows(
                {
                    "LLLLwwwwLLLL",
                    "LLLLwwwwLLLL",
                    "LLLLwwwwLLLL",
                },
                100, 0, 50_ss);
            GameSimulation sim(std::move(terrain), 0u, 0, 0);

            ReachabilityMap reach;

            // Tank based on the west bank.
            auto westBank = sim.terrain.heightmapIndexToWorldCenter(1, 1);
            auto eastBank = sim.terrain.heightmapIndexToWorldCenter(9, 1);
            reach.rebuild(sim, groundMover(), westBank);
            REQUIRE(reach.isReachable(sim, westBank));
            REQUIRE_FALSE(reach.isReachable(sim, eastBank));
            REQUIRE_FALSE(reach.isWalkable(sim, sim.terrain.heightmapIndexToWorldCenter(5, 1)));

            // Ship based in the lake, well away from either shore.
            auto nearWestShore = sim.terrain.heightmapIndexToWorldCenter(4, 1);
            auto nearEastShore = sim.terrain.heightmapIndexToWorldCenter(7, 1);
            reach.rebuildNaval(sim, floatingMover(), nearWestShore);
            REQUIRE(reach.isNavalReachable(sim, nearWestShore));
            REQUIRE(reach.isNavalReachable(sim, nearEastShore));
            REQUIRE_FALSE(reach.isNavalReachable(sim, eastBank));
            REQUIRE_FALSE(reach.isNavalWalkable(sim, westBank));

            // Rebuilding the naval layer must not have disturbed the ground
            // labelling that was already cached -- this is the whole point
            // of the two layers being independent.
            REQUIRE(reach.isReachable(sim, westBank));
            REQUIRE_FALSE(reach.isReachable(sim, eastBank));
        }

        SECTION("a land bridge blocks a ship from crossing it, but a tank can cross the same bridge")
        {
            // The reverse shape: lake - land - lake. The tank crosses the
            // bridge end to end; the ship, confined to water, cannot get
            // from one lake to the other at all.
            auto terrain = terrainFromRows(
                {
                    "wwwwLLLLwwww",
                    "wwwwLLLLwwww",
                    "wwwwLLLLwwww",
                },
                100, 0, 50_ss);
            GameSimulation sim(std::move(terrain), 0u, 0, 0);

            ReachabilityMap reach;

            auto bridgeWestEnd = sim.terrain.heightmapIndexToWorldCenter(4, 1);
            auto bridgeEastEnd = sim.terrain.heightmapIndexToWorldCenter(7, 1);
            reach.rebuild(sim, groundMover(), bridgeWestEnd);
            REQUIRE(reach.isReachable(sim, bridgeEastEnd));

            auto westLake = sim.terrain.heightmapIndexToWorldCenter(1, 1);
            auto eastLake = sim.terrain.heightmapIndexToWorldCenter(10, 1);
            reach.rebuildNaval(sim, floatingMover(), westLake);
            REQUIRE(reach.isNavalReachable(sim, westLake));
            REQUIRE_FALSE(reach.isNavalReachable(sim, eastLake));

            // And the ground layer, rebuilt first, is still exactly what it was.
            REQUIRE(reach.isReachable(sim, bridgeEastEnd));
        }

        SECTION("an unrebuilt map treats everything as reachable and walkable, for both layers")
        {
            ReachabilityMap reach;
            GameSimulation sim(MapTerrain(Grid<unsigned char>(4, 4, static_cast<unsigned char>(0)), 0_ss), 0u, 0, 0);
            auto somewhere = sim.terrain.heightmapIndexToWorldCenter(2, 2);
            REQUIRE_FALSE(reach.isValid());
            REQUIRE_FALSE(reach.isNavalValid());
            REQUIRE(reach.isReachable(sim, somewhere));
            REQUIRE(reach.isWalkable(sim, somewhere));
            REQUIRE(reach.isNavalReachable(sim, somewhere));
            REQUIRE(reach.isNavalWalkable(sim, somewhere));
        }

        SECTION("tile counts are reported per layer, and stay sane relative to each other")
        {
            auto terrain = terrainFromRows(
                {
                    "LLLLwwwwLLLL",
                    "LLLLwwwwLLLL",
                    "LLLLwwwwLLLL",
                },
                100, 0, 50_ss);
            GameSimulation sim(std::move(terrain), 0u, 0, 0);

            ReachabilityMap reach;
            reach.rebuild(sim, groundMover(), sim.terrain.heightmapIndexToWorldCenter(1, 1));
            reach.rebuildNaval(sim, floatingMover(), sim.terrain.heightmapIndexToWorldCenter(4, 1));

            // Both banks together are walkable for the tank, but only the
            // one the anchor stands on is reachable from it -- so reachable
            // is strictly less than walkable, not equal to it, which is what
            // would happen if the two banks were wrongly seen as connected.
            REQUIRE(reach.walkableTileCount() > 0);
            REQUIRE(reach.reachableTileCount() > 0);
            REQUIRE(reach.reachableTileCount() < reach.walkableTileCount());

            // The lake is a single connected body, so every tile the ship
            // can float on is also one it can reach from its anchor.
            REQUIRE(reach.navalWalkableTileCount() > 0);
            REQUIRE(reach.navalReachableTileCount() == reach.navalWalkableTileCount());

            // Rebuilding the naval layer left the ground counts untouched.
            REQUIRE(reach.walkableTileCount() > 0);
        }
    }

}
