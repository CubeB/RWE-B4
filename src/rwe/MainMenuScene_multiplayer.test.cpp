#include <catch2/catch_test_macros.hpp>
#include <rwe/MainMenuScene_multiplayer.h>

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

    TEST_CASE("the option list is written and read by one table", "[multiplayer]")
    {
        SECTION("every option the screen has round-trips")
        {
            MainMenuModel host;
            host.skirmishOptions.commanderDeath.next(1u);
            host.skirmishOptions.startLocation.next(1u);
            host.skirmishOptions.mapping.next(1u);
            host.skirmishOptions.lineOfSight.next(2u);
            host.skirmishOptions.difficulty.next(0u);

            MainMenuModel guest;
            applyOptionsToModel(guest, optionsFromModel(host));

            CHECK(guest.skirmishOptions.commanderDeath.getValue() == 1u);
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
            CHECK(options.size() == 5);
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
