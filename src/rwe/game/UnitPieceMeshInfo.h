#pragma once

#include <memory>
#include <optional>
#include <rwe/math/Vector3f.h>
#include <rwe/render/ShaderMesh.h>
#include <vector>

namespace rwe
{
    /** One polygon outline edge (model space) with the outward normals of the polygons it borders. */
    struct WireframeEdge
    {
        Vector3f start;
        Vector3f end;
        Vector3f normalA;
        std::optional<Vector3f> normalB;
    };

    struct UnitPieceMeshInfo
    {
        std::shared_ptr<ShaderMesh> mesh;

        // Used for vector-based SFX.
        Vector3f firstVertexPosition;
        Vector3f secondVertexPosition;

        /** Outline of every polygon in the piece, each edge once. Drawn as the construction wireframe. */
        std::shared_ptr<std::vector<WireframeEdge>> edges;
    };
}
