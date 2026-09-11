#include <catch2/catch_test_macros.hpp>
#include <rwe/GlobalConfig.h>

namespace rwe
{
    TEST_CASE("ShadingMode")
    {
        SECTION("cycles through all four stages and wraps")
        {
            // The VISUALS button is a staged button: clicking it calls
            // nextStage once, so the cycle has to visit every mode and come
            // back, or a mode becomes unreachable from the UI.
            auto mode = ShadingMode::Off;
            mode = nextStage(mode);
            REQUIRE(mode == ShadingMode::UnitsOnly);
            mode = nextStage(mode);
            REQUIRE(mode == ShadingMode::BuildingsOnly);
            mode = nextStage(mode);
            REQUIRE(mode == ShadingMode::Both);
            mode = nextStage(mode);
            REQUIRE(mode == ShadingMode::Off);
        }

        SECTION("covers the categories its name claims")
        {
            REQUIRE(!shadingModeCoversUnits(ShadingMode::Off));
            REQUIRE(!shadingModeCoversBuildings(ShadingMode::Off));

            REQUIRE(shadingModeCoversUnits(ShadingMode::UnitsOnly));
            REQUIRE(!shadingModeCoversBuildings(ShadingMode::UnitsOnly));

            REQUIRE(!shadingModeCoversUnits(ShadingMode::BuildingsOnly));
            REQUIRE(shadingModeCoversBuildings(ShadingMode::BuildingsOnly));

            REQUIRE(shadingModeCoversUnits(ShadingMode::Both));
            REQUIRE(shadingModeCoversBuildings(ShadingMode::Both));
        }

        SECTION("labels are in stage order")
        {
            // The button is built from this list and its index IS the mode,
            // so a list in the wrong order would silently mislabel every
            // stage while still working.
            auto labels = shadingModeLabels();
            REQUIRE(labels.size() == 4);
            REQUIRE(labels[static_cast<unsigned int>(ShadingMode::Off)] == shadingModeDisplayName(ShadingMode::Off));
            REQUIRE(labels[static_cast<unsigned int>(ShadingMode::UnitsOnly)] == shadingModeDisplayName(ShadingMode::UnitsOnly));
            REQUIRE(labels[static_cast<unsigned int>(ShadingMode::BuildingsOnly)] == shadingModeDisplayName(ShadingMode::BuildingsOnly));
            REQUIRE(labels[static_cast<unsigned int>(ShadingMode::Both)] == shadingModeDisplayName(ShadingMode::Both));
        }
    }

    TEST_CASE("optionsFromConfig")
    {
        SECTION("carries the shading mode across as a mode, not a bool")
        {
            // rwe.cfg stores the mode as a number, and the options screens
            // deal in the enum. This is the one place the two meet.
            GlobalConfig config;
            config.shadingMode = static_cast<unsigned int>(ShadingMode::BuildingsOnly);
            REQUIRE(optionsFromConfig(config).shading == ShadingMode::BuildingsOnly);

            config.shadingMode = static_cast<unsigned int>(ShadingMode::Off);
            REQUIRE(optionsFromConfig(config).shading == ShadingMode::Off);
        }

        SECTION("defaults to shading buildings only, as the original does")
        {
            REQUIRE(optionsFromConfig(GlobalConfig()).shading == ShadingMode::BuildingsOnly);
        }
    }
}
