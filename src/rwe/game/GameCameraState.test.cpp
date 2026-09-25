#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameCameraState.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimScalar.h>
#include <utility>

namespace rwe
{
    TEST_CASE("GameCameraState::scaleDimension")
    {
        GameCameraState camera;

        camera.zoom = 1.0f;
        REQUIRE(camera.scaleDimension(100.0f) == Catch::Approx(100.0f));

        // Zoom is the reciprocal of the visible extent: half the zoom draws
        // twice as much world across the same screen.
        camera.zoom = 0.5f;
        REQUIRE(camera.scaleDimension(100.0f) == Catch::Approx(200.0f));

        camera.zoom = 2.0f;
        REQUIRE(camera.scaleDimension(100.0f) == Catch::Approx(50.0f));
    }

    TEST_CASE("GameCameraState::scaleDimension folds in display density")
    {
        GameCameraState camera;
        camera.zoom = 1.0f;
        camera.density = 2.0f;
        REQUIRE(camera.scaleDimension(100.0f) == Catch::Approx(50.0f));

        // Parity: at a fixed zoom the world extent in logical points does not
        // depend on the density, because the frame and the divisor scale
        // together.
        camera.zoom = 0.5f;
        REQUIRE(camera.scaleDimension(100.0f) == Catch::Approx(100.0f));
    }

    TEST_CASE("computeCameraConstraint centres a view larger than the map")
    {
        Grid<unsigned char> heights(16, 16, static_cast<unsigned char>(0));
        MapTerrain terrain(std::move(heights), 0_ss);

        auto mapLeft = simScalarToFloat(terrain.leftInWorldUnits());
        auto mapRight = simScalarToFloat(terrain.rightCutoffInWorldUnits());
        auto mapTop = simScalarToFloat(terrain.topInWorldUnits());
        auto mapBottom = simScalarToFloat(terrain.bottomCutoffInWorldUnits());

        // A viewport bigger than the map collapses each axis to its midpoint,
        // which is what keeps a fully-zoomed-out view centred on the map
        // rather than pinned to a corner.
        auto constraint = computeCameraConstraint(terrain, 100000.0f, 100000.0f);
        REQUIRE(constraint.left() == Catch::Approx(constraint.right()));
        REQUIRE(constraint.top() == Catch::Approx(constraint.bottom()));
        REQUIRE(constraint.left() == Catch::Approx((mapLeft + mapRight) / 2.0f));
        REQUIRE(constraint.top() == Catch::Approx((mapTop + mapBottom) / 2.0f));
    }

    TEST_CASE("camera projection round trip")
    {
        const int width = 1280;
        const int height = 800;
        const Vector3f world(123.0f, 32.0f, 456.0f);

        for (auto zoom : {1.0f, 0.5f, 2.0f})
        {
            GameCameraState camera;
            camera.zoom = zoom;

            auto viewProjection = computeViewProjectionMatrix(camera, width, height);
            auto inverseViewProjection = computeInverseViewProjectionMatrix(camera, width, height);

            auto clip = viewProjection * world;
            auto back = inverseViewProjection * clip;

            REQUIRE(back.x == Catch::Approx(world.x).margin(0.01f));
            REQUIRE(back.y == Catch::Approx(world.y).margin(0.01f));
            REQUIRE(back.z == Catch::Approx(world.z).margin(0.01f));
        }
    }

    TEST_CASE("advanceCameraZoom")
    {
        SECTION("converges to the target")
        {
            float zoom = 1.0f;
            for (int i = 0; i < 500; ++i)
            {
                zoom = advanceCameraZoom(zoom, 2.0f, 10);
            }
            REQUIRE(zoom == Catch::Approx(2.0f).margin(1e-5f));
        }

        SECTION("is frame-rate independent")
        {
            // An exact exponential approach, so ten short steps have to land
            // in the same place as one long one.
            auto oneStep = advanceCameraZoom(1.0f, 2.0f, 100);

            auto manySteps = 1.0f;
            for (int i = 0; i < 10; ++i)
            {
                manySteps = advanceCameraZoom(manySteps, 2.0f, 10);
            }

            REQUIRE(oneStep == Catch::Approx(manySteps).margin(1e-6f));
        }

        SECTION("an already-arrived zoom stays put")
        {
            REQUIRE(advanceCameraZoom(1.5f, 1.5f, 16) == Catch::Approx(1.5f));
        }
    }
}
