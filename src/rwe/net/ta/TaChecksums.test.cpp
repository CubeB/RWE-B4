#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <rwe/net/ta/TaChecksums.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <span>
#include <string>

namespace rwe
{
    namespace
    {
        std::span<const std::uint8_t> asBytes(const std::string& text)
        {
            return std::span<const std::uint8_t>(
                reinterpret_cast<const std::uint8_t*>(text.data()),
                text.size());
        }
    }

    TEST_CASE("TA's checksum is four one-byte accumulators combined big-endian", "[net][ta]")
    {
        REQUIRE(taChecksum(asBytes("")) == 0x00000000u);
        REQUIRE(taChecksum(asBytes("A")) == 0x41414141u);
        REQUIRE(taChecksum(asBytes("AB")) == 0x02840383u);
        REQUIRE(taChecksum(asBytes("GlobalHeader")) == 0x1ebe149au);

        // A unit type's id is the same routine over its FBI bytes, nothing more.
        REQUIRE(taUnitTypeChecksum(asBytes("AB")) == taChecksum(asBytes("AB")));
    }

    TEST_CASE("a map's checksum is the value a real host's 0x20 carried", "[net][ta]")
    {
        const char* home = std::getenv("HOME");
        if (home == nullptr)
        {
            SKIP("no HOME to find game data under");
        }
        auto dataPath = std::filesystem::path(home) / ".ta";
        if (!std::filesystem::exists(dataPath))
        {
            SKIP("no ~/.ta to read maps from");
        }
        auto vfs = constructVfs(dataPath);

        struct Case
        {
            const char* map;
            std::uint32_t expected;
        };
        const Case cases[]{
            {"Canal Crossing", 0x6ebe0529u},
            {"Great Divide", 0xe4d8389au},
        };

        for (const auto& one : cases)
        {
            CAPTURE(one.map);
            auto checksum = taMapChecksum(vfs, one.map);
            if (!checksum)
            {
                SKIP("the map data is not installed");
            }
            REQUIRE(*checksum == one.expected);
        }
    }
}
