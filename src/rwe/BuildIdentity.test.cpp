#include <catch2/catch_test_macros.hpp>
#include <rwe/BuildIdentity.h>

namespace rwe
{
    TEST_CASE("a build knows which commit it is, and refuses a peer that is another")
    {
        const std::string a = "927583c7f41d1c64a91c8d75d2a3a4f5b8a0e111";
        const std::string b = "0e25e75b4d0d3f6b2f1d0a3c5d1b7e9a6c4f2d10";

        SECTION("this build has an identity")
        {
            CHECK_FALSE(buildIdentity().empty());
            CHECK(isPlausibleBuildIdentity(buildIdentity()));
        }

        SECTION("the same commit is the same build")
        {
            CHECK(buildsCompatible(a, a));
        }

        SECTION("a different commit is not, in either direction")
        {
            CHECK_FALSE(buildsCompatible(a, b));
            CHECK_FALSE(buildsCompatible(b, a));
        }

        SECTION("a build with uncommitted changes is not the commit it came from")
        {
            CHECK_FALSE(buildsCompatible(a, a + "-d"));
            CHECK_FALSE(buildsCompatible(a + "-d", a));
            CHECK(buildsCompatible(a + "-d", a + "-d"));
        }

        SECTION("a peer that says nothing is a build from before this existed")
        {
            CHECK_FALSE(buildsCompatible(a, ""));
        }

        SECTION("a build that does not know what it is is not made a reason to refuse")
        {
            CHECK(buildsCompatible("unknown", a));
            CHECK(buildsCompatible(a, "unknown"));
            CHECK(buildsCompatible("unknown", ""));
        }

        SECTION("what a person is shown is short")
        {
            CHECK(shortBuildIdentity(a) == "927583c7");
            CHECK(shortBuildIdentity(a + "-d") == "927583c7-d");
            CHECK(shortBuildIdentity("") == "an older version");
            CHECK(shortBuildIdentity("unknown") == "unknown");
        }

        SECTION("what arrives from the wire is checked before it is believed")
        {
            CHECK(isPlausibleBuildIdentity(a));
            CHECK(isPlausibleBuildIdentity(a + "-d"));
            CHECK(isPlausibleBuildIdentity("unknown"));
            CHECK_FALSE(isPlausibleBuildIdentity(""));
            CHECK_FALSE(isPlausibleBuildIdentity("not a hash"));
            CHECK_FALSE(isPlausibleBuildIdentity(std::string(100, 'a')));
            CHECK_FALSE(isPlausibleBuildIdentity("ABCDEF"));
            CHECK_FALSE(isPlausibleBuildIdentity("-d"));
        }
    }
}
