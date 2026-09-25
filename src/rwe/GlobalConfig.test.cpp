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
}
