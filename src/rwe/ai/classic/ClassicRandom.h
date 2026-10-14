#pragma once

#include <cstdint>

namespace rwe
{
    /**
     * The original computer player's random draw (TOTALA-EXE-COMPUTER-PLAYER.md,
     * the conventions paragraph): a Park-Miller "minimal standard" generator,
     * multiplier 16807 and modulus 2^31 - 1, computed in 64-bit integers.
     *
     * `below(n)` is the original's `rand(n)`: it returns 0 WITHOUT advancing the
     * generator when n < 2, and otherwise advances once and returns the state
     * modulo n. That rule is part of the finding, because it decides how many
     * numbers a sequence of draws consumes.
     *
     * Integer arithmetic only, and no <random> distribution anywhere: the
     * computer player runs inside the simulation, so two machines have to draw
     * the same numbers from the same seed.
     */
    class ClassicRandom
    {
    public:
        static constexpr std::uint32_t Modulus = 2147483647u;
        static constexpr std::uint32_t Multiplier = 16807u;
        static constexpr std::uint32_t SeedMask = 0x66e29572u;

        /**
         * The seeded generator: `(input ^ 0x66e29572) | 1`, reduced into
         * [1, 2^31 - 2]. A 64-bit seed is folded to 32 bits first.
         */
        static ClassicRandom fromSeed(std::uint64_t seed)
        {
            auto input = static_cast<std::uint32_t>(seed ^ (seed >> 32));
            std::uint32_t state = (input ^ SeedMask) | 1u;
            // Into [1, Modulus - 1): state % (Modulus - 1) is [0, Modulus - 2),
            // and one more puts it in [1, Modulus - 2].
            state = (state % (Modulus - 1u)) + 1u;
            return ClassicRandom(state);
        }

        /** A generator started from an exact state, for tests. A state of 0 is bumped to 1. */
        static ClassicRandom fromState(std::uint32_t state)
        {
            return ClassicRandom(state == 0 ? 1u : state % Modulus);
        }

        /** `rand(n)`. */
        std::uint32_t below(std::uint32_t n)
        {
            if (n < 2u)
            {
                return 0u;
            }
            advance();
            return state % n;
        }

        /** The state, so that a test can see whether a draw advanced the generator. */
        std::uint32_t current() const { return state; }

    private:
        explicit ClassicRandom(std::uint32_t state) : state(state) {}

        void advance()
        {
            state = static_cast<std::uint32_t>((static_cast<std::uint64_t>(state) * Multiplier) % Modulus);
        }

        std::uint32_t state;
    };
}
