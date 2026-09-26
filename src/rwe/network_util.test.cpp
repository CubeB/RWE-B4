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

    TEST_CASE("FrameScheduler: the drift gate")
    {
        const auto buffer = 1000u;
        const int cap = 100;

        SECTION("skips only past the tolerance and only on a check interval")
        {
            FrameScheduler scheduler(buffer, SceneTime(0), cap);
            // Average is 0, so the tolerance band is [0, 3].
            REQUIRE(scheduler.next(SceneTime(0)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(1)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(4)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(5)) == FrameDispatch::Skip);
            REQUIRE(scheduler.next(SceneTime(10)) == FrameDispatch::Skip);
        }

        SECTION("an off-interval tick past the tolerance is attempted")
        {
            FrameScheduler scheduler(buffer, SceneTime(0), cap);
            REQUIRE(scheduler.next(SceneTime(11)) == FrameDispatch::Attempt);
        }

        SECTION("a tick level with or behind the average is never skipped")
        {
            FrameScheduler scheduler(buffer, SceneTime(100), cap);
            // Average is 100, so the tolerance band is [97, 103].
            REQUIRE(scheduler.next(SceneTime(95)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(100)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(104)) == FrameDispatch::Attempt);
        }
    }

    TEST_CASE("FrameScheduler: the catch-up extra tick")
    {
        const auto buffer = 1000u;

        SECTION("runs when a tick lands on the interval while behind the average")
        {
            FrameScheduler scheduler(buffer, SceneTime(100), 10);
            // The tick at 94 reaches 95: on the interval and below the low
            // mark of 97.
            REQUIRE(scheduler.next(SceneTime(94)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.extraTick(SceneTime(95)));
            REQUIRE(scheduler.ticksThisFrame() == 2);
        }

        SECTION("does not run when the reached time is off the interval")
        {
            FrameScheduler scheduler(buffer, SceneTime(100), 10);
            REQUIRE(scheduler.next(SceneTime(93)) == FrameDispatch::Attempt);
            REQUIRE_FALSE(scheduler.extraTick(SceneTime(94)));
        }

        SECTION("does not run when the reached time is not behind the average")
        {
            FrameScheduler scheduler(buffer, SceneTime(0), 10);
            REQUIRE(scheduler.next(SceneTime(94)) == FrameDispatch::Attempt);
            REQUIRE_FALSE(scheduler.extraTick(SceneTime(95)));
        }

        SECTION("never takes the frame past its cap")
        {
            FrameScheduler scheduler(buffer, SceneTime(100), 1);
            REQUIRE(scheduler.next(SceneTime(94)) == FrameDispatch::Attempt);
            REQUIRE_FALSE(scheduler.extraTick(SceneTime(95)));
            REQUIRE(scheduler.ticksThisFrame() == 1);
        }

        SECTION("the extra reaches the cap but a second one does not pass it")
        {
            FrameScheduler scheduler(buffer, SceneTime(100), 2);
            REQUIRE(scheduler.next(SceneTime(94)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.extraTick(SceneTime(95)));
            REQUIRE_FALSE(scheduler.extraTick(SceneTime(96)));
            REQUIRE(scheduler.ticksThisFrame() == 2);
        }
    }

    TEST_CASE("FrameScheduler: the cap drops the backlog")
    {
        const auto tick = static_cast<unsigned int>(SimMillisecondsPerTick);

        SECTION("loses exactly the whole ticks still buffered when the cap is hit")
        {
            // Five ticks of buffer against a cap of two.
            FrameScheduler scheduler(5 * tick, SceneTime(0), 2);
            REQUIRE(scheduler.next(SceneTime(0)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(1)) == FrameDispatch::Attempt);
            REQUIRE_FALSE(scheduler.hasWork());

            auto outcome = scheduler.finish();
            REQUIRE(outcome.ticksDispatched == 2);
            REQUIRE(outcome.millisecondsLeft == 0);
            REQUIRE(outcome.ticksLostToCap == 3);
        }

        SECTION("loses nothing when the frame ends on its own")
        {
            FrameScheduler scheduler(2 * tick, SceneTime(0), 2);
            REQUIRE(scheduler.next(SceneTime(0)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(1)) == FrameDispatch::Attempt);
            REQUIRE_FALSE(scheduler.hasWork());

            auto outcome = scheduler.finish();
            REQUIRE(outcome.ticksDispatched == 2);
            REQUIRE(outcome.millisecondsLeft == 0);
            REQUIRE(outcome.ticksLostToCap == 0);
        }

        SECTION("a partial tick left over is not a lost tick")
        {
            FrameScheduler scheduler(2 * tick + 20, SceneTime(0), 2);
            REQUIRE(scheduler.next(SceneTime(0)) == FrameDispatch::Attempt);
            REQUIRE(scheduler.next(SceneTime(1)) == FrameDispatch::Attempt);

            auto outcome = scheduler.finish();
            REQUIRE(outcome.ticksLostToCap == 0);
            REQUIRE(outcome.millisecondsLeft == 0);
        }
    }

    TEST_CASE("FrameScheduler: a skip spends its time but runs nothing")
    {
        const auto tick = static_cast<unsigned int>(SimMillisecondsPerTick);
        FrameScheduler scheduler(3 * tick, SceneTime(0), 100);

        REQUIRE(scheduler.next(SceneTime(5)) == FrameDispatch::Skip);

        auto outcome = scheduler.finish();
        REQUIRE(outcome.gateSkips == 1);
        REQUIRE(outcome.ticksDispatched == 0);
        REQUIRE(outcome.millisecondsLeft == 2 * tick);
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
