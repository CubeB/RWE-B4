#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>
#include <rwe/mesh_util.h>

namespace rwe
{
    TEST_CASE("SHATTER takes the textured quads of the piece and nothing else", "[shatter]")
    {
        // 0x421784 wants exactly four corners, 0x42178E skips a flat-coloured
        // primitive and 0x42179F the selection plate.
        _3do::Object o{};
        o.vertices = {
            _3do::Vertex(0, 0, 0),
            _3do::Vertex(65536 * 4, 0, 0),
            _3do::Vertex(65536 * 4, 0, 65536 * 4),
            _3do::Vertex(0, 0, 65536 * 4),
            _3do::Vertex(0, 65536 * 8, 0),
        };
        _3do::Primitive texturedQuad{std::nullopt, {0, 1, 2, 3}, std::string("PLATE")};
        _3do::Primitive colouredQuad{7u, {0, 1, 2, 3}, std::nullopt};
        _3do::Primitive texturedTriangle{std::nullopt, {0, 1, 4}, std::string("PLATE")};
        _3do::Primitive selectionPlate{std::nullopt, {0, 1, 2, 3}, std::string("PLATE")};
        o.primitives = {texturedQuad, colouredQuad, texturedTriangle, selectionPlate};
        o.selectionPrimitiveIndex = 3u;

        std::unordered_map<std::string, Rectangle2f> atlas{{"PLATE", Rectangle2f::fromTLBR(0.0f, 0.0f, 0.5f, 0.5f)}};
        std::unordered_map<std::string, Rectangle2f> teamAtlas;
        auto fragments = fragmentSourcesFrom3do(atlas, teamAtlas, o);

        REQUIRE(fragments.size() == 1);
        // Two triangles, centred on the quad: the centre is where it sat.
        REQUIRE(fragments[0].mesh.faces.size() == 2);
        REQUIRE(fragments[0].mesh.teamFaces.empty());
        auto centre = vertexToVector(_3do::Vertex(65536 * 2, 0, 65536 * 2));
        REQUIRE(fragments[0].centre.x == Catch::Approx(centre.x));
        REQUIRE(fragments[0].centre.z == Catch::Approx(centre.z));
        for (const auto& t : fragments[0].mesh.faces)
        {
            for (const auto& v : {t.a, t.b, t.c})
            {
                REQUIRE(std::abs(v.position.x) == Catch::Approx(std::abs(centre.x)));
            }
        }
    }

    TEST_CASE("a SHATTER fragment is thrown the way 0x421700 throws it", "[shatter]")
    {
        const float g = 112.0f / 900.0f;

        SECTION("the lowest draws give the most negative scatter and spin")
        {
            // rand(n) returning n-1: (80 - 159) / 128 and 800 - 1599.
            auto m = throwShatterFragment(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), g, [](unsigned int n) { return n - 1; });
            REQUIRE(m.velocity.x == Catch::Approx(-79.0f / 128.0f));
            REQUIRE(m.velocity.z == Catch::Approx(-79.0f / 128.0f));
            REQUIRE(m.velocity.y == Catch::Approx(g * 30.0f - 79.0f / 128.0f));
            REQUIRE(m.spin.x == Catch::Approx(-799.0f * 2.0f * Pif / 65536.0f));
        }

        SECTION("half the unit's velocity rides along, and thirty ticks of gravity go upward")
        {
            // rand(n) returning 80 for the scatter and 800 for the spin: all zero.
            auto m = throwShatterFragment(Vector3f(1.0f, 2.0f, 3.0f), Vector3f(4.0f, 0.0f, -2.0f), g, [](unsigned int n) { return n / 2; });
            REQUIRE(m.position == Vector3f(1.0f, 2.0f, 3.0f));
            REQUIRE(m.velocity.x == Catch::Approx(2.0f));
            REQUIRE(m.velocity.z == Catch::Approx(-1.0f));
            REQUIRE(m.velocity.y == Catch::Approx(g * 30.0f));
            REQUIRE(m.spin == Vector3f(0.0f, 0.0f, 0.0f));
        }
    }

    TEST_CASE("a SHATTER fragment falls, bounces at half speed, and stops", "[shatter]")
    {
        const float g = 112.0f / 900.0f;
        auto flat = [](float, float) { return 0.0f; };

        SECTION("thrown straight up over dry ground it bounces until the rebound is under a unit a tick")
        {
            ShatterFragmentMotion m{Vector3f(0.0f, 1.0f, 0.0f), Vector3f(0.0f, 30.0f * g, 0.0f), Vector3f(), Vector3f()};
            int bounces = 0;
            ShatterFragmentFate fate = ShatterFragmentFate::Flying;
            float lastRebound = 100.0f;
            for (int t = 0; t < 2000 && fate == ShatterFragmentFate::Flying; ++t)
            {
                auto goingDown = m.velocity.y < 0.0f;
                fate = stepShatterFragment(m, g, -10.0f, flat);
                if (fate == ShatterFragmentFate::Flying && goingDown && m.velocity.y > 0.0f)
                {
                    ++bounces;
                    // Each rebound is half the speed it came down at, so no
                    // faster than the last.
                    REQUIRE(m.velocity.y < lastRebound);
                    lastRebound = m.velocity.y;
                    REQUIRE(m.velocity.y >= 1.0f);
                }
            }
            REQUIRE(fate == ShatterFragmentFate::Stopped);
            REQUIRE(bounces >= 1);
            REQUIRE(m.position.y > 0.0f);
        }

        SECTION("over the sea it sinks instead of bouncing")
        {
            auto seabed = [](float, float) { return -20.0f; };
            ShatterFragmentMotion m{Vector3f(0.0f, 5.0f, 0.0f), Vector3f(0.0f, -1.0f, 0.0f), Vector3f(), Vector3f()};
            ShatterFragmentFate fate = ShatterFragmentFate::Flying;
            for (int t = 0; t < 100 && fate == ShatterFragmentFate::Flying; ++t)
            {
                fate = stepShatterFragment(m, g, 0.0f, seabed);
            }
            REQUIRE(fate == ShatterFragmentFate::Sank);
        }

        SECTION("it spins by its rate every tick")
        {
            ShatterFragmentMotion m{Vector3f(0.0f, 50.0f, 0.0f), Vector3f(), Vector3f(), Vector3f(0.1f, -0.2f, 0.05f)};
            stepShatterFragment(m, g, -10.0f, flat);
            stepShatterFragment(m, g, -10.0f, flat);
            REQUIRE(m.rotation.x == Catch::Approx(0.2f));
            REQUIRE(m.rotation.y == Catch::Approx(-0.4f));
        }
    }
}
