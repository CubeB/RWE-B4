#include <catch2/catch_test_macros.hpp>
#include <rwe/AlphaTable.h>

namespace rwe
{
    /**
     * A palette shaped like TA's in the ways that matter here: a black, a
     * white, a mid grey and a magenta, plus a spread to give the nearest
     * match somewhere to land.
     */
    ColorPalette testPalette()
    {
        ColorPalette palette;
        palette.reserve(256);
        for (unsigned int i = 0; i < 256; ++i)
        {
            auto v = static_cast<unsigned char>(i);
            palette.push_back(Color(v, static_cast<unsigned char>(255 - i), static_cast<unsigned char>((i * 7) % 256)));
        }
        return palette;
    }

    TEST_CASE("readAlphaTable")
    {
        SECTION("rejects anything that is not exactly 65536 bytes")
        {
            REQUIRE(!readAlphaTable(std::vector<char>(0)));
            REQUIRE(!readAlphaTable(std::vector<char>(65535)));
            REQUIRE(!readAlphaTable(std::vector<char>(65537)));
            REQUIRE(!!readAlphaTable(std::vector<char>(65536)));
        }

        SECTION("keeps the bytes in order, unsigned")
        {
            std::vector<char> bytes(65536);
            bytes[0] = static_cast<char>(0);
            bytes[1] = static_cast<char>(253);
            bytes[65535] = static_cast<char>(255);

            auto table = readAlphaTable(bytes);
            REQUIRE(!!table);
            REQUIRE((*table)[0] == 0);
            REQUIRE((*table)[1] == 253);
            REQUIRE((*table)[65535] == 255);
        }
    }

    TEST_CASE("generateAlphaTable")
    {
        auto palette = testPalette();
        auto table = generateAlphaTable(palette);

        SECTION("is symmetric")
        {
            // The shipped file is symmetric in all 65536 pairs -- measured,
            // not assumed -- so the reconstruction has to be as well. This is
            // what makes the source-versus-destination orientation of the
            // table a non-question.
            unsigned int asymmetric = 0;
            for (unsigned int a = 0; a < 256; ++a)
            {
                for (unsigned int b = 0; b < 256; ++b)
                {
                    if (table[(a * 256) + b] != table[(b * 256) + a])
                    {
                        ++asymmetric;
                    }
                }
            }
            REQUIRE(asymmetric == 0);
        }

        SECTION("blending an entry with itself is the identity")
        {
            // True of the shipped file at every one of the 256 diagonal
            // entries. It has to be: the average of a colour with itself is
            // that colour, and it is present in the palette by construction.
            for (unsigned int i = 0; i < 256; ++i)
            {
                REQUIRE(table[(i * 256) + i] == i);
            }
        }

        SECTION("blends towards the average of the two entries")
        {
            // Entry 0 is (0,255,0) and entry 254 is (254,1,...); their average
            // is near the middle of the ramp, so the match must not be either
            // operand.
            auto blended = table[(0 * 256) + 254];
            REQUIRE(blended > 100);
            REQUIRE(blended < 160);
        }
    }

    TEST_CASE("alphaTableToImage")
    {
        auto palette = testPalette();
        auto table = generateAlphaTable(palette);
        auto image = alphaTableToImage(table, palette);

        SECTION("is 256x256")
        {
            REQUIRE(image.getWidth() == 256);
            REQUIRE(image.getHeight() == 256);
        }

        SECTION("carries the blended colour in rgb and the blended index in alpha")
        {
            // Both halves matter. The shader draws the rgb, and it feeds the
            // alpha back in as the operand of the third lookup -- which is the
            // whole of the original's "3 lookups".
            for (unsigned int b = 0; b < 256; b += 37)
            {
                for (unsigned int a = 0; a < 256; a += 29)
                {
                    auto expectedIndex = table[(b * 256) + a];
                    auto pixel = image.get(static_cast<int>(a), static_cast<int>(b));
                    REQUIRE(pixel.a == expectedIndex);
                    REQUIRE(pixel.r == palette[expectedIndex].r);
                    REQUIRE(pixel.g == palette[expectedIndex].g);
                    REQUIRE(pixel.b == palette[expectedIndex].b);
                }
            }
        }

        SECTION("a chained lookup lands on a real palette entry")
        {
            // What worldPost.frag does: two lookups, then a lookup of their
            // two results.
            auto top = image.get(10, 20).a;
            auto bottom = image.get(30, 40).a;
            auto final = image.get(static_cast<int>(top), static_cast<int>(bottom));
            REQUIRE(final.a == table[(static_cast<unsigned int>(bottom) * 256) + top]);
        }
    }
}
