#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rwe/network_util.h>

namespace rwe
{
    TEST_CASE("ema")
    {
        SECTION("combines value with average according to weight")
        {
            REQUIRE(ema(1.0f, 8.0f, 0.5f) == 4.5f);
            REQUIRE(ema(1.0f, 8.0f, 0.25f) == 6.25f);
        }
    }

    TEST_CASE("readInt")
    {
        rc::prop("readInt inverts writeInt", [](unsigned int i) {
            std::array<char, 4> arr;
            writeInt(arr.data(), i);
            auto result = readInt(arr.data());
            RC_ASSERT(result == i);
        });
    }

    TEST_CASE("computeCrc")
    {
        SECTION("empty input")
        {
            REQUIRE(computeCrc("", 0) == 0x00000000u);
        }

        SECTION("known CRC32 values")
        {
            // CRC32 of "123456789" is 0xCBF43926
            const char* input = "123456789";
            REQUIRE(computeCrc(input, 9) == 0xCBF43926u);
        }

        SECTION("single byte")
        {
            // CRC32 of "a" is 0xE8B7BE43
            REQUIRE(computeCrc("a", 1) == 0xE8B7BE43u);
        }
    }

    TEST_CASE("chooseChatCountForPacket")
    {
        // A packet of a fixed 100 bytes plus 200 for every chat line in it.
        auto sizeOf = [](std::size_t count) -> unsigned long long { return 100 + (200 * count); };

        SECTION("takes the lot when the lot fits")
        {
            REQUIRE(chooseChatCountForPacket(4, 1500, sizeOf) == 4);
        }

        SECTION("takes exactly what fits")
        {
            // 100 + 3*200 is 700; a fourth line would be 900.
            REQUIRE(chooseChatCountForPacket(8, 899, sizeOf) == 3);
        }

        SECTION("takes none when not even one fits")
        {
            REQUIRE(chooseChatCountForPacket(8, 299, sizeOf) == 0);
        }

        SECTION("takes none when the packet is over the limit with no chat at all")
        {
            // The commands alone have filled it. Nothing here can help, and
            // the caller is the one that decides what that means.
            REQUIRE(chooseChatCountForPacket(8, 50, sizeOf) == 0);
        }

        SECTION("takes none from an empty buffer")
        {
            REQUIRE(chooseChatCountForPacket(0, 1500, sizeOf) == 0);
        }
    }
}
