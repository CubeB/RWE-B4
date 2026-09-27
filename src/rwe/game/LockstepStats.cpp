#include "LockstepStats.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace rwe
{
    double LockstepSummary::effectiveTicksPerSecond() const
    {
        auto seconds = std::chrono::duration<double>(elapsed).count();
        if (seconds <= 0.0)
        {
            return 0.0;
        }
        return static_cast<double>(ticks) / seconds;
    }

    std::string LockstepSummary::describe() const
    {
        std::ostringstream out;
        out << "Lockstep summary: " << ticks << " ticks in " << elapsed.count() << " ms ("
            << std::fixed << std::setprecision(1) << effectiveTicksPerSecond() << " tps)"
            << ", stalls " << stalls
            << " totalling " << totalStalled.count() << " ms"
            << ", longest " << longestStall.count() << " ms"
            << ", cap-dropped " << ticksLostToCap
            << ", gate-skips " << gateSkips;
        return out.str();
    }

    LockstepSummary LockstepStats::summary(Timestamp now, unsigned int ticksLostToCap, unsigned int gateSkips) const
    {
        LockstepSummary result;
        result.ticks = ticks;
        result.elapsed = firstRan ? std::chrono::duration_cast<std::chrono::milliseconds>(now - *firstRan) : std::chrono::milliseconds(0);
        result.stalls = stalls;
        result.totalStalled = stalledSoFar(now);
        result.longestStall = longest;
        result.ticksLostToCap = ticksLostToCap;
        result.gateSkips = gateSkips;
        return result;
    }

    void LockstepStats::tickBlocked(Timestamp now, const std::vector<PlayerId>& players)
    {
        if (!stallStart)
        {
            stallStart = now;
        }

        // A tick can be blocked with no named player -- the local buffer
        // itself briefly empty -- and that says nothing about who stalled.
        if (!players.empty())
        {
            waitingFor = players;
        }
    }

    void LockstepStats::tickRan(Timestamp now)
    {
        ++ticks;
        if (!firstRan)
        {
            firstRan = now;
        }

        if (!stallStart)
        {
            return;
        }

        auto length = std::chrono::duration_cast<std::chrono::milliseconds>(now - *stallStart);
        total += length;
        if (length >= CountedStall)
        {
            ++stalls;
            longest = std::max(longest, length);
        }
        stallStart = std::nullopt;
    }

    std::optional<std::chrono::milliseconds> LockstepStats::currentStall(Timestamp now) const
    {
        if (!stallStart)
        {
            return std::nullopt;
        }
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - *stallStart);
    }

    std::chrono::milliseconds LockstepStats::stalledSoFar(Timestamp now) const
    {
        return total + currentStall(now).value_or(std::chrono::milliseconds(0));
    }
}
