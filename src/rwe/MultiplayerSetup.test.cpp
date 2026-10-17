#include <catch2/catch_test_macros.hpp>
#include <rwe/MultiplayerSetup.h>

// What the multiplayer screen refuses to start, and why. A direct-connect game
// agrees nothing over the wire before it begins, so the things that can be
// checked on one machine are worth checking there: the rest is found by the
// first sync hash, which is a worse way to learn you picked the wrong map.

namespace rwe
{
    namespace
    {
        MultiplayerSlot me()
        {
            return MultiplayerSlot{true, false, std::string()};
        }

        MultiplayerSlot peer(const std::string& address)
        {
            return MultiplayerSlot{false, true, address};
        }

        MultiplayerSlot other()
        {
            // An open slot, or a computer player: neither is local and neither
            // is on the other end of a wire.
            return MultiplayerSlot{false, false, std::string()};
        }
    }

    TEST_CASE("multiplayerSetupProblem")
    {
        SECTION("is happy with one seat here and one on the wire")
        {
            REQUIRE(!multiplayerSetupProblem({me(), peer("192.168.0.5:1337")}, "1337"));
        }

        SECTION("takes a host name and an ipv6 address as readily as an ipv4 one")
        {
            REQUIRE(!multiplayerSetupProblem({me(), peer("desktop.lan:1337")}, "1337"));
            REQUIRE(!multiplayerSetupProblem({me(), peer("[::1]:15338")}, "15337"));
        }

        SECTION("wants exactly one seat played from this machine")
        {
            // Without one the loader throws "No local player!" after the map
            // has been read; with two it throws "Multiple local human players
            // found". Both are true and neither is a thing to read.
            REQUIRE(multiplayerSetupProblem({other(), peer("192.168.0.5:1337")}, "1337"));
            REQUIRE(multiplayerSetupProblem({me(), me(), peer("192.168.0.5:1337")}, "1337"));
        }

        SECTION("wants somebody on the other end")
        {
            REQUIRE(multiplayerSetupProblem({me(), other()}, "1337"));
        }

        SECTION("names the slot whose address is missing or malformed")
        {
            auto missing = multiplayerSetupProblem({me(), peer("")}, "1337");
            REQUIRE(missing);
            REQUIRE(missing->find("Player 2") != std::string::npos);

            auto malformed = multiplayerSetupProblem({me(), peer("192.168.0.5")}, "1337");
            REQUIRE(malformed);
            REQUIRE(malformed->find("Player 2") != std::string::npos);
        }

        SECTION("refuses a port that is not one")
        {
            REQUIRE(multiplayerSetupProblem({me(), peer("192.168.0.5:0")}, "1337"));
            REQUIRE(multiplayerSetupProblem({me(), peer("192.168.0.5:70000")}, "1337"));
            REQUIRE(multiplayerSetupProblem({me(), peer("192.168.0.5:1337")}, "banana"));
            REQUIRE(multiplayerSetupProblem({me(), peer("192.168.0.5:1337")}, ""));
        }
    }

    TEST_CASE("isUsablePort")
    {
        REQUIRE(isUsablePort("1"));
        REQUIRE(isUsablePort("1337"));
        REQUIRE(isUsablePort("65535"));
        REQUIRE(!isUsablePort("65536"));
        REQUIRE(!isUsablePort("0"));
        REQUIRE(!isUsablePort(""));
        REQUIRE(!isUsablePort("13 37"));
        REQUIRE(!isUsablePort("-1"));
        // Five digits is the most a port can have, so anything longer is
        // refused before it is converted rather than after it has overflowed.
        REQUIRE(!isUsablePort("999999999999"));
    }

    namespace
    {
        SetupSeat seat(SetupSeat::Kind kind, std::optional<int> team)
        {
            return SetupSeat{kind, team};
        }

        const auto Human = SetupSeat::Kind::Human;
        const auto Computer = SetupSeat::Kind::Computer;
        const auto Open = SetupSeat::Kind::Open;
    }

    TEST_CASE("allOnOneTeamProblem refuses a start with every seat on one team (BT-194)")
    {
        SECTION("two seats on the same team")
        {
            REQUIRE(allOnOneTeamProblem({seat(Human, 1), seat(Computer, 1)}));
        }

        SECTION("open seats do not count towards the group")
        {
            REQUIRE(allOnOneTeamProblem({seat(Human, 2), seat(Open, 4), seat(Computer, 2), seat(Open, std::nullopt)}));
        }

        SECTION("two different teams are fine")
        {
            REQUIRE_FALSE(allOnOneTeamProblem({seat(Human, 1), seat(Computer, 2)}));
        }

        SECTION("a seat on no team is its own group")
        {
            REQUIRE_FALSE(allOnOneTeamProblem({seat(Human, std::nullopt), seat(Computer, std::nullopt)}));
            REQUIRE_FALSE(allOnOneTeamProblem({seat(Human, 1), seat(Computer, 1), seat(Computer, std::nullopt)}));
        }

        SECTION("a lone seat is not a team game")
        {
            REQUIRE_FALSE(allOnOneTeamProblem({seat(Human, 1)}));
        }
    }

    TEST_CASE("skirmishSetupProblem wants a human and a computer, and not one team (BT-194)")
    {
        SECTION("a human and a computer on different teams start")
        {
            REQUIRE_FALSE(skirmishSetupProblem({seat(Human, 1), seat(Computer, 2)}));
        }

        SECTION("only humans is refused")
        {
            REQUIRE(skirmishSetupProblem({seat(Human, 1), seat(Human, 2)}));
        }

        SECTION("only computers is refused")
        {
            REQUIRE(skirmishSetupProblem({seat(Computer, 1), seat(Computer, 2)}));
        }

        SECTION("a human alone is refused")
        {
            REQUIRE(skirmishSetupProblem({seat(Human, std::nullopt), seat(Open, std::nullopt)}));
        }

        SECTION("a human and a computer on one team is refused for that")
        {
            auto problem = skirmishSetupProblem({seat(Human, 3), seat(Computer, 3)});
            REQUIRE(problem);
            REQUIRE(problem->find("same team") != std::string::npos);
        }
    }

    TEST_CASE("Previous Menu at the root main menu does nothing (BT-201)")
    {
        // A cancel closes a dialog first, then steps back a panel, and with
        // only the main menu left there is nowhere to go and it must not quit.
        REQUIRE(previousMenuAction(true, 1) == PreviousMenuAction::CloseDialog);
        REQUIRE(previousMenuAction(true, 3) == PreviousMenuAction::CloseDialog);
        REQUIRE(previousMenuAction(false, 3) == PreviousMenuAction::PopPanel);
        REQUIRE(previousMenuAction(false, 2) == PreviousMenuAction::PopPanel);
        REQUIRE(previousMenuAction(false, 1) == PreviousMenuAction::Nothing);
        REQUIRE(previousMenuAction(false, 0) == PreviousMenuAction::Nothing);
    }
}
