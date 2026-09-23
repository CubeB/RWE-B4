#include <catch2/catch_test_macros.hpp>
#include <rwe/game/DesyncReport.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/sim_test_util.h>

namespace rwe
{
    namespace
    {
        /**
         * Registers the given players, all of them reporting a sync hash.
         *
         * Takes the service rather than returning one: it holds a mutex, so it
         * neither copies nor moves.
         */
        void addHashSources(PlayerCommandService& service, unsigned int playerCount)
        {
            for (unsigned int i = 0; i < playerCount; ++i)
            {
                service.registerPlayer(PlayerId(i));
                service.registerHashSource(PlayerId(i));
            }
        }

        /** One tick's worth of hashes, one per player, in player order. */
        void pushRound(PlayerCommandService& service, const std::vector<uint32_t>& hashes)
        {
            for (std::size_t i = 0; i < hashes.size(); ++i)
            {
                service.pushHash(PlayerId(static_cast<unsigned int>(i)), GameHash(hashes[i]));
            }
        }
    }

    TEST_CASE("PlayerCommandService::checkHashes")
    {
        SECTION("says nothing while the peers agree")
        {
            PlayerCommandService service;
            addHashSources(service, 2);
            pushRound(service, {7, 7});
            pushRound(service, {9, 9});

            REQUIRE(!service.checkHashes());
        }

        SECTION("says nothing while a peer's hashes have yet to arrive")
        {
            PlayerCommandService service;
            addHashSources(service, 2);

            // Player 1 has sent nothing at all, which is the ordinary state of
            // affairs for a round trip's worth of ticks after every one of
            // ours. It is not evidence of anything.
            service.pushHash(PlayerId(0), GameHash(7));
            service.pushHash(PlayerId(0), GameHash(9));

            REQUIRE(!service.checkHashes());
        }

        SECTION("names the tick the peers first disagreed on")
        {
            PlayerCommandService service;
            addHashSources(service, 2);
            pushRound(service, {7, 7});
            pushRound(service, {9, 9});
            pushRound(service, {11, 12});

            auto report = service.checkHashes();
            REQUIRE(report);

            // The first hash a peer submits is for the state at the end of
            // tick 1, so the third round is tick 3.
            REQUIRE(report->tick == SceneTime(3));
            REQUIRE(report->hashes == std::vector<std::pair<PlayerId, GameHash>>{
                                          {PlayerId(0), GameHash(11)},
                                          {PlayerId(1), GameHash(12)}});
        }

        SECTION("names the FIRST divergent tick, not the last one buffered")
        {
            PlayerCommandService service;
            addHashSources(service, 2);
            pushRound(service, {7, 7});
            pushRound(service, {11, 12});
            pushRound(service, {20, 30});
            pushRound(service, {40, 50});

            auto report = service.checkHashes();
            REQUIRE(report);
            REQUIRE(report->tick == SceneTime(2));
            REQUIRE(report->hashes.at(1).second == GameHash(12));
        }

        SECTION("carries on counting ticks across calls")
        {
            PlayerCommandService service;
            addHashSources(service, 2);
            pushRound(service, {7, 7});
            REQUIRE(!service.checkHashes());

            pushRound(service, {9, 9});
            REQUIRE(!service.checkHashes());

            pushRound(service, {11, 12});
            auto report = service.checkHashes();
            REQUIRE(report);
            REQUIRE(report->tick == SceneTime(3));
        }

        SECTION("reports every peer's hash, not just the two that differ")
        {
            PlayerCommandService service;
            addHashSources(service, 3);
            pushRound(service, {7, 7, 7});
            pushRound(service, {9, 9, 4});

            auto report = service.checkHashes();
            REQUIRE(report);
            REQUIRE(report->tick == SceneTime(2));
            REQUIRE(report->hashes == std::vector<std::pair<PlayerId, GameHash>>{
                                          {PlayerId(0), GameHash(9)},
                                          {PlayerId(1), GameHash(9)},
                                          {PlayerId(2), GameHash(4)}});
        }

        SECTION("hands back the same report however often it is asked")
        {
            PlayerCommandService service;
            addHashSources(service, 2);
            pushRound(service, {7, 7});
            pushRound(service, {11, 12});

            auto first = service.checkHashes();
            REQUIRE(first);

            // Comparing consumes the hashes, so a report that was not kept
            // could not be given twice -- and a desync is walked more than
            // once on the way out of a game.
            pushRound(service, {1, 1});
            auto second = service.checkHashes();
            REQUIRE(second);
            REQUIRE(second->tick == first->tick);
            REQUIRE(second->hashes == first->hashes);
        }

        SECTION("is not stalled by a player who reports no hash of their own")
        {
            // A computer player has commands and no sync hash. It is
            // registered as a player and not as a hash source, and the
            // comparison between the two peers who do report one carries on
            // without it. Registering it as a hash source instead would leave
            // an empty buffer nobody ever fills, which stops the comparison
            // for everybody -- the state desync detection was quietly in for
            // every game with an AI in it.
            PlayerCommandService service;
            service.registerPlayer(PlayerId(0));
            service.registerHashSource(PlayerId(0));
            service.registerPlayer(PlayerId(1));
            service.registerHashSource(PlayerId(1));
            service.registerPlayer(PlayerId(2));

            pushRound(service, {7, 7});
            pushRound(service, {11, 12});

            auto report = service.checkHashes();
            REQUIRE(report);
            REQUIRE(report->tick == SceneTime(2));
            REQUIRE(report->hashes.size() == 2);
        }

        SECTION("a lone peer can never disagree with anybody")
        {
            PlayerCommandService service;
            addHashSources(service, 1);
            for (uint32_t i = 0; i < 10; ++i)
            {
                service.pushHash(PlayerId(0), GameHash(i));
            }

            REQUIRE(!service.checkHashes());
        }
    }

    TEST_CASE("desyncDumpPath")
    {
        SECTION("names the tick and this peer, so two peers' dumps pair up")
        {
            DesyncReport report{SceneTime(4281), {{PlayerId(0), GameHash(1)}, {PlayerId(1), GameHash(2)}}};

            auto zero = desyncDumpPath("out", report, PlayerId(0));
            auto one = desyncDumpPath("out", report, PlayerId(1));

            REQUIRE(zero.filename().string() == "rwe-desync-tick4281-player0.json");
            REQUIRE(one.filename().string() == "rwe-desync-tick4281-player1.json");
        }
    }

    TEST_CASE("describeDesync")
    {
        DesyncReport report{SceneTime(4281), {{PlayerId(0), GameHash(0x1a2b3c4d)}, {PlayerId(1), GameHash(0xff)}}};

        SECTION("names the divergent tick, and separately when it was noticed")
        {
            auto text = describeDesync(report, PlayerId(1), SceneTime(4305), std::nullopt);

            REQUIRE(text.find("diverged from the other players at tick 4281") != std::string::npos);
            REQUIRE(text.find("noticed at tick 4305") != std::string::npos);
        }

        SECTION("writes every peer's hash at its full width, and says which is ours")
        {
            auto text = describeDesync(report, PlayerId(1), SceneTime(4305), std::nullopt);

            REQUIRE(text.find("player 0  0x1a2b3c4d\n") != std::string::npos);
            REQUIRE(text.find("player 1  0x000000ff  (this peer)") != std::string::npos);
        }

        SECTION("names the dump when there is one")
        {
            auto text = describeDesync(report, PlayerId(0), SceneTime(4305), std::filesystem::path("out/rwe-desync-tick4281-player0.json"));

            REQUIRE(text.find("rwe-desync-tick4281-player0.json") != std::string::npos);
        }

        SECTION("still reports the desync when the dump could not be written")
        {
            auto text = describeDesync(report, PlayerId(0), SceneTime(4305), std::nullopt);

            REQUIRE(text.find("tick 4281") != std::string::npos);
            REQUIRE(text.find("could not be written") != std::string::npos);
        }
    }

    TEST_CASE("desyncDumpJson")
    {
        GameSimulation sim(makeFlatTerrain(32, 32), 0u, 100, 3000);
        addPlayer(sim);
        addPlayer(sim);

        DesyncReport report{SceneTime(4281), {{PlayerId(0), GameHash(11)}, {PlayerId(1), GameHash(12)}}};
        auto json = desyncDumpJson(report, PlayerId(1), SceneTime(4305), sim);

        SECTION("carries what disagreed")
        {
            REQUIRE(json["desync"]["firstDivergentTick"] == 4281);
            REQUIRE(json["desync"]["detectedAtTick"] == 4305);
            REQUIRE(json["desync"]["localPlayer"] == 1);
            REQUIRE(json["desync"]["hashes"].size() == 2);
            REQUIRE(json["desync"]["hashes"][0]["player"] == 0);
            REQUIRE(json["desync"]["hashes"][0]["hash"] == 11);
            REQUIRE(json["desync"]["hashes"][1]["hash"] == 12);
        }

        SECTION("carries the state beside it, as dump_util writes it")
        {
            REQUIRE(json["state"] == dumpJson(sim));
        }
    }
}
