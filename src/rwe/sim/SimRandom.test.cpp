#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/SimRandom.h>
#include <random>

namespace rwe
{
    TEST_CASE("simulation draws are a plain modulo of the generator", "[determinism]")
    {
        // The point of these helpers is not convenience. A desync in a
        // network game is about the most expensive bug this codebase can
        // have, and std::uniform_int_distribution can cause one: its bias
        // correction is implementation-defined, so two builds given the same
        // seeded generator may draw different numbers and, because rejection
        // consumes draws, fall permanently out of step afterwards.
        //
        // These tests pin both halves of the fix -- the value, and how many
        // raw draws it costs -- against a copy of the same generator, so a
        // future change back to a distribution fails here rather than in
        // somebody's multiplayer match.

        SECTION("randomBelow is one raw draw, taken modulo")
        {
            std::minstd_rand rng;
            std::minstd_rand reference;

            for (unsigned int n : {2u, 3u, 100u, 1000u, 65536u})
            {
                REQUIRE(randomBelow(rng, n) == reference() % n);
            }
        }

        SECTION("randomBetween is the same draw, shifted")
        {
            std::minstd_rand rng;
            std::minstd_rand reference;

            REQUIRE(randomBetween(rng, 1, 100) == 1 + static_cast<int>(reference() % 100u));
            REQUIRE(randomBetween(rng, -50, 50) == -50 + static_cast<int>(reference() % 101u));
            REQUIRE(randomBetween(rng, 5u, 14u) == 5u + (reference() % 10u));
        }

        SECTION("an empty range costs no draw at all")
        {
            // Worth pinning: a range that collapses must not consume a draw,
            // or a build where it collapses and one where it does not would
            // diverge from that point on.
            std::minstd_rand rng;
            auto before = rng;

            REQUIRE(randomBelow(rng, 0) == 0);
            REQUIRE(randomBetween(rng, 7, 7) == 7);
            REQUIRE(randomBetween(rng, 9, 3) == 9);
            REQUIRE(randomBetween(rng, 4u, 4u) == 4u);

            REQUIRE(rng == before);
        }

        SECTION("the results stay inside the range asked for")
        {
            std::minstd_rand rng;
            for (int i = 0; i < 2000; ++i)
            {
                auto v = randomBetween(rng, -7, 11);
                REQUIRE(v >= -7);
                REQUIRE(v <= 11);
            }
        }
    }
}
