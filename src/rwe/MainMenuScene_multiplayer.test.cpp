#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <rwe/MainMenuScene_multiplayer.h>
#include <rwe/game/AiSeat.h>
#include <set>

namespace rwe
{
    namespace
    {
        using Type = MainMenuModel::PlayerSettings::Type;
        using Side = MainMenuModel::PlayerSettings::Side;

        /** A model as a host would have set it up: seat 0 is the host, the rest on offer. */
        MainMenuModel hostModel()
        {
            MainMenuModel model;
            for (std::size_t i = 0; i < model.players.size(); ++i)
            {
                model.players[i].type.next(i == 0 ? Type::Human : Type::Open);
            }
            model.players[0].name.next("Hector");
            return model;
        }

        LobbyIdentity identityFor(const std::string& session)
        {
            LobbyIdentity id;
            id.session = session;
            id.label = "Bring snacks";
            id.hostName = "Hector";
            id.port = 1337;
            return id;
        }

        std::vector<std::string> noTokens()
        {
            return std::vector<std::string>(10);
        }
    }

    TEST_CASE("setup seat buttons offer both AI engines and return to an open seat", "[multiplayer][ai][battleroom]")
    {
        auto model = hostModel();
        auto& seat = model.players[1];
        seat.colorIndex.next(PlayerColorIndex(3));
        seat.teamIndex.next(2);
        // Reopening a formerly RWE seat still starts with the default engine.
        seat.personality.next("Balanced");
        cycleComputerSeat(seat);
        CHECK(seat.type.getValue() == Type::Computer);
        CHECK(seat.personality.getValue() == "Original TA");
        CHECK(seatCaption(seat.type.getValue(), "Old name", seat.personality.getValue()) == "Original TA");
        cycleComputerSeat(seat);
        CHECK(seat.type.getValue() == Type::Computer);
        CHECK(seat.personality.getValue() == "Balanced");
        CHECK(seatCaption(seat.type.getValue(), "Old name", seat.personality.getValue()) == "RWE AI");
        CHECK(seat.colorIndex.getValue() == PlayerColorIndex(3));
        CHECK(seat.teamIndex.getValue() == 2);

        SECTION("skirmish and battleroom reopen the seat")
        {
            cycleComputerSeat(seat);
            CHECK(seat.type.getValue() == Type::Open);
            cycleComputerSeat(seat);
            CHECK(seat.personality.getValue() == "Original TA");
        }
        SECTION("direct multiplayer setup next offers a network seat")
        {
            cycleComputerSeat(seat, Type::Network);
            CHECK(seat.type.getValue() == Type::Network);
        }
    }

    TEST_CASE("a setup AI selection reaches a guest with the same launch engine", "[multiplayer][ai][engine]")
    {
        auto host = hostModel();
        host.players[1].type.next(Type::Network);
        const auto personalities = builtInAiPersonalities();
        for (const auto expected : {AiEngine::Classic, AiEngine::Rwe})
        {
            cycleComputerSeat(host.players[2]);
            const auto lobby = lobbyFromModel(host, identityFor("ai-choice"), "Great Divide", noTokens());
            const auto encoded = LanDiscovery::encode(lobby);
            REQUIRE(encoded);
            const auto wire = LanDiscovery::decode(*encoded);
            REQUIRE(wire);
            MainMenuModel guest;
            applyLobbyToModel(guest, *wire, 1, "127.0.0.1:1337", "Ada");
            const auto& choice = guest.players[2].personality.getValue();
            CHECK(choice == host.players[2].personality.getValue());
            const auto personality = findAiPersonality(personalities, choice);
            REQUIRE(personality);
            CHECK(chooseAiEngine(personality, std::nullopt, AiEngine::Classic) == expected);
            CHECK(seatCaption(Type::Computer, "", choice) == (expected == AiEngine::Classic ? "Original TA" : "RWE AI"));
        }
    }

    TEST_CASE("the option list is written and read by one table", "[multiplayer]")
    {
        SECTION("every option the screen has round-trips")
        {
            MainMenuModel host;
            host.skirmishOptions.commanderDeath.next(1u);
            host.skirmishOptions.allowWatching.next(0u);
            host.skirmishOptions.startLocation.next(1u);
            host.skirmishOptions.mapping.next(1u);
            host.skirmishOptions.lineOfSight.next(2u);
            host.skirmishOptions.difficulty.next(0u);

            MainMenuModel guest;
            applyOptionsToModel(guest, optionsFromModel(host));

            CHECK(guest.skirmishOptions.commanderDeath.getValue() == 1u);
            CHECK(guest.skirmishOptions.allowWatching.getValue() == 0u);
            CHECK(guest.skirmishOptions.startLocation.getValue() == 1u);
            CHECK(guest.skirmishOptions.mapping.getValue() == 1u);
            CHECK(guest.skirmishOptions.lineOfSight.getValue() == 2u);
            CHECK(guest.skirmishOptions.difficulty.getValue() == 0u);
        }

        SECTION("the options are named, and say which stage")
        {
            MainMenuModel model;
            model.skirmishOptions.lineOfSight.next(2u);
            auto options = optionsFromModel(model);

            bool found = false;
            for (const auto& [name, value] : options)
            {
                if (name == "los")
                {
                    found = true;
                    CHECK(value == "2");
                }
            }
            CHECK(found);
            CHECK(options.size() == 7);

            bool community = false;
            for (const auto& [name, value] : options)
            {
                if (name == "community")
                {
                    community = true;
                    CHECK(value == "0");
                }
            }
            CHECK(community);
        }

        SECTION("a name it does not know is ignored, and a value out of range leaves the option alone")
        {
            MainMenuModel model;
            model.skirmishOptions.lineOfSight.next(2u);
            model.skirmishOptions.difficulty.next(2u);

            applyOptionsToModel(model, {{"future_thing", "1"}, {"los", "7"}, {"difficulty", "-1"}, {"mapping", "x"}, {"commander", ""}});

            CHECK(model.skirmishOptions.lineOfSight.getValue() == 2u);
            CHECK(model.skirmishOptions.difficulty.getValue() == 2u);
            CHECK(model.skirmishOptions.mapping.getValue() == 0u);
            CHECK(model.skirmishOptions.commanderDeath.getValue() == 0u);
        }

        SECTION("a host that sent no options leaves the guest's as they were")
        {
            MainMenuModel model;
            model.skirmishOptions.mapping.next(1u);
            applyOptionsToModel(model, {});
            CHECK(model.skirmishOptions.mapping.getValue() == 1u);
        }
    }

    TEST_CASE("the setup screen is announced from the host and mirrored by the guest", "[multiplayer]")
    {
        auto host = hostModel();
        host.players[0].side.next(Side::Core);
        host.players[0].colorIndex.next(PlayerColorIndex(3));
        host.players[1].type.next(Type::Network);
        host.players[1].name.next("Guest");
        host.players[1].networkAddress.next("192.168.0.9:1400");
        host.players[2].type.next(Type::Computer);
        host.players[2].personality.next("Rush");
        // A name left on a seat that is no longer a person's must not travel.
        host.players[2].name.next("Stale");
        host.players[3].metal.next(Metal(2500));
        host.players[3].energy.next(Energy(500));
        host.players[3].type.next(Type::Computer);
        host.skirmishOptions.lineOfSight.next(0u);

        auto tokens = noTokens();
        tokens[1] = "a1b2c3d4";

        auto id = identityFor("0123456789abcdef");
        id.passwordHash = LanDiscovery::hashPassword(id.session, "pw");
        id.port = 1400;
        const auto lobby = lobbyFromModel(host, id, "Coast To Coast", tokens);

        SECTION("what the host announces")
        {
            REQUIRE(lobby.slots.size() == 10);
            CHECK(lobby.session == "0123456789abcdef");
            CHECK(lobby.map == "Coast To Coast");
            CHECK(lobby.label == "Bring snacks");
            CHECK(lobby.hostName == "Hector");
            CHECK(lobby.port == 1400);

            CHECK(lobby.slots[0].controller == "human");
            CHECK(lobby.slots[0].name == "Hector");
            CHECK(lobby.slots[0].side == "CORE");
            CHECK(lobby.slots[0].colour == 3);

            CHECK(lobby.slots[1].controller == "network");
            CHECK(lobby.slots[1].address == "192.168.0.9:1400");
            CHECK(lobby.slots[1].name == "Guest");
            // The claim's token rides on the seat it was given.
            CHECK(lobby.slots[1].personality == "a1b2c3d4");

            CHECK(lobby.slots[2].controller == "computer");
            CHECK(lobby.slots[2].personality == "Rush");
            CHECK(lobby.slots[2].name.empty());

            CHECK(lobby.slots[3].metal == 2500);
            CHECK(lobby.slots[3].energy == 500);
            CHECK(lobby.slots[4].controller == "open");
        }

        SECTION("the announcement fits on the wire and comes back whole")
        {
            auto text = LanDiscovery::encode(lobby);
            REQUIRE(text.has_value());
            auto back = LanDiscovery::decode(*text);
            REQUIRE(back.has_value());
            CHECK(back->slots.size() == 10);
            CHECK(back->slots[1].personality == "a1b2c3d4");
            CHECK(back->slots[3].metal == 2500);
            CHECK(back->options == lobby.options);
            CHECK(back->passwordHash == id.passwordHash);
        }

        SECTION("the guest's mirror agrees with the host seat by seat")
        {
            MainMenuModel guest;
            applyLobbyToModel(guest, lobby, 1, "192.168.0.2:1400", "Guest");

            // The host's own seat is somebody to connect to.
            CHECK(guest.players[0].type.getValue() == Type::Network);
            CHECK(guest.players[0].networkAddress.getValue() == "192.168.0.2:1400");
            CHECK(guest.players[0].name.getValue() == "Hector");
            CHECK(guest.players[0].side.getValue() == Side::Core);
            CHECK(guest.players[0].colorIndex.getValue().value == 3);

            // This machine's seat is the player.
            CHECK(guest.players[1].type.getValue() == Type::Human);
            CHECK(guest.players[1].name.getValue() == "Guest");

            CHECK(guest.players[2].type.getValue() == Type::Computer);
            CHECK(guest.players[2].personality.getValue() == "Rush");
            CHECK(guest.players[2].name.getValue().empty());
            CHECK(guest.players[3].type.getValue() == Type::Computer);
            CHECK(guest.players[3].metal.getValue() == Metal(2500));
            CHECK(guest.players[3].energy.getValue() == Energy(500));
            CHECK(guest.players[4].type.getValue() == Type::Open);

            CHECK(guest.skirmishOptions.lineOfSight.getValue() == 0u);

            // The property that matters: for every seat a person plays, host
            // and guest hold the same name, side, colour and resources --
            // the game seeds its generator from them, so a difference is
            // two different games.
            for (std::size_t i = 0; i < 2; ++i)
            {
                CHECK(guest.players[i].name.getValue() == host.players[i].name.getValue());
                CHECK(guest.players[i].side.getValue() == host.players[i].side.getValue());
                CHECK(guest.players[i].colorIndex.getValue() == host.players[i].colorIndex.getValue());
                CHECK(guest.players[i].metal.getValue() == host.players[i].metal.getValue());
                CHECK(guest.players[i].energy.getValue() == host.players[i].energy.getValue());
            }
        }

        SECTION("applying again changes nothing, and a host's change follows")
        {
            MainMenuModel guest;
            applyLobbyToModel(guest, lobby, 1, "192.168.0.2:1400", "Guest");

            // A stale edit on the guest's copy is put back by the next apply.
            guest.players[4].type.next(Type::Computer);
            guest.skirmishOptions.mapping.next(1u);
            applyLobbyToModel(guest, lobby, 1, "192.168.0.2:1400", "Guest");
            CHECK(guest.players[4].type.getValue() == Type::Open);
            CHECK(guest.skirmishOptions.mapping.getValue() == 0u);

            // The host turns seat 4 into a computer.
            host.players[4].type.next(Type::Computer);
            host.skirmishOptions.mapping.next(1u);
            auto changed = lobbyFromModel(host, id, "Coast To Coast", tokens);
            applyLobbyToModel(guest, changed, 1, "192.168.0.2:1400", "Guest");
            CHECK(guest.players[4].type.getValue() == Type::Computer);
            CHECK(guest.skirmishOptions.mapping.getValue() == 1u);
        }

        SECTION("a colour or team that would index past a sprite sheet is not taken")
        {
            auto hostile = lobby;
            hostile.slots[0].colour = 200;
            hostile.slots[0].team = 9;

            MainMenuModel guest;
            applyLobbyToModel(guest, hostile, 1, "192.168.0.2:1400", "Guest");
            CHECK(guest.players[0].colorIndex.getValue().value < 10);
            CHECK_FALSE(guest.players[0].teamIndex.getValue().has_value());
        }

        SECTION("seats the announcement does not list are open")
        {
            auto short_ = lobby;
            short_.slots.resize(2);

            MainMenuModel guest;
            guest.players[5].type.next(Type::Computer);
            applyLobbyToModel(guest, short_, 1, "192.168.0.2:1400", "Guest");
            CHECK(guest.players[5].type.getValue() == Type::Open);
            CHECK(guest.players[5].name.getValue().empty());
        }
    }

    TEST_CASE("a guest asks for a seat and the host's next announcement says whether it got it", "[multiplayer]")
    {
        auto host = hostModel();
        auto id = identityFor("0123456789abcdef");
        auto lobby = lobbyFromModel(host, id, "Coast To Coast", noTokens());

        SECTION("the first open seat is on offer")
        {
            CHECK(firstOpenSeat(lobby) == 1);
            CHECK(firstOpenSeat(lobby, {1}) == 2);
            CHECK(firstOpenSeat(lobby, {1, 2, 3, 4, 5, 6, 7, 8, 9}) == std::nullopt);
        }

        SECTION("a seat that is a computer, a person or the host is not on offer")
        {
            host.players[1].type.next(Type::Computer);
            host.players[2].type.next(Type::Network);
            lobby = lobbyFromModel(host, id, "Coast To Coast", noTokens());
            CHECK(firstOpenSeat(lobby) == 3);

            for (std::size_t i = 3; i < 10; ++i)
            {
                host.players[i].type.next(Type::Computer);
            }
            lobby = lobbyFromModel(host, id, "Coast To Coast", noTokens());
            CHECK_FALSE(firstOpenSeat(lobby).has_value());
        }

        SECTION("the claim names the host's session, the token and the seat")
        {
            auto claim = claimAnnouncement(lobby, 1, "deadbeef", "Guest", 1400);
            CHECK(claim.session == "0123456789abcdef-gdeadbeef");
            CHECK(claim.map == "Coast To Coast");
            CHECK(claim.port == 1400);
            CHECK(claim.slots.size() == 1);
            CHECK(claim.slots[0].index == 1);
            CHECK(claim.slots[0].name == "Guest");
            CHECK(LanDiscovery::encode(claim).has_value());
        }

        SECTION("a claim is told from a lobby, and from another host's")
        {
            CHECK(isClaimSession("0123456789abcdef-gdeadbeef"));
            CHECK_FALSE(isClaimSession("0123456789abcdef"));
            CHECK_FALSE(isClaimSession("0123456789abcdef-gzzzzzzzz"));
            CHECK_FALSE(isClaimSession("-gdeadbeef"));

            auto mine = claimAnnouncement(lobby, 1, "deadbeef", "Guest", 1400);
            mine.sourceAddress = "192.168.0.9:1338";
            auto theirs = claimAnnouncement(lobby, 2, "cafef00d", "Other", 1337);
            theirs.session = "ffffffffffffffff-gcafef00d";
            theirs.sourceAddress = "192.168.0.10:1338";
            auto second = claimAnnouncement(lobby, 3, "01234567", "Third", 1500);
            second.sourceAddress = "[fe80::1]:1338";
            auto noSeat = claimAnnouncement(lobby, 1, "89abcdef", "None", 1400);
            noSeat.slots.clear();
            noSeat.sourceAddress = "192.168.0.11:1338";

            auto claims = claimsOn({lobby, theirs, mine, second, noSeat}, lobby.session);
            REQUIRE(claims.size() == 2);

            CHECK(claims[0].token == "deadbeef");
            CHECK(claims[0].seat == 1);
            CHECK(claims[0].name == "Guest");
            // The address is where the datagram came from, and the port is
            // the game port the guest announced, not the discovery port.
            CHECK(claims[0].address == "192.168.0.9:1400");

            CHECK(claims[1].token == "01234567");
            CHECK(claims[1].address == "[fe80::1]:1500");
        }

        SECTION("the guest reads its seat's state from the host's seat list")
        {
            CHECK(claimState(lobby, 1, "deadbeef") == ClaimState::Waiting);

            host.players[1].type.next(Type::Network);
            host.players[1].name.next("Guest");
            auto tokens = noTokens();
            tokens[1] = "deadbeef";
            auto seated = lobbyFromModel(host, id, "Coast To Coast", tokens);
            CHECK(claimState(seated, 1, "deadbeef") == ClaimState::Accepted);

            // The same seat, held by somebody who asked first.
            CHECK(claimState(seated, 1, "00000000") == ClaimState::Lost);
            // A seat that is something else altogether.
            CHECK(claimState(seated, 0, "deadbeef") == ClaimState::Lost);
            // A seat the lobby does not list.
            auto shortened = seated;
            shortened.slots.resize(1);
            CHECK(claimState(shortened, 1, "deadbeef") == ClaimState::Lost);
        }
    }

    TEST_CASE("an address is the sender's host and the announced game port", "[multiplayer]")
    {
        CHECK(hostPartOf("192.168.0.9:1338") == "192.168.0.9");
        CHECK(hostPartOf("[::1]:1338") == "[::1]");
        CHECK(hostPartOf("[fe80::1%4]:1338") == "[fe80::1%4]");
        CHECK(hostPartOf("192.168.0.9") == "192.168.0.9");
        CHECK(hostPartOf("::1") == "::1");
        CHECK(hostPartOf("") == "");

        CHECK(gameAddress("192.168.0.9:1338", 1337) == "192.168.0.9:1337");
        CHECK(gameAddress("[::1]:1338", 15338) == "[::1]:15338");
    }

    TEST_CASE("a game address announced from a neighbouring outside address is believed", "[multiplayer]")
    {
        // The Smarty/Three 4G connection, 2026-10-14: the discovery socket
        // left as .165 and the game port as .167. Not believing the second
        // sent the game to .165:1337, where nothing listened, and both
        // machines sat on "starting game".
        DiscoveredGame heard;
        heard.sourceAddress = "92.40.168.165:17992";
        heard.port = 1337;
        heard.gameAddress = "92.40.168.167:57274";
        CHECK(announcedGameAddress(heard, true) == "92.40.168.167:57274");

        SECTION("only by a reader that is traversing")
        {
            CHECK(announcedGameAddress(heard, false) == "92.40.168.165:1337");
        }

        SECTION("an address on another network is somebody aiming traffic, and is not")
        {
            heard.gameAddress = "198.51.100.9:57274";
            CHECK(announcedGameAddress(heard, true) == "92.40.168.165:1337");
        }
    }

    TEST_CASE("a row of the game list says what SELGAME's columns ask", "[multiplayer]")
    {
        auto host = hostModel();
        host.skirmishOptions.commanderDeath.next(0u);
        host.skirmishOptions.mapping.next(1u);
        host.skirmishOptions.lineOfSight.next(1u);
        host.players[0].metal.next(Metal(1500));
        host.players[0].energy.next(Energy(2000));
        host.players[1].type.next(Type::Network);
        host.players[2].type.next(Type::Computer);

        auto id = identityFor("0123456789abcdef");
        auto game = lobbyFromModel(host, id, "Coast To Coast", noTokens());

        auto row = gameListRow(game);
        CHECK(row.gameName == "Bring snacks");
        CHECK(row.mapName == "Coast To Coast");
        CHECK(row.commander == "Yes");
        CHECK(row.ping == "-");
        CHECK(row.lineOfSight == "Tru");
        CHECK(row.mapping == "Yes");
        CHECK(row.metal == "1500");
        CHECK(row.energy == "2000");
        // Two people over the nine seats a person could take -- the host, the
        // guest and seven open ones; the computer has a seat and is not a
        // player the game waits for.
        CHECK(row.players == "2/9");
        CHECK(row.status == "Open");

        SECTION("a full lobby, a started one")
        {
            for (std::size_t i = 0; i < 10; ++i)
            {
                host.players[i].type.next(i == 0 ? Type::Human : Type::Computer);
            }
            auto full = gameListRow(lobbyFromModel(host, id, "Coast To Coast", noTokens()));
            CHECK(full.status == "Full");

            id.launching = true;
            auto started = gameListRow(lobbyFromModel(host, id, "Coast To Coast", noTokens()));
            CHECK(started.status == "Play");
        }

        SECTION("a game with no name is named for its host, or its map")
        {
            game.label.clear();
            CHECK(gameListRow(game).gameName == "Hector's game");
            game.hostName.clear();
            CHECK(gameListRow(game).gameName == "Coast To Coast");
        }

        SECTION("options the host did not send leave their columns empty")
        {
            game.options.clear();
            auto bare = gameListRow(game);
            CHECK(bare.commander.empty());
            CHECK(bare.lineOfSight.empty());
            CHECK(bare.mapping.empty());
        }
    }

    TEST_CASE("a password gate says yes only to the password it was made with", "[multiplayer]")
    {
        DiscoveredGame game;
        game.session = "0123456789abcdef";

        SECTION("no password set lets anybody in")
        {
            CHECK(passwordAccepted(game, ""));
            CHECK(passwordAccepted(game, "anything"));
        }

        SECTION("a password set is checked against the session it was hashed with")
        {
            game.passwordHash = LanDiscovery::hashPassword(game.session, "sesame");
            CHECK(passwordAccepted(game, "sesame"));
            CHECK_FALSE(passwordAccepted(game, "Sesame"));
            CHECK_FALSE(passwordAccepted(game, ""));

            game.session = "fedcba9876543210";
            CHECK_FALSE(passwordAccepted(game, "sesame"));
        }
    }

    TEST_CASE("a name is what is left once its spaces are gone", "[multiplayer]")
    {
        CHECK(trimmed("  Hector  ") == "Hector");
        CHECK(trimmed("Hector Bailey") == "Hector Bailey");
        CHECK(trimmed("   ") == "");
        CHECK(trimmed("") == "");
    }

    TEST_CASE("a visit to the multiplayer screens leaves the skirmish screen as it found it", "[multiplayer]")
    {
        MainMenuModel model;
        model.players[1].type.next(Type::Computer);
        model.players[1].personality.next("Turtle");
        model.skirmishOptions.lineOfSight.next(0u);

        auto snapshot = snapshotModel(model);

        auto host = hostModel();
        auto lobby = lobbyFromModel(host, identityFor("0123456789abcdef"), "Coast To Coast", noTokens());
        applyLobbyToModel(model, lobby, 1, "192.168.0.2:1337", "Guest");
        REQUIRE(model.players[0].type.getValue() == Type::Network);
        REQUIRE(model.players[1].type.getValue() == Type::Human);

        restoreModel(model, snapshot);

        CHECK(model.players[0].type.getValue() == Type::Human);
        CHECK(model.players[1].type.getValue() == Type::Computer);
        CHECK(model.players[1].personality.getValue() == "Turtle");
        CHECK(model.skirmishOptions.lineOfSight.getValue() == 0u);
        for (const auto& p : model.players)
        {
            CHECK(p.name.getValue().empty());
            CHECK(p.networkAddress.getValue().empty());
        }
    }
}

namespace rwe
{
    namespace
    {
        /** LOUNGE2.GUI's own captions for the four option buttons, in the file's order. */
        std::vector<std::string> battleroomLabels(const std::string& option)
        {
            if (option == "commander")
            {
                return {"Continues", "Game ends", "Deathmatch"};
            }
            if (option == "startloc")
            {
                return {"Random", "Fixed"};
            }
            if (option == "mapping")
            {
                return {"Unmapped", "Mapped"};
            }
            return {"True", "Circular", "Permanent"};
        }

        unsigned int stagesOf(const std::string& option)
        {
            return option == "los" || option == "commander" ? 3u : 2u;
        }

        MainMenuModel roomModel()
        {
            MainMenuModel model;
            for (std::size_t i = 0; i < model.players.size(); ++i)
            {
                model.players[i].type.next(i == 0 ? Type::Human : Type::Open);
                model.players[i].colorIndex.next(PlayerColorIndex(static_cast<unsigned int>(i)));
            }
            model.players[0].name.next("Hector");
            return model;
        }

        SeatClaim claimFor(const std::string& side, unsigned int colour, int team, bool ready = false)
        {
            SeatClaim claim;
            claim.token = "deadbeef";
            claim.seat = 1;
            claim.name = "Guest";
            claim.side = side;
            claim.colour = colour;
            claim.team = team;
            claim.ready = ready;
            return claim;
        }
    }

    TEST_CASE("every stage of every battleroom option round-trips through its button and the model, by label", "[multiplayer][battleroom]")
    {
        for (const auto& option : battleroomOptions())
        {
            const std::string name = option.option;
            const auto labels = battleroomLabels(name);

            SECTION(name)
            {
                for (unsigned int modelStage = 0; modelStage < stagesOf(name); ++modelStage)
                {
                    // The model's stage becomes a button face and comes back
                    // as itself...
                    auto face = buttonStageOfOptionStage(name, labels, modelStage);
                    REQUIRE(face.has_value());
                    CHECK(optionStageOfButtonStage(name, labels, *face) == modelStage);

                    // ...and through a model, written as a lobby announces it.
                    MainMenuModel host;
                    applyOptionsToModel(host, {{name, std::to_string(modelStage)}});
                    MainMenuModel guest;
                    applyOptionsToModel(guest, optionsFromModel(host));
                    CHECK(optionsFromModel(guest) == optionsFromModel(host));
                }

                // Clicking walks every stage once and returns to the start.
                const auto usable = usableOptionLabels(name, labels);
                REQUIRE(usable.size() == stagesOf(name));
                unsigned int stage = 0;
                std::set<unsigned int> seen;
                for (std::size_t click = 0; click < usable.size(); ++click)
                {
                    seen.insert(stage);
                    auto next = nextOptionStage(name, usable, stage);
                    REQUIRE(next.has_value());
                    stage = *next;
                }
                CHECK(seen.size() == usable.size());
                CHECK(stage == 0u);
            }
        }
    }

    TEST_CASE("the battleroom's stage order is not the setup screen's, and the labels are what decide", "[multiplayer][battleroom]")
    {
        // The model's stage 0 of the commander option is "game ends"; the
        // battleroom button lists Continues first. Position would get this
        // exactly backwards.
        const auto commander = battleroomLabels("commander");
        CHECK(optionStageOfButtonStage("commander", commander, 0) == 1u); // Continues
        CHECK(optionStageOfButtonStage("commander", commander, 1) == 0u); // Game ends
        CHECK(buttonStageOfOptionStage("commander", commander, 0) == 1u);

        // Location: the button lists Random first, the model has fixed at 0.
        const auto location = battleroomLabels("startloc");
        CHECK(optionStageOfButtonStage("startloc", location, 0) == 1u);
        CHECK(optionStageOfButtonStage("startloc", location, 1) == 0u);

        // Line of sight: True|Circular|Permanent against permanent|true|circular.
        const auto los = battleroomLabels("los");
        CHECK(optionStageOfButtonStage("los", los, 0) == 1u);
        CHECK(optionStageOfButtonStage("los", los, 1) == 2u);
        CHECK(optionStageOfButtonStage("los", los, 2) == 0u);

        // Mapping happens to agree.
        const auto mapping = battleroomLabels("mapping");
        CHECK(optionStageOfButtonStage("mapping", mapping, 0) == 0u);
        CHECK(optionStageOfButtonStage("mapping", mapping, 1) == 1u);

        SECTION("a label with no meaning here maps to nothing, and is not offered")
        {
            CHECK(optionStageOfButtonStage("commander", commander, 2) == 2u);
            CHECK(optionStageForLabel("commander", "Deathmatch") == 2u);
            CHECK(usableOptionLabels("commander", commander) == std::vector<std::string>{"Continues", "Game ends", "Deathmatch"});
            // An option the model does not have, and a stage past the buttons.
            CHECK_FALSE(optionStageForLabel("cheating", "Allowed").has_value());
            CHECK_FALSE(optionStageOfButtonStage("mapping", mapping, 9).has_value());
        }

        SECTION("a click on the usable labels flips the commander the right way")
        {
            const auto usable = usableOptionLabels("commander", commander);
            // The caption order is Continues, Game ends, Deathmatch.
            CHECK(nextOptionStage("commander", usable, 0) == 2u);
            CHECK(nextOptionStage("commander", usable, 1) == 0u);
            CHECK(nextOptionStage("commander", usable, 2) == 1u);
        }

        SECTION("a click with the full list includes Deathmatch")
        {
            // Continues (model 1) is the first label, so the next is Game
            // ends; from Game ends the next is Deathmatch.
            CHECK(nextOptionStage("commander", commander, 1) == 0u);
            CHECK(nextOptionStage("commander", commander, 0) == 2u);
        }
    }

    TEST_CASE("the starting resources' slider walks the setup screen's ladder", "[multiplayer][battleroom]")
    {
        CHECK(resourceStopValue(0) == 200u);
        CHECK(resourceStopValue(1) == 500u);
        CHECK(resourceStopValue(2) == 1000u);
        CHECK(resourceStopValue(resourceStopCount - 1) == 10000u);

        for (unsigned int stop = 0; stop < resourceStopCount; ++stop)
        {
            CHECK(resourceStopNearest(static_cast<float>(resourceStopValue(stop))) == stop);
            CHECK(resourceStopAtPercent(resourceStopPercent(stop)) == stop);
        }

        CHECK(resourceStopNearest(0.0f) == 0u);
        CHECK(resourceStopNearest(1e9f) == resourceStopCount - 1);
        CHECK(resourceStopAtPercent(-3.0f) == 0u);
        CHECK(resourceStopAtPercent(7.0f) == resourceStopCount - 1);
    }

    TEST_CASE("which controls of a seat each role may touch", "[multiplayer][battleroom]")
    {
        SECTION("the host chooses for itself and the computers, and opens and removes")
        {
            auto own = seatControls(RoomRole::Host, Type::Human, true);
            CHECK_FALSE(own.name);
            CHECK(own.side);
            CHECK(own.colour);
            CHECK(own.team);
            CHECK_FALSE(own.ready);

            auto computer = seatControls(RoomRole::Host, Type::Computer, false);
            CHECK(computer.name);
            CHECK(computer.side);
            CHECK(computer.colour);
            CHECK(computer.team);
            CHECK_FALSE(computer.ready);

            auto open = seatControls(RoomRole::Host, Type::Open, false);
            CHECK(open.name);
            CHECK_FALSE(open.side);
            CHECK_FALSE(open.colour);
            CHECK_FALSE(open.team);
        }

        SECTION("the host does not choose a guest's side, colour, team or readiness")
        {
            auto guest = seatControls(RoomRole::Host, Type::Network, false);
            CHECK(guest.name); // which removes them
            CHECK_FALSE(guest.side);
            CHECK_FALSE(guest.colour);
            CHECK_FALSE(guest.team);
            CHECK_FALSE(guest.ready);
        }

        SECTION("a guest chooses for itself, says it is ready, and touches nothing else")
        {
            auto own = seatControls(RoomRole::Guest, Type::Human, true);
            CHECK_FALSE(own.name);
            CHECK(own.side);
            CHECK(own.colour);
            CHECK(own.team);
            CHECK(own.ready);

            for (auto type : {Type::Open, Type::Computer, Type::Network})
            {
                auto other = seatControls(RoomRole::Guest, type, false);
                CHECK_FALSE(other.name);
                CHECK_FALSE(other.side);
                CHECK_FALSE(other.colour);
                CHECK_FALSE(other.team);
                CHECK_FALSE(other.ready);
            }
        }

        SECTION("nobody outside a room may touch anything")
        {
            for (auto type : {Type::Open, Type::Human, Type::Computer, Type::Network})
            {
                auto c = seatControls(RoomRole::None, type, true);
                CHECK_FALSE(c.name);
                CHECK_FALSE(c.side);
                CHECK_FALSE(c.ready);
            }
        }
    }

    TEST_CASE("the host cannot start while a person in the room is not ready", "[multiplayer][battleroom]")
    {
        auto model = roomModel();
        std::vector<bool> ready(10, false);

        SECTION("alone, or with computers, there is no network game to start")
        {
            REQUIRE(startBlocker(model, ready, true).has_value());
            CHECK(*startBlocker(model, ready, true) == "Waiting for another player to join");
            model.players[1].type.next(Type::Computer);
            CHECK(*startBlocker(model, ready, true) == "Waiting for another player to join");
        }

        SECTION("computers beside a ready guest are no reason to wait")
        {
            model.players[1].type.next(Type::Network);
            model.players[1].name.next("Ada");
            model.players[2].type.next(Type::Computer);
            ready[1] = true;
            CHECK_FALSE(startBlocker(model, ready, true).has_value());
        }

        SECTION("no map is a game with nothing to start")
        {
            auto blocker = startBlocker(model, ready, false);
            REQUIRE(blocker.has_value());
            CHECK(*blocker == "Select a map");
        }

        SECTION("a person who is not ready is waited for, by name")
        {
            model.players[2].type.next(Type::Network);
            model.players[2].name.next("Ada");

            auto blocker = startBlocker(model, ready, true);
            REQUIRE(blocker.has_value());
            CHECK(*blocker == "Waiting for Ada to be ready");

            ready[2] = true;
            CHECK_FALSE(startBlocker(model, ready, true).has_value());

            // And unreadying closes it again.
            ready[2] = false;
            CHECK(startBlocker(model, ready, true).has_value());
        }

        SECTION("the first person not ready is the one named, and every one must be")
        {
            model.players[1].type.next(Type::Network);
            model.players[1].name.next("Ada");
            model.players[3].type.next(Type::Network);
            model.players[3].name.next("Bo");
            ready[1] = true;

            CHECK(*startBlocker(model, ready, true) == "Waiting for Bo to be ready");
            ready[3] = true;
            CHECK_FALSE(startBlocker(model, ready, true).has_value());
        }

        SECTION("a seat with no name is called by its place")
        {
            model.players[4].type.next(Type::Network);
            CHECK(*startBlocker(model, ready, true) == "Waiting for player 5 to be ready");
        }

        SECTION("a ready flag on an open seat waits for nobody, and a short list is nobody ready")
        {
            model.players[6].type.next(Type::Network);
            model.players[6].name.next("Cy");
            ready[6] = true;
            ready[5] = true;
            CHECK_FALSE(startBlocker(model, ready, true).has_value());
            CHECK(*startBlocker(model, std::vector<bool>{}, true) == "Waiting for Cy to be ready");
        }
    }

    TEST_CASE("a guest's choices are applied by the host, and a colour another seat holds is refused", "[multiplayer][battleroom]")
    {
        auto model = roomModel();
        model.players[1].type.next(Type::Network);
        model.players[1].side.next(Side::Arm);
        model.players[1].colorIndex.next(PlayerColorIndex(1));
        model.players[2].type.next(Type::Computer);
        model.players[2].colorIndex.next(PlayerColorIndex(2));

        SECTION("side and team are taken")
        {
            REQUIRE(applyClaimChoice(model, 1, claimFor("CORE", 1, 3)));
            CHECK(model.players[1].side.getValue() == Side::Core);
            CHECK(model.players[1].teamIndex.getValue() == std::optional<int>(3));
        }

        SECTION("a free colour is taken")
        {
            REQUIRE(applyClaimChoice(model, 1, claimFor("ARM", 7, -1)));
            CHECK(model.players[1].colorIndex.getValue().value == 7u);
        }

        SECTION("the host's colour is refused, and so is a computer's")
        {
            CHECK_FALSE(applyClaimChoice(model, 1, claimFor("ARM", 0, -1)));
            CHECK(model.players[1].colorIndex.getValue().value == 1u);
            CHECK_FALSE(applyClaimChoice(model, 1, claimFor("ARM", 2, -1)));
            CHECK(model.players[1].colorIndex.getValue().value == 1u);
        }

        SECTION("a colour an open seat has is free: nobody is in that seat")
        {
            REQUIRE(applyClaimChoice(model, 1, claimFor("ARM", 5, -1)));
            CHECK(model.players[1].colorIndex.getValue().value == 5u);
        }

        SECTION("a refused colour does not stop the rest of the choice")
        {
            REQUIRE(applyClaimChoice(model, 1, claimFor("CORE", 0, 2)));
            CHECK(model.players[1].colorIndex.getValue().value == 1u);
            CHECK(model.players[1].side.getValue() == Side::Core);
            CHECK(model.players[1].teamIndex.getValue() == std::optional<int>(2));
        }

        SECTION("a colour or team out of range is not taken")
        {
            applyClaimChoice(model, 1, claimFor("ARM", 400, 9));
            CHECK(model.players[1].colorIndex.getValue().value == 1u);
            CHECK_FALSE(model.players[1].teamIndex.getValue().has_value());
        }

        SECTION("the same choice again changes nothing")
        {
            REQUIRE(applyClaimChoice(model, 1, claimFor("CORE", 7, 1)));
            CHECK_FALSE(applyClaimChoice(model, 1, claimFor("CORE", 7, 1)));
        }

        SECTION("a seat coming into the game takes a colour nobody has")
        {
            model.players[3].colorIndex.next(PlayerColorIndex(0)); // the host's, held
            model.players[3].type.next(Type::Computer);
            ensureFreeColour(model, 3);
            CHECK(model.players[3].colorIndex.getValue().value != 0u);
            CHECK_FALSE(colourHeldByAnotherSeat(model, 3, model.players[3].colorIndex.getValue()));

            // One that is free is left alone.
            model.players[4].colorIndex.next(PlayerColorIndex(8));
            model.players[4].type.next(Type::Computer);
            ensureFreeColour(model, 4);
            CHECK(model.players[4].colorIndex.getValue().value == 8u);
        }
    }

    TEST_CASE("a seat's control is named PLAYER, its number and the control", "[multiplayer][battleroom]")
    {
        auto name = parseSeatMessage("PLAYER3", 10);
        REQUIRE(name.has_value());
        CHECK(name->seat == 3u);
        CHECK(name->control.empty());

        auto side = parseSeatMessage("PLAYER7_side", 10);
        REQUIRE(side.has_value());
        CHECK(side->seat == 7u);
        CHECK(side->control == "side");

        CHECK_FALSE(parseSeatMessage("PLAYER10_side", 10).has_value());
        CHECK_FALSE(parseSeatMessage("PLAYER", 10).has_value());
        CHECK_FALSE(parseSeatMessage("PLAYERx", 10).has_value());
        CHECK_FALSE(parseSeatMessage("PLAYER3_", 10).has_value());
        CHECK_FALSE(parseSeatMessage("PLAYER3x", 10).has_value());
        CHECK_FALSE(parseSeatMessage("START", 10).has_value());
        CHECK_FALSE(parseSeatMessage("PLAYER99999", 10).has_value());
    }

    TEST_CASE("the room's ready flags and chat travel in the announcement", "[multiplayer][battleroom]")
    {
        auto model = roomModel();
        model.players[1].type.next(Type::Network);
        model.players[1].name.next("Ada");
        model.players[2].type.next(Type::Network);
        model.players[2].name.next("Bo");
        model.players[3].type.next(Type::Computer);

        LobbyIdentity id = identityFor("0123456789abcdef");
        id.ready = std::vector<bool>(10, false);
        id.ready[2] = true;
        for (unsigned int n = 1; n <= 12; ++n)
        {
            id.chat.push_back(DiscoveredChatLine{n, "Ada", "line " + std::to_string(n)});
        }

        auto tokens = noTokens();
        const auto lobby = lobbyFromModel(model, id, "Coast To Coast", tokens);

        CHECK(lobby.slots[0].ready); // the host
        CHECK_FALSE(lobby.slots[1].ready);
        CHECK(lobby.slots[2].ready);
        CHECK(lobby.slots[3].ready);       // a computer
        CHECK_FALSE(lobby.slots[4].ready); // open

        // Only the newest lines an announcement can carry.
        REQUIRE(lobby.chat.size() == LanDiscovery::maxChatLines);
        CHECK(lobby.chat.front().sequence == 5u);
        CHECK(lobby.chat.back().sequence == 12u);

        auto text = LanDiscovery::encode(lobby);
        REQUIRE(text.has_value());
        auto back = LanDiscovery::decode(*text);
        REQUIRE(back.has_value());
        CHECK(back->slots[2].ready);
        CHECK_FALSE(back->slots[1].ready);
        CHECK(back->chat.size() == lobby.chat.size());
    }

    TEST_CASE("the setup a guest is ready for is the map, the rules and the seat table (#719)", "[multiplayer][battleroom]")
    {
        auto model = hostModel();
        model.players[1].type.next(Type::Network);
        model.players[1].name.next("Ada");
        const auto id = identityFor("0123456789abcdef");
        const auto base = setupFingerprint(lobbyFromModel(model, id, "Coast To Coast", noTokens()));

        SECTION("ready flags, chat, names, tokens and the launching flag are not the setup")
        {
            auto changed = id;
            changed.ready = std::vector<bool>(10, true);
            changed.chat.push_back(DiscoveredChatLine{1, "Ada", "hello"});
            changed.launching = true;
            auto tokens = noTokens();
            tokens[1] = "deadbeef";
            model.players[1].name.next("Someone else");
            CHECK(setupFingerprint(lobbyFromModel(model, changed, "Coast To Coast", tokens)) == base);
        }

        SECTION("another map is another setup")
        {
            CHECK(setupFingerprint(lobbyFromModel(model, id, "Great Divide", noTokens())) != base);
        }

        SECTION("a seat opening, closing or being taken is another setup")
        {
            model.players[2].type.next(Type::Computer);
            CHECK(setupFingerprint(lobbyFromModel(model, id, "Coast To Coast", noTokens())) != base);
        }

        SECTION("a colour or a team moving is another setup")
        {
            model.players[1].colorIndex.next(PlayerColorIndex(7));
            const auto recoloured = setupFingerprint(lobbyFromModel(model, id, "Coast To Coast", noTokens()));
            CHECK(recoloured != base);
            model.players[1].teamIndex.next(3);
            CHECK(setupFingerprint(lobbyFromModel(model, id, "Coast To Coast", noTokens())) != recoloured);
        }

        SECTION("a computer seat's personality is a rule")
        {
            model.players[2].type.next(Type::Computer);
            model.players[2].personality.next("Original TA");
            const auto first = setupFingerprint(lobbyFromModel(model, id, "Coast To Coast", noTokens()));
            model.players[2].personality.next("Balanced");
            CHECK(setupFingerprint(lobbyFromModel(model, id, "Coast To Coast", noTokens())) != first);
        }
    }

    TEST_CASE("a claim says which version of the setup its ready is for (#719)", "[multiplayer][battleroom]")
    {
        auto model = hostModel();
        auto id = identityFor("0123456789abcdef");
        id.setupEpoch = 5;
        id.mapIdentity = "1f4-2a00-0123456789abcdef";
        auto lobby = lobbyFromModel(model, id, "Coast To Coast", noTokens());
        CHECK(lobby.setupEpoch == 5u);
        CHECK(lobby.mapIdentity == "1f4-2a00-0123456789abcdef");

        GuestChoice choice{Side::Arm, PlayerColorIndex(1), std::optional<int>(), true};
        auto claim = claimAnnouncement(lobby, 1, "deadbeef", "Guest", 1400, choice);
        claim.sourceAddress = "192.168.0.9:1338";
        auto text = LanDiscovery::encode(claim);
        REQUIRE(text.has_value());
        auto heard = LanDiscovery::decode(*text);
        REQUIRE(heard.has_value());
        heard->sourceAddress = claim.sourceAddress;

        auto claims = claimsOn({lobby, *heard}, lobby.session);
        REQUIRE(claims.size() == 1);
        CHECK(claims[0].ready);
        CHECK(claims[0].setupEpoch == 5u);
    }

    TEST_CASE("a guest's choices and lines ride its claim", "[multiplayer][battleroom]")
    {
        auto host = roomModel();
        auto lobby = lobbyFromModel(host, identityFor("0123456789abcdef"), "Coast To Coast", noTokens());

        GuestChoice choice{Side::Core, PlayerColorIndex(6), std::optional<int>(2), true};
        std::vector<DiscoveredChatLine> said{{1, "Guest", "hello"}, {2, "Guest", "anyone there"}};
        auto claim = claimAnnouncement(lobby, 1, "deadbeef", "Guest", 1400, choice, said);
        claim.sourceAddress = "192.168.0.9:1338";

        // Over the wire and back, as the host hears it.
        auto text = LanDiscovery::encode(claim);
        REQUIRE(text.has_value());
        auto heard = LanDiscovery::decode(*text);
        REQUIRE(heard.has_value());
        heard->sourceAddress = claim.sourceAddress;

        auto claims = claimsOn({lobby, *heard}, lobby.session);
        REQUIRE(claims.size() == 1);
        CHECK(claims[0].side == "CORE");
        CHECK(claims[0].colour == 6u);
        CHECK(claims[0].team == 2);
        CHECK(claims[0].ready);
        CHECK_FALSE(claims[0].approvalRequired);
        REQUIRE(claims[0].chat.size() == 2);
        CHECK(claims[0].chat[1].text == "anyone there");

        SECTION("a claim with no choice says what the host's seat has")
        {
            auto plain = claimAnnouncement(lobby, 1, "deadbeef", "Guest", 1400);
            REQUIRE(plain.slots.size() == 1);
            CHECK(plain.slots[0].side == lobby.slots[1].side);
            CHECK(plain.slots[0].colour == lobby.slots[1].colour);
            CHECK_FALSE(plain.slots[0].ready);
            CHECK(plain.chat.empty());
        }
        SECTION("an approval lobby's guest keeps its name and approval requirement on the wire")
        {
            auto identity = identityFor("0123456789abcdef");
            identity.approvalRequired = true;
            lobby = lobbyFromModel(host, identity, "Coast To Coast", noTokens());
            REQUIRE(lobby.approvalRequired);
            auto request = claimAnnouncement(lobby, 1, "deadbeef", "Guest Name", 1400);
            request.discoveryAddress = "203.0.113.42:2463";
            auto text = LanDiscovery::encode(request);
            REQUIRE(text);
            auto heard = LanDiscovery::decode(*text);
            REQUIRE(heard);
            heard->sourceAddress = "192.168.0.2:1363";
            auto requests = claimsOn({*heard}, lobby.session);
            REQUIRE(requests.size() == 1);
            REQUIRE(requests.front().name == "Guest Name");
            REQUIRE(requests.front().approvalRequired);
            REQUIRE(requests.front().discoveryAddress == request.discoveryAddress);
            REQUIRE(requests.front().sourceAddress == heard->sourceAddress);
        }
    }

    TEST_CASE("chat: what can be said, and how the transcript is kept", "[multiplayer][battleroom]")
    {
        SECTION("control characters are dropped and a line of nothing is not a line")
        {
            CHECK(sanitisedChatText("  hello\tthere\x01 \n") == "hellothere");
            CHECK(sanitisedChatText(std::string("a\x7f") + "b") == "ab");
            CHECK(sanitisedChatText("   ").empty());
            CHECK(sanitisedChatText("\x01\x02").empty());
            CHECK(sanitisedChatText("").empty());
        }

        SECTION("a line is bounded to what the wire carries")
        {
            const auto cut = sanitisedChatText(std::string(500, 'x'));
            CHECK(cut.size() == LanDiscovery::maxChatTextLength);
            // Not cut through a multi-byte character.
            const auto accented = sanitisedChatText(std::string(LanDiscovery::maxChatTextLength - 1, 'x') + "\xC3\xA9");
            CHECK(accented.size() == LanDiscovery::maxChatTextLength - 1);
        }

        SECTION("the host numbers lines in order, and an announcement carries the newest")
        {
            ChatTranscript room;
            for (unsigned int n = 1; n <= 12; ++n)
            {
                REQUIRE(room.say("Hector", "line " + std::to_string(n)));
            }
            CHECK_FALSE(room.say("Hector", "   "));
            CHECK(room.lastSequence == 12u);
            CHECK(room.lines.front().sequence == 1u);

            auto tail = room.tail();
            REQUIRE(tail.size() == LanDiscovery::maxChatLines);
            CHECK(tail.front().sequence == 5u);
            CHECK(tail.back().text == "line 12");
        }

        SECTION("a listener merges what is new, once, in order")
        {
            ChatTranscript room;
            for (unsigned int n = 1; n <= 3; ++n)
            {
                room.say("Hector", "line " + std::to_string(n));
            }

            ChatTranscript guest;
            CHECK(guest.merge(room.tail()) == 3u);
            // The same announcement again is nothing new.
            CHECK(guest.merge(room.tail()) == 0u);
            CHECK(guest.lines.size() == 3u);

            room.say("Ada", "line 4");
            CHECK(guest.merge(room.tail()) == 1u);
            REQUIRE(guest.lines.size() == 4u);
            CHECK(guest.lines.back().name == "Ada");

            // Same lines, same order, as the host's.
            for (std::size_t i = 0; i < 4; ++i)
            {
                CHECK(guest.lines[i].sequence == room.lines[i].sequence);
                CHECK(guest.lines[i].text == room.lines[i].text);
            }
        }

        SECTION("a sequence that goes back, or repeats, is ignored")
        {
            ChatTranscript guest;
            guest.merge({{5, "A", "five"}, {3, "A", "three"}, {5, "A", "five again"}, {6, "A", "six"}});
            REQUIRE(guest.lines.size() == 2u);
            CHECK(guest.lines[0].text == "five");
            CHECK(guest.lines[1].text == "six");
        }

        SECTION("the transcript is bounded")
        {
            ChatTranscript room;
            for (std::size_t n = 0; n < ChatTranscript::maxKept + 50; ++n)
            {
                room.say("Hector", "line " + std::to_string(n));
            }
            CHECK(room.lines.size() == ChatTranscript::maxKept);
            CHECK(room.lines.back().sequence == room.lastSequence);

            ChatTranscript guest;
            for (unsigned int n = 1; n <= ChatTranscript::maxKept + 50; ++n)
            {
                guest.merge({{n, "Hector", "x"}});
            }
            CHECK(guest.lines.size() == ChatTranscript::maxKept);
        }

        SECTION("a line is shown with its speaker, and a line from the room without")
        {
            CHECK(chatLineText({1, "Ada", "hi"}) == "Ada: hi");
            CHECK(chatLineText({2, "", "Ada has joined"}) == "Ada has joined");
        }
    }

    TEST_CASE("the host takes each of a guest's lines once, as that guest", "[multiplayer][battleroom]")
    {
        ChatTranscript room;
        unsigned int taken = 0;

        // The claim carries the last few lines and repeats them every second.
        std::vector<DiscoveredChatLine> claimed{{1, "Mallory", "one"}, {2, "Mallory", "two"}};

        CHECK(takeClaimChat(room, taken, "Ada", claimed) == 2u);
        REQUIRE(room.lines.size() == 2u);
        // The speaker is the seat's, not whatever the claim put on the line.
        CHECK(room.lines[0].name == "Ada");
        CHECK(taken == 2u);

        // The same claim heard again says nothing new.
        CHECK(takeClaimChat(room, taken, "Ada", claimed) == 0u);
        CHECK(room.lines.size() == 2u);

        // A new line is taken, and the old ones in the claim are not.
        claimed.push_back({3, "Ada", "three"});
        CHECK(takeClaimChat(room, taken, "Ada", claimed) == 1u);
        CHECK(room.lines.back().text == "three");

        SECTION("out of order in the claim, said in order")
        {
            ChatTranscript other;
            unsigned int t = 0;
            takeClaimChat(other, t, "Bo", {{2, "Bo", "second"}, {1, "Bo", "first"}});
            REQUIRE(other.lines.size() == 2u);
            CHECK(other.lines[0].text == "first");
            CHECK(other.lines[1].text == "second");
        }

        SECTION("a line that is nothing once cleaned is not said, and is not tried again")
        {
            ChatTranscript other;
            unsigned int t = 0;
            CHECK(takeClaimChat(other, t, "Bo", {{1, "Bo", "\x01\x02"}}) == 0u);
            CHECK(t == 1u);
            CHECK(other.lines.empty());
        }

        SECTION("sequence zero is nobody's line")
        {
            ChatTranscript other;
            unsigned int t = 0;
            CHECK(takeClaimChat(other, t, "Bo", {{0, "Bo", "zero"}}) == 0u);
        }

        SECTION("two guests' sequences are their own")
        {
            ChatTranscript other;
            unsigned int adaTaken = 0;
            unsigned int boTaken = 0;
            takeClaimChat(other, adaTaken, "Ada", {{1, "Ada", "from ada"}});
            takeClaimChat(other, boTaken, "Bo", {{1, "Bo", "from bo"}});
            CHECK(other.lines.size() == 2u);
        }
    }

    TEST_CASE("a chat line is wrapped to the width of the box", "[multiplayer][battleroom]")
    {
        // Ten units a character: easy to count and independent of any font.
        const auto width = [](const std::string& text) { return static_cast<float>(text.size()) * 10.0f; };

        SECTION("a short line is one row")
        {
            auto rows = wrapChatLine("Ada: hi", 200.0f, width);
            REQUIRE(rows.size() == 1u);
            CHECK(rows[0] == "Ada: hi");
        }

        SECTION("a long line breaks at spaces, and the continuation is indented")
        {
            auto rows = wrapChatLine("Ada: one two three four five", 100.0f, width);
            REQUIRE(rows.size() > 1u);
            for (const auto& row : rows)
            {
                CHECK(width(row) <= 100.0f);
            }
            CHECK(rows[0].substr(0, 2) != "  ");
            CHECK(rows[1].substr(0, 2) == "  ");
            // Nothing lost.
            std::string joined;
            for (const auto& row : rows)
            {
                joined += row + " ";
            }
            for (const char* word : {"Ada:", "one", "two", "three", "four", "five"})
            {
                CHECK(joined.find(word) != std::string::npos);
            }
        }

        SECTION("a word wider than the box is broken, and nothing is lost")
        {
            auto rows = wrapChatLine(std::string(45, 'w'), 100.0f, width);
            std::size_t letters = 0;
            for (const auto& row : rows)
            {
                CHECK(width(row) <= 100.0f);
                letters += static_cast<std::size_t>(std::count(row.begin(), row.end(), 'w'));
            }
            CHECK(letters == 45u);
        }

        SECTION("an empty line has no rows, and a box too narrow for anything still ends")
        {
            CHECK(wrapChatLine("", 100.0f, width).empty());
            auto rows = wrapChatLine("abcdef", 5.0f, width);
            CHECK_FALSE(rows.empty());
        }

        SECTION("the longest line that can be typed fits in a few rows")
        {
            const std::string longest = std::string(20, 'N') + ": " + std::string(LanDiscovery::maxChatTextLength, 'x');
            auto rows = wrapChatLine(longest, 400.0f, width);
            CHECK(rows.size() <= 4u);
        }
    }
}
