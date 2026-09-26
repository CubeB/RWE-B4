#include <catch2/catch_test_macros.hpp>
#include <rwe/game/SimDiagnostics.h>

namespace rwe
{
    using namespace std::chrono_literals;

    TEST_CASE("parseSimLag")
    {
        SECTION("nothing to read is not a lag")
        {
            REQUIRE(!parseSimLag(nullptr).has_value());
            REQUIRE(!parseSimLag("").has_value());
        }

        SECTION("a bare millisecond count starts at the first tick")
        {
            auto lag = parseSimLag("50");
            REQUIRE(lag.has_value());
            REQUIRE(lag->delay == 50ms);
            REQUIRE(lag->fromTick == 0);
        }

        SECTION("an at-tick starts part way through")
        {
            auto lag = parseSimLag("50@100");
            REQUIRE(lag.has_value());
            REQUIRE(lag->delay == 50ms);
            REQUIRE(lag->fromTick == 100);
        }

        SECTION("zero is a valid, do-nothing delay")
        {
            auto lag = parseSimLag("0");
            REQUIRE(lag.has_value());
            REQUIRE(lag->delay == 0ms);
            REQUIRE(lag->fromTick == 0);
        }

        SECTION("bad input is refused rather than guessed at")
        {
            REQUIRE(!parseSimLag("abc").has_value());
            REQUIRE(!parseSimLag("50ms").has_value());
            REQUIRE(!parseSimLag("-5").has_value());
            REQUIRE(!parseSimLag("50 ").has_value());
            REQUIRE(!parseSimLag("@100").has_value());
            REQUIRE(!parseSimLag("50@").has_value());
            REQUIRE(!parseSimLag("50@abc").has_value());
            REQUIRE(!parseSimLag("50@-1").has_value());
            REQUIRE(!parseSimLag("50@100@200").has_value());
            REQUIRE(!parseSimLag("50@99999999999999999999").has_value());
        }
    }
}
