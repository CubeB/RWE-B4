#include <catch2/catch_test_macros.hpp>
#include <rwe/GameLaunch.h>

namespace rwe
{
    TEST_CASE("a player spec can put two players on a team", "[launch]")
    {
        // Without this there is no way to launch a team game from the command
        // line, so a 2v2 asked for on the harness was four players in a
        // free-for-all, and the AI's alliance handling had nothing to run
        // against. The field is last and optional, so every spec written
        // before it existed still means what it meant.
        SECTION("four components, as it always was: nobody on a team")
        {
            auto info = parsePlayerInfoFromArg("P0;Computer;ARM;0");
            REQUIRE(info.has_value());
            CHECK(info->side == "ARM");
            CHECK_FALSE(info->teamId.has_value());
        }

        SECTION("a fifth puts them on one")
        {
            auto first = parsePlayerInfoFromArg("P0;Computer;ARM;0;1");
            auto second = parsePlayerInfoFromArg("P1;Computer;CORE;1;1");
            auto other = parsePlayerInfoFromArg("P2;Computer;ARM;2;2");
            REQUIRE(first.has_value());
            REQUIRE(second.has_value());
            REQUIRE(other.has_value());
            REQUIRE(first->teamId.has_value());
            REQUIRE(second->teamId.has_value());
            REQUIRE(other->teamId.has_value());

            // Allies are the ones sharing a number, and the sides they play
            // have nothing to do with it -- ARM and CORE can be on one team.
            CHECK(*first->teamId == *second->teamId);
            CHECK(*first->teamId != *other->teamId);
        }

        SECTION("an empty fifth is no team, not team zero")
        {
            auto info = parsePlayerInfoFromArg("P0;Computer;ARM;0;");
            REQUIRE(info.has_value());
            CHECK_FALSE(info->teamId.has_value());
        }

        SECTION("a fifth that is not a number is refused rather than ignored")
        {
            // Quietly dropping it would put a player who asked for a team
            // into a free-for-all, and the game would look like it worked.
            CHECK_THROWS(parsePlayerInfoFromArg("P0;Computer;ARM;0;left"));
        }

        SECTION("still \"empty\" for a slot nobody is in")
        {
            CHECK_FALSE(parsePlayerInfoFromArg("empty").has_value());
        }
    }
}
