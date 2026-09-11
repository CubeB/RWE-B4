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

    TEST_CASE("wireframePolygonsFrom3do", "[mesh][wireframe]")
    {
        _3do::Object o{};
        o.vertices = {corner(0, 0, 0), corner(1, 0, 0), corner(1, 0, 1), corner(0, 0, 1), corner(0, 1, 0)};

        SECTION("keeps each polygon's corners in the file's order")
        {
            // The order is what tells the wireframe which way a polygon faces.
            o.primitives = {polygon({0, 1, 2, 3}), polygon({3, 2, 1, 0})};

            auto polygons = wireframePolygonsFrom3do(o);
            REQUIRE(polygons.size() == 2);
            REQUIRE(polygons[0].vertices.size() == 4);
            REQUIRE(polygons[0].vertices[1] == vertexToVector(o.vertices[1]));
            REQUIRE(polygons[1].vertices[1] == vertexToVector(o.vertices[2]));
        }

        SECTION("leaves out the selection plate")
        {
            o.primitives = {polygon({0, 1, 2, 3}), polygon({0, 1, 4})};
            o.selectionPrimitiveIndex = 0;

            auto polygons = wireframePolygonsFrom3do(o);
            REQUIRE(polygons.size() == 1);
            REQUIRE(polygons[0].vertices.size() == 3);
        }

        SECTION("leaves out lines, points and polygons naming a corner that is not there")
        {
            o.primitives = {polygon({0, 1}), polygon({4}), polygon({0, 1, 9})};

            REQUIRE(wireframePolygonsFrom3do(o).empty());
        }
    }
}
