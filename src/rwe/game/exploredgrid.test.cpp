#include <catch2/catch_test_macros.hpp>
#include <random>
#include <rwe/game/save_util.h>
#include <rwe/grid/Grid.h>

/**
 * The explored grid's own round trip.
 *
 * It is the one piece of visibility a load cannot recompute -- the visible
 * grid is rebuilt from where the units are standing, but where a player has
 * *been* exists nowhere else. It is stored as run lengths because the grid is
 * strictly zero or one and what a player has explored is contiguous, so a map
 * walked for an hour comes out as a few hundred numbers rather than tens of
 * thousands of cells.
 *
 * Tested here rather than only through a whole-simulation round trip, because
 * an encoder that gained or lost a single cell is invisible in a save that
 * also carries several thousand other fields.
 */
namespace rwe
{
    namespace
    {
        Grid<unsigned char> roundTrip(const Grid<unsigned char>& original)
        {
            Grid<unsigned char> restored(original.getWidth(), original.getHeight(), static_cast<unsigned char>(0));
            loadExploredGrid(saveExploredGrid(original), restored);
            return restored;
        }
    }

    TEST_CASE("the explored grid survives a round trip", "[saveload][explored]")
    {
        SECTION("a map nobody has walked")
        {
            Grid<unsigned char> grid(64, 64, static_cast<unsigned char>(0));
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());

            // One run, and it covers everything.
            auto j = saveExploredGrid(grid);
            REQUIRE(j.at("runs").size() == 1);
            REQUIRE(j.at("runs")[0].get<std::uint32_t>() == 64u * 64u);
        }

        SECTION("a map walked from end to end")
        {
            Grid<unsigned char> grid(64, 64, static_cast<unsigned char>(1));
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());

            // The leading run is the zero-length one: the runs alternate and
            // start with unexplored, so a grid whose first cell is explored
            // has to open with nothing. It looks like a mistake in the file
            // and is not.
            auto j = saveExploredGrid(grid);
            REQUIRE(j.at("runs").size() == 2);
            REQUIRE(j.at("runs")[0].get<std::uint32_t>() == 0u);
            REQUIRE(j.at("runs")[1].get<std::uint32_t>() == 64u * 64u);
        }

        SECTION("the first cell explored and nothing else")
        {
            Grid<unsigned char> grid(8, 8, static_cast<unsigned char>(0));
            grid.set(0, 0, 1);
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("the last cell explored and nothing else")
        {
            // The run that ends exactly at the edge: the decoder's bound is on
            // the cell being written, not on the start of the run, or a run
            // beginning in range finishes past the end of the vector.
            Grid<unsigned char> grid(8, 8, static_cast<unsigned char>(0));
            grid.set(7, 7, 1);
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("a patchwork, which is what a real game looks like")
        {
            Grid<unsigned char> grid(64, 64, static_cast<unsigned char>(0));
            // Two explored blobs with unexplored ground between them.
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
                    grid.set(x, y, 1);
                }
            }
            REQUIRE(roundTrip(grid).getVector() == grid.getVector());
        }

        SECTION("noise, which is what it must not break on")
        {
            // Deliberately the worst case for run lengths -- every other cell
            // -- to prove correctness rather than compression.
            std::mt19937 rng(1234);
            Grid<unsigned char> grid(37, 53, static_cast<unsigned char>(0));
            for (unsigned int y = 0; y < grid.getHeight(); ++y)
            {
                for (unsigned int x = 0; x < grid.getWidth(); ++x)
                {
                    grid.set(x, y, static_cast<unsigned char>(rng() % 2));
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
        Grid<unsigned char> saved(16, 16, static_cast<unsigned char>(1));
        auto j = saveExploredGrid(saved);

        Grid<unsigned char> other(32, 32, static_cast<unsigned char>(0));
        loadExploredGrid(j, other);

        for (auto cell : other.getVector())
        {
            REQUIRE(cell == 0);
        }
    }
}
