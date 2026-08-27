#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/sim/GameSimulation.h>

namespace rwe
{
    namespace
    {
        GamePlayerInfo makePlayer(float metal, float energy)
        {
            return GamePlayerInfo{
                std::optional<std::string>("player"),
                GamePlayerType::Human,
                PlayerColorIndex(0),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(metal),
                Energy(energy),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(1000.0f),
                Energy(1000.0f),
            };
        }

        bool spend(GamePlayerInfo& p, float energy, float metal)
        {
            return p.addResourceDelta(Energy(-energy), Metal(-metal), Energy(-energy), Metal(-metal));
        }
    }

    TEST_CASE("GamePlayerInfo::addResourceDelta", "[economy]")
    {
        SECTION("spending from a full stockpile is accepted and booked")
        {
            auto p = makePlayer(100.0f, 100.0f);
            REQUIRE(spend(p, 10.0f, 30.0f));
            REQUIRE(p.actualMetalConsumptionBuffer.value == Catch::Approx(30.0f));
            REQUIRE(p.actualEnergyConsumptionBuffer.value == Catch::Approx(10.0f));
            REQUIRE_FALSE(p.metalStalled);
            REQUIRE_FALSE(p.energyStalled);
        }

        SECTION("an empty stockpile still lets income earned this second be spent as it arrives")
        {
            auto p = makePlayer(0.0f, 100.0f);
            p.metalProductionBuffer = Metal(5.0f);

            REQUIRE(spend(p, 0.0f, 3.0f));
            REQUIRE_FALSE(spend(p, 0.0f, 3.0f)); // only 2 left
            REQUIRE(p.metalStalled);
            REQUIRE(spend(p, 0.0f, 2.0f));
            REQUIRE_FALSE(spend(p, 0.0f, 1.0f));

            // The refusals were still recorded as demand for the display.
            REQUIRE(p.desiredMetalConsumptionBuffer.value == Catch::Approx(9.0f));
            // Only what was affordable was actually taken.
            REQUIRE(p.actualMetalConsumptionBuffer.value == Catch::Approx(5.0f));
        }

        SECTION("spending is refused as a whole when either resource is short")
        {
            auto p = makePlayer(100.0f, 0.0f);
            REQUIRE_FALSE(spend(p, 1.0f, 1.0f));
            REQUIRE(p.energyStalled);
            REQUIRE_FALSE(p.metalStalled);
            REQUIRE(p.actualMetalConsumptionBuffer.value == Catch::Approx(0.0f));
        }

        SECTION("income is always accepted")
        {
            auto p = makePlayer(0.0f, 0.0f);
            REQUIRE(p.addResourceDelta(Energy(5.0f), Metal(5.0f), Energy(5.0f), Metal(5.0f)));
            REQUIRE(p.metalProductionBuffer.value == Catch::Approx(5.0f));
            REQUIRE(p.energyProductionBuffer.value == Catch::Approx(5.0f));
        }
    }
}
