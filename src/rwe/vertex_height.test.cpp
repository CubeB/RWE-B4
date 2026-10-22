#include <catch2/catch_test_macros.hpp>
#include <rwe/MeshService.h>
#include <rwe/util/SpanStream.h>
#include <rwe/vertex_height.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <cstdlib>

namespace rwe
{
    namespace
    {
        _3do::Object heightPiece(int offset, std::vector<int> vertices, std::vector<_3do::Object> children = {})
        {
            _3do::Object piece{};
            piece.y = offset;
            for (auto y : vertices)
            {
                piece.vertices.emplace_back(0, y, 0);
            }
            piece.children = std::move(children);
            return piece;
        }
    }

    TEST_CASE("model height floors each translated recursive list", "[modelheight]")
    {
        // Arithmetic fixtures from BT-MODEL-HEIGHT-SCROLLBAR-FINDINGS.
        // These mod-only edge cases have no shipped counterpart.
        REQUIRE(modelHeightFrom3do(heightPiece(-20, {5})) == 0);
        REQUIRE(modelHeightFrom3do(heightPiece(20, {-5})) == 15);
        REQUIRE(modelHeightFrom3do(heightPiece(20, {}, {heightPiece(0, {-5})})) == 20);
        REQUIRE(modelHeightFrom3do(heightPiece(-20, {}, {heightPiece(5, {30})})) == 15);
        REQUIRE(modelHeightFrom3do(heightPiece(20, {})) == 0);
        REQUIRE(modelHeightFrom3do(heightPiece(20, {}, {heightPiece(50, {})})) == 20);
        REQUIRE(modelHeightFrom3do(heightPiece(-20, {}, {heightPiece(50, {})})) == 0);

        // Siblings are independently translated, rather than accumulating offsets.
        REQUIRE(modelHeightFrom3do(heightPiece(0, {}, {
            heightPiece(100, {-95}), heightPiece(0, {10})})) == 10);
        REQUIRE(modelHeightFrom3do(heightPiece(0, {30}, {heightPiece(0, {5})})) == 30);
    }

    TEST_CASE("retail model heights retain their shipped values through both mesh loaders", "[modelheight][retail]")
    {
        const auto* dataPath = std::getenv("RWE_SCENARIO_DATA");
        if (!dataPath)
        {
            SKIP("set RWE_SCENARIO_DATA to the retail archive directory");
        }
        auto vfs = constructVfs(dataPath);
        auto names = vfs.getFileNames("objects3d", ".3do");
        REQUIRE(names.size() > 500);
        MeshService meshes(&vfs, nullptr, {}, {}, {});
        for (const auto& filename : names)
        {
            CAPTURE(filename);
            auto bytes = vfs.readFile("objects3d/" + filename);
            REQUIRE(bytes);
            SpanStream stream(bytes->data(), bytes->size());
            auto objects = parse3doObjects(stream, stream.tellg());
            REQUIRE(objects.size() == 1);
            auto height = modelHeightFrom3do(objects.front());
            REQUIRE(height == findHighestVertex(objects.front()).y);
            auto name = filename.substr(0, filename.size() - 4);
            REQUIRE(meshes.loadUnitMesh(name).modelDefinition.height == simScalarFromFixed(height));
            REQUIRE(meshes.loadProjectileMesh(name).modelDefinition.height == simScalarFromFixed(height));
        }
    }
}
