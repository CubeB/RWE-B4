#include <catch2/catch_test_macros.hpp>
#include <rwe/ShadeTable.h>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        /** Entry i is (i, i, i), so a mapped index and its brightness are the same number. */
        ColorPalette makeGreyRampPalette()
        {
            ColorPalette palette;
            palette.reserve(256);
            for (int i = 0; i < 256; ++i)
            {
                auto v = static_cast<unsigned char>(i);
                palette.emplace_back(v, v, v);
            }
            return palette;
        }

        /**
         * The row scale as the generator arrives at it: repeated addition, not
         * multiplication, because the two disagree in the last bits and the
         * truncation below can see the difference.
         */
        std::array<double, ShadeTableRows> shadeTableRowScales()
        {
            std::array<double, ShadeTableRows> scales{};
            auto scale = 0.0;
            for (unsigned int row = 0; row < ShadeTableRows; ++row)
            {
                scales[row] = scale;
                scale += 0.06875;
            }
            return scales;
        }

        int truncateChannel(int channel, double scale)
        {
            auto v = static_cast<int>(channel * scale);
            return ((v & 0xffff) > 255) ? 255 : (v & 0xff);
        }
    }

    TEST_CASE("ShadeTable")
    {
        SECTION("readShadeTable")
        {
            SECTION("rejects a file that is one byte short")
            {
                std::vector<char> bytes(8191, 0);
                REQUIRE(!readShadeTable(bytes).has_value());
            }

            SECTION("rejects a file that is one byte long")
            {
                std::vector<char> bytes(8193, 0);
                REQUIRE(!readShadeTable(bytes).has_value());
            }

            SECTION("reads a file of exactly 8192 bytes")
            {
                std::vector<char> bytes(8192);
                for (std::size_t i = 0; i < bytes.size(); ++i)
                {
                    bytes[i] = static_cast<char>(i & 0xff);
                }

                auto table = readShadeTable(bytes);
                REQUIRE(table.has_value());

                REQUIRE((*table)[0] == 0);
                REQUIRE((*table)[1] == 1);
                REQUIRE((*table)[127] == 127);

                // Above 127 a char is negative on this platform, so this is
                // where a missing cast would show up as a wrapped index.
                REQUIRE((*table)[128] == 128);
                REQUIRE((*table)[200] == 200);
                REQUIRE((*table)[255] == 255);
                REQUIRE((*table)[8191] == 255);
            }
        }

        SECTION("generateShadeTable")
        {
            SECTION("darkens the whole palette to black in row 0")
            {
                auto palette = makeGreyRampPalette();
                auto table = generateShadeTable(palette);

                for (unsigned int i = 0; i < ShadeTableEntriesPerRow; ++i)
                {
                    const auto& c = palette[table[i]];
                    REQUIRE(c.r == 0);
                    REQUIRE(c.g == 0);
                    REQUIRE(c.b == 0);
                }
            }

            SECTION("never darkens an entry as the row goes up")
            {
                auto palette = makeGreyRampPalette();
                auto table = generateShadeTable(palette);

                // The scale rises monotonically and truncation preserves that,
                // so this is non-strict: neighbouring rows agree on plenty of
                // entries, and every entry that is already at white agrees on
                // all the rows above it.
                for (unsigned int i = 0; i < ShadeTableEntriesPerRow; i += 17)
                {
                    for (unsigned int row = 0; row + 1 < ShadeTableRows; ++row)
                    {
                        auto here = palette[table[(row * ShadeTableEntriesPerRow) + i]].r;
                        auto next = palette[table[((row + 1) * ShadeTableEntriesPerRow) + i]].r;
                        REQUIRE(here <= next);
                    }
                }
            }

            SECTION("agrees with an exhaustive nearest match")
            {
                // A grey ramp's channel sums are 3i, spaced three apart, so the
                // generator's +-40 window always reaches thirteen entries either
                // side of the target and cannot exclude the true nearest. Any
                // disagreement here would be the window cutting the search short.
                auto palette = makeGreyRampPalette();
                auto table = generateShadeTable(palette);
                auto scales = shadeTableRowScales();

                for (unsigned int row = 0; row < ShadeTableRows; ++row)
                {
                    for (unsigned int i = 0; i < ShadeTableEntriesPerRow; ++i)
                    {
                        const auto& source = palette[i];
                        auto r = truncateChannel(source.r, scales[row]);
                        auto g = truncateChannel(source.g, scales[row]);
                        auto b = truncateChannel(source.b, scales[row]);

                        auto best = 256 * 256 * 3 + 1;
                        int bestIndex = 0;
                        for (int p = 0; p < 256; ++p)
                        {
                            const auto& c = palette[p];
                            auto dr = static_cast<int>(c.r) - r;
                            auto dg = static_cast<int>(c.g) - g;
                            auto db = static_cast<int>(c.b) - b;
                            auto d = (dr * dr) + (dg * dg) + (db * db);
                            if (d < best)
                            {
                                best = d;
                                bestIndex = p;
                            }
                        }

                        REQUIRE(table[(row * ShadeTableEntriesPerRow) + i] == bestIndex);
                    }
                }
            }

            SECTION("refuses a palette shorter than 256 entries")
            {
                ColorPalette palette;
                for (int i = 0; i < 255; ++i)
                {
                    auto v = static_cast<unsigned char>(i);
                    palette.emplace_back(v, v, v);
                }

                REQUIRE_THROWS_AS(generateShadeTable(palette), std::runtime_error);
            }
        }

        SECTION("shadeTableToImage")
        {
            auto palette = makeGreyRampPalette();
            auto table = generateShadeTable(palette);
            auto image = shadeTableToImage(table, palette);

            REQUIRE(image.getWidth() == 256);
            REQUIRE(image.getHeight() == 32);

            for (int r = 0; r < 32; r += 7)
            {
                for (int t = 0; t < 256; t += 29)
                {
                    const auto& actual = image.get(t, r);
                    const auto& expected = palette[table[(r * 256) + t]];
                    REQUIRE(actual.r == expected.r);
                    REQUIRE(actual.g == expected.g);
                    REQUIRE(actual.b == expected.b);
                }
            }
        }
    }
}
