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
            config.uiScale = 2;
            REQUIRE(optionsFromConfig(config).uiScale == 2u);
        }
    }

    TEST_CASE("camera zoom helpers")
    {
        SECTION("stages are the four percentages in order")
        {
            auto stages = cameraZoomStages();
            REQUIRE(stages.size() == 4);
            REQUIRE(stages[0] == 50u);
            REQUIRE(stages[1] == 100u);
            REQUIRE(stages[2] == 150u);
            REQUIRE(stages[3] == 200u);
        }

        SECTION("next cycles through every stage and wraps")
        {
            REQUIRE(nextCameraZoom(50u) == 100u);
            REQUIRE(nextCameraZoom(100u) == 150u);
            REQUIRE(nextCameraZoom(150u) == 200u);
            REQUIRE(nextCameraZoom(200u) == 50u);
        }

        SECTION("a value not in the stages starts from 100 and advances")
        {
            REQUIRE(nextCameraZoom(0u) == 150u);
            REQUIRE(nextCameraZoom(120u) == 150u);
            REQUIRE(nextCameraZoom(1000u) == 150u);
        }

        SECTION("stage index round trips every stage")
        {
            auto stages = cameraZoomStages();
            for (unsigned int i = 0; i < stages.size(); ++i)
            {
                REQUIRE(cameraZoomStageIndex(stages[i]) == i);
            }
        }

        SECTION("an unknown value reads as the 100 stage")
        {
            REQUIRE(cameraZoomStageIndex(0u) == cameraZoomStageIndex(100u));
            REQUIRE(cameraZoomStageIndex(120u) == cameraZoomStageIndex(100u));
        }

        SECTION("labels are in stage order and name the percentage")
        {
            auto labels = cameraZoomLabels();
            REQUIRE(labels.size() == cameraZoomStages().size());
            REQUIRE(labels[0] == "Zoom 50%");
            REQUIRE(labels[1] == "Zoom 100%");
            REQUIRE(labels[2] == "Zoom 150%");
            REQUIRE(labels[3] == "Zoom 200%");
        }
    }

    TEST_CASE("UI scale helpers")
    {
        SECTION("stages are Auto and the three integer scales in order")
        {
            auto stages = uiScaleStages();
            REQUIRE(stages.size() == 4);
            REQUIRE(stages[0] == 0u);
            REQUIRE(stages[1] == 1u);
            REQUIRE(stages[2] == 2u);
            REQUIRE(stages[3] == 3u);
        }

        SECTION("next cycles through every stage and wraps")
        {
            REQUIRE(nextUiScale(0u) == 1u);
            REQUIRE(nextUiScale(1u) == 2u);
            REQUIRE(nextUiScale(2u) == 3u);
            REQUIRE(nextUiScale(3u) == 0u);
        }

        SECTION("a value not in the stages starts from Auto and advances")
        {
            REQUIRE(nextUiScale(4u) == 1u);
            REQUIRE(nextUiScale(99u) == 1u);
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
            REQUIRE(uiScaleStageIndex(4u) == uiScaleStageIndex(0u));
            REQUIRE(uiScaleStageIndex(99u) == uiScaleStageIndex(0u));
        }

        SECTION("labels are in stage order")
        {
            auto labels = uiScaleLabels();
            REQUIRE(labels.size() == uiScaleStages().size());
            REQUIRE(labels[0] == "UI Auto");
            REQUIRE(labels[1] == "UI 1x");
            REQUIRE(labels[2] == "UI 2x");
            REQUIRE(labels[3] == "UI 3x");
        }

        SECTION("Auto follows the display density, rounded and clamped to 1..3")
        {
            REQUIRE(resolveUiScale(0u, 1.0f) == 1u);
            REQUIRE(resolveUiScale(0u, 2.0f) == 2u);
            REQUIRE(resolveUiScale(0u, 1.5f) == 2u);
            REQUIRE(resolveUiScale(0u, 3.0f) == 3u);
            REQUIRE(resolveUiScale(0u, 4.0f) == 3u);
            REQUIRE(resolveUiScale(0u, 0.0f) == 1u);
        }

        SECTION("an explicit scale passes through and clamps out of range")
        {
            REQUIRE(resolveUiScale(1u, 2.0f) == 1u);
            REQUIRE(resolveUiScale(2u, 1.0f) == 2u);
            REQUIRE(resolveUiScale(3u, 1.0f) == 3u);
            REQUIRE(resolveUiScale(4u, 1.0f) == 3u);
        }
    }
}
