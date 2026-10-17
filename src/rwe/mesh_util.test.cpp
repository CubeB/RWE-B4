#include <catch2/catch_test_macros.hpp>
#include <rwe/mesh_util.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitModelVertices.h>

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

    TEST_CASE("unitVerticesFrom3do keeps the root's selection rectangle (BT-41)", "[mesh][bt41]")
    {
        _3do::Object o{};
        o.vertices = {corner(-3, 0, -5), corner(3, 0, -5), corner(3, 0, 5), corner(-3, 0, 5), corner(0, 9, 0)};

        SECTION("the first four vertices of the selection primitive, in its own order, z negated")
        {
            o.primitives = {polygon({0, 1, 4}), polygon({2, 3, 0, 1})};
            o.selectionPrimitiveIndex = 1;

            auto table = unitVerticesFrom3do(o);
            REQUIRE(table->selectionRectangle.has_value());
            const auto& rectangle = *table->selectionRectangle;
            auto is = [](const SimVector& v, float x, float z) { return v.x.value == x && v.y.value == 0.0f && v.z.value == z; };
            REQUIRE(is(rectangle[0], 3.0f, -5.0f));
            REQUIRE(is(rectangle[1], -3.0f, -5.0f));
            REQUIRE(is(rectangle[2], -3.0f, 5.0f));
            REQUIRE(is(rectangle[3], 3.0f, 5.0f));
        }

        SECTION("none when the root has no selection primitive")
        {
            o.primitives = {polygon({0, 1, 2, 3})};
            REQUIRE_FALSE(unitVerticesFrom3do(o)->selectionRectangle.has_value());
        }

        SECTION("none when the primitive names fewer than four corners or one that is not there")
        {
            o.primitives = {polygon({0, 1, 2}), polygon({0, 1, 2, 9})};
            o.selectionPrimitiveIndex = 0;
            REQUIRE_FALSE(unitVerticesFrom3do(o)->selectionRectangle.has_value());
            o.selectionPrimitiveIndex = 1;
            REQUIRE_FALSE(unitVerticesFrom3do(o)->selectionRectangle.has_value());
        }
    }
}
