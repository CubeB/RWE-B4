#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/LockstepStats.h>
#include <rwe/game/NetworkHistory.h>
#include <rwe/game/RoundTripWindow.h>

namespace rwe
{
    using namespace std::chrono_literals;

    TEST_CASE("RoundTripWindow")
    {
        SECTION("reads zero until it has a sample")
        {
            RoundTripWindow w;
            REQUIRE(w.size() == 0);
            REQUIRE(w.min() == 0.0f);
            REQUIRE(w.max() == 0.0f);
        }

        SECTION("keeps the spike an average would smooth away")
        {
            RoundTripWindow w;
            w.add(50.0f);
            w.add(400.0f);
            w.add(60.0f);
            REQUIRE(w.min() == 50.0f);
            REQUIRE(w.max() == 400.0f);
        }

        SECTION("forgets a spike once it has been pushed out of the window")
        {
            RoundTripWindow w;
            w.add(400.0f);
            for (std::size_t i = 0; i < RoundTripWindow::Capacity; ++i)
            {
                w.add(50.0f);
            }
            REQUIRE(w.size() == RoundTripWindow::Capacity);
            REQUIRE(w.max() == 50.0f);
        }
    }

    TEST_CASE("LockstepStats")
    {
        Timestamp t0{};

        SECTION("a stall is timed from the first blocked tick to the next tick that runs")
        {
            LockstepStats s;
            s.tickRan(t0);
            s.tickBlocked(t0 + 10ms, {PlayerId(1)});
            s.tickBlocked(t0 + 100ms, {PlayerId(1)});
            REQUIRE(s.currentStall(t0 + 200ms) == 190ms);

            s.tickRan(t0 + 310ms);
            REQUIRE(!s.currentStall(t0 + 310ms));
            REQUIRE(s.stallCount() == 1);
            REQUIRE(s.longestStall() == 300ms);
            REQUIRE(s.totalStalled() == 300ms);
            REQUIRE(s.lastWaitingFor() == std::vector<PlayerId>{PlayerId(1)});
        }

        SECTION("a stall shorter than a tick adds to the total but is not counted")
        {
            LockstepStats s;
            s.tickBlocked(t0, {PlayerId(1)});
            s.tickRan(t0 + 16ms);
            REQUIRE(s.stallCount() == 0);
            REQUIRE(s.longestStall() == 0ms);
            REQUIRE(s.totalStalled() == 16ms);
        }

        SECTION("an unnamed stall keeps the last player who was named")
        {
            LockstepStats s;
            s.tickBlocked(t0, {PlayerId(2)});
            s.tickRan(t0 + 50ms);
            s.tickBlocked(t0 + 60ms, {});
            REQUIRE(s.lastWaitingFor() == std::vector<PlayerId>{PlayerId(2)});
        }

        SECTION("the running total includes the stall in progress")
        {
            LockstepStats s;
            s.tickBlocked(t0, {PlayerId(1)});
            s.tickRan(t0 + 100ms);
            s.tickBlocked(t0 + 200ms, {PlayerId(1)});
            REQUIRE(s.stalledSoFar(t0 + 250ms) == 150ms);
        }
    }

    TEST_CASE("LockstepSummary")
    {
        SECTION("averages ticks over the run's wall clock")
        {
            LockstepSummary s;
            s.ticks = 300;
            s.elapsed = 10000ms;
            REQUIRE(s.effectiveTicksPerSecond() == Catch::Approx(30.0));
        }

        SECTION("a run with no elapsed time reads zero rather than dividing by zero")
        {
            LockstepSummary s;
            s.ticks = 5;
            s.elapsed = 0ms;
            REQUIRE(s.effectiveTicksPerSecond() == 0.0);
        }

        SECTION("folds in the cap drops and gate skips it was handed, with the stall in progress")
        {
            LockstepStats stats;
            Timestamp t0{};
            stats.tickRan(t0);
            stats.tickBlocked(t0 + 100ms, {PlayerId(1)});
            stats.tickRan(t0 + 400ms);
            stats.tickRan(t0 + 500ms);
            stats.tickBlocked(t0 + 600ms, {PlayerId(1)});

            auto summary = stats.summary(t0 + 700ms, 3, 7);
            REQUIRE(summary.ticks == 3);
            REQUIRE(summary.elapsed == 700ms);
            REQUIRE(summary.stalls == 1);
            REQUIRE(summary.totalStalled == 400ms);
            REQUIRE(summary.longestStall == 300ms);
            REQUIRE(summary.ticksLostToCap == 3);
            REQUIRE(summary.gateSkips == 7);
        }

        SECTION("describes the figures it carries")
        {
            LockstepSummary s;
            s.ticks = 300;
            s.elapsed = 10000ms;
            s.stalls = 2;
            s.totalStalled = 150ms;
            s.longestStall = 120ms;
            s.ticksLostToCap = 4;
            s.gateSkips = 5;
            auto text = s.describe();
            REQUIRE(text.find("Lockstep summary: 300 ticks") != std::string::npos);
            REQUIRE(text.find("30.0 tps") != std::string::npos);
            REQUIRE(text.find("cap-dropped 4") != std::string::npos);
            REQUIRE(text.find("gate-skips 5") != std::string::npos);
        }
    }

    TEST_CASE("SampleRing")
    {
        SECTION("plots from the start until full, then from the oldest sample")
        {
            SampleRing r;
            r.push(1.0f);
            r.push(2.0f);
            REQUIRE(r.size() == 2);
            REQUIRE(r.offset() == 0);
            REQUIRE(r.latest() == 2.0f);

            for (std::size_t i = 0; i < SampleRing::Capacity; ++i)
            {
                r.push(static_cast<float>(i + 10));
            }
            REQUIRE(r.size() == SampleRing::Capacity);
            REQUIRE(r.data()[r.offset()] == 10.0f);
            REQUIRE(r.latest() == static_cast<float>(SampleRing::Capacity + 9));
        }
    }

    TEST_CASE("NetworkHistory")
    {
        Timestamp t0{};
        auto peer = [](float rtt, unsigned int buffered, float quiet) {
            return std::vector<PeerSample>{PeerSample{PlayerId(1), rtt, buffered, quiet, 0.0f, 0.0f}};
        };

        SECTION("takes nothing until an interval has passed")
        {
            NetworkHistory h;
            h.observe(t0, 0, 0ms, peer(50.0f, 10, 0.0f));
            h.observe(t0 + 100ms, 3, 0ms, peer(50.0f, 10, 0.0f));
            REQUIRE(h.tickRate().size() == 0);
            REQUIRE(h.peers().size() == 1);
            REQUIRE(h.peers()[0].roundTrip.size() == 0);
        }

        SECTION("keeps the worst each figure was in the interval, not the last")
        {
            NetworkHistory h;
            h.observe(t0, 0, 0ms, peer(50.0f, 10, 20.0f));
            h.observe(t0 + 100ms, 3, 0ms, peer(300.0f, 0, 90.0f));
            h.observe(t0 + 250ms, 8, 0ms, peer(60.0f, 12, 10.0f));

            const auto& series = h.peers()[0];
            REQUIRE(series.present);
            REQUIRE(series.roundTrip.latest() == 300.0f);
            REQUIRE(series.buffered.latest() == 0.0f);
            REQUIRE(series.quiet.latest() == 90.0f);
        }

        SECTION("turns the running totals into a rate and a per-interval stall")
        {
            NetworkHistory h;
            h.observe(t0, 100, 40ms, {});
            h.observe(t0 + 250ms, 107, 140ms, {});
            REQUIRE(h.tickRate().latest() == 28.0f);
            REQUIRE(h.stalled().latest() == 100.0f);
        }

        SECTION("a peer missing from an interval is marked absent and keeps its history")
        {
            NetworkHistory h;
            h.observe(t0, 0, 0ms, peer(50.0f, 10, 0.0f));
            h.observe(t0 + 250ms, 0, 0ms, peer(50.0f, 10, 0.0f));
            h.observe(t0 + 500ms, 0, 0ms, {});
            REQUIRE(!h.peers()[0].present);
            REQUIRE(h.peers()[0].roundTrip.size() == 1);
        }
    }
}
