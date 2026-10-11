#include <catch2/catch_test_macros.hpp>
#include <rwe/ui/NinePatch.h>

namespace rwe
{
    namespace
    {
        int frameAt(const std::vector<NinePatchTile>& tiles, int x, int y)
        {
            int found = -1;
            for (const auto& t : tiles)
            {
                if (t.x == x && t.y == y)
                {
                    found = t.frame;
                }
            }
            return found;
        }
    }

    TEST_CASE("a nine-patch is corners, edges and a middle, pinned to the far edges", "[ui]")
    {
        SECTION("an area of exactly three frames each way uses each frame once")
        {
            auto tiles = layoutNinePatch(192, 192, 64, 64);
            REQUIRE(tiles.size() == 9);
            CHECK(frameAt(tiles, 0, 0) == 0);
            CHECK(frameAt(tiles, 64, 0) == 1);
            CHECK(frameAt(tiles, 128, 0) == 2);
            CHECK(frameAt(tiles, 0, 64) == 3);
            CHECK(frameAt(tiles, 64, 64) == 4);
            CHECK(frameAt(tiles, 128, 64) == 5);
            CHECK(frameAt(tiles, 0, 128) == 6);
            CHECK(frameAt(tiles, 64, 128) == 7);
            CHECK(frameAt(tiles, 128, 128) == 8);
        }

        SECTION("the last column and row end exactly on the edge when the size is not a multiple")
        {
            // 100 wide: tiles at 0 and, pinned, at 36. 130 tall: rows at 0, 64 and, pinned, 66.
            auto tiles = layoutNinePatch(100, 130, 64, 64);
            CHECK(frameAt(tiles, 0, 0) == 0);
            CHECK(frameAt(tiles, 36, 0) == 2);
            CHECK(frameAt(tiles, 0, 64) == 3);
            CHECK(frameAt(tiles, 36, 64) == 5);
            CHECK(frameAt(tiles, 0, 66) == 6);
            CHECK(frameAt(tiles, 36, 66) == 8);
        }

        SECTION("the pinned row is drawn after the row it overlaps")
        {
            auto tiles = layoutNinePatch(64, 100, 64, 64);
            REQUIRE(tiles.size() == 2);
            CHECK(tiles[0].frame == 0);
            CHECK(tiles[1].frame == 6);
            CHECK(tiles[1].y == 36);
        }

        SECTION("an area no bigger than a frame is one top-left tile")
        {
            auto tiles = layoutNinePatch(40, 30, 64, 64);
            REQUIRE(tiles.size() == 1);
            CHECK(tiles[0].frame == 0);
            CHECK(tiles[0].x == 0);
            CHECK(tiles[0].y == 0);
        }

        SECTION("a wide dialog repeats the middle frame along its rows")
        {
            auto tiles = layoutNinePatch(400, 100, 64, 64);
            CHECK(frameAt(tiles, 64, 0) == 1);
            CHECK(frameAt(tiles, 192, 0) == 1);
            CHECK(frameAt(tiles, 336, 0) == 2);
            CHECK(frameAt(tiles, 336, 36) == 8);
        }

        SECTION("nothing is laid over an empty area or a step of nothing")
        {
            CHECK(layoutNinePatch(0, 100, 64, 64).empty());
            CHECK(layoutNinePatch(100, 100, 0, 64).empty());
        }
    }
}
