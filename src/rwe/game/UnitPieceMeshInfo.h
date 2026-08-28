#pragma once

#include <memory>
#include <rwe/geometry/Line3f.h>
#include <rwe/math/Vector3f.h>
#include <rwe/render/ShaderMesh.h>
#include <vector>

namespace rwe
{
    struct UnitPieceMeshInfo
    {
        std::shared_ptr<ShaderMesh> mesh;

        // Used for vector-based SFX.
        Vector3f firstVertexPosition;
        Vector3f secondVertexPosition;

        /** Outline of every polygon in the piece (model space), each edge once. Drawn as the construction wireframe. */
        std::shared_ptr<std::vector<Line3f>> edges;
    };
}
