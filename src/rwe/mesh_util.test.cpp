#include <catch2/catch_test_macros.hpp>
#include <rwe/mesh_util.h>

namespace rwe
{
    namespace
    {
        constexpr int One = 65536;

        _3do::Vertex corner(int x, int y, int z)
        {
            return _3do::Vertex{x * One, y * One, z * One};
        }

        _3do::Primitive polygon(std::vector<unsigned int> vertices)
        {
            return _3do::Primitive{std::nullopt, std::move(vertices), std::nullopt};
        }
    }

    TEST_CASE("polygonEdgesFrom3do outlines a flat face once", "[mesh][wireframe]")
    {
        SECTION("two polygons side by side in one plane outline as one rectangle")
        {
            // The shared edge is named by two different pairs of vertex
            // indices, as a 3DO usually names it, and still goes.
            _3do::Object o{};
            o.vertices = {
                corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1),
                corner(1, 0, 0), corner(2, 0, 0), corner(2, 0, 1), corner(1, 0, 1)};
            o.primitives = {polygon({0, 1, 2, 3}), polygon({4, 5, 6, 7})};

            REQUIRE(polygonEdgesFrom3do(o).size() == 6);
        }

        SECTION("two polygons meeting at a fold keep the edge between them")
        {
            _3do::Object o{};
            o.vertices = {
                corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1),
                corner(0, 1, 0), corner(1, 1, 0)};
            o.primitives = {polygon({0, 1, 2, 3}), polygon({1, 0, 4, 5})};

            REQUIRE(polygonEdgesFrom3do(o).size() == 7);
        }

        SECTION("the two sides of a thin plate keep their rim")
        {
            // Same corners, opposite windings: the faces point opposite ways,
            // so every edge they share is an edge of the plate.
            _3do::Object o{};
            o.vertices = {corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1)};
            o.primitives = {polygon({0, 1, 2, 3}), polygon({3, 2, 1, 0})};

            auto edges = polygonEdgesFrom3do(o);
            REQUIRE(edges.size() == 4);
            for (const auto& edge : edges)
            {
                REQUIRE(edge.normalB.has_value());
            }
        }
    }
}
