#pragma once

#include <memory>
#include <rwe/Mesh.h>
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

    /**
     * One textured quad of a piece as a mesh of its own, recentred on the
     * quad's centre, for SHATTER to throw (TOTALA-EXE-WRECKS.md, "What
     * SHATTER does"). Kept on the CPU; the scene turns it into a GL mesh the
     * first time the piece shatters.
     */
    struct PieceFragmentSource
    {
        Mesh mesh;
        /** Where the quad's centre sits in the piece's own model space. */
        Vector3f centre;
    };

    struct UnitPieceMeshInfo
    {
        std::shared_ptr<ShaderMesh> mesh;

        // Used for vector-based SFX.
        Vector3f firstVertexPosition;
        Vector3f secondVertexPosition;

        /** Every polygon of the piece but the selection plate. Drawn as the construction wireframe. */
        std::shared_ptr<std::vector<WireframePolygon>> polygons;

        /** Every textured quad of the piece but the selection plate, as SHATTER breaks it up. */
        std::shared_ptr<std::vector<PieceFragmentSource>> fragments;
    };
}
