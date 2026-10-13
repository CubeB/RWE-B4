#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
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

        SECTION("the game address behind a router survives, and is nothing when absent")
        {
            auto game = sampleGame();
            auto plain = LanDiscovery::decode(*LanDiscovery::encode(game));
            REQUIRE(plain);
            CHECK(plain->gameAddress.empty());

            game.gameAddress = "100.109.106.220:33780";
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->gameAddress == "100.109.106.220:33780");

            // It is a field of its own: an older reader that has never heard
            // of the key ignores it and still reads the lobby.
            CHECK(text->find("gaddr=") != std::string::npos);
        }

        SECTION("a game address that is too long is refused rather than truncated")
        {
            auto game = sampleGame();
            game.gameAddress = std::string(65, '1');
            CHECK_FALSE(LanDiscovery::encode(game));

            auto text = *LanDiscovery::encode(sampleGame());
            text += "\ngaddr=" + std::string(65, '1');
            CHECK_FALSE(LanDiscovery::decode(text));
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

    namespace
    {
        const std::string minimalAnnouncement = "RWE-DISCOVERY/1\nsession=abc\nmap=Coast\nport=1337";

        /** A lobby that uses every field the extended format has. */
        DiscoveredGame extendedGame()
        {
            auto game = sampleGame();
            game.hostName = "Bailey";
            game.passwordHash = LanDiscovery::hashPassword(game.session, "hunter2");
            game.launching = true;
            game.options = {
                {"los", "1"},
                {"mapping", "0"},
                {"startloc", "random"},
                {"commanderdeath", "1"},
                {"pathfinding", "original"},
                {"aidifficulty", "hard"},
            };
            game.slots[0].metal = 5000;
            game.slots[0].energy = 7500;
            game.slots[2].metal = 0;
            game.slots[2].energy = 1000000;
            game.slots[2].personality = "rusher";
            return game;
        }

        /** A bare UDP socket on loopback, for sending a peer things by hand. */
        class RawPeer
        {
        public:
            RawPeer()
            {
                socket.open(asio::ip::udp::v4());
                socket.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
                socket.non_blocking(true);
            }

            unsigned short localPort() const
            {
                return socket.local_endpoint().port();
            }

            void send(const std::string& data, unsigned short port)
            {
                asio::error_code ec;
                socket.send_to(
                    asio::buffer(data),
                    asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), port),
                    0,
                    ec);
            }

            /** Every datagram that arrives within the window. */
            std::vector<std::string> receiveFor(std::chrono::milliseconds window)
            {
                std::vector<std::string> received;
                std::array<char, 4096> buffer{};
                auto deadline = std::chrono::steady_clock::now() + window;
                while (std::chrono::steady_clock::now() < deadline)
                {
                    asio::ip::udp::endpoint from;
                    asio::error_code ec;
                    auto n = socket.receive_from(asio::buffer(buffer), from, 0, ec);
                    if (ec)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }
                    received.emplace_back(buffer.data(), n);
                }
                return received;
            }

        private:
            asio::io_context io;
            asio::ip::udp::socket socket{io};
        };

        bool waitUntilHeard(const LanDiscovery& discovery, const std::string& session, std::chrono::milliseconds limit)
        {
            auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline)
            {
                for (const auto& g : discovery.games())
                {
                    if (g.session == session) return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return false;
        }
    }

    TEST_CASE("LanDiscovery carries what the lobby screen shows")
    {
        SECTION("every extended field survives a round trip")
        {
            auto game = extendedGame();
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);

            CHECK(back->hostName == "Bailey");
            CHECK(back->passwordHash == game.passwordHash);
            CHECK(back->passwordHash.size() == 16);
            CHECK(back->launching);
            CHECK(back->options == game.options);

            REQUIRE(back->slots.size() == game.slots.size());
            CHECK(back->slots[0].metal == 5000);
            CHECK(back->slots[0].energy == 7500);
            CHECK(back->slots[0].personality.empty());
            CHECK(back->slots[1].metal == 1000);
            CHECK(back->slots[1].energy == 1000);
            CHECK(back->slots[2].metal == 0);
            CHECK(back->slots[2].energy == 1000000);
            CHECK(back->slots[2].personality == "rusher");
            // The seat fields that were already there are undisturbed.
            CHECK(back->slots[1].address == "192.168.2.120:1338");
            CHECK(back->slots[2].team == 1);
        }

        SECTION("an announcement that says none of it decodes to the defaults")
        {
            // This is every announcement an older build makes.
            auto back = LanDiscovery::decode(
                minimalAnnouncement + "\nslot=0,human,ARM,0,-1,A,\nslot=1,computer,CORE,1,1,B,");
            REQUIRE(back);
            CHECK(back->hostName.empty());
            CHECK(back->passwordHash.empty());
            CHECK_FALSE(back->launching);
            CHECK(back->options.empty());
            REQUIRE(back->slots.size() == 2);
            CHECK(back->slots[0].metal == 1000);
            CHECK(back->slots[0].energy == 1000);
            CHECK(back->slots[1].personality.empty());
        }

        SECTION("a plain lobby is not made bigger by fields it does not use")
        {
            auto text = LanDiscovery::encode(sampleGame());
            REQUIRE(text);
            CHECK(text->find("slotx") == std::string::npos);
            CHECK(text->find("opt.") == std::string::npos);
            CHECK(text->find("pw=") == std::string::npos);
            CHECK(text->find("launch") == std::string::npos);
            CHECK(text->find("host=") == std::string::npos);
        }

        SECTION("an older decoder can still read it")
        {
            // The older decoder wants exactly seven comma separated fields
            // on a `slot=` line and refuses the announcement otherwise, so
            // the extras have to be somewhere else. Check that on the text
            // itself rather than trusting the new decoder to be as lenient
            // as the old one: every line it would look at is either a key
            // it knows in the shape it knows, or a key it will skip.
            auto text = LanDiscovery::encode(extendedGame());
            REQUIRE(text);

            std::string stripped;
            int slotLines = 0;
            std::size_t start = 0;
            while (start <= text->size())
            {
                auto end = text->find('\n', start);
                auto line = text->substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (line.rfind("slot=", 0) == 0)
                {
                    ++slotLines;
                    CHECK(std::count(line.begin(), line.end(), ',') == 6);
                }
                // What is left when the keys it would skip are taken away
                // is a valid lobby of the older format.
                bool isNew = line.rfind("slotx=", 0) == 0 || line.rfind("opt.", 0) == 0
                    || line.rfind("host=", 0) == 0 || line.rfind("pw=", 0) == 0
                    || line.rfind("launch=", 0) == 0;
                if (!isNew)
                {
                    if (!stripped.empty()) stripped += "\n";
                    stripped += line;
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }
            CHECK(slotLines == 4);

            auto old = LanDiscovery::decode(stripped);
            REQUIRE(old);
            CHECK(old->slots.size() == 4);
            CHECK(old->map == "Coast To Coast");
        }

        SECTION("keys from a later version are ignored beside the new ones")
        {
            auto text = *LanDiscovery::encode(extendedGame()) + "\nfuture=whatever\nopt=notanoption\nslotz=1,2,3";
            auto back = LanDiscovery::decode(text);
            REQUIRE(back);
            CHECK(back->options.size() == 6);
            CHECK(back->hostName == "Bailey");
        }

        SECTION("a value that looks like the wire format does not confuse the new fields")
        {
            auto game = extendedGame();
            game.hostName = "a=b,c\nopt.x=1";
            game.slots[2].personality = "slotx=1,2,3,4";
            game.options = {{"los", "1\nopt.hack=1"}, {"b", "x=y,z;%"}};
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->hostName == game.hostName);
            CHECK(back->slots[2].personality == game.slots[2].personality);
            CHECK(back->options == game.options);
        }
    }

    TEST_CASE("LanDiscovery refuses a new field it cannot trust")
    {
        const auto& base = minimalAnnouncement;

        SECTION("a password hash of the wrong length or alphabet")
        {
            CHECK_FALSE(LanDiscovery::decode(base + "\npw=abc"));
            CHECK_FALSE(LanDiscovery::decode(base + "\npw=0123456789abcde"));
            CHECK_FALSE(LanDiscovery::decode(base + "\npw=0123456789abcdef0"));
            CHECK_FALSE(LanDiscovery::decode(base + "\npw=0123456789abcdeg"));
            CHECK_FALSE(LanDiscovery::decode(base + "\npw=0123456789ABCDEF"));
            CHECK_FALSE(LanDiscovery::decode(base + "\npw=0123456789abcde%66"));
            CHECK(LanDiscovery::decode(base + "\npw=0123456789abcdef"));
            // Empty means no password, which is a lobby anybody can join.
            auto open = LanDiscovery::decode(base + "\npw=");
            REQUIRE(open);
            CHECK(open->passwordHash.empty());
        }

        SECTION("encoding refuses a hash that would be refused")
        {
            auto game = sampleGame();
            game.passwordHash = "short";
            CHECK_FALSE(LanDiscovery::encode(game));
            game.passwordHash = "0123456789ABCDEF";
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("too many options")
        {
            std::string text = base;
            for (int i = 0; i < 32; ++i)
            {
                text += "\nopt.o" + std::to_string(i) + "=1";
            }
            auto thirtyTwo = LanDiscovery::decode(text);
            REQUIRE(thirtyTwo);
            CHECK(thirtyTwo->options.size() == 32);

            text += "\nopt.one_more=1";
            CHECK_FALSE(LanDiscovery::decode(text));

            auto game = sampleGame();
            for (int i = 0; i < 33; ++i)
            {
                game.options.emplace_back("o" + std::to_string(i), "1");
            }
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("an option name that is not a name")
        {
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.has space=1"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.=1"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.a-b=1"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.a.b=1"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.a%20b=1"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt." + std::string(33, 'a') + "=1"));
            CHECK(LanDiscovery::decode(base + "\nopt." + std::string(32, 'a') + "=1"));

            auto game = sampleGame();
            game.options = {{"has space", "1"}};
            CHECK_FALSE(LanDiscovery::encode(game));
            game.options = {{"", "1"}};
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("an option value that is not safe")
        {
            // A NUL ending a value early is the same fault it is in a map
            // name: the rest of the string silently goes missing.
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.los=a%00b"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.los=a%2"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nopt.los=" + std::string(65, 'x')));
            CHECK(LanDiscovery::decode(base + "\nopt.los=" + std::string(64, 'x')));

            auto game = sampleGame();
            game.options = {{"los", std::string(65, 'x')}};
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("a name given twice keeps the first")
        {
            auto back = LanDiscovery::decode(base + "\nopt.los=1\nopt.mapping=0\nopt.los=0");
            REQUIRE(back);
            REQUIRE(back->options.size() == 2);
            CHECK(back->options[0] == std::make_pair(std::string("los"), std::string("1")));
            CHECK(back->options[1] == std::make_pair(std::string("mapping"), std::string("0")));

            // A repeat adds nothing and so does not count against the limit.
            std::string text = base;
            for (int i = 0; i < 32; ++i)
            {
                text += "\nopt.same=" + std::to_string(i % 10);
            }
            text += "\nopt.other=1";
            auto many = LanDiscovery::decode(text);
            REQUIRE(many);
            CHECK(many->options.size() == 2);
            CHECK(many->options[0].second == "0");

            auto game = sampleGame();
            game.options = {{"los", "1"}, {"los", "0"}};
            auto encoded = LanDiscovery::encode(game);
            REQUIRE(encoded);
            auto round = LanDiscovery::decode(*encoded);
            REQUIRE(round);
            REQUIRE(round->options.size() == 1);
            CHECK(round->options[0].second == "1");
        }

        SECTION("starting resources out of range are clamped, and nonsense is refused")
        {
            auto negative = LanDiscovery::decode(base + "\nslot=0,human,ARM,0,-1,A,\nslotx=0,-5,99999999999,");
            REQUIRE(negative);
            CHECK(negative->slots[0].metal == 0);
            CHECK(negative->slots[0].energy == 1000000);

            auto huge = LanDiscovery::decode(
                base + "\nslot=0,human,ARM,0,-1,A,\nslotx=0,1000001,99999999999999999999,");
            REQUIRE(huge);
            CHECK(huge->slots[0].metal == 1000000);
            CHECK(huge->slots[0].energy == 1000000);

            CHECK_FALSE(LanDiscovery::decode(base + "\nslot=0,human,ARM,0,-1,A,\nslotx=0,abc,10,"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nslot=0,human,ARM,0,-1,A,\nslotx=0,10,,"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nslot=0,human,ARM,0,-1,A,\nslotx=0,10"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nslot=0,human,ARM,0,-1,A,\nslotx=0,10,10,x,y"));

            // And encoding clamps rather than writing out what would be refused.
            auto game = sampleGame();
            game.slots[0].metal = -5;
            game.slots[0].energy = 2000000000;
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->slots[0].metal == 0);
            CHECK(back->slots[0].energy == 1000000);
        }

        SECTION("a seat's extras that are wrong in other ways")
        {
            const std::string seat = base + "\nslot=0,human,ARM,0,-1,A,";
            // Out of the screen's seat range, listed twice, or a NUL.
            CHECK_FALSE(LanDiscovery::decode(seat + "\nslotx=10,1,1,"));
            CHECK_FALSE(LanDiscovery::decode(seat + "\nslotx=-1,1,1,"));
            CHECK_FALSE(LanDiscovery::decode(seat + "\nslotx=0,1,1,\nslotx=0,2,2,"));
            CHECK_FALSE(LanDiscovery::decode(seat + "\nslotx=0,1,1,a%00b"));
            CHECK_FALSE(LanDiscovery::decode(seat + "\nslotx=0,1,1," + std::string(33, 'p')));
            CHECK(LanDiscovery::decode(seat + "\nslotx=0,1,1," + std::string(32, 'p')));

            // Extras for a seat that is not listed describe nothing.
            auto stray = LanDiscovery::decode(seat + "\nslotx=5,7,7,x");
            REQUIRE(stray);
            CHECK(stray->slots.size() == 1);
            CHECK(stray->slots[0].metal == 1000);

            // Extras may come before the seat they belong to.
            auto early = LanDiscovery::decode(base + "\nslotx=0,7,8,p\nslot=0,human,ARM,0,-1,A,");
            REQUIRE(early);
            CHECK(early->slots[0].metal == 7);
            CHECK(early->slots[0].personality == "p");

            auto game = sampleGame();
            game.slots[0].personality = std::string(33, 'p');
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("an enormous host name")
        {
            CHECK_FALSE(LanDiscovery::decode(base + "\nhost=" + std::string(21, 'h')));
            CHECK_FALSE(LanDiscovery::decode(base + "\nhost=" + std::string(1000, 'h')));
            CHECK_FALSE(LanDiscovery::decode(base + "\nhost=a%00b"));
            auto edge = LanDiscovery::decode(base + "\nhost=" + std::string(20, 'h'));
            REQUIRE(edge);
            CHECK(edge->hostName == std::string(20, 'h'));

            auto game = sampleGame();
            game.hostName = std::string(21, 'h');
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("a launch flag that is not 1 or absent")
        {
            CHECK_FALSE(LanDiscovery::decode(base + "\nlaunch=yes"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nlaunch=2"));
            CHECK_FALSE(LanDiscovery::decode(base + "\nlaunch="));
            auto on = LanDiscovery::decode(base + "\nlaunch=1");
            REQUIRE(on);
            CHECK(on->launching);
        }
    }

    TEST_CASE("LanDiscovery keeps an announcement inside one datagram")
    {
        SECTION("ten seats with extras and every option fit")
        {
            DiscoveredGame game;
            game.session = "a1b2c3d4e5f6a7b8";
            game.map = "Coast To Coast";
            game.label = "Bailey's game";
            game.hostName = "Bailey";
            game.passwordHash = LanDiscovery::hashPassword(game.session, "pw");
            game.launching = true;
            for (int i = 0; i < 10; ++i)
            {
                DiscoveredSlot slot;
                slot.index = i;
                slot.controller = "network";
                slot.name = "Player" + std::to_string(i);
                slot.side = "CORE";
                slot.colour = static_cast<unsigned int>(i);
                slot.team = i % 4;
                slot.address = "192.168.100.10:1338";
                slot.metal = 10000 + i;
                slot.energy = 20000 + i;
                slot.personality = "rusher";
                game.slots.push_back(slot);
            }
            for (int i = 0; i < 32; ++i)
            {
                game.options.emplace_back("opt" + std::to_string(i), "v" + std::to_string(i));
            }

            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->size() <= LanDiscovery::maxMessageSize);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->options == game.options);
            REQUIRE(back->slots.size() == 10);
            CHECK(back->slots[9].energy == 20009);
            CHECK(back->slots[9].personality == "rusher");
        }

        SECTION("when it will not all fit the trailing options go first")
        {
            auto game = sampleGame();
            game.hostName = "Bailey";
            for (int i = 0; i < 32; ++i)
            {
                // Sixty-odd bytes each, so 32 of them cannot fit.
                game.options.emplace_back("long_option_name_" + std::to_string(i), std::string(40, 'v'));
            }

            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->size() <= LanDiscovery::maxMessageSize);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            // Everything that is needed to join is still there...
            CHECK(back->slots.size() == game.slots.size());
            CHECK(back->hostName == "Bailey");
            // ...and the options are a prefix of what the host listed.
            CHECK(back->options.size() > 0);
            CHECK(back->options.size() < game.options.size());
            for (std::size_t i = 0; i < back->options.size(); ++i)
            {
                CHECK(back->options[i] == game.options[i]);
            }
        }

        SECTION("a lobby that does not fit even with no options is refused")
        {
            DiscoveredGame game;
            game.session = "abc";
            game.map = "Coast";
            for (int i = 0; i < 10; ++i)
            {
                DiscoveredSlot slot;
                slot.index = i;
                // Spaces are three bytes apiece on the wire.
                slot.name = std::string(120, ' ');
                slot.address = std::string(20, ' ');
                game.slots.push_back(slot);
            }
            game.options = {{"los", "1"}};
            CHECK_FALSE(LanDiscovery::encode(game));
        }

        SECTION("no session is still no lobby")
        {
            auto game = extendedGame();
            game.session.clear();
            CHECK_FALSE(LanDiscovery::encode(game));
        }
    }

    TEST_CASE("LanDiscovery::hashPassword")
    {
        SECTION("is the same value every time, on every build")
        {
            // FNV-1a 64 over "a1b2c3d4e5f6a7b8\nhunter2", worked out
            // independently of this code. A change to this value is a change
            // to what every peer on a LAN computes, and an older build would
            // then call every password wrong.
            CHECK(LanDiscovery::hashPassword("a1b2c3d4e5f6a7b8", "hunter2") == "8351a0039023534b");
            // FNV-1a 64 of the single byte "\n", which is what an empty
            // session and an empty password hash.
            CHECK(LanDiscovery::hashPassword("", "") == "af63c74c8601c8dd");
        }

        SECTION("is sixteen lower-case hex characters")
        {
            auto h = LanDiscovery::hashPassword("s", "p");
            REQUIRE(h.size() == 16);
            for (char c : h)
            {
                CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
            }
        }

        SECTION("differs by session and by password")
        {
            auto a = LanDiscovery::hashPassword("session-one", "secret");
            CHECK(a == LanDiscovery::hashPassword("session-one", "secret"));
            CHECK(a != LanDiscovery::hashPassword("session-two", "secret"));
            CHECK(a != LanDiscovery::hashPassword("session-one", "Secret"));
            CHECK(a != LanDiscovery::hashPassword("session-one", ""));
            // The separator is what keeps "ab"+"c" and "a"+"bc" apart.
            CHECK(LanDiscovery::hashPassword("ab", "c") != LanDiscovery::hashPassword("a", "bc"));
        }

        SECTION("what it produces is accepted by the codec")
        {
            auto game = sampleGame();
            game.passwordHash = LanDiscovery::hashPassword(game.session, "any password at all");
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->passwordHash == game.passwordHash);
        }
    }

    TEST_CASE("LanDiscovery answers a probe")
    {
        // Ports well away from the default, so that nothing else on the
        // machine is the second party. The periodic announcement goes to the
        // default port, which neither instance here is bound to, so a lobby
        // that turns up in B's list can only have come from the reply.
        constexpr unsigned short portA = 1350;
        constexpr unsigned short portB = 1351;

        auto game = extendedGame();
        game.session = "probe-session";

        SECTION("a probe by address finds an advertising machine at once")
        {
            LanDiscovery a;
            a.start(portA);
            REQUIRE(a.isRunning());
            a.advertise(game);

            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());
            REQUIRE(b.games().empty());

            auto began = std::chrono::steady_clock::now();
            REQUIRE(b.probe("127.0.0.1:" + std::to_string(portA)));

            REQUIRE(waitUntilHeard(b, game.session, std::chrono::milliseconds(2000)));
            auto took = std::chrono::steady_clock::now() - began;
            // Well inside one announce interval: it is the reply, and not
            // the next announcement, that put it there.
            CHECK(took < LanDiscovery::announceInterval);

            auto heard = b.games();
            REQUIRE(heard.size() == 1);
            CHECK(heard[0].session == game.session);
            CHECK(heard[0].map == "Coast To Coast");
            CHECK(heard[0].hostName == "Bailey");
            CHECK(heard[0].launching);
            CHECK(heard[0].options == game.options);
            CHECK(heard[0].slots.size() == game.slots.size());
            CHECK(heard[0].sourceAddress == "127.0.0.1:" + std::to_string(portA));

            // B is not advertising, so a probe the other way is not answered.
            REQUIRE(a.probe("127.0.0.1:" + std::to_string(portB)));
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            CHECK(a.games().empty());

            b.stop();
            a.stop();
        }

        SECTION("a machine that is not advertising does not answer")
        {
            LanDiscovery a;
            a.start(portA);
            LanDiscovery b;
            b.start(portB);

            REQUIRE(b.probe("127.0.0.1:" + std::to_string(portA)));
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            CHECK(b.games().empty());

            // And it starts answering the moment it has something to say,
            // without being restarted.
            a.advertise(game);
            REQUIRE(b.probe("127.0.0.1:" + std::to_string(portA)));
            CHECK(waitUntilHeard(b, game.session, std::chrono::milliseconds(2000)));

            // And stops again.
            a.stopAdvertising();
            b.stop();
            b.start(portB);
            REQUIRE(b.probe("127.0.0.1:" + std::to_string(portA)));
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            CHECK(b.games().empty());

            b.stop();
            a.stop();
        }

        SECTION("a machine does not list itself when asked not to")
        {
            LanDiscovery a;
            a.start(portA);
            a.advertise(game);

            // Probing itself is the one way for it to hear itself on a port
            // that is not the default, which is what makes this checkable.
            REQUIRE(a.probe("127.0.0.1:" + std::to_string(portA)));
            REQUIRE(waitUntilHeard(a, game.session, std::chrono::milliseconds(2000)));

            CHECK(a.games().size() == 1);
            CHECK(a.games(game.session).empty());
            CHECK(a.games("some-other-session").size() == 1);
            CHECK(a.games("").size() == 1);

            a.stop();
        }

        SECTION("an address with no port goes to the default port")
        {
            LanDiscovery a;
            a.start(LanDiscovery::defaultPort);
            if (!a.isRunning())
            {
                SUCCEED("the default port is taken on this machine");
                return;
            }
            a.advertise(game);

            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.probe("127.0.0.1"));
            CHECK(waitUntilHeard(b, game.session, std::chrono::milliseconds(2000)));

            b.stop();
            a.stop();
        }

        SECTION("IPv6 and v4-mapped forms are accepted")
        {
            LanDiscovery a;
            a.start(portA);
            a.advertise(game);

            LanDiscovery b;
            b.start(portB);

            auto port = std::to_string(portA);

            // A v4-mapped address is an IPv4 machine and goes out the IPv4 way.
            REQUIRE(b.probe("::ffff:127.0.0.1:" + port));
            CHECK(waitUntilHeard(b, game.session, std::chrono::milliseconds(2000)));

            // A genuine IPv6 loopback works where the machine has IPv6, and
            // is a clean refusal where it does not.
            b.stop();
            b.start(portB);
            if (b.probe("[::1]:" + port))
            {
                CHECK(waitUntilHeard(b, game.session, std::chrono::milliseconds(2000)));
                auto heard = b.games();
                if (!heard.empty())
                {
                    CHECK(heard[0].sourceAddress == "[::1]:" + port);
                }
            }

            b.stop();
            a.stop();
        }

        SECTION("a probe that is not an address is refused without sending anything")
        {
            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());

            CHECK_FALSE(b.probe(""));
            CHECK_FALSE(b.probe("   "));
            CHECK_FALSE(b.probe("not an address"));
            CHECK_FALSE(b.probe("-bad.com"));
            CHECK_FALSE(b.probe("a..b"));
            CHECK_FALSE(b.probe("127.0.0.1:0"));
            CHECK_FALSE(b.probe("127.0.0.1:99999"));
            CHECK_FALSE(b.probe("127.0.0.1:port"));
            CHECK_FALSE(b.probe("[::1"));
            CHECK_FALSE(b.probe("999.1.1.1"));
            CHECK_FALSE(b.probe(std::string(5000, '1')));
            b.stop();
        }

        SECTION("probing before start, or after stop, is harmless")
        {
            LanDiscovery b;
            CHECK_FALSE(b.probe("127.0.0.1:" + std::to_string(portA)));
            b.start(portB);
            b.stop();
            CHECK_FALSE(b.probe("127.0.0.1:" + std::to_string(portA)));
        }

        SECTION("replies are rate limited per sender")
        {
            LanDiscovery a;
            a.start(portA);
            a.advertise(game);

            RawPeer peer;
            peer.send(LanDiscovery::probeMessage(), portA);
            peer.send(LanDiscovery::probeMessage(), portA);
            peer.send(LanDiscovery::probeMessage(), portA);
            auto first = peer.receiveFor(std::chrono::milliseconds(150));
            REQUIRE(first.size() == 1);
            CHECK(LanDiscovery::decode(first[0]));

            // Once the interval has passed it answers again.
            std::this_thread::sleep_for(LanDiscovery::probeReplyInterval + std::chrono::milliseconds(100));
            peer.send(LanDiscovery::probeMessage(), portA);
            auto second = peer.receiveFor(std::chrono::milliseconds(200));
            CHECK(second.size() == 1);

            a.stop();
        }

        SECTION("only the probe datagram itself is a probe")
        {
            LanDiscovery a;
            a.start(portA);
            a.advertise(game);

            RawPeer peer;
            peer.send("RWE-DISCOVERY-PROBE/2\n", portA);
            peer.send("RWE-DISCOVERY-PROBE/1\nand then some", portA);
            peer.send("XRWE-DISCOVERY-PROBE/1\n", portA);
            peer.send("probe", portA);
            peer.send(std::string(LanDiscovery::maxMessageSize + 10, 'x'), portA);
            // An announcement is not a probe either, and is not answered.
            peer.send(*LanDiscovery::encode(sampleGame()), portA);
            CHECK(peer.receiveFor(std::chrono::milliseconds(300)).empty());

            // The spelling without the final newline is still the probe.
            std::string bare = LanDiscovery::probeMessage();
            bare.pop_back();
            peer.send(bare, portA);
            CHECK(peer.receiveFor(std::chrono::milliseconds(200)).size() == 1);

            a.stop();
        }

        SECTION("a datagram larger than the receive buffer does not deafen it")
        {
            LanDiscovery a;
            a.start(portA);
            a.advertise(game);

            RawPeer peer;
            peer.send(std::string(3000, 'x'), portA);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            peer.send(LanDiscovery::probeMessage(), portA);
            CHECK(peer.receiveFor(std::chrono::milliseconds(300)).size() == 1);

            a.stop();
        }
    }

    TEST_CASE("LanDiscovery::isProbeAddress says what the TCP screen may accept")
    {
        // The same parser probe() uses, asked without a socket: the screen
        // refuses "Invalid TCP/IP Address" at the dialog, not a second later
        // as a list that never fills.
        SECTION("addresses, with and without a port")
        {
            CHECK(LanDiscovery::isProbeAddress("192.168.2.120"));
            CHECK(LanDiscovery::isProbeAddress("192.168.2.120:1350"));
            CHECK(LanDiscovery::isProbeAddress("  10.0.0.7  "));
            CHECK(LanDiscovery::isProbeAddress("::1"));
            CHECK(LanDiscovery::isProbeAddress("[::1]:1338"));
            CHECK(LanDiscovery::isProbeAddress("[fe80::1]"));
        }

        SECTION("what is not an address")
        {
            CHECK_FALSE(LanDiscovery::isProbeAddress(""));
            CHECK_FALSE(LanDiscovery::isProbeAddress("   "));
            CHECK_FALSE(LanDiscovery::isProbeAddress("not an address"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("192.168.2.120:0"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("192.168.2.120:70000"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("192.168.2.300"));
        }
    }

    TEST_CASE("LanDiscovery announces to a named machine")
    {
        // A broadcast stays on its subnet, so a guest that found its host by
        // typing an address has to be able to say so to that address. The
        // default port is not bound by either instance here, so a lobby in
        // B's list can only have come from the named target.
        constexpr unsigned short portA = 1352;
        constexpr unsigned short portB = 1353;

        auto game = extendedGame();
        game.session = "unicast-session";

        SECTION("an announcement reaches the target and only the target")
        {
            LanDiscovery a;
            a.start(portA);
            REQUIRE(a.isRunning());
            a.advertise(game);

            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());

            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            CHECK(b.games().empty());

            a.setAnnounceTargets({"127.0.0.1:" + std::to_string(portB), "not an address"});
            REQUIRE(waitUntilHeard(b, game.session, std::chrono::milliseconds(2500)));
            CHECK(b.games()[0].map == game.map);

            // Cleared, it goes quiet again: the entry ages out of B's list.
            a.setAnnounceTargets({});
            std::this_thread::sleep_for(LanDiscovery::expiry + std::chrono::milliseconds(1500));
            CHECK(b.games().empty());

            b.stop();
            a.stop();
        }
    }

    namespace
    {
        /** Sequence numbers counting up from 1, all spoken by one person. */
        std::vector<DiscoveredChatLine> manyChatLines(std::size_t count, std::size_t textLength)
        {
            std::vector<DiscoveredChatLine> lines;
            for (std::size_t i = 0; i < count; ++i)
            {
                // Each line differs, so that a survivor can be told apart.
                auto text = std::to_string(i) + ":" + std::string(textLength, 'x');
                text.resize(textLength);
                lines.push_back(DiscoveredChatLine{static_cast<unsigned int>(i + 1), "Player" + std::to_string(i % 3), text});
            }
            return lines;
        }

        /** Ten seats, every one with extras, one of them ready. */
        DiscoveredGame fullLobby()
        {
            DiscoveredGame game;
            game.session = "a1b2c3d4e5f6a7b8";
            game.map = "Coast To Coast";
            game.label = "Bailey's game";
            game.hostName = "Bailey";
            game.passwordHash = LanDiscovery::hashPassword(game.session, "pw");
            game.launching = true;
            for (int i = 0; i < 10; ++i)
            {
                DiscoveredSlot slot;
                slot.index = i;
                slot.controller = "network";
                slot.name = "Player" + std::to_string(i);
                slot.side = "CORE";
                slot.colour = static_cast<unsigned int>(i);
                slot.team = i % 4;
                slot.address = "192.168.100.10:1338";
                slot.metal = 10000 + i;
                slot.energy = 20000 + i;
                slot.personality = "rusher";
                slot.ready = i == 8;
                game.slots.push_back(slot);
            }
            return game;
        }

        bool canResolveLocalhost()
        {
            asio::io_context io;
            asio::ip::udp::resolver resolver(io);
            asio::error_code ec;
            auto results = resolver.resolve("localhost", "", ec);
            return !ec && !results.empty();
        }
    }

    TEST_CASE("LanDiscovery carries ready flags and chat")
    {
        SECTION("both survive a round trip")
        {
            auto game = extendedGame();
            game.slots[1].ready = true;
            game.slots[3].ready = true;
            game.chat = {
                {1, "Bailey", "hello"},
                {2, "", "Guest has joined"},
                {7, "G|u=est,", "a b;c%d|e\xC3\xA9 !"},
            };

            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->size() <= LanDiscovery::maxMessageSize);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            REQUIRE(back->slots.size() == 4);
            CHECK_FALSE(back->slots[0].ready);
            CHECK(back->slots[1].ready);
            CHECK_FALSE(back->slots[2].ready);
            CHECK(back->slots[3].ready);

            REQUIRE(back->chat.size() == 3);
            for (std::size_t i = 0; i < 3; ++i)
            {
                CHECK(back->chat[i].sequence == game.chat[i].sequence);
                CHECK(back->chat[i].name == game.chat[i].name);
                CHECK(back->chat[i].text == game.chat[i].text);
            }

            // Nothing else is disturbed by them.
            CHECK(back->options == game.options);
            CHECK(back->slots[2].personality == "rusher");
            CHECK(back->hostName == "Bailey");
        }

        SECTION("an announcement that says neither decodes to the defaults")
        {
            // This is every announcement an older build makes, and every
            // lobby nobody has spoken in or pressed Ready in.
            auto back = LanDiscovery::decode(
                minimalAnnouncement + "\nslot=0,human,ARM,0,-1,A,\nslot=1,computer,CORE,1,1,B,");
            REQUIRE(back);
            CHECK(back->chat.empty());
            REQUIRE(back->slots.size() == 2);
            CHECK_FALSE(back->slots[0].ready);
            CHECK_FALSE(back->slots[1].ready);
        }

        SECTION("a lobby with neither is not made bigger by them")
        {
            auto text = LanDiscovery::encode(extendedGame());
            REQUIRE(text);
            CHECK(text->find("slotr") == std::string::npos);
            CHECK(text->find("chat=") == std::string::npos);
        }

        SECTION("an older decoder can still read it")
        {
            // The older decoder skips a key it does not know, so what is
            // left when `slotr=` and `chat=` are taken away has to be a
            // whole lobby of the older format, and the `slot=` lines it
            // does read must still have exactly seven fields.
            auto game = extendedGame();
            game.slots[0].ready = true;
            game.chat = {{1, "Bailey", "hello"}, {2, "Bailey", "anyone?"}};
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->find("slotr=0:1") != std::string::npos);
            CHECK(text->find("chat=1|Bailey|hello") != std::string::npos);

            std::string stripped;
            std::size_t start = 0;
            while (start <= text->size())
            {
                auto end = text->find('\n', start);
                auto line = text->substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (line.rfind("slot=", 0) == 0)
                {
                    CHECK(std::count(line.begin(), line.end(), ',') == 6);
                }
                if (line.rfind("slotx=", 0) == 0)
                {
                    CHECK(std::count(line.begin(), line.end(), ',') == 3);
                }
                if (line.rfind("slotr=", 0) != 0 && line.rfind("chat=", 0) != 0)
                {
                    if (!stripped.empty()) stripped += "\n";
                    stripped += line;
                }
                if (end == std::string::npos) break;
                start = end + 1;
            }

            auto old = LanDiscovery::decode(stripped);
            REQUIRE(old);
            CHECK(old->slots.size() == 4);
            CHECK(old->options.size() == 6);
        }

        SECTION("keys from a later version are still ignored beside them")
        {
            auto game = extendedGame();
            game.slots[1].ready = true;
            game.chat = {{1, "Bailey", "hi"}};
            auto text = *LanDiscovery::encode(game) + "\nfuture=whatever\nslotz=1:1\nchatty=1|a|b";
            auto back = LanDiscovery::decode(text);
            REQUIRE(back);
            CHECK(back->slots[1].ready);
            CHECK(back->chat.size() == 1);
        }

        SECTION("a name or a line that looks like the wire format does not confuse it")
        {
            auto game = extendedGame();
            game.chat = {{1, "a\"b|c", "chat=2|x|y"}, {2, "slotr=0:1", "opt.hack=1"}};
            auto back = LanDiscovery::decode(*LanDiscovery::encode(game));
            REQUIRE(back);
            REQUIRE(back->chat.size() == 2);
            CHECK(back->chat[0].name == "a\"b|c");
            CHECK(back->chat[0].text == "chat=2|x|y");
            CHECK(back->chat[1].name == "slotr=0:1");
            CHECK(back->options.size() == 6);
            CHECK_FALSE(back->slots[0].ready);
        }
    }

    TEST_CASE("LanDiscovery refuses ready flags and chat it cannot trust")
    {
        const std::string seat = minimalAnnouncement + "\nslot=0,human,ARM,0,-1,A,";

        SECTION("a ready flag that is not one is dropped, not believed")
        {
            for (const char* bad : {"slotr=10:1", "slotr=-1:1", "slotr=x:1", "slotr=0:2", "slotr=0:", "slotr=0",
                     "slotr=:1", "slotr=0:true", "slotr=0:1:1", "slotr=0:%31", "slotr=99999999999:1"})
            {
                auto back = LanDiscovery::decode(seat + "\n" + bad);
                REQUIRE(back);
                REQUIRE(back->slots.size() == 1);
                CHECK_FALSE(back->slots[0].ready);
            }
        }

        SECTION("a ready flag that is one is read, wherever it is")
        {
            auto on = LanDiscovery::decode(seat + "\nslotr=0:1");
            REQUIRE(on);
            CHECK(on->slots[0].ready);

            auto early = LanDiscovery::decode(minimalAnnouncement + "\nslotr=0:1\nslot=0,human,ARM,0,-1,A,");
            REQUIRE(early);
            CHECK(early->slots[0].ready);

            auto off = LanDiscovery::decode(seat + "\nslotr=0:0");
            REQUIRE(off);
            CHECK_FALSE(off->slots[0].ready);

            // The first word on a seat is the one that counts.
            auto twice = LanDiscovery::decode(seat + "\nslotr=0:1\nslotr=0:0");
            REQUIRE(twice);
            CHECK(twice->slots[0].ready);

            // A seat the lobby does not list has nothing to be ready.
            auto stray = LanDiscovery::decode(seat + "\nslotr=5:1");
            REQUIRE(stray);
            CHECK(stray->slots.size() == 1);
            CHECK_FALSE(stray->slots[0].ready);
        }

        SECTION("a sequence that is not 1..2147483647 drops the line")
        {
            for (const char* bad : {"0", "-1", "99999999999", "abc", "2147483648", "", "1x", "%201"})
            {
                auto back = LanDiscovery::decode(minimalAnnouncement + "\nchat=" + bad + "|Bob|hi");
                REQUIRE(back);
                CHECK(back->chat.empty());
            }
            auto edge = LanDiscovery::decode(minimalAnnouncement + "\nchat=2147483647|Bob|hi\nchat=1|Bob|hi");
            REQUIRE(edge);
            REQUIRE(edge->chat.size() == 2);
            CHECK(edge->chat[0].sequence == 2147483647u);
            CHECK(edge->chat[1].sequence == 1u);
        }

        SECTION("more than eight lines are not read past the eighth")
        {
            std::string text = minimalAnnouncement;
            for (int i = 1; i <= 9; ++i)
            {
                text += "\nchat=" + std::to_string(i) + "|Bob|line" + std::to_string(i);
            }
            auto back = LanDiscovery::decode(text);
            REQUIRE(back);
            REQUIRE(back->chat.size() == 8);
            CHECK(back->chat.front().text == "line1");
            CHECK(back->chat.back().text == "line8");
        }

        SECTION("a name or text over its bound drops the line")
        {
            auto tooLongName = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|" + std::string(21, 'n') + "|hi");
            REQUIRE(tooLongName);
            CHECK(tooLongName->chat.empty());

            auto longestName = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|" + std::string(20, 'n') + "|hi");
            REQUIRE(longestName);
            REQUIRE(longestName->chat.size() == 1);
            CHECK(longestName->chat[0].name.size() == 20);

            auto tooLongText = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|Bob|" + std::string(81, 't'));
            REQUIRE(tooLongText);
            CHECK(tooLongText->chat.empty());

            auto longestText = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|Bob|" + std::string(80, 't'));
            REQUIRE(longestText);
            REQUIRE(longestText->chat.size() == 1);
            CHECK(longestText->chat[0].text.size() == 80);

            // The bound is on what it decodes to, not on what was written:
            // eighty escaped characters are far more than eighty bytes of
            // wire and no more than eighty of text.
            std::string escaped;
            for (int i = 0; i < 80; ++i) escaped += "%41";
            auto fits = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|Bob|" + escaped);
            REQUIRE(fits);
            REQUIRE(fits->chat.size() == 1);
            CHECK(fits->chat[0].text == std::string(80, 'A'));
            auto over = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|Bob|" + escaped + "%41");
            REQUIRE(over);
            CHECK(over->chat.empty());
        }

        SECTION("a control character, a NUL or an empty text drops the line")
        {
            for (const char* text : {"a%00b", "a%0Ab", "a%0Db", "a%1Fb", "a%7Fb", "a%09b", "a%1Bb", "a%2", "a%zz", "a\tb", ""})
            {
                auto back = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|Bob|" + text);
                REQUIRE(back);
                CHECK(back->chat.empty());
            }
            auto badName = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|B%0Ab|hi");
            REQUIRE(badName);
            CHECK(badName->chat.empty());
            auto nulName = LanDiscovery::decode(minimalAnnouncement + "\nchat=1|B%00b|hi");
            REQUIRE(nulName);
            CHECK(nulName->chat.empty());

            // A system line has no speaker, and that is allowed.
            auto system = LanDiscovery::decode(minimalAnnouncement + "\nchat=1||Bob%20has%20joined");
            REQUIRE(system);
            REQUIRE(system->chat.size() == 1);
            CHECK(system->chat[0].name.empty());
            CHECK(system->chat[0].text == "Bob has joined");
        }

        SECTION("the wrong number of fields drops the line")
        {
            for (const char* bad : {"1", "1|Bob", "1|Bob|hi|there", "|Bob|hi", "Bob|hi"})
            {
                auto back = LanDiscovery::decode(minimalAnnouncement + "\nchat=" + bad);
                REQUIRE(back);
                CHECK(back->chat.empty());
            }
        }

        SECTION("one bad line does not take the good ones with it")
        {
            auto back = LanDiscovery::decode(
                seat + "\nchat=1|Bob|first\nchat=0|Bob|bad\nchat=2|Bob|a%00b\nchat=3|Bob|third\nslotr=0:1");
            REQUIRE(back);
            REQUIRE(back->chat.size() == 2);
            CHECK(back->chat[0].text == "first");
            CHECK(back->chat[1].text == "third");
            CHECK(back->slots[0].ready);
        }

        SECTION("encoding leaves out a line it would not take back, and never fails the lobby")
        {
            auto game = sampleGame();
            game.chat = {
                {0, "Bob", "sequence zero"},
                {1, "Bob", ""},
                {2, "Bob", std::string(81, 't')},
                {3, std::string(21, 'n'), "long name"},
                {4, "Bob", "tab\there"},
                {5, "Bob", "new\nline"},
                {6, "Bob", "this one is fine"},
            };
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            REQUIRE(back->chat.size() == 1);
            CHECK(back->chat[0].sequence == 6u);
        }

        SECTION("encoding keeps the newest eight of more than eight")
        {
            auto game = sampleGame();
            game.chat = manyChatLines(12, 10);
            auto back = LanDiscovery::decode(*LanDiscovery::encode(game));
            REQUIRE(back);
            REQUIRE(back->chat.size() == 8);
            CHECK(back->chat.front().sequence == 5u);
            CHECK(back->chat.back().sequence == 12u);
        }
    }

    TEST_CASE("LanDiscovery sheds chat first, then options, when an announcement is too big")
    {
        SECTION("ten seats, thirty-two options and eight full lines")
        {
            auto game = fullLobby();
            for (int i = 0; i < 32; ++i)
            {
                game.options.emplace_back("opt" + std::to_string(i), "v" + std::to_string(i));
            }

            // Known to fit as it is, so any shedding below is the chat's.
            auto without = LanDiscovery::encode(game);
            REQUIRE(without);
            REQUIRE(without->size() <= LanDiscovery::maxMessageSize);

            game.chat = manyChatLines(8, LanDiscovery::maxChatTextLength);
            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->size() <= LanDiscovery::maxMessageSize);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            // Every option and every seat made it: chat is what gave way.
            CHECK(back->options == game.options);
            REQUIRE(back->slots.size() == 10);
            CHECK(back->slots[8].ready);
            CHECK_FALSE(back->slots[9].ready);
            // What is left of the chat is whole lines, the newest ones.
            REQUIRE(back->chat.size() < 8);
            for (std::size_t i = 0; i < back->chat.size(); ++i)
            {
                const auto& expected = game.chat[game.chat.size() - back->chat.size() + i];
                CHECK(back->chat[i].sequence == expected.sequence);
                CHECK(back->chat[i].text == expected.text);
            }
        }

        SECTION("part of the chat fits: the oldest lines are the ones that go")
        {
            auto game = fullLobby();
            game.options = {{"los", "1"}, {"mapping", "0"}, {"startloc", "random"}};
            game.chat = manyChatLines(8, LanDiscovery::maxChatTextLength);

            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->size() <= LanDiscovery::maxMessageSize);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            CHECK(back->options == game.options);
            REQUIRE(back->chat.size() > 0);
            REQUIRE(back->chat.size() < 8);
            CHECK(back->chat.back().sequence == 8u);
            for (std::size_t i = 1; i < back->chat.size(); ++i)
            {
                CHECK(back->chat[i].sequence == back->chat[i - 1].sequence + 1);
            }

            // Asking for only the lines that survived says the same thing:
            // it is the oldest that were shed, and nothing else changed.
            auto only = game;
            only.chat.erase(only.chat.begin(), only.chat.begin() + static_cast<std::ptrdiff_t>(8 - back->chat.size()));
            auto onlyText = LanDiscovery::encode(only);
            REQUIRE(onlyText);
            CHECK(*onlyText == *text);

            // And one line more would not have fitted: that is what makes
            // this the shedding order and not merely a size limit.
            auto more = game;
            more.chat.erase(more.chat.begin(), more.chat.begin() + static_cast<std::ptrdiff_t>(8 - back->chat.size() - 1));
            auto moreText = LanDiscovery::encode(more);
            REQUIRE(moreText);
            auto moreBack = LanDiscovery::decode(*moreText);
            REQUIRE(moreBack);
            CHECK(moreBack->chat.size() == back->chat.size());
        }

        SECTION("when options alone fill it, chat is gone entirely and options give way next")
        {
            auto game = sampleGame();
            game.hostName = "Bailey";
            for (int i = 0; i < 32; ++i)
            {
                game.options.emplace_back("long_option_name_" + std::to_string(i), std::string(40, 'v'));
            }
            game.chat = manyChatLines(8, LanDiscovery::maxChatTextLength);

            auto text = LanDiscovery::encode(game);
            REQUIRE(text);
            CHECK(text->size() <= LanDiscovery::maxMessageSize);

            auto back = LanDiscovery::decode(*text);
            REQUIRE(back);
            // Not one chat line is kept while an option has been dropped...
            CHECK(back->chat.empty());
            // ...and the options are what they were without any chat.
            auto noChat = game;
            noChat.chat.clear();
            CHECK(*LanDiscovery::encode(noChat) == *text);
            CHECK(back->options.size() > 0);
            CHECK(back->options.size() < game.options.size());
            for (std::size_t i = 0; i < back->options.size(); ++i)
            {
                CHECK(back->options[i] == game.options[i]);
            }
            CHECK(back->slots.size() == game.slots.size());
        }

        SECTION("a lobby that does not fit without chat or options is refused with them")
        {
            DiscoveredGame game;
            game.session = "abc";
            game.map = "Coast";
            for (int i = 0; i < 10; ++i)
            {
                DiscoveredSlot slot;
                slot.index = i;
                slot.name = std::string(120, ' ');
                slot.address = std::string(20, ' ');
                game.slots.push_back(slot);
            }
            game.options = {{"los", "1"}};
            game.chat = manyChatLines(3, 10);
            CHECK_FALSE(LanDiscovery::encode(game));
        }
    }

    TEST_CASE("LanDiscovery announces a change at once")
    {
        // A very long interval, so that anything that arrives did so
        // because of the change and not because a timer went off. Ports
        // that nothing else here uses; the target is a bare socket that
        // can count what it is sent.
        constexpr unsigned short portA = 1354;
        constexpr unsigned short portB = 1355;
        const auto minute = std::chrono::minutes(1);

        auto game = extendedGame();
        game.session = "change-session";

        SECTION("the first lobby, then a change, and an identical lobby sends nothing")
        {
            RawPeer peer;
            LanDiscovery a;
            a.setAnnounceInterval(minute);
            a.start(portA);
            REQUIRE(a.isRunning());
            a.setAnnounceTargets({"127.0.0.1:" + std::to_string(peer.localPort())});

            a.advertise(game);
            auto first = peer.receiveFor(std::chrono::milliseconds(300));
            REQUIRE(first.size() == 1);
            auto firstBack = LanDiscovery::decode(first[0]);
            REQUIRE(firstBack);
            CHECK(firstBack->session == game.session);
            CHECK(firstBack->chat.empty());

            // The same lobby again is not a change.
            a.advertise(game);
            a.advertise(game);
            CHECK(peer.receiveFor(std::chrono::milliseconds(300)).empty());

            // One the codec writes the same way is not a change either: a
            // chat line with nothing in it is left out on the wire.
            auto sameOnTheWire = game;
            sameOnTheWire.chat.push_back({1, "Bailey", ""});
            a.advertise(sameOnTheWire);
            CHECK(peer.receiveFor(std::chrono::milliseconds(300)).empty());

            // A chat line is.
            auto spoken = game;
            spoken.chat.push_back({1, "Bailey", "hello"});
            a.advertise(spoken);
            auto second = peer.receiveFor(std::chrono::milliseconds(300));
            REQUIRE(second.size() == 1);
            auto secondBack = LanDiscovery::decode(second[0]);
            REQUIRE(secondBack);
            REQUIRE(secondBack->chat.size() == 1);
            CHECK(secondBack->chat[0].text == "hello");

            // So is a ready toggle.
            auto ready = spoken;
            ready.slots[1].ready = true;
            a.advertise(ready);
            auto third = peer.receiveFor(std::chrono::milliseconds(300));
            REQUIRE(third.size() == 1);
            auto thirdBack = LanDiscovery::decode(third[0]);
            REQUIRE(thirdBack);
            CHECK(thirdBack->slots[1].ready);

            // And the lobby that was stopped and is advertised again is
            // announced again, the same lobby or not.
            a.stopAdvertising();
            a.advertise(ready);
            CHECK(peer.receiveFor(std::chrono::milliseconds(300)).size() == 1);

            a.stop();
        }

        SECTION("changes that come faster than ten a second are folded into one")
        {
            RawPeer peer;
            LanDiscovery a;
            a.setAnnounceInterval(minute);
            a.start(portA);
            a.setAnnounceTargets({"127.0.0.1:" + std::to_string(peer.localPort())});

            a.advertise(game);
            for (int i = 1; i <= 5; ++i)
            {
                auto changed = game;
                changed.label = "label " + std::to_string(i);
                a.advertise(changed);
            }

            auto got = peer.receiveFor(std::chrono::milliseconds(500));
            // The first goes at once and the rest wait for the window to
            // end and go as one, carrying the last. How many datagrams that
            // is depends on whether the network thread woke before the
            // sixth call returned -- in a Release build it does not, and
            // all six are one send -- so what is asserted is the bound and
            // the last lobby, not the count of the first.
            REQUIRE(got.size() >= 1);
            CHECK(got.size() <= 3);
            auto last = LanDiscovery::decode(got.back());
            REQUIRE(last);
            CHECK(last->label == "label 5");

            // And nothing is held back after that.
            CHECK(peer.receiveFor(std::chrono::milliseconds(300)).empty());

            a.stop();
        }

        SECTION("a second instance sees the change well inside one interval")
        {
            LanDiscovery a;
            a.setAnnounceInterval(minute);
            a.start(portA);
            REQUIRE(a.isRunning());

            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());

            a.setAnnounceTargets({"127.0.0.1:" + std::to_string(portB)});
            a.advertise(game);
            REQUIRE(waitUntilHeard(b, game.session, std::chrono::milliseconds(2000)));

            auto changed = game;
            changed.slots[1].ready = true;
            changed.chat.push_back({1, "Bailey", "ready when you are"});

            auto began = std::chrono::steady_clock::now();
            a.advertise(changed);

            bool seen = false;
            while (!seen && std::chrono::steady_clock::now() - began < std::chrono::milliseconds(2000))
            {
                for (const auto& heard : b.games())
                {
                    if (heard.session == game.session && heard.slots.size() > 1 && heard.slots[1].ready
                        && heard.chat.size() == 1)
                    {
                        seen = true;
                    }
                }
                if (!seen) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            REQUIRE(seen);
            // Well inside the minute, and inside half a second.
            CHECK(std::chrono::steady_clock::now() - began < std::chrono::milliseconds(500));

            b.stop();
            a.stop();
        }

        SECTION("a restart does not leave two announce loops behind")
        {
            RawPeer peer;
            LanDiscovery a;
            a.setAnnounceInterval(std::chrono::milliseconds(200));
            a.start(portA);
            a.stop();
            a.start(portA);
            a.setAnnounceTargets({"127.0.0.1:" + std::to_string(peer.localPort())});
            a.advertise(game);

            // One at once for the change, and one every 200 ms after: a
            // second loop left over from the first run would double that.
            auto got = peer.receiveFor(std::chrono::milliseconds(1050));
            CHECK(got.size() >= 4);
            CHECK(got.size() <= 7);
            a.stop();
        }
    }

    TEST_CASE("LanDiscovery::isProbeAddress takes a host name")
    {
        SECTION("names that are written like one")
        {
            CHECK(LanDiscovery::isProbeAddress("localhost"));
            CHECK(LanDiscovery::isProbeAddress("my-pc.lan"));
            CHECK(LanDiscovery::isProbeAddress("my-pc:1338"));
            CHECK(LanDiscovery::isProbeAddress("localhost:1338"));
            CHECK(LanDiscovery::isProbeAddress("example.com"));
            CHECK(LanDiscovery::isProbeAddress("  Bailey-PC.Example.COM  "));
            CHECK(LanDiscovery::isProbeAddress("a"));
            CHECK(LanDiscovery::isProbeAddress("3com.example"));
            CHECK(LanDiscovery::isProbeAddress(std::string(63, 'a') + ".com"));
            CHECK(LanDiscovery::isProbeAddress(std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(61, 'd')));
        }

        SECTION("what is still not an address")
        {
            CHECK_FALSE(LanDiscovery::isProbeAddress("999.1.1.1"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("999.1.1.1:1338"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("-bad.com"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("bad-.com"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("a..b"));
            CHECK_FALSE(LanDiscovery::isProbeAddress(".a"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("a."));
            CHECK_FALSE(LanDiscovery::isProbeAddress(""));
            CHECK_FALSE(LanDiscovery::isProbeAddress("my pc"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("my_pc"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("my-pc:0"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("my-pc:70000"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("my-pc:port"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("my-pc:1:2"));
            CHECK_FALSE(LanDiscovery::isProbeAddress("[my-pc]:1338"));
            CHECK_FALSE(LanDiscovery::isProbeAddress(std::string(300, 'a')));
            CHECK_FALSE(LanDiscovery::isProbeAddress(std::string(64, 'a') + ".com"));
            CHECK_FALSE(LanDiscovery::isProbeAddress(
                std::string(63, 'a') + "." + std::string(63, 'b') + "." + std::string(63, 'c') + "." + std::string(62, 'd')));
        }
    }

    TEST_CASE("LanDiscovery looks a host name up without blocking the caller")
    {
        constexpr unsigned short portA = 1356;
        constexpr unsigned short portB = 1357;

        auto game = extendedGame();
        game.session = "named-session";

        if (!canResolveLocalhost())
        {
            SUCCEED("this machine cannot resolve localhost, so there is nothing to look up");
            return;
        }

        SECTION("a probe by name finds an advertising machine")
        {
            LanDiscovery a;
            a.start(portA);
            REQUIRE(a.isRunning());
            a.advertise(game);

            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());

            auto began = std::chrono::steady_clock::now();
            REQUIRE(b.probe("LocalHost:" + std::to_string(portA)));
            // The lookup is somebody else's thread; this is a hand-off.
            CHECK(std::chrono::steady_clock::now() - began < std::chrono::milliseconds(250));

            REQUIRE(waitUntilHeard(b, game.session, std::chrono::milliseconds(3000)));
            auto heard = b.games();
            REQUIRE(heard.size() == 1);
            CHECK(heard[0].map == game.map);
            CHECK(heard[0].sourceAddress.find(":" + std::to_string(portA)) != std::string::npos);

            b.stop();
            a.stop();
        }

        SECTION("a name that is known is probed straight away the second time")
        {
            LanDiscovery a;
            a.start(portA);
            a.advertise(game);

            LanDiscovery b;
            b.start(portB);

            REQUIRE(b.probe("localhost:" + std::to_string(portA)));
            REQUIRE(waitUntilHeard(b, game.session, std::chrono::milliseconds(3000)));

            // Let it expire from the list and ask again: the name is still
            // cached, so the probe is sent from this call and answered
            // inside the second.
            a.stopAdvertising();
            std::this_thread::sleep_for(LanDiscovery::expiry + std::chrono::milliseconds(300));
            REQUIRE(b.games().empty());
            a.advertise(game);
            REQUIRE(b.probe("localhost:" + std::to_string(portA)));
            CHECK(waitUntilHeard(b, game.session, std::chrono::milliseconds(1000)));

            b.stop();
            a.stop();
        }

        SECTION("a name that does not resolve is accepted, says nothing, and is not hammered")
        {
            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());

            auto began = std::chrono::steady_clock::now();
            CHECK(b.probe("no-such-host.invalid"));
            CHECK(b.probe("no-such-host.invalid:1350"));
            CHECK(std::chrono::steady_clock::now() - began < std::chrono::milliseconds(250));

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            CHECK(b.games().empty());
            // Still accepted while the failure is remembered, and still
            // nothing comes of it.
            CHECK(b.probe("no-such-host.invalid"));
            CHECK(b.games().empty());

            b.stop();
        }

        SECTION("before start or after stop a name is refused like an address")
        {
            LanDiscovery b;
            CHECK_FALSE(b.probe("localhost:" + std::to_string(portA)));
            b.start(portB);
            b.stop();
            CHECK_FALSE(b.probe("localhost:" + std::to_string(portA)));
        }

        SECTION("an announcement to a name reaches it once it has resolved")
        {
            LanDiscovery a;
            a.setAnnounceInterval(std::chrono::minutes(1));
            a.start(portA);
            REQUIRE(a.isRunning());

            LanDiscovery b;
            b.start(portB);
            REQUIRE(b.isRunning());

            a.setAnnounceTargets({"localhost:" + std::to_string(portB), "-not a name-"});
            a.advertise(game);

            // Not the regular announcement: that is a minute away.
            CHECK(waitUntilHeard(b, game.session, std::chrono::milliseconds(3000)));

            a.setAnnounceTargets({});
            b.stop();
            a.stop();
        }
    }
}
