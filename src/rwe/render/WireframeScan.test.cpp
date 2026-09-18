#include <catch2/catch_test_macros.hpp>
#include <rwe/render/WireframeScan.h>

namespace rwe
{
    namespace
    {
        std::vector<WireframePixel> scan(std::vector<Vector2f> corners)
        {
            std::vector<WireframePixel> pixels;
            scanWireframePolygon(corners, pixels);
            return pixels;
        }

        bool has(const std::vector<WireframePixel>& pixels, int x, int y)
        {
            for (const auto& p : pixels)
            {
                if (p.x == x && p.y == y)
                {
                    return true;
                }
            }
            return false;
        }
    }

    TEST_CASE("scanWireframePolygon keeps the two ends of each row", "[wireframe]")
    {
        SECTION("a square facing the camera draws its two sides and not its top or bottom")
        {
            // Clockwise on a y-down screen, as a 3DO lists a polygon that
            // faces the viewer. It covers pixels 0-3 on rows 0-3; the right
            // end is the first pixel past the edge, as the original's is.
            auto pixels = scan({Vector2f(0.0f, 0.0f), Vector2f(4.0f, 0.0f), Vector2f(4.0f, 4.0f), Vector2f(0.0f, 4.0f)});
            REQUIRE(pixels.size() == 8);
            for (int row = 0; row < 4; ++row)
            {
                REQUIRE(has(pixels, 0, row));
                REQUIRE(has(pixels, 4, row));
            }
        }

        SECTION("the same square wound the other way faces away and draws nothing")
        {
            auto pixels = scan({Vector2f(0.0f, 0.0f), Vector2f(0.0f, 4.0f), Vector2f(4.0f, 4.0f), Vector2f(4.0f, 0.0f)});
            REQUIRE(pixels.empty());
        }

        SECTION("a shallow edge comes out as one pixel a row")
        {
            // The right edge runs eight across and two down: two dots, a
            // quarter and three quarters of the way along it.
            auto pixels = scan({Vector2f(0.0f, 0.0f), Vector2f(8.0f, 2.0f), Vector2f(0.0f, 2.0f)});
            REQUIRE(pixels.size() == 4);
            REQUIRE(has(pixels, 0, 0));
            REQUIRE(has(pixels, 2, 0));
            REQUIRE(has(pixels, 0, 1));
            REQUIRE(has(pixels, 6, 1));

            for (const auto& p : pixels)
            {
                if (p.x == 6)
                {
                    REQUIRE(p.from == 0);
                    REQUIRE(p.to == 1);
                    REQUIRE(p.t == 0.75f);
                }
            }
        }

        SECTION("a sliver no pixel centre falls inside draws nothing")
        {
            auto pixels = scan({Vector2f(0.0f, 0.0f), Vector2f(0.2f, 0.0f), Vector2f(0.2f, 4.0f), Vector2f(0.0f, 4.0f)});
            REQUIRE(pixels.empty());
        }

        SECTION("a polygon less than a row high draws nothing")
        {
            auto pixels = scan({Vector2f(0.0f, 0.1f), Vector2f(4.0f, 0.1f), Vector2f(4.0f, 0.3f), Vector2f(0.0f, 0.3f)});
            REQUIRE(pixels.empty());
        }
    }

    TEST_CASE("scanLine draws a line one pixel wide and unbroken", "[wireframe]")
    {
        auto line = [](Vector2f a, Vector2f b) {
            std::vector<LinePixel> pixels;
            scanLine(a, b, pixels);
            return pixels;
        };

        SECTION("a shallow line has one pixel in each column it crosses")
        {
            auto pixels = line(Vector2f(0.5f, 0.5f), Vector2f(8.5f, 2.5f));
            REQUIRE(pixels.size() == 9);
            for (int i = 0; i < 9; ++i)
            {
                REQUIRE(pixels[i].x == i);
            }
            REQUIRE(pixels.front().y == 0);
            REQUIRE(pixels.back().y == 2);
            REQUIRE(pixels.back().t == 1.0f);
        }

        SECTION("a steep line has one pixel in each row")
        {
            auto pixels = line(Vector2f(3.5f, 0.5f), Vector2f(4.5f, 6.5f));
            REQUIRE(pixels.size() == 7);
            for (int i = 0; i < 7; ++i)
            {
                REQUIRE(pixels[i].y == i);
            }
        }

        SECTION("a point is one pixel")
        {
            auto pixels = line(Vector2f(2.25f, 3.75f), Vector2f(2.25f, 3.75f));
            REQUIRE(pixels.size() == 1);
            REQUIRE(pixels[0].x == 2);
            REQUIRE(pixels[0].y == 3);
        }
    }
}
