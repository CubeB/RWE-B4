#include <catch2/catch_test_macros.hpp>
#include <rwe/io/pcx/pcx.h>

namespace rwe
{
    TEST_CASE("encodePcx24 writes a 24-bit PCX the decoder reads back", "[pcx]")
    {
        // Three wide, so the padding byte at the end of each plane is
        // exercised; a repeated byte, and bytes at and above 0xC0, which
        // must be escaped even when they do not repeat.
        const unsigned int width = 3;
        const unsigned int height = 2;
        const std::vector<uint8_t> rgb = {
            200, 0, 7, 200, 0, 7, 0xC0, 1, 2,
            10, 20, 30, 40, 50, 60, 0xFF, 0xC1, 0x3F};

        auto bytes = encodePcx24(width, height, rgb);
        STATIC_REQUIRE(sizeof(PcxHeader) == 128);
        REQUIRE(bytes.size() > sizeof(PcxHeader));

        const auto* header = reinterpret_cast<const PcxHeader*>(bytes.data());
        auto manufacturer = static_cast<unsigned int>(header->manufacturer);
        auto version = static_cast<unsigned int>(header->version);
        auto encoding = static_cast<unsigned int>(header->encoding);
        auto bitsPerPixel = static_cast<unsigned int>(header->bitsPerPixel);
        auto planes = static_cast<unsigned int>(header->numberOfPlanes);
        auto bytesPerLine = static_cast<unsigned int>(header->bytesPerLine);
        CHECK(manufacturer == 10u);
        CHECK(version == 5u);
        CHECK(encoding == 1u);
        CHECK(bitsPerPixel == 8u);
        CHECK(planes == 3u);
        CHECK(bytesPerLine == 4u);

        PcxDecoder decoder(bytes.cbegin(), bytes.cend());
        REQUIRE(decoder.getWidth() == width);
        REQUIRE(decoder.getHeight() == height);
        auto decoded = decoder.decodeImage();
        REQUIRE(decoded.size() == 4u * 3u * height);

        for (unsigned int y = 0; y < height; ++y)
        {
            for (unsigned int c = 0; c < 3; ++c)
            {
                for (unsigned int x = 0; x < width; ++x)
                {
                    auto got = static_cast<uint8_t>(decoded[(y * 12) + (c * 4) + x]);
                    CHECK(got == rgb[(y * width * 3) + (x * 3) + c]);
                }
                auto padding = static_cast<uint8_t>(decoded[(y * 12) + (c * 4) + 3]);
                CHECK(padding == 0);
            }
        }
    }
}
