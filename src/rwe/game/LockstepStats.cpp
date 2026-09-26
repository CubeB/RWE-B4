#include "LockstepStats.h"

#include <algorithm>

namespace rwe
{
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
