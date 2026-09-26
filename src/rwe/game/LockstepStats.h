#pragma once

#include <chrono>
#include <optional>
#include <rwe/rwe_time.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The whole run's lockstep figures: how many ticks ran, against what wall
     * clock, and how many the lockstep or the frame cap got in the way of.
     *
     * Wall-clock figures about this one machine, written to the log for
     * tools/net-test.ps1 to read back. The simulation must never read any of
     * it.
     */
    struct LockstepSummary
    {
        unsigned int ticks{0};
        std::chrono::milliseconds elapsed{0};
        unsigned int stalls{0};
        std::chrono::milliseconds totalStalled{0};
        std::chrono::milliseconds longestStall{0};
        unsigned int ticksLostToCap{0};
        unsigned int gateSkips{0};

        /** Ticks a second averaged over the run; zero for a run with no elapsed time. */
        double effectiveTicksPerSecond() const;

        /** One log line, prefixed so a log can be grepped for it. */
        std::string describe() const;
    };

    /**
     * How often and for how long a tick that was due could not run, and who it
     * was waiting for.
     *
     * Wall-clock figures about this one machine, for the network overlay. The
     * simulation must never read any of it.
     */
    class LockstepStats
    {
    public:
        /**
         * A stall shorter than one tick is a packet arriving a frame late, which
         * happens throughout a healthy game. It still adds to the total, but it
         * is not counted as a stall.
         */
        static constexpr std::chrono::milliseconds CountedStall{33};

        /** A tick was due and could not run, because these players' commands had not arrived. */
        void tickBlocked(Timestamp now, const std::vector<PlayerId>& waitingFor);

        void tickRan(Timestamp now);

        unsigned int stallCount() const { return stalls; }
        std::chrono::milliseconds longestStall() const { return longest; }
        std::chrono::milliseconds totalStalled() const { return total; }

        /** How long the stall in progress has lasted, or nothing while ticks are running. */
        std::optional<std::chrono::milliseconds> currentStall(Timestamp now) const;

        /** The total, including the stall in progress. */
        std::chrono::milliseconds stalledSoFar(Timestamp now) const;

        /** Who the most recent stall was waiting for. */
        const std::vector<PlayerId>& lastWaitingFor() const { return waitingFor; }

        /**
         * The run so far, with the frame loop's own counters folded in: only
         * the frame loop knows when the cap dropped a tick the scheduler
         * handed it, or when the drift gate skipped one.
         */
        LockstepSummary summary(Timestamp now, unsigned int ticksLostToCap, unsigned int gateSkips) const;

    private:
        std::optional<Timestamp> firstRan;
        unsigned int ticks{0};
        std::optional<Timestamp> stallStart;
        unsigned int stalls{0};
        std::chrono::milliseconds longest{0};
        std::chrono::milliseconds total{0};
        std::vector<PlayerId> waitingFor;
    };
}
