#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <rwe/LoadingNetworkService.h>
#include <thread>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        template <typename Pred>
        bool waitUntil(Pred pred, std::chrono::milliseconds limit = std::chrono::milliseconds(4000))
        {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline)
            {
                if (pred())
                {
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return pred();
        }
    }

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

    TEST_CASE("LoadingNetworkService follows a peer that is sending from somewhere it was not told")
    {
        constexpr unsigned short portA = 1421;
        constexpr unsigned short portB = 1422;
        constexpr unsigned short wrongPort = 1499;

        SECTION("a status that names its seat, from the right address on another port, is followed")
        {
            LoadingNetworkService a;
            a.setLocalSlot(0);
            a.addEndpoint(1, "127.0.0.1", std::to_string(wrongPort));
            LoadingNetworkService b;
            b.setLocalSlot(1);
            b.addEndpoint(0, "127.0.0.1", std::to_string(portA));
            a.start(std::to_string(portA));
            b.start(std::to_string(portB));
            b.setDoneLoading();

            REQUIRE(waitUntil([&]() { return a.areAllClientsReady(); }));
            // Followed, so that the game's own transport is seeded with the
            // address that works.
            CHECK(a.getEndpoint(1).port() == portB);
        }

        SECTION("a status from the address it was told still works as it did")
        {
            LoadingNetworkService a;
            a.addEndpoint(1, "127.0.0.1", std::to_string(portB));
            LoadingNetworkService b;
            b.addEndpoint(0, "127.0.0.1", std::to_string(portA));
            a.start(std::to_string(portA));
            b.start(std::to_string(portB));
            b.setDoneLoading();

            REQUIRE(waitUntil([&]() { return a.areAllClientsReady(); }));
            CHECK(a.getEndpoint(1).port() == portB);
        }

        SECTION("a sender that does not say which seat it is is not followed")
        {
            // A build from before the field existed, or a stranger.
            LoadingNetworkService a;
            a.addEndpoint(1, "127.0.0.1", std::to_string(wrongPort));
            LoadingNetworkService b;
            b.addEndpoint(0, "127.0.0.1", std::to_string(portA));
            a.start(std::to_string(portA));
            b.start(std::to_string(portB));
            b.setDoneLoading();

            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            CHECK_FALSE(a.areAllClientsReady());
            CHECK(a.getEndpoint(1).port() == wrongPort);
        }

        SECTION("a different address is not followed, whatever it says")
        {
            LoadingNetworkService a;
            a.addEndpoint(1, "192.0.2.7", std::to_string(wrongPort));
            LoadingNetworkService b;
            b.setLocalSlot(1);
            b.addEndpoint(0, "127.0.0.1", std::to_string(portA));
            a.start(std::to_string(portA));
            b.start(std::to_string(portB));
            b.setDoneLoading();

            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            CHECK_FALSE(a.areAllClientsReady());
            CHECK(a.getEndpoint(1).port() == wrongPort);
        }

        SECTION("a neighbouring address of the same carrier block is followed")
        {
            // 127.0.0.2 was the address given and the datagram comes from
            // 127.0.0.1: what 92.40.168.165 and .167 are on a carrier's NAT,
            // where one machine's two sockets leave through two addresses.
            LoadingNetworkService a;
            a.addEndpoint(1, "127.0.0.2", std::to_string(wrongPort));
            LoadingNetworkService b;
            b.setLocalSlot(1);
            b.addEndpoint(0, "127.0.0.1", std::to_string(portA));
            a.start(std::to_string(portA));
            b.start(std::to_string(portB));
            b.setDoneLoading();

            REQUIRE(waitUntil([&]() { return a.areAllClientsReady(); }));
            CHECK(a.getEndpoint(1).port() == portB);
        }

        SECTION("a seat that is not the one expected is not followed")
        {
            LoadingNetworkService a;
            a.addEndpoint(1, "127.0.0.1", std::to_string(wrongPort));
            LoadingNetworkService b;
            b.setLocalSlot(3);
            b.addEndpoint(0, "127.0.0.1", std::to_string(portA));
            a.start(std::to_string(portA));
            b.start(std::to_string(portB));
            b.setDoneLoading();

            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            CHECK_FALSE(a.areAllClientsReady());
            CHECK(a.getEndpoint(1).port() == wrongPort);
        }
    }
}
