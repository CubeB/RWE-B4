#pragma once

#include <memory>
#include <rwe/math/Vector3f.h>
#include <rwe/render/ShaderMesh.h>
#include <vector>

namespace rwe
{
    /**
     * One polygon of a piece as authored, model space, corners in the file's
     * order. The construction wireframe scan-converts it the way the
     * original does, and that order is what tells it which way the polygon
     * faces (TOTALA-EXE.md S:3).
     */
    struct WireframePolygon
    {
        std::vector<Vector3f> vertices;
    };

    struct UnitPieceMeshInfo
    {
        std::shared_ptr<ShaderMesh> mesh;

        // Used for vector-based SFX.
        Vector3f firstVertexPosition;
        Vector3f secondVertexPosition;

        /** Every polygon of the piece but the selection plate. Drawn as the construction wireframe. */
        std::shared_ptr<std::vector<WireframePolygon>> polygons;
    };
}
