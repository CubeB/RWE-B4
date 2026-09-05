#pragma once

#include <algorithm>
#include <random>

namespace rwe
{
    /**
     * Random numbers the simulation is allowed to draw.
     *
     * Never `std::uniform_int_distribution`. Its bias correction is
     * implementation-defined: two builds of the engine, given the same seeded
     * generator, can draw different numbers from it and fall out of step in a
     * network game -- and a desync found in a multiplayer match is about the
     * most expensive kind of bug this codebase can have.
     *
     * `std::minstd_rand` itself is specified exactly, so a modulo of its raw
     * output is the same everywhere. It is very slightly biased towards the
     * low end of the range, which does not matter here and is what the
     * original's own rand-below-n does anyway.
     */

    /** A whole number in [0, n), or zero if the range is empty. */
    template <typename Rng>
    unsigned int randomBelow(Rng& rng, unsigned int n)
    {
        if (n == 0)
        {
            return 0;
        }

        return static_cast<unsigned int>(rng() % n);
    }

    /** A whole number in [low, high], both ends included. */
    template <typename Rng>
    int randomBetween(Rng& rng, int low, int high)
    {
        if (high <= low)
        {
            return low;
        }

        auto span = static_cast<unsigned int>(high - low) + 1u;
        return low + static_cast<int>(randomBelow(rng, span));
    }

    /** The same, for counts and sizes. */
    template <typename Rng>
    unsigned int randomBetween(Rng& rng, unsigned int low, unsigned int high)
    {
        if (high <= low)
        {
            return low;
        }

        return low + randomBelow(rng, (high - low) + 1u);
    }
}
