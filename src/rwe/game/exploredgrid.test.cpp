#include <catch2/catch_test_macros.hpp>
#include <rwe/game/save_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/PlayerVisibility.h>

/**
 * The explored grid's own round trip.
 *
 * It is the one piece of visibility a load cannot recompute -- the visible
 * grid is rebuilt from where the units are standing, but where a player has
 * *been* exists nowhere else. There is one such grid for the whole game, a bit
 * per line-of-sight group, so a cell is a small mask rather than a yes/no and
 * the encoder stores the value of each run rather than assuming 0 and 1.
 *
 * Stored as runs of equal values because the grid is almost all zeros with a
 * few explored blobs, so a map walked for an hour comes out as a few hundred
 * numbers rather than tens of thousands of cells.
 *
 * Tested here rather than only through a whole-simulation round trip, because
 * an encoder that gained or lost a single cell is invisible in a save that
 * also carries several thousand other fields.
 */
namespace rwe
{
    namespace
    {
        Grid<ExploredMask> roundTrip(const Grid<ExploredMask>& original)
        {
            Grid<ExploredMask> restored(original.getWidth(), original.getHeight(), static_cast<ExploredMask>(0));
            loadExploredGrid(saveExploredGrid(original), restored);
            return restored;
        }
    }

    TEST_CASE("the explored grid survives a round trip", "[saveload][explored]")
    {
        SECTION("a map nobody has walked")
        {
            Grid<ExploredMask> grid(64, 64, static_cast<ExploredMask>(0));
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());

            // One run, and it covers everything.
            auto j = saveExploredGrid(grid);
            REQUIRE(j.at("runs").size() == 1);
            REQUIRE(j.at("runs")[0].at(0).get<ExploredMask>() == 0);
            REQUIRE(j.at("runs")[0].at(1).get<std::uint32_t>() == 64u * 64u);
        }

        SECTION("a map walked from end to end by one group")
        {
            Grid<ExploredMask> grid(64, 64, static_cast<ExploredMask>(1));
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());

            // The value is written out rather than implied, so there is no
            // leading zero-length run here the way the old 0/1 encoding had.
            auto j = saveExploredGrid(grid);
            REQUIRE(j.at("runs").size() == 1);
            REQUIRE(j.at("runs")[0].at(0).get<ExploredMask>() == 1);
            REQUIRE(j.at("runs")[0].at(1).get<std::uint32_t>() == 64u * 64u);
        }

        SECTION("the first cell explored and nothing else")
        {
            Grid<ExploredMask> grid(8, 8, static_cast<ExploredMask>(0));
            grid.set(0, 0, 1);
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("the last cell explored and nothing else")
        {
            // The run that ends exactly at the edge: the decoder's bound is on
            // the cell being written, not on the start of the run, or a run
            // beginning in range finishes past the end of the vector.
            Grid<ExploredMask> grid(8, 8, static_cast<ExploredMask>(0));
            grid.set(7, 7, 1);
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("a cell can carry a different bit per line-of-sight group")
        {
            // The whole point of the one grid: two allies that have each
            // walked a stretch leave two bits set where they overlap and one
            // elsewhere, and the encoder has to carry the mask through.
            Grid<ExploredMask> grid(8, 8, static_cast<ExploredMask>(0));
            grid.set(1, 1, 0b001);
            grid.set(2, 1, 0b011);
            grid.set(3, 1, 0b010);
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("a patchwork, which is what a real game looks like")
        {
            Grid<ExploredMask> grid(64, 64, static_cast<ExploredMask>(0));
            // Two explored blobs with unexplored ground between them, one
            // shared between two groups where they meet.
            for (int y = 5; y < 20; ++y)
            {
                for (int x = 3; x < 25; ++x)
                {
                    grid.set(x, y, 1);
                }
            }
            for (int y = 40; y < 62; ++y)
            {
                for (int x = 30; x < 64; ++x)
                {
                    grid.set(x, y, 2);
                }
            }
            for (int y = 40; y < 50; ++y)
            {
                for (int x = 30; x < 40; ++x)
                {
                    grid.set(x, y, 3);
                }
            }
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("noise, which is what it must not break on")
        {
            // Deliberately the worst case for run lengths -- every cell a
            // different mask -- to prove correctness rather than compression.
            Grid<ExploredMask> grid(37, 53, static_cast<ExploredMask>(0));
            std::uint32_t state = 1234u;
            for (unsigned int y = 0; y < static_cast<unsigned int>(grid.getHeight()); ++y)
            {
                for (unsigned int x = 0; x < static_cast<unsigned int>(grid.getWidth()); ++x)
                {
                    state = (state * 1664525u) + 1013904223u;
                    grid.set(x, y, static_cast<ExploredMask>(state & 0x3u));
                }
            }
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }
    }

    TEST_CASE("an explored grid from another map is refused", "[saveload][explored]")
    {
        // A save whose vision grid is a different shape cannot be laid over
        // this one. The player loses their map memory rather than the load
        // failing outright, or worse, being written past the end of.
        Grid<ExploredMask> saved(16, 16, static_cast<ExploredMask>(3));
        auto j = saveExploredGrid(saved);

        Grid<ExploredMask> other(32, 32, static_cast<ExploredMask>(0));
        loadExploredGrid(j, other);

        for (auto cell : other.getVector())
        {
            REQUIRE(cell == 0);
        }
    }
}
