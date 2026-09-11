#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <rwe/util/range_util.h>
#include <vector>

namespace rwe
{
    namespace
    {
        struct Peer
        {
            std::optional<int> lastKnown;
        };
    }

    TEST_CASE("choose: the view outlives the call and reads the caller's container", "[range_util]")
    {
        // The view must refer to `peers` itself, not to a copy made for the
        // call. A copy would be gone by the time the loop below runs, which
        // is exactly how GameNetworkService::estimateAvergeSceneTime read
        // freed stack on Linux.
        std::vector<Peer> peers{{std::nullopt}, {3}, {std::nullopt}, {5}, {8}};
        auto chosen = choose(peers, [](const auto& p) { return p.lastKnown; });

        std::vector<int> seen;
        for (auto v : chosen)
        {
            seen.push_back(v);
        }
        REQUIRE(seen == std::vector<int>{3, 5, 8});
    }

    TEST_CASE("choose: a temporary range is owned by the view", "[range_util]")
    {
        auto chosen = choose(std::vector<std::optional<int>>{{2}, {std::nullopt}, {4}}, [](const auto& o) { return o; });
        std::vector<int> seen;
        for (auto v : chosen)
        {
            seen.push_back(v);
        }
        REQUIRE(seen == std::vector<int>{2, 4});
    }

    TEST_CASE("choose: nothing engaged means an empty view, not a fault", "[range_util]")
    {
        std::vector<Peer> peers{{std::nullopt}, {std::nullopt}};
        auto chosen = choose(peers, [](const auto& p) { return p.lastKnown; });
        REQUIRE(chosen.begin() == chosen.end());
    }
}
