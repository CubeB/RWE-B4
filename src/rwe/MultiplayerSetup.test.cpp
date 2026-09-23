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
}
