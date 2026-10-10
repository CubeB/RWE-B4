#include <catch2/catch_test_macros.hpp>
#include <rwe/net/LanDiscovery.h>

namespace rwe
{
    namespace
    {
        DiscoveredGame sampleGame()
        {
            DiscoveredGame game;
            game.session = "a1b2c3d4e5f6a7b8";
            game.map = "Coast To Coast";
            game.port = 1337;
            game.label = "Bailey's game";

            game.slots.push_back(DiscoveredSlot{0, "human", "Host", "ARM", 0, -1, ""});
            game.slots.push_back(DiscoveredSlot{1, "network", "Guest", "CORE", 1, -1, "192.168.2.120:1338"});
            game.slots.push_back(DiscoveredSlot{2, "computer", "AI", "ARM", 2, 1, ""});
            game.slots.push_back(DiscoveredSlot{3, "open", "", "ARM", 0, -1, ""});
            return game;
        }
    }

    TEST_CASE("LanDiscovery encodes a lobby")
    {
        SECTION("a two-player game survives a round trip")
        {
            auto game = sampleGame();
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);

            CHECK(back->session == game.session);
            CHECK(back->map == game.map);
            CHECK(back->port == game.port);
            CHECK(back->label == game.label);
            REQUIRE(back->slots.size() == game.slots.size());
            for (std::size_t i = 0; i < game.slots.size(); ++i)
            {
                CHECK(back->slots[i].index == game.slots[i].index);
                CHECK(back->slots[i].controller == game.slots[i].controller);
                CHECK(back->slots[i].name == game.slots[i].name);
                CHECK(back->slots[i].side == game.slots[i].side);
                CHECK(back->slots[i].colour == game.slots[i].colour);
                CHECK(back->slots[i].team == game.slots[i].team);
                CHECK(back->slots[i].address == game.slots[i].address);
            }
        }

        SECTION("a map name with spaces and punctuation survives")
        {
            // Map names come from the data and are chosen by a person, so
            // "Coast To Coast" and worse have to work.
            auto game = sampleGame();
            game.map = "Bases & Loading 2 (big)=v2;50%";
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->map == game.map);
        }

        SECTION("a name that looks like the wire format does not confuse it")
        {
            auto game = sampleGame();
            game.slots[0].name = "slot=0,human,ARM,0,-1,x";
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->slots.size() == 4);
            CHECK(back->slots[0].name == "slot=0,human,ARM,0,-1,x");
        }

        SECTION("a lobby with no seats is still a lobby")
        {
            DiscoveredGame game;
            game.session = "abc";
            game.map = "Hundred Isles";
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->slots.empty());
        }

        SECTION("refuses a lobby it could not put on the wire")
        {
            auto game = sampleGame();

            SECTION("too many seats")
            {
                for (int i = 0; i < 20; ++i)
                {
                    game.slots.push_back(DiscoveredSlot{9, "open", "", "ARM", 0, -1, ""});
                }
                CHECK_FALSE(LanDiscovery::encode(game));
            }

            SECTION("a seat number that is not one the screen offers")
            {
                game.slots[0].index = 12;
                CHECK_FALSE(LanDiscovery::encode(game));
            }

            SECTION("a controller it does not know")
            {
                game.slots[0].controller = "root";
                CHECK_FALSE(LanDiscovery::encode(game));
            }

            SECTION("a colour out of range")
            {
                game.slots[0].colour = 900;
                CHECK_FALSE(LanDiscovery::encode(game));
            }

            SECTION("no port")
            {
                game.port = 0;
                CHECK_FALSE(LanDiscovery::encode(game));
            }
        }
    }

    // Everything below is input somebody on the LAN wrote. Nothing here may
    // read as a lobby that is merely odd; it either is one or it is not.
    TEST_CASE("LanDiscovery refuses an announcement it cannot trust")
    {
        auto good = LanDiscovery::encode(sampleGame());
        REQUIRE(good);

        SECTION("the wrong magic")
        {
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/2\nsession=x\nmap=y\nport=1337"));
            CHECK_FALSE(LanDiscovery::decode(""));
            CHECK_FALSE(LanDiscovery::decode("hello"));
        }

        SECTION("a required field left out")
        {
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nmap=Coast\nport=1337"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nport=1337"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast"));
        }

        SECTION("numbers that are not numbers")
        {
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=13x7"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=99999999999"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=0"));
        }

        SECTION("an out-of-range value")
        {
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337\nslot=99,human,ARM,0,-1,x,"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337\nslot=0,root,ARM,0,-1,x,"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337\nslot=0,human,MEC,0,-1,x,"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337\nslot=0,human,ARM,300,-1,x,"));
        }

        SECTION("a truncated escape")
        {
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=a%2\nmap=Coast\nport=1337"));
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=a%zz\nmap=Coast\nport=1337"));
            // A NUL smuggled in through an escape is the one that matters:
            // a map name ending early would silently start the wrong map.
            CHECK_FALSE(LanDiscovery::decode("RWE-DISCOVERY/1\nsession=a%00b\nmap=Coast\nport=1337"));
        }

        SECTION("the same seat listed twice")
        {
            CHECK_FALSE(LanDiscovery::decode(
                "RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337\n"
                "slot=0,human,ARM,0,-1,A,\nslot=0,network,CORE,1,-1,B,1.2.3.4:1"));
        }

        SECTION("more seats than the screen offers")
        {
            std::string text = "RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337";
            for (int i = 0; i < 40; ++i)
            {
                text += "\nslot=0,human,ARM,0,-1,x,";
            }
            CHECK_FALSE(LanDiscovery::decode(text));
        }

        SECTION("something enormous")
        {
            CHECK_FALSE(LanDiscovery::decode(std::string(LanDiscovery::maxMessageSize + 1, 'x')));
        }

        SECTION("a field it does not know is ignored rather than refused")
        {
            // This is what lets an older build hear a newer announcement.
            auto text = *good + "\nfuture=whatever\nanother=42";
            auto back = LanDiscovery::decode(text);
            REQUIRE(back);
            CHECK(back->session == sampleGame().session);
        }
    }

    TEST_CASE("LanDiscovery hears another lobby")
    {
        SECTION("two instances on this machine find each other")
        {
            // The loopback broadcast is in broadcastAddresses() precisely so
            // this can run without a second machine.
            LanDiscovery listener;
            listener.start(LanDiscovery::defaultPort);
            REQUIRE(listener.isRunning());

            LanDiscovery talker;
            talker.start(LanDiscovery::defaultPort + 1);

            DiscoveredGame game = sampleGame();
            game.session = "round-trip-session";
            talker.advertise(game);

            bool found = false;
            for (int i = 0; i < 50 && !found; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                for (const auto& heard : listener.games())
                {
                    if (heard.session == game.session)
                    {
                        found = true;
                        CHECK(heard.map == "Coast To Coast");
                        CHECK(heard.port == 1337);
                        REQUIRE(heard.slots.size() == 4);
                        // The whole slot list is what makes a join possible
                        // without having to ask the other machine anything.
                        CHECK(heard.slots[1].address == "192.168.2.120:1338");
                        CHECK(heard.sourceAddress.find("127.0.0.1") == 0);
                    }
                }
            }
            CHECK(found);

            talker.stopAdvertising();

            bool gone = false;
            for (int i = 0; i < 80 && !gone; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                // Bound first, then search. games() returns by value, so
                // .begin() and .end() from two separate calls are iterators
                // into two different vectors -- a range that is not a range.
                const auto remaining = listener.games();
                gone = std::none_of(
                    remaining.begin(), remaining.end(), [&](const auto& h) {
                        return h.session == game.session;
                    });
            }
            // A lobby that has stopped saying so has gone away, and the list
            // has to stop offering it.
            CHECK(gone);

            talker.stop();
            listener.stop();
        }

        SECTION("a machine that never starts is not a failure")
        {
            // Discovery binds one extra port per run. If it cannot, a game
            // must still be playable by typing addresses, so this is allowed
            // to fail quietly rather than taking the front end with it.
            LanDiscovery discovery;
            discovery.start(LanDiscovery::defaultPort);
            discovery.advertise(sampleGame());
            discovery.stopAdvertising();
            discovery.stop();
            SUCCEED();
        }
    }
}