#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <numeric>
#include <rwe/io/gaf/GafArchive.h>

namespace rwe
{
    TEST_CASE("shipped Tree1 burn death and reclaim metadata yield their update lengths", "[gaf][burning]")
    {
        // totala1.hpi anims/TREES.GAF: BT-255/257 and the independent shipped
        // inspection in docs/agents/681-burning-implementation-plan.md.
        struct Timing
        {
            unsigned int frames;
            unsigned int countdown;
            unsigned int updates;
        };
        for (const auto& timing : {Timing{96, 2, 192}, Timing{20, 3, 60}, Timing{15, 2, 30}})
        {
            GafArchive::Entry entry{};
            entry.frameOffsets.assign(timing.frames, GafFrameEntry{0, timing.countdown});
            const auto durations = entry.frameDurationTicks();
            REQUIRE(durations.size() == timing.frames);
            REQUIRE(std::all_of(durations.begin(), durations.end(), [&](unsigned int t) { return t == timing.countdown; }));
            REQUIRE(std::accumulate(durations.begin(), durations.end(), 0u) == timing.updates);
        }
    }

    TEST_CASE("GAF timing uses its low word and spends at least one update per frame", "[gaf][burning]")
    {
        // BT-255's documented wire-word boundary rules, not invented game data.
        GafArchive::Entry entry{};
        entry.frameOffsets = {{0, 0}, {0, 1}, {0, 0xabcd0002u}, {0, 0x1234ffffu}};
        const auto durations = entry.frameDurationTicks();
        const std::vector<unsigned int> expected{1, 1, 2, 65535};
        REQUIRE(durations == expected);
        entry.frameOffsets.clear();
        REQUIRE(entry.frameDurationTicks().empty());
    }
}
