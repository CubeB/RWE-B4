#include <catch2/catch_test_macros.hpp>
#include <rwe/game/PlayerCommandService.h>
#include <vector>

/**
 * The order a tick's commands come out of the service in is the order they
 * are applied in, so it has to be the same on every peer whatever the
 * container holding them does (issue #349).
 */
namespace rwe
{
    namespace
    {
        std::vector<PlayerId> poppedOrder(const std::vector<unsigned int>& registrationOrder)
        {
            PlayerCommandService service;
            for (auto id : registrationOrder)
            {
                service.registerPlayer(PlayerId(id));
            }
            for (auto id : registrationOrder)
            {
                service.pushCommands(PlayerId(id), {});
            }

            auto popped = service.tryPopCommands();
            REQUIRE(popped.has_value());
            std::vector<PlayerId> order;
            for (const auto& [player, commands] : *popped)
            {
                order.push_back(player);
            }
            return order;
        }
    }

    TEST_CASE("a tick's commands come out highest player first, however the players were registered", "[network]")
    {
        std::vector<PlayerId> expected{PlayerId(3), PlayerId(2), PlayerId(1), PlayerId(0)};

        REQUIRE(poppedOrder({0, 1, 2, 3}) == expected);
        REQUIRE(poppedOrder({3, 2, 1, 0}) == expected);
        REQUIRE(poppedOrder({2, 0, 3, 1}) == expected);
    }

    TEST_CASE("a dropped player's empty set takes its place in the order", "[network]")
    {
        PlayerCommandService service;
        for (auto id : {0u, 1u, 2u})
        {
            service.registerPlayer(PlayerId(id));
        }
        service.dropPlayer(PlayerId(1), 1);
        service.pushCommands(PlayerId(0), {});
        service.pushCommands(PlayerId(2), {});

        auto popped = service.tryPopCommands();
        REQUIRE(popped.has_value());
        REQUIRE(popped->size() == 3u);
        REQUIRE((*popped)[0].first == PlayerId(2));
        REQUIRE((*popped)[1].first == PlayerId(1));
        REQUIRE((*popped)[1].second.empty());
        REQUIRE((*popped)[2].first == PlayerId(0));
    }
}
