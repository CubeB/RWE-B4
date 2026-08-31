#include <catch2/catch_test_macros.hpp>
#include <rwe/render/FogTiles.h>

namespace rwe
{
    /**
     * The square tile set paints whole quadrants, so every texel of the result
     * is decided by the corner codes alone. That makes it the right stand-in
     * for testing the marching-squares wiring: with TA's ragged artwork the
     * same tests could only assert on the middle of each quadrant.
     */
    static const auto squareTiles = makeSquareFogTileSet();

    /** Reads a texel by its position on the map rather than in the window. */
    static unsigned char texelAt(const FogRasterizer& r, int mapX, int mapY)
    {
        auto x = mapX - r.getOffsetX();
        auto y = mapY - r.getOffsetY();
        return r.getData()[(y * r.getWidth()) + x];
    }

    /** Counts texels not equal to the expected value, so a whole area is one assertion. */
    static int countOtherThan(const FogRasterizer& r, const GridRegion& mapRegion, unsigned char expected)
    {
        int count = 0;
        for (int y = mapRegion.y; y < mapRegion.y + mapRegion.height; ++y)
        {
            for (int x = mapRegion.x; x < mapRegion.x + mapRegion.width; ++x)
            {
                if (texelAt(r, x, y) != expected)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    TEST_CASE("FogRasterizer")
    {
        SECTION("draws nothing where the whole map is in sight")
        {
            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(1));

            FogRasterizer r;
            r.update(squareTiles, visible, explored, explored.getRegion());

            REQUIRE(countOtherThan(r, GridRegion(0, 0, 4 * 32, 4 * 32), 0) == 0);
        }

        SECTION("fills a cell nobody has ever seen, out to its edges and no further")
        {
            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(1));
            explored.set(1, 1, 0);
            visible.set(1, 1, 0);

            FogRasterizer r;
            r.update(squareTiles, visible, explored, explored.getRegion());

            // The four overlay tiles around the cell each contribute the
            // quadrant that lies inside it, and between them cover it exactly.
            REQUIRE(countOtherThan(r, GridRegion(32, 32, 32, 32), FogRasterizer::UnexploredValue) == 0);
            REQUIRE(texelAt(r, 31, 48) == 0);
            REQUIRE(texelAt(r, 64, 48) == 0);
            REQUIRE(texelAt(r, 48, 31) == 0);
            REQUIRE(texelAt(r, 48, 64) == 0);
        }

        SECTION("greys ground that is remembered but not currently seen")
        {
            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(1));
            visible.set(2, 2, 0);

            FogRasterizer r;
            r.update(squareTiles, visible, explored, explored.getRegion());

            REQUIRE(texelAt(r, 80, 80) == FogRasterizer::UnseenValue);
            REQUIRE(texelAt(r, 64, 64) == FogRasterizer::UnseenValue);
            REQUIRE(texelAt(r, 63, 80) == 0);
        }

        SECTION("treats a visible count above one as seen")
        {
            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(3));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(1));

            FogRasterizer r;
            r.update(squareTiles, visible, explored, explored.getRegion());

            REQUIRE(texelAt(r, 48, 48) == 0);
        }

        SECTION("leaves the map border alone when the edge cells are explored")
        {
            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(1));

            FogRasterizer r;
            r.update(squareTiles, visible, explored, explored.getRegion());

            REQUIRE(texelAt(r, 0, 0) == 0);
            REQUIRE(texelAt(r, (4 * 32) - 1, (4 * 32) - 1) == 0);
        }

        SECTION("meets the map border flush, with no ragged edge left exposed")
        {
            // A frame's raggedness is covered by the tile drawing the other
            // side of the boundary. At the map's edge there is no such tile, so
            // every entry along it has to come out 0 or 15, the two codes that
            // are drawn without artwork at all. This tile set paints nothing
            // for any other code, so a solid result proves it.
            auto holeyTiles = makeSquareFogTileSet();
            for (int v = 0; v < FogTileSet::VariantCount; ++v)
            {
                for (int i = 0; i < 14; ++i)
                {
                    holeyTiles.unexplored[v][i].mask.assign(holeyTiles.unexplored[v][i].mask.size(), 0);
                    holeyTiles.unseen[v][i].mask.assign(holeyTiles.unseen[v][i].mask.size(), 0);
                }
            }

            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(0));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(0));

            FogRasterizer r;
            r.update(holeyTiles, visible, explored, explored.getRegion());

            REQUIRE(countOtherThan(r, GridRegion(0, 0, 4 * 32, 4 * 32), FogRasterizer::UnexploredValue) == 0);
        }

        SECTION("reports nothing to redraw when the fog has not moved")
        {
            Grid<unsigned char> visible(4, 4, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(4, 4, static_cast<unsigned char>(1));
            visible.set(1, 2, 0);

            FogRasterizer r;
            REQUIRE(r.update(squareTiles, visible, explored, explored.getRegion()).has_value());
            REQUIRE(!r.update(squareTiles, visible, explored, explored.getRegion()).has_value());

            // A change in the count that does not change what is seen is not
            // a change in the fog.
            visible.set(0, 0, 2);
            REQUIRE(!r.update(squareTiles, visible, explored, explored.getRegion()).has_value());
        }

        SECTION("redraws only around what changed")
        {
            Grid<unsigned char> visible(8, 8, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(8, 8, static_cast<unsigned char>(1));

            FogRasterizer r;
            auto first = r.update(squareTiles, visible, explored, explored.getRegion());
            REQUIRE(first.has_value());
            REQUIRE(first->windowChanged);

            visible.set(4, 4, 0);
            auto second = r.update(squareTiles, visible, explored, explored.getRegion());
            REQUIRE(second.has_value());
            REQUIRE(!second->windowChanged);

            auto left = second->dirty.x + r.getOffsetX();
            auto top = second->dirty.y + r.getOffsetY();

            // The four overlay tiles around the cell, and no more.
            REQUIRE(left <= 4 * 32);
            REQUIRE(top <= 4 * 32);
            REQUIRE(left + second->dirty.width >= 5 * 32);
            REQUIRE(top + second->dirty.height >= 5 * 32);
            REQUIRE(second->dirty.width <= 3 * 32);
            REQUIRE(second->dirty.height <= 3 * 32);
        }

        SECTION("keeps only a window around the view, and moves it when the view leaves")
        {
            Grid<unsigned char> visible(64, 64, static_cast<unsigned char>(1));
            Grid<unsigned char> explored(64, 64, static_cast<unsigned char>(1));

            FogRasterizer r;
            auto first = r.update(squareTiles, visible, explored, GridRegion(10, 10, 6, 6));
            REQUIRE(first.has_value());
            REQUIRE(first->windowChanged);

            // Six cells of view plus the margin on each side, never the whole
            // 64 cell map.
            REQUIRE(r.getWidth() == (6 + 1 + (2 * FogRasterizer::MarginInTiles)) * 32);
            REQUIRE(r.getOffsetX() == ((10 - FogRasterizer::MarginInTiles) * 32) - 16);

            // A scroll that stays inside the window costs nothing.
            REQUIRE(!r.update(squareTiles, visible, explored, GridRegion(11, 11, 6, 6)).has_value());

            // One that does not moves the window.
            auto moved = r.update(squareTiles, visible, explored, GridRegion(20, 20, 6, 6));
            REQUIRE(moved.has_value());
            REQUIRE(moved->windowChanged);
            REQUIRE(r.getOffsetX() == ((20 - FogRasterizer::MarginInTiles) * 32) - 16);
        }

        SECTION("an incremental redraw matches a build from scratch")
        {
            Grid<unsigned char> visible(8, 8, static_cast<unsigned char>(0));
            Grid<unsigned char> explored(8, 8, static_cast<unsigned char>(0));
            for (int y = 1; y < 7; ++y)
            {
                for (int x = 1; x < 7; ++x)
                {
                    explored.set(x, y, 1);
                }
            }
            visible.set(3, 3, 1);

            FogRasterizer incremental;
            incremental.update(squareTiles, visible, explored, explored.getRegion());

            visible.set(3, 3, 0);
            visible.set(4, 5, 1);
            explored.set(4, 5, 1);
            explored.set(0, 0, 1);
            incremental.update(squareTiles, visible, explored, explored.getRegion());

            FogRasterizer fresh;
            fresh.update(squareTiles, visible, explored, explored.getRegion());

            int differences = 0;
            for (int y = 0; y < fresh.getHeight(); ++y)
            {
                for (int x = 0; x < fresh.getWidth(); ++x)
                {
                    if (incremental.getData()[(y * fresh.getWidth()) + x] != fresh.getData()[(y * fresh.getWidth()) + x])
                    {
                        ++differences;
                    }
                }
            }
            REQUIRE(differences == 0);
        }
    }
}
