#include <catch2/catch_approx.hpp>
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

        SECTION("carries the camera zoom across")
        {
            GlobalConfig config;
            config.cameraZoom = 150;
            REQUIRE(optionsFromConfig(config).cameraZoom == 150u);
        }

        SECTION("carries the UI scale across")
        {
            GlobalConfig config;
            config.uiScale = 150;
            REQUIRE(optionsFromConfig(config).uiScale == 150u);
        }
    }

    TEST_CASE("UI scale helpers")
    {
        SECTION("stages are Auto and the five percentages in order")
        {
            auto stages = uiScaleStages();
            REQUIRE(stages.size() == 6);
            REQUIRE(stages[0] == 0u);
            REQUIRE(stages[1] == 100u);
            REQUIRE(stages[2] == 150u);
            REQUIRE(stages[3] == 200u);
            REQUIRE(stages[4] == 250u);
            REQUIRE(stages[5] == 300u);
        }

        SECTION("next cycles through every stage and wraps")
        {
            REQUIRE(nextUiScale(0u) == 100u);
            REQUIRE(nextUiScale(100u) == 150u);
            REQUIRE(nextUiScale(150u) == 200u);
            REQUIRE(nextUiScale(200u) == 250u);
            REQUIRE(nextUiScale(250u) == 300u);
            REQUIRE(nextUiScale(300u) == 0u);
        }

        SECTION("a value not in the stages starts from Auto and advances")
        {
            REQUIRE(nextUiScale(400u) == 100u);
            REQUIRE(nextUiScale(99u) == 100u);
        }

        SECTION("stage index round trips every stage")
        {
            auto stages = uiScaleStages();
            for (unsigned int i = 0; i < stages.size(); ++i)
            {
                REQUIRE(uiScaleStageIndex(stages[i]) == i);
            }
        }

        SECTION("an unknown value reads as the Auto stage")
        {
            REQUIRE(uiScaleStageIndex(400u) == uiScaleStageIndex(0u));
            REQUIRE(uiScaleStageIndex(99u) == uiScaleStageIndex(0u));
        }

        SECTION("labels are in stage order")
        {
            auto labels = uiScaleLabels();
            REQUIRE(labels.size() == uiScaleStages().size());
            REQUIRE(labels[0] == "UI Auto");
            REQUIRE(labels[1] == "UI 1x");
            REQUIRE(labels[2] == "UI 1.5x");
            REQUIRE(labels[3] == "UI 2x");
            REQUIRE(labels[4] == "UI 2.5x");
            REQUIRE(labels[5] == "UI 3x");
        }

        // A 4K frame, big enough that only the 0.5..3 clamp applies.
        const int w = 3840;
        const int h = 2160;

        SECTION("Auto snaps the content scale to the nearest half")
        {
            REQUIRE(resolveUiScale(0u, 1.0f, w, h) == Catch::Approx(1.0f));
            REQUIRE(resolveUiScale(0u, 1.2f, w, h) == Catch::Approx(1.0f));
            REQUIRE(resolveUiScale(0u, 1.3f, w, h) == Catch::Approx(1.5f));
            REQUIRE(resolveUiScale(0u, 1.5f, w, h) == Catch::Approx(1.5f));
            REQUIRE(resolveUiScale(0u, 2.0f, w, h) == Catch::Approx(2.0f));
            REQUIRE(resolveUiScale(0u, 3.0f, w, h) == Catch::Approx(3.0f));
            REQUIRE(resolveUiScale(0u, 4.0f, w, h) == Catch::Approx(3.0f));
            REQUIRE(resolveUiScale(0u, 0.0f, w, h) == Catch::Approx(0.5f));
        }

        SECTION("an explicit percentage is the scale, clamped")
        {
            REQUIRE(resolveUiScale(100u, 2.0f, w, h) == Catch::Approx(1.0f));
            REQUIRE(resolveUiScale(150u, 1.0f, w, h) == Catch::Approx(1.5f));
            REQUIRE(resolveUiScale(250u, 1.0f, w, h) == Catch::Approx(2.5f));
            REQUIRE(resolveUiScale(300u, 1.0f, w, h) == Catch::Approx(3.0f));
            REQUIRE(resolveUiScale(30u, 1.0f, w, h) == Catch::Approx(0.5f));
            REQUIRE(resolveUiScale(500u, 1.0f, w, h) == Catch::Approx(3.0f));
        }

        SECTION("no scale leaves less than 640x480 of layout")
        {
            // 1080p holds 2x (960x540) and 2.25x, but not 2.5x (768x432).
            REQUIRE(resolveUiScale(300u, 1.0f, 1920, 1080) == Catch::Approx(2.0f));
            REQUIRE(resolveUiScale(250u, 1.0f, 1920, 1080) == Catch::Approx(2.0f));
            REQUIRE(resolveUiScale(0u, 3.0f, 1920, 1080) == Catch::Approx(2.0f));
            REQUIRE(resolveUiScale(200u, 1.0f, 1280, 960) == Catch::Approx(2.0f));
            REQUIRE(resolveUiScale(200u, 1.0f, 1279, 960) == Catch::Approx(1.5f));
            // A frame smaller than the layout shrinks it as far as 0.5x.
            REQUIRE(resolveUiScale(100u, 1.0f, 320, 240) == Catch::Approx(0.5f));
            REQUIRE(resolveUiScale(100u, 1.0f, 100, 100) == Catch::Approx(0.5f));
        }
    }
}
