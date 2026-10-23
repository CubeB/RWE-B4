#pragma once

#include <rwe/math/Matrix4f.h>
#include <rwe/math/Vector3f.h>

namespace rwe
{
    // AA-PHASE-01 and ANTIALIASING-MASK-VALIDATION.md: keep the sharp
    // supersample at the native raster centre when the world buffer changes
    // size. This corrects RWE's screen-space approximation, not the original's
    // per-image reduction. Coordinates here are OpenGL's bottom-left ones.
    inline Matrix4f alignWorldRaster(const Matrix4f& matrix, unsigned int width, unsigned int height, unsigned int scale)
    {
        const float offset = 1.0f / static_cast<float>(scale) - 1.0f;
        return Matrix4f::translation(Vector3f(offset / width, offset / height, 0.0f)) * matrix;
    }
}
