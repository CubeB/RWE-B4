#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>

namespace rwe
{
    namespace
    {
        // The world projection the game scene builds: an orthographic box
        // over a cabinet shear, which is what makes the test exact. There is
        // no perspective divide, so a world point lands in clip space with a
        // single multiply and a model's own size scales by a constant.
        Matrix4f testViewProjection(float viewWidth, float viewHeight)
        {
            auto cabinet = Matrix4f::cabinetProjection(0.0f, 0.5f);
            auto ortho = Matrix4f::orthographicProjection(
                -viewWidth / 2.0f,
                viewWidth / 2.0f,
                -viewHeight / 2.0f,
                viewHeight / 2.0f,
                -1000.0f,
                1000.0f);
            return ortho * cabinet;
        }
    }

    TEST_CASE("ViewCullTest: what is on screen is kept", "[viewcull]")
    {
        auto cull = makeViewCullTest(testViewProjection(1280.0f, 800.0f));

        REQUIRE(cull.couldBeVisible(Vector3f(0.0f, 0.0f, 0.0f), ViewCullModelRadius));
        REQUIRE(cull.couldBeVisible(Vector3f(600.0f, 0.0f, 0.0f), ViewCullModelRadius));
        REQUIRE(cull.couldBeVisible(Vector3f(-600.0f, 0.0f, 0.0f), ViewCullModelRadius));
        REQUIRE(cull.couldBeVisible(Vector3f(0.0f, 0.0f, 380.0f), ViewCullModelRadius));
        REQUIRE(cull.couldBeVisible(Vector3f(0.0f, 0.0f, -380.0f), ViewCullModelRadius));
    }

    TEST_CASE("ViewCullTest: a model whose origin is just outside is still kept", "[viewcull]")
    {
        // The point of the radius. A unit standing a little past the edge can
        // still have its near side, or the top of it, inside the view, and
        // culling on the origin alone would pop it out.
        auto cull = makeViewCullTest(testViewProjection(1280.0f, 800.0f));

        REQUIRE(cull.couldBeVisible(Vector3f(640.0f + ViewCullModelRadius, 0.0f, 0.0f), ViewCullModelRadius));
        REQUIRE(cull.couldBeVisible(Vector3f(0.0f, 0.0f, 400.0f + ViewCullModelRadius), ViewCullModelRadius));

        // Height too: the cabinet shear puts a tall model further up the
        // screen than its origin, so the margin has to cover that as well.
        REQUIRE(cull.couldBeVisible(Vector3f(0.0f, ViewCullModelRadius, 400.0f), ViewCullModelRadius));
    }

    TEST_CASE("ViewCullTest: what is far outside is dropped", "[viewcull]")
    {
        auto cull = makeViewCullTest(testViewProjection(1280.0f, 800.0f));

        REQUIRE(!cull.couldBeVisible(Vector3f(8000.0f, 0.0f, 0.0f), ViewCullModelRadius));
        REQUIRE(!cull.couldBeVisible(Vector3f(-8000.0f, 0.0f, 0.0f), ViewCullModelRadius));
        REQUIRE(!cull.couldBeVisible(Vector3f(0.0f, 0.0f, 8000.0f), ViewCullModelRadius));
        REQUIRE(!cull.couldBeVisible(Vector3f(0.0f, 0.0f, -8000.0f), ViewCullModelRadius));
    }

    TEST_CASE("ViewCullTest: a zoomed-out view keeps what a zoomed-in one drops", "[viewcull]")
    {
        // The margin is worked out from the matrix rather than fixed in clip
        // space, so it follows the zoom instead of covering half the screen
        // at one scale and nothing at another.
        auto zoomedIn = makeViewCullTest(testViewProjection(640.0f, 400.0f));
        auto zoomedOut = makeViewCullTest(testViewProjection(5120.0f, 3200.0f));

        Vector3f point(1200.0f, 0.0f, 0.0f);

        REQUIRE(!zoomedIn.couldBeVisible(point, ViewCullModelRadius));
        REQUIRE(zoomedOut.couldBeVisible(point, ViewCullModelRadius));
    }
}
