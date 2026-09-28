#pragma once

#include <rwe/render/VaoHandle.h>
#include <rwe/render/VboHandle.h>

namespace rwe
{
    struct GlMesh
    {
        VaoHandle vao;
        VboHandle vbo;
        unsigned int vertexCount{0};

        /**
         * Vertices the buffer has room for, which is not the number in it:
         * updateColoredMesh keeps the buffer at its high-water mark so that a
         * batch whose size drifts a few percent from frame to frame does not
         * reallocate every frame. Zero until the mesh is made.
         */
        unsigned int capacity{0};

        /** A mesh with no vertex array or buffer yet, for updateColoredMesh to fill. */
        GlMesh() = default;

        GlMesh(VaoHandle&& vao, VboHandle&& vbo, unsigned int vertexCount);
    };
}
