#include <catch2/catch_test_macros.hpp>
#include <rwe/net/MapIdentity.h>

namespace rwe
{
    TEST_CASE("a map's identity changes with either file and with where a byte sits (#720)", "[net]")
    {
        auto base = mapIdentityOf("ota text", "tnt bytes");
        REQUIRE(isPlausibleMapIdentity(base));
        REQUIRE(base == mapIdentityOf("ota text", "tnt bytes"));

        REQUIRE(base != mapIdentityOf("ota texu", "tnt bytes"));
        REQUIRE(base != mapIdentityOf("ota text", "tnt bytet"));

        // The same bytes cut at another place are another pair of files.
        REQUIRE(mapIdentityOf("ab", "c") != mapIdentityOf("a", "bc"));
        REQUIRE(mapIdentityOf("", "") != mapIdentityOf("", std::string(1, '\0')));
    }

    TEST_CASE("only a hex-and-dash string of sensible length is a map identity (#720)", "[net]")
    {
        REQUIRE(isPlausibleMapIdentity("1f-2a00-0123456789abcdef"));
        REQUIRE_FALSE(isPlausibleMapIdentity(""));
        REQUIRE_FALSE(isPlausibleMapIdentity("1F-2A"));
        REQUIRE_FALSE(isPlausibleMapIdentity("12 34"));
        REQUIRE_FALSE(isPlausibleMapIdentity(std::string(49, 'a')));
        REQUIRE(isPlausibleMapIdentity(std::string(48, 'a')));
    }
}
