#pragma once

#include <array>
#include <rwe/geometry/CollisionMesh.h>
#include <rwe/math/Vector3f.h>

namespace rwe
{
    struct SelectionMesh
    {
        CollisionMesh collisionMesh;
        /** The selection plate's four corners, model space, in the file's order: the box drawn round a selected unit. */
        std::array<Vector3f, 4> corners;
    };
}
