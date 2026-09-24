#include <catch2/catch_test_macros.hpp>
#include <rwe/io/moveinfotdf/io.h>
#include <rwe/io/tdf/tdf.h>

namespace rwe
{
    TEST_CASE("BadSlope and BadWaterSlope are read, seeded, and clamped the way the original does", "[moveinfotdf]")
    {
        // TOTALA-EXE-MOVEMENT.md section 95: each defaults to half the
        // corresponding max (0x4403B9, 0x4403E7) and is then clamped to it
        // (0x440400-0x440417).
        SECTION("the shipped hover class names both equal to its max")
        {
            auto tdf = parseTdfFromString(R"(
[CLASS1]
{
    Name=TANKHOVER3;
    FootprintX=3;
    FootprintZ=3;
    MaxSlope=12;
    BadSlope=12;
    MaxWaterSlope=12;
    BadWaterSlope=12;
}
)");
            auto classes = parseMoveInfoTdf(tdf);
            REQUIRE(classes.size() == 1);
            const auto& c = classes[0].second;
            REQUIRE(c.maxSlope == 12u);
            REQUIRE(c.badSlope == 12u);
            REQUIRE(c.maxWaterSlope == 12u);
            REQUIRE(c.badWaterSlope == 12u);
        }

        SECTION("a class that names neither gets half of each max")
        {
            auto tdf = parseTdfFromString(R"(
[CLASS1]
{
    Name=TANKSH2;
    FootprintX=2;
    FootprintZ=2;
    MaxSlope=14;
    MaxWaterSlope=20;
}
)");
            const auto& c = parseMoveInfoTdf(tdf)[0].second;
            REQUIRE(c.badSlope == 7u);
            REQUIRE(c.badWaterSlope == 10u);
        }

        SECTION("a class that names nothing at all is seeded from the 255 defaults")
        {
            auto tdf = parseTdfFromString(R"(
[CLASS1]
{
    Name=ANYWHERE;
    FootprintX=1;
    FootprintZ=1;
}
)");
            const auto& c = parseMoveInfoTdf(tdf)[0].second;
            REQUIRE(c.maxSlope == 255u);
            REQUIRE(c.badSlope == 127u);
            REQUIRE(c.badWaterSlope == 127u);
        }

        SECTION("a free threshold above the max is clamped to the max")
        {
            auto tdf = parseTdfFromString(R"(
[CLASS1]
{
    Name=ODD;
    FootprintX=1;
    FootprintZ=1;
    MaxSlope=10;
    BadSlope=40;
    MaxWaterSlope=8;
    BadWaterSlope=9;
}
)");
            const auto& c = parseMoveInfoTdf(tdf)[0].second;
            REQUIRE(c.badSlope == 10u);
            REQUIRE(c.badWaterSlope == 8u);
        }
    }
}
