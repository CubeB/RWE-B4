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
            CHECK_FALSE(b.probe("example.com"));
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
}
