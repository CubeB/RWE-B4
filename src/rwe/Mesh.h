#pragma once


#include <array>
#include <rwe/ColorPalette.h>
#include <rwe/math/Vector2f.h>
#include <rwe/math/Vector3f.h>
#include <rwe/render/TextureHandle.h>

namespace rwe
{
    struct Mesh
    {
        struct Vertex
        {
            Vector3f position;
            Vector2f textureCoord;

            /**
             * The smoothed normal for this vertex: the mean of the unit
             * normals of the POLYGONS meeting at it, deliberately left
             * unnormalised. Carried per vertex rather than derived from the
             * triangle, because the triangles are not the original's faces --
             * a textured quad becomes a 4x4 patch here -- and averaging over
             * them would weight a polygon by how many triangles it happens to
             * have been cut into.
             */
            Vector3f normal{0.0f, 1.0f, 0.0f};

            Vertex() = default;
            Vertex(const Vector3f& position, const Vector2f& textureCoord);
            Vertex(const Vector3f& position, const Vector2f& textureCoord, const Vector3f& normal);
        };

        struct Triangle
        {
            Vertex a;
            Vertex b;
            Vertex c;

            Triangle() = default;
            Triangle(const Vertex& a, const Vertex& b, const Vertex& c);
        };

        std::vector<Triangle> faces;
        std::vector<Triangle> teamFaces;
    };
}
