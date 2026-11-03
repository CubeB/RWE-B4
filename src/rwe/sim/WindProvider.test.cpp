#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/WindProvider.h>

namespace rwe
{
    namespace
    {
        WindInput input(GameTime now, GameTime next)
        {
            WindInput value;
            value.now = now;
            value.nextChange = next;
            value.direction = SimAngle(1000);
            value.generationFactor = SimScalar(0.5f);
            value.vector = SimVector(1_ss, 0_ss, 2_ss);
            value.minSpeed = 100;
            value.maxSpeed = 1000;
            return value;
        }
    }

    TEST_CASE("the native wind provider is inert before its next change", "[modsdk][windprovider]")
    {
        NativeWindProvider provider;
        std::minstd_rand rng(7);
        const auto before = provider.update(input(GameTime(10), GameTime(20)), rng);

        CHECK_FALSE(before.changed);
        CHECK(before.direction == SimAngle(1000));
        CHECK(before.generationFactor == SimScalar(0.5f));
        CHECK(before.vector.x.value == 1.0f);
        CHECK(before.vector.z.value == 2.0f);
        CHECK(before.nextChange == GameTime(20));
    }

    TEST_CASE("the native wind provider is deterministic on the shared stream", "[modsdk][windprovider]")
    {
        NativeWindProvider provider;
        const auto due = input(GameTime(20), GameTime(20));

        std::minstd_rand first(7);
        std::minstd_rand second(7);
        const auto a = provider.update(due, first);
        const auto b = provider.update(due, second);

        REQUIRE(a.changed);
        CHECK(a.nextChange == b.nextChange);
        CHECK(a.speed == b.speed);
        CHECK(a.direction == b.direction);
        CHECK(a.vector.x.value == b.vector.x.value);
        CHECK(a.vector.z.value == b.vector.z.value);
        CHECK(a.generationFactor == b.generationFactor);

        CHECK(a.nextChange > due.now);
        CHECK(a.speed >= due.minSpeed);
        CHECK(a.speed < due.maxSpeed);
    }

    TEST_CASE("a wind provider has a stable identity", "[modsdk][windprovider]")
    {
        NativeWindProvider provider;
        CHECK(provider.id() == "native.wind@1");
    }
}

