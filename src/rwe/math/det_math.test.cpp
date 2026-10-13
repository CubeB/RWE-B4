#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <rwe/math/det_math.h>

namespace rwe
{
    namespace
    {
        uint32_t bitsOf(float f)
        {
            uint32_t u;
            std::memcpy(&u, &f, sizeof(u));
            return u;
        }

        // Distance in representable floats.
        int ulpsApart(float a, float b)
        {
            int32_t x = static_cast<int32_t>(bitsOf(a));
            int32_t y = static_cast<int32_t>(bitsOf(b));
            if (x < 0)
            {
                x = std::numeric_limits<int32_t>::min() - x;
            }
            if (y < 0)
            {
                y = std::numeric_limits<int32_t>::min() - y;
            }
            const int d = x - y;
            return d < 0 ? -d : d;
        }

        struct Inputs
        {
            float angle;
            float big;
            float x;
            float y;
            float unit;
            float ratio;
        };

        // The same stream every time, on every machine: xorshift32 and only
        // integer-to-float conversions and one division.
        class InputStream
        {
        public:
            Inputs next()
            {
                constexpr float pi = 3.14159265358979f;
                Inputs in{};
                in.angle = static_cast<float>(static_cast<uint16_t>(step())) / 32768.0f * pi;
                in.big = static_cast<float>(static_cast<int32_t>(step() % 2000001) - 1000000) / 1000.0f;
                in.x = static_cast<float>(static_cast<int32_t>(step() % 200001) - 100000) / 137.0f;
                in.y = static_cast<float>(static_cast<int32_t>(step() % 200001) - 100000) / 91.0f;
                in.unit = static_cast<float>(static_cast<int32_t>(step() % 20001) - 10000) / 10000.0f;
                in.ratio = in.y / (in.x + 0.5f);
                return in;
            }

        private:
            uint32_t step()
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                return state;
            }

            uint32_t state{12345};
        };
    }

    TEST_CASE("the deterministic math is as accurate as the C library")
    {
        // Not the property the module is for, but the one that lets the
        // simulation use it without anybody noticing: every result is the
        // double-precision library's answer, rounded to float.
        InputStream stream;
        int worst = 0;
        for (int i = 0; i < 20000; ++i)
        {
            const auto in = stream.next();
            const float angle = (i % 2 == 0) ? in.angle : in.big;
            const double a = angle;

            worst = std::max(worst, ulpsApart(detSin(angle), static_cast<float>(std::sin(a))));
            worst = std::max(worst, ulpsApart(detCos(angle), static_cast<float>(std::cos(a))));
            worst = std::max(worst, ulpsApart(detAtan2(in.y, in.x), static_cast<float>(std::atan2(static_cast<double>(in.y), static_cast<double>(in.x)))));
            worst = std::max(worst, ulpsApart(detHypot(in.x, in.y), static_cast<float>(std::hypot(static_cast<double>(in.x), static_cast<double>(in.y)))));
            worst = std::max(worst, ulpsApart(detAcos(in.unit), static_cast<float>(std::acos(static_cast<double>(in.unit)))));
            worst = std::max(worst, ulpsApart(detAtan(in.ratio), static_cast<float>(std::atan(static_cast<double>(in.ratio)))));
        }
        CHECK(worst <= 1);
    }

    TEST_CASE("the deterministic math gives the same bits on every toolchain")
    {
        // This is the test that matters. The fingerprint below was taken on
        // MinGW (-O0 and -O2) and on glibc (-O2, and -O3 with -march=native);
        // CI runs it under gcc, clang and MSVC, and a toolchain whose result
        // differs in one bit of one of these three hundred thousand answers
        // fails here, rather than as a desync between two players.
        //
        // The C library's own functions do not pass it: on the same inputs
        // MinGW and glibc disagree on 1.4% of sin and cos, 5% of atan, 8.5%
        // of acos and 16% of atan2.
        InputStream stream;
        uint64_t fingerprint = 1469598103934665603ull;
        const auto mix = [&](float f) {
            fingerprint ^= bitsOf(f);
            fingerprint *= 1099511628211ull;
        };
        for (int i = 0; i < 50000; ++i)
        {
            const auto in = stream.next();
            const float angle = (i % 2 != 0) ? in.angle : in.big;
            mix(detSin(angle));
            mix(detCos(angle));
            mix(detAtan2(in.y, in.x));
            mix(detHypot(in.x, in.y));
            mix(detAcos(in.unit));
            mix(detAtan(in.ratio));
        }
        CHECK(fingerprint == 0xf55bb7da977d3c32ull);
    }

    TEST_CASE("the deterministic math follows C's conventions at the edges")
    {
        constexpr float inf = std::numeric_limits<float>::infinity();
        constexpr float nan = std::numeric_limits<float>::quiet_NaN();
        constexpr float pi = 3.14159265358979f;

        SECTION("atan2 of zeros and axes")
        {
            CHECK(detAtan2(0.0f, 0.0f) == 0.0f);
            CHECK(detAtan2(0.0f, -0.0f) == pi);
            CHECK(detAtan2(-0.0f, -0.0f) == -pi);
            CHECK(std::signbit(detAtan2(-0.0f, 1.0f)));
            CHECK(detAtan2(1.0f, 0.0f) == detAtan2(1.0f, -0.0f));
            CHECK(detAtan2(1.0f, 0.0f) == detAtan2(1.0f, 0.0f));
            CHECK(detAtan2(-1.0f, 0.0f) == -detAtan2(1.0f, 0.0f));
            CHECK(detAtan2(0.0f, -1.0f) == pi);
            CHECK(detAtan2(0.0f, 1.0f) == 0.0f);
        }

        SECTION("atan2 with infinities and NaN")
        {
            CHECK(detAtan2(inf, inf) == static_cast<float>(0.78539816339744830962));
            CHECK(detAtan2(1.0f, -inf) == pi);
            CHECK(detAtan2(1.0f, inf) == 0.0f);
            CHECK(detAtan2(inf, 1.0f) == detAtan2(1.0f, 0.0f));
            CHECK(std::isnan(detAtan2(nan, 1.0f)));
            CHECK(std::isnan(detAtan2(1.0f, nan)));
        }

        SECTION("acos is defined on [-1, 1] only")
        {
            CHECK(detAcos(1.0f) == 0.0f);
            CHECK(detAcos(-1.0f) == pi);
            CHECK(detAcos(0.0f) == detAtan2(1.0f, 0.0f));
            CHECK(std::isnan(detAcos(1.0001f)));
            CHECK(std::isnan(detAcos(-2.0f)));
            CHECK(std::isnan(detAcos(nan)));
        }

        SECTION("sin and cos")
        {
            CHECK(detSin(0.0f) == 0.0f);
            CHECK(detCos(0.0f) == 1.0f);
            CHECK(detSin(pi / 2.0f) == 1.0f);
            CHECK(std::isnan(detSin(inf)));
            CHECK(std::isnan(detCos(nan)));
            // far outside one turn, still exact to the float
            CHECK(ulpsApart(detSin(100000.0f), static_cast<float>(std::sin(100000.0))) <= 1);
        }

        SECTION("hypot")
        {
            CHECK(detHypot(3.0f, 4.0f) == 5.0f);
            CHECK(detHypot(-3.0f, 4.0f) == 5.0f);
            CHECK(detHypot(0.0f, 0.0f) == 0.0f);
            CHECK(detHypot(inf, nan) == inf);
            CHECK(std::isnan(detHypot(nan, 1.0f)));
            // would overflow a float's square
            CHECK(detHypot(1.0e30f, 1.0e30f) > 1.4e30f);
        }
    }
}
