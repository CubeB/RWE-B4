#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingNetworkService.h>
#include <stdexcept>

namespace rwe
{
    TEST_CASE("LoadingNetworkService waits for peers, but not for ever")
    {
        SECTION("a peer that never reports ready is named, not waited on")
        {
            // The failure this covers is a silent one: a peer that has gone
            // away never sends anything at all, so an unbounded wait is not
            // slow, it is permanent. The screen stays black and the log keeps
            // saying "we are ready" into nothing.
            LoadingNetworkService service;
            service.addEndpoint(1, "127.0.0.1", "1338");
            service.loadingTimeout = std::chrono::milliseconds(1);

            CHECK_THROWS_AS(service.waitForAllToBeReady(), std::runtime_error);

            auto missing = service.peersNotReady();
            REQUIRE(missing.size() == 1);
            // One-based, because that is how the setup screen numbers seats
            // and how the player is told which one is absent.
            CHECK(missing[0].find("player 2") != std::string::npos);
            CHECK(missing[0].find("127.0.0.1:1338") != std::string::npos);
        }

        SECTION("every missing peer is named, not just the first")
        {
            LoadingNetworkService service;
            service.addEndpoint(0, "127.0.0.1", "1338");
            service.addEndpoint(2, "127.0.0.1", "1339");
            service.loadingTimeout = std::chrono::milliseconds(1);

            CHECK_THROWS_AS(service.waitForAllToBeReady(), std::runtime_error);
            CHECK(service.peersNotReady().size() == 2);
        }

        SECTION("a single-player game waits for nobody")
        {
            // There is nothing to agree a start with, so there is nothing to
            // wait for. This must not be the thing that times out.
            LoadingNetworkService service;
            service.loadingTimeout = std::chrono::milliseconds(1);
            CHECK_NOTHROW(service.waitForAllToBeReady());
            CHECK(service.peersNotReady().empty());
        }

        SECTION("the timeout is long enough to be worth having")
        {
            // Two minutes. The wait is for the other machine to finish
            // loading a map, not for a network round trip, so anything close
            // to the in-game --drop-timeout of ten seconds would fail games
            // on a slow disk that are perfectly healthy.
            CHECK(LoadingNetworkService::defaultLoadingTimeout.count() > 30000);
        }
    }
}