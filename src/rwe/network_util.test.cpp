#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rwe/network_util.h>
#include <utility>
#include <vector>

namespace rwe
{
    TEST_CASE("ema")
    {
        SECTION("combines value with average according to weight")
        {
            REQUIRE(ema(1.0f, 8.0f, 0.5f) == 4.5f);
            REQUIRE(ema(1.0f, 8.0f, 0.25f) == 6.25f);
        }
    }

    TEST_CASE("readInt")
    {
        rc::prop("readInt inverts writeInt", [](unsigned int i) {
            std::array<char, 4> arr;
            writeInt(arr.data(), i);
            auto result = readInt(arr.data());
            RC_ASSERT(result == i);
        });
    }

    TEST_CASE("computeCrc")
    {
        SECTION("empty input")
        {
            REQUIRE(computeCrc("", 0) == 0x00000000u);
        }

        SECTION("known CRC32 values")
        {
            // CRC32 of "123456789" is 0xCBF43926
            const char* input = "123456789";
            REQUIRE(computeCrc(input, 9) == 0xCBF43926u);
        }

        SECTION("single byte")
        {
            // CRC32 of "a" is 0xE8B7BE43
            REQUIRE(computeCrc("a", 1) == 0xE8B7BE43u);
        }
    }

    TEST_CASE("chooseChatCountForPacket")
    {
        // A packet of a fixed 100 bytes plus 200 for every chat line in it.
        auto sizeOf = [](std::size_t count) -> unsigned long long { return 100 + (200 * count); };

        SECTION("takes the lot when the lot fits")
        {
            REQUIRE(chooseChatCountForPacket(4, 1500, sizeOf) == 4);
        }

        SECTION("takes exactly what fits")
        {
            // 100 + 3*200 is 700; a fourth line would be 900.
            REQUIRE(chooseChatCountForPacket(8, 899, sizeOf) == 3);
        }

        SECTION("takes none when not even one fits")
        {
            REQUIRE(chooseChatCountForPacket(8, 299, sizeOf) == 0);
        }

        SECTION("takes none when the packet is over the limit with no chat at all")
        {
            // The commands alone have filled it. Nothing here can help, and
            // the caller is the one that decides what that means.
            REQUIRE(chooseChatCountForPacket(8, 50, sizeOf) == 0);
        }

        SECTION("takes none from an empty buffer")
        {
            REQUIRE(chooseChatCountForPacket(0, 1500, sizeOf) == 0);
        }
    }

    TEST_CASE("longestPrefixThatFits")
    {
        // Issue #75: what each stream contributes to a packet. The same
        // fixed-plus-per-item shape as the chat case above.
        auto sizeOf = [](std::size_t count) -> unsigned long long { return 100 + (30 * count); };

        SECTION("takes everything when everything fits")
        {
            REQUIRE(longestPrefixThatFits(40, 1496, sizeOf) == 40);
        }

        SECTION("takes exactly the longest prefix that fits")
        {
            // 100 + 46*30 is 1480; a 47th is 1510.
            REQUIRE(longestPrefixThatFits(900, 1496, sizeOf) == 46);
        }

        SECTION("agrees with trying every length")
        {
            for (unsigned long long limit = 0; limit < 1000; limit += 7)
            {
                std::size_t linear = 0;
                while (linear < 25 && sizeOf(linear + 1) <= limit)
                {
                    ++linear;
                }
                REQUIRE(longestPrefixThatFits(25, limit, sizeOf) == linear);
            }
        }

        SECTION("takes none of an empty stream")
        {
            REQUIRE(longestPrefixThatFits(0, 1496, sizeOf) == 0);
        }
    }

    TEST_CASE("gateAdjustment")
    {
        SECTION("a peer level with the average is not adjusted")
        {
            REQUIRE(gateAdjustment(SceneTime(100), SceneTime(100)) == 1.0f);
        }

        SECTION("a lead slows in proportion to the gain")
        {
            REQUIRE(gateAdjustment(SceneTime(90), SceneTime(93)) == Catch::Approx(0.91f));
        }

        SECTION("a lag speeds up in proportion to the gain")
        {
            REQUIRE(gateAdjustment(SceneTime(100), SceneTime(96)) == Catch::Approx(1.12f));
        }

        SECTION("clamped to plus or minus twenty-five percent")
        {
            REQUIRE(gateAdjustment(SceneTime(0), SceneTime(100)) == 0.75f);
            REQUIRE(gateAdjustment(SceneTime(100), SceneTime(0)) == 1.25f);
        }
    }

    TEST_CASE("FrameScheduler: the proportional gate")
    {
        const auto tick = static_cast<unsigned int>(SimMillisecondsPerTick);

        SECTION("a level frame runs one tick per tick's worth of buffer")
        {
            FrameScheduler scheduler(12 * tick, SceneTime(10), SceneTime(10), 100);
            while (scheduler.hasWork())
            {
                scheduler.next();
            }
            REQUIRE(scheduler.ticksThisFrame() == 12);
        }

        SECTION("a lead makes a tick cost more, so fewer run")
        {
            // Three ahead: factor 0.91, so a tick costs 36 ms instead of 33.
            FrameScheduler scheduler(12 * tick, SceneTime(10), SceneTime(13), 100);
            while (scheduler.hasWork())
            {
                scheduler.next();
            }
            REQUIRE(scheduler.ticksThisFrame() == 11);
        }

        SECTION("a lag makes a tick cost less, so more run")
        {
            // Four behind: factor 1.12, so a tick costs 29 ms instead of 33.
            FrameScheduler scheduler(12 * tick, SceneTime(14), SceneTime(10), 100);
            while (scheduler.hasWork())
            {
                scheduler.next();
            }
            REQUIRE(scheduler.ticksThisFrame() == 13);
        }

        SECTION("a lagging peer runs its backlog down faster than a level one")
        {
            FrameScheduler lagging(6 * tick, SceneTime(20), SceneTime(10), 100);
            FrameScheduler level(6 * tick, SceneTime(10), SceneTime(10), 100);
            while (lagging.hasWork())
            {
                lagging.next();
            }
            while (level.hasWork())
            {
                level.next();
            }

            REQUIRE(lagging.ticksThisFrame() > level.ticksThisFrame());
        }

        SECTION("a lead holds a tick back until its higher cost is met")
        {
            FrameScheduler scheduler(tick, SceneTime(10), SceneTime(13), 100);
            REQUIRE_FALSE(scheduler.hasWork());
            REQUIRE(scheduler.ticksThisFrame() == 0);
        }
    }

    TEST_CASE("FrameScheduler: the cap drops the backlog")
    {
        const auto tick = static_cast<unsigned int>(SimMillisecondsPerTick);

        SECTION("loses exactly the whole ticks still buffered when the cap is hit")
        {
            // Five ticks of buffer against a cap of two.
            FrameScheduler scheduler(5 * tick, SceneTime(0), SceneTime(0), 2);
            while (scheduler.hasWork())
            {
                scheduler.next();
            }

            auto outcome = scheduler.finish();
            REQUIRE(outcome.ticksDispatched == 2);
            REQUIRE(outcome.millisecondsLeft == 0);
            REQUIRE(outcome.ticksLostToCap == 3);
        }

        SECTION("loses nothing when the frame ends on its own")
        {
            FrameScheduler scheduler(2 * tick, SceneTime(0), SceneTime(0), 2);
            while (scheduler.hasWork())
            {
                scheduler.next();
            }

            auto outcome = scheduler.finish();
            REQUIRE(outcome.ticksDispatched == 2);
            REQUIRE(outcome.millisecondsLeft == 0);
            REQUIRE(outcome.ticksLostToCap == 0);
        }

        SECTION("a partial tick left over is not a lost tick")
        {
            FrameScheduler scheduler(2 * tick + 20, SceneTime(0), SceneTime(0), 2);
            while (scheduler.hasWork())
            {
                scheduler.next();
            }

            auto outcome = scheduler.finish();
            REQUIRE(outcome.ticksLostToCap == 0);
            REQUIRE(outcome.millisecondsLeft == 0);
        }
    }

    TEST_CASE("estimateSustainableSpeedPermille")
    {
        SECTION("keeps the chosen speed when nothing was lost and ticks are cheap")
        {
            REQUIRE(estimateSustainableSpeedPermille(1000, 30, 0, 1.0f) == 1000);
        }

        SECTION("scales down by the fraction of the owed ticks that ran")
        {
            // Fifteen of thirty owed ticks ran at a chosen 1000.
            REQUIRE(estimateSustainableSpeedPermille(1000, 15, 15, 1.0f) == 500);
        }

        SECTION("drops to what a tick's cost allows")
        {
            // 40 ms a tick against the 33 ms budget of normal speed.
            REQUIRE(estimateSustainableSpeedPermille(1000, 30, 0, 40.0f) == 825);
        }

        SECTION("takes the lower of the two measures")
        {
            // Throughput says 500, cost alone would allow 1320.
            REQUIRE(estimateSustainableSpeedPermille(1000, 15, 15, 25.0f) == 500);

            // Cost says 660; nothing was lost to the cap.
            REQUIRE(estimateSustainableSpeedPermille(1000, 30, 0, 50.0f) == 660);
        }

        SECTION("never reports above the chosen speed")
        {
            REQUIRE(estimateSustainableSpeedPermille(500, 30, 0, 1.0f) == 500);
        }

        SECTION("with no ticks to go on keeps the chosen speed")
        {
            REQUIRE(estimateSustainableSpeedPermille(1000, 0, 0, 0.0f) == 1000);
        }

        SECTION("floors at the slowest playable speed")
        {
            REQUIRE(estimateSustainableSpeedPermille(1000, 1, 999, 500.0f) == MinimumSustainableSpeedPermille);
        }
    }

    TEST_CASE("SpeedGovernor")
    {
        auto t0 = getTimestamp();

        SECTION("drops at once to the slowest machine")
        {
            SpeedGovernor governor;
            std::vector<PeerCapacity> peers{{PlayerId(1), 600}, {PlayerId(2), 800}};
            REQUIRE(governor.update(t0, 1000, peers) == 600);
            REQUIRE(governor.limitingPeers() == std::vector<PlayerId>{PlayerId(1)});
        }

        SECTION("recovers at a fixed rate rather than at once")
        {
            SpeedGovernor governor;
            std::vector<PeerCapacity> slow{{PlayerId(1), 400}};
            REQUIRE(governor.update(t0, 1000, slow) == 400);

            // The cap lifts, but the speed climbs no faster than 100 a second.
            std::vector<PeerCapacity> fast{{PlayerId(1), 1000}};
            REQUIRE(governor.update(t0 + std::chrono::milliseconds(1000), 1000, fast) == 500);
            REQUIRE(governor.update(t0 + std::chrono::milliseconds(2000), 1000, fast) == 600);
            REQUIRE(governor.update(t0 + std::chrono::milliseconds(3000), 1000, fast) == 700);
        }

        SECTION("a transient dip does not make it hunt")
        {
            SpeedGovernor governor;
            REQUIRE(governor.update(t0, 1000, {}) == 1000);

            // One bad report drops it at once...
            std::vector<PeerCapacity> dip{{PlayerId(1), 400}};
            REQUIRE(governor.update(t0 + std::chrono::milliseconds(100), 1000, dip) == 400);

            // ...and it does not snap back when the report clears.
            REQUIRE(governor.update(t0 + std::chrono::milliseconds(1100), 1000, {}) == 500);
        }

        SECTION("a player's speed-up takes effect at once")
        {
            SpeedGovernor governor;
            REQUIRE(governor.update(t0, 1000, {}) == 1000);
            REQUIRE(governor.update(t0 + std::chrono::milliseconds(100), 1500, {}) == 1500);
        }
    }

    TEST_CASE("planCommandSets")
    {
        SECTION("pads an empty buffer up to the target depth")
        {
            auto plan = planCommandSets(0, 5, false, false);
            // The one set the push branch contributes, then four more.
            REQUIRE(plan.pushLocalSet);
            REQUIRE(plan.emptySetsToPush == 4);
        }

        SECTION("a pushed local set counts toward the target depth")
        {
            auto plan = planCommandSets(2, 5, false, true);
            REQUIRE(plan.pushLocalSet);
            REQUIRE(plan.emptySetsToPush == 2);
        }

        SECTION("pushes the local set at the target depth")
        {
            auto plan = planCommandSets(5, 5, false, true);
            REQUIRE(plan.pushLocalSet);
            REQUIRE(plan.emptySetsToPush == 0);
        }

        SECTION("defers the local set once the buffer is over the target depth")
        {
            auto plan = planCommandSets(6, 5, false, true);
            REQUIRE_FALSE(plan.pushLocalSet);
            REQUIRE(plan.emptySetsToPush == 0);
        }

        SECTION("pushes the local set while stalled even over the target depth")
        {
            // The drop-deadlock arm: the tick that would drain the buffer is
            // itself waiting on the drop command sitting in it.
            auto plan = planCommandSets(6, 5, true, true);
            REQUIRE(plan.pushLocalSet);
            REQUIRE(plan.emptySetsToPush == 0);
        }

        SECTION("a stalled buffer with no local set still pushes nothing")
        {
            auto plan = planCommandSets(6, 5, true, false);
            REQUIRE_FALSE(plan.pushLocalSet);
            REQUIRE(plan.emptySetsToPush == 0);
        }
    }

    TEST_CASE("estimateAverageSceneTimeStatic")
    {
        auto now = getTimestamp();

        SECTION("with no peers it is the local scene time alone")
        {
            std::vector<PeerSceneTimeReport> peers;
            REQUIRE(estimateAverageSceneTimeStatic(SceneTime(12), peers, now) == 12u);
        }

        SECTION("averages the local time with each peer, truncating")
        {
            std::vector<PeerSceneTimeReport> peers{
                {SceneTime(10), now, {}},
                {SceneTime(20), now, {}}};
            // (4 + 10 + 20) / 3, truncated.
            REQUIRE(estimateAverageSceneTimeStatic(SceneTime(4), peers, now) == 11u);
        }

        SECTION("a peer reporting at now is not projected forward")
        {
            std::vector<PeerSceneTimeReport> peers{{SceneTime(10), now, {}}};
            REQUIRE(estimateAverageSceneTimeStatic(SceneTime(4), peers, now) == 7u);
        }

        SECTION("a peer's last time is carried forward before averaging")
        {
            auto earlier = now - std::chrono::milliseconds(100);
            std::vector<PeerSceneTimeReport> peers{{SceneTime(0), earlier, {}}};
            auto projected = projectSceneTime(SceneTime(0), 100);
            auto expected = (SceneTime(0).value + projected.value) / 2;
            REQUIRE(estimateAverageSceneTimeStatic(SceneTime(0), peers, now) == expected);
        }

        SECTION("a paused peer's time is not carried forward")
        {
            auto earlier = now - std::chrono::milliseconds(100);
            std::vector<PeerSceneTimeReport> peers{{SceneTime(0), earlier, PeerRunState{1000, true, false}}};
            REQUIRE(estimateAverageSceneTimeStatic(SceneTime(0), peers, now) == 0u);
        }

        SECTION("a stalled peer's time is not carried forward")
        {
            auto earlier = now - std::chrono::milliseconds(100);
            std::vector<PeerSceneTimeReport> peers{{SceneTime(0), earlier, PeerRunState{1000, false, true}}};
            REQUIRE(estimateAverageSceneTimeStatic(SceneTime(0), peers, now) == 0u);
        }
    }

    TEST_CASE("projectSceneTime")
    {
        SECTION("projects at the sim tick rate, not the old 16 ms frame")
        {
            REQUIRE(projectSceneTime(SceneTime(0), 100) == SceneTime(3));
        }

        SECTION("half speed advances half as far")
        {
            REQUIRE(projectSceneTime(SceneTime(0), 100, PeerRunState{500, false, false}) == SceneTime(1));
        }

        SECTION("double speed advances twice as far")
        {
            REQUIRE(projectSceneTime(SceneTime(0), 100, PeerRunState{2000, false, false}) == SceneTime(6));
        }

        SECTION("a paused peer does not advance")
        {
            REQUIRE(projectSceneTime(SceneTime(10), 100, PeerRunState{1000, true, false}) == SceneTime(10));
        }

        SECTION("a stalled peer does not advance")
        {
            REQUIRE(projectSceneTime(SceneTime(10), 100, PeerRunState{1000, false, true}) == SceneTime(10));
        }

        SECTION("an old peer without the fields reads as 1x and moving")
        {
            REQUIRE(projectSceneTime(SceneTime(0), 100, PeerRunState{}) == SceneTime(3));
        }
    }
}
