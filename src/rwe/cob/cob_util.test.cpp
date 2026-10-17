#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <rapidcheck.h>
#include <rapidcheck/catch.h>
#include <rwe/cob/cob_util.h>
#include <rwe/math/rwe_math.h>

namespace rwe
{
    TEST_CASE("cob_util")
    {
        SECTION("cobPackCoords")
        {
            SECTION("packs trivial positive values")
            {
                auto a = CobPosition::fromInt(5); // 0x0005
                auto b = CobPosition::fromInt(3); // 0x0003
                REQUIRE(cobPackCoords(a, b) == 0x00050003);
            }

            SECTION("packs negative values")
            {
                auto a = CobPosition::fromInt(-5); // 0xfffb
                auto b = CobPosition::fromInt(-3); // 0xfffd
                // BT-76: packed by addition, so the negative Z (all ones in
                // the top half) borrows one from X.
                REQUIRE(cobPackCoords(a, b) == 0xfffafffd);
            }

            SECTION("packs very low negative values")
            {
                auto a = CobPosition::fromInt(-28675); // 0x8ffd
                auto b = CobPosition::fromInt(-24412); // 0xa0a4
                REQUIRE(cobPackCoords(a, b) == 0x8ffca0a4);
            }

            SECTION("a non-negative Z leaves X alone, as an OR would")
            {
                REQUIRE(cobPackCoords(CobPosition::fromInt(100), CobPosition::fromInt(50)) == 0x00640032);
            }
        }

        SECTION("packed values subtract to the offset between the two points (BT-76)")
        {
            // How the four boom transports use it: the script subtracts the
            // packed position of the boom piece from the packed position of
            // the cargo and decodes the result. With a negative Z difference
            // the X half of an OR-packed value comes out one too small.
            auto offset = [](int cargoX, int cargoZ, int pieceX, int pieceZ) {
                auto cargo = cobPackCoords(CobPosition::fromInt(cargoX), CobPosition::fromInt(cargoZ));
                auto piece = cobPackCoords(CobPosition::fromInt(pieceX), CobPosition::fromInt(pieceZ));
                return cobUnpackCoords(cargo - piece);
            };

            auto zNegative = offset(90, 30, 100, 50);
            REQUIRE(zNegative.first == CobPosition::fromInt(-10));
            REQUIRE(zNegative.second == CobPosition::fromInt(-20));

            auto zPositive = offset(120, 70, 100, 50);
            REQUIRE(zPositive.first == CobPosition::fromInt(20));
            REQUIRE(zPositive.second == CobPosition::fromInt(20));

            auto bothNegative = offset(-300, -250, -200, -120);
            REQUIRE(bothNegative.first == CobPosition::fromInt(-100));
            REQUIRE(bothNegative.second == CobPosition::fromInt(-130));
        }

        SECTION("an encoded negative Z is decoded with the borrow given back")
        {
            // 0xffff in the low half is Z = -1; the top half holds X - 1.
            auto packed = cobPackCoords(CobPosition::fromInt(7), CobPosition::fromInt(-1));
            REQUIRE(packed == 0x0006ffff);
            auto unpacked = cobUnpackCoords(packed);
            REQUIRE(unpacked.first == CobPosition::fromInt(7));
            REQUIRE(unpacked.second == CobPosition::fromInt(-1));
        }

        SECTION("cobUnpackCoords")
        {
            rc::prop("pack followed by unpack is the identity", [](int16_t a, int16_t b) {
                auto result = cobUnpackCoords(cobPackCoords(CobPosition::fromInt(a), CobPosition::fromInt(b)));
                RC_ASSERT(result.first == CobPosition::fromInt(a));
                RC_ASSERT(result.second == CobPosition::fromInt(b));
            });
        }

        SECTION("cobAtan")
        {
            REQUIRE(cobAtan(0, 0) == 0);
            REQUIRE(cobAtan(1, 1) == 8192);
            REQUIRE(cobAtan(1, 0) == 16384);
            REQUIRE(cobAtan(0, -1) == 32768);
            REQUIRE(cobAtan(-1, 0) == 49152);
            REQUIRE(cobAtan(0, 1) == 0);
        }

        SECTION("toRadians")
        {
            REQUIRE(toRadians(CobAngle(0)).value == Catch::Approx(0.0f));
            REQUIRE(toRadians(CobAngle(8192)).value == Catch::Approx(Pif / 4.0f));
            REQUIRE(toRadians(CobAngle(16384)).value == Catch::Approx(Pif / 2.0f));
            REQUIRE(toRadians(CobAngle(32768)).value == Catch::Approx(-Pif));
            REQUIRE(toRadians(CobAngle(49152)).value == Catch::Approx(-Pif / 2.0f));
        }

        SECTION("toCobAngle")
        {
            rc::prop("toCobAngle inverts toRadians", [](uint16_t a) {
                CobAngle t(a);
                auto t2 = toCobAngle(toRadians(t));
                RC_ASSERT(t2.value == t.value);
            });
        }
    }
}
