#pragma once

#include <glad/gl.h>
#include <rwe/util/OpaqueId.h>

namespace rwe
{
    struct UniformLocationTag;
    using UniformLocation = OpaqueId<GLint, UniformLocationTag>;
}
