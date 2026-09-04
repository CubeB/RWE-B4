#include "mesh_util.h"
#include <algorithm>
#include <map>
#include <rwe/fixed_point.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    Vector3f getNormal(const Mesh::Triangle& t)
    {
        auto v1 = t.b.position - t.a.position;
        auto v2 = t.c.position - t.a.position;
        return v1.cross(v2).normalizedOr(Vector3f(1.0f, 0.0f, 0.0f));
    }

    TextureRegionInfo getTextureRegion(
        const std::unordered_map<std::string, Rectangle2f>& atlasMap,
        const std::unordered_map<std::string, Rectangle2f>& teamAtlasMap,
        const std::string& name)
    {
        // Everything else in the data path — archive lookup, GAF entry lookup
        // — matches names case-insensitively, and TA's own data is
        // inconsistent about case. Match that here too: a mismatch used to
        // fall through to the empty region below, collapsing the whole face
        // onto one atlas texel and painting it a flat arbitrary colour with
        // no complaint.
        auto key = toUpper(name);

        auto it = atlasMap.find(key);
        if (it != atlasMap.end())
        {
            return TextureRegionInfo{false, it->second};
        }

        auto it2 = teamAtlasMap.find(key);
        if (it2 != teamAtlasMap.end())
        {
            return TextureRegionInfo{true, it2->second};
        }

        // Some unit models in the wild (e.g. the ARM commander in TA:Zero)
        // contain references to textures that don't exist,
        // so we cannot simply throw here.
        // TODO: consider returning an optional and making the caller decide what to do
        return {false, Rectangle2f(0, 0, 0, 0)};
    }

    Vector2f getColorTexturePoint(
        const std::vector<Vector2f>& atlasColorMap,
        unsigned int colorIndex)
    {
        assert(colorIndex < atlasColorMap.size());
        return atlasColorMap[colorIndex];
    }

    void extractMeshes(
        GraphicsContext& graphics,
        const std::unordered_map<std::string, Rectangle2f>& atlasMap,
        const std::unordered_map<std::string, Rectangle2f>& teamAtlasMap,
        const std::vector<Vector2f>& atlasColorMap,
        const _3do::Object& o,
        std::vector<std::pair<std::string, UnitPieceMeshInfo>>& v)
    {
        auto firstVertex = o.vertices.size() > 0 ? vertexToVector(o.vertices[0]) : Vector3f(0.0f, 0.0f, 0.0f);
        auto secondVertex = o.vertices.size() > 1 ? vertexToVector(o.vertices[1]) : Vector3f(1.0f, 0.0f, 0.0f);
        auto mesh = meshFrom3do(atlasMap, teamAtlasMap, atlasColorMap, o);
        auto shaderMesh = convertMesh(graphics, mesh);
        auto edges = std::make_shared<std::vector<WireframeEdge>>(polygonEdgesFrom3do(o));

        v.push_back(std::make_pair(o.name, UnitPieceMeshInfo{std::make_shared<ShaderMesh>(std::move(shaderMesh)), firstVertex, secondVertex, std::move(edges)}));

        for (const auto& c : o.children)
        {
            extractMeshes(graphics, atlasMap, teamAtlasMap, atlasColorMap, c, v);
        }
    }

    std::vector<WireframeEdge> polygonEdgesFrom3do(const _3do::Object& o)
    {
        // Edges follow the polygons as authored, not the triangulation,
        // so a quad outlines as four lines with no diagonal.
        // Each edge remembers the outward normals of the polygons on either
        // side so the renderer can keep only the edges the camera can see.
        std::map<std::pair<unsigned int, unsigned int>, std::size_t> edgeIndices;
        std::vector<WireframeEdge> edges;
        for (const auto& p : o.primitives)
        {
            auto count = p.vertices.size();
            if (count < 3)
            {
                continue;
            }
            bool valid = true;
            for (auto index : p.vertices)
            {
                valid = valid && index < o.vertices.size();
            }
            if (!valid)
            {
                continue;
            }

            // Newell's method over the vertices in reverse order, matching the
            // winding meshFrom3do uses, so the normal points outward like the lit faces.
            Vector3f normal(0.0f, 0.0f, 0.0f);
            for (std::size_t i = 0; i < count; ++i)
            {
                auto current = vertexToVector(o.vertices[p.vertices[(count - i) % count]]);
                auto next = vertexToVector(o.vertices[p.vertices[(count - i - 1) % count]]);
                normal.x += (current.y - next.y) * (current.z + next.z);
                normal.y += (current.z - next.z) * (current.x + next.x);
                normal.z += (current.x - next.x) * (current.y + next.y);
            }
            normal = normal.normalizedOr(Vector3f(0.0f, 1.0f, 0.0f));

            for (std::size_t i = 0; i < count; ++i)
            {
                auto a = p.vertices[i];
                auto b = p.vertices[(i + 1) % count];
                if (a == b)
                {
                    continue;
                }
                auto key = std::minmax(a, b);
                auto it = edgeIndices.find(key);
                if (it != edgeIndices.end())
                {
                    auto& edge = edges[it->second];
                    if (!edge.normalB)
                    {
                        edge.normalB = normal;
                    }
                    continue;
                }
                edgeIndices.emplace(key, edges.size());
                edges.push_back(WireframeEdge{vertexToVector(o.vertices[a]), vertexToVector(o.vertices[b]), normal, std::nullopt});
            }
        }
        return edges;
    }

    Mesh meshFrom3do(
        const std::unordered_map<std::string, Rectangle2f>& atlasMap,
        const std::unordered_map<std::string, Rectangle2f>& teamAtlasMap,
        const std::vector<Vector2f>& atlasColorMap,
        const _3do::Object& o)
    {
        Mesh m;

        // The original's vertex normals (0x45A195-0x45A21E): every POLYGON
        // adds its own unit normal to each of the vertices it names, and the
        // vertex takes the mean, divided by the count and never renormalised.
        //
        // Both halves of that matter. It is per polygon, so a quad
        // contributes once to each of its four corners -- averaging over
        // this function's output instead would count a quad twice at the two
        // corners its diagonal happens to touch, and on ARMSOLAR that alone
        // was enough to push a corner of the right-hand panel across the
        // truncation boundary from row 0 to row 31: one bright corner
        // bleeding across a panel the original draws solid black. And it is
        // not renormalised, because the shortening of the mean where faces
        // disagree is part of what sets the shading's range.
        std::vector<Vector3f> vertexNormals(o.vertices.size(), Vector3f(0.0f, 0.0f, 0.0f));
        std::vector<int> vertexNormalCounts(o.vertices.size(), 0);
        for (Index primitiveIndex = 0; primitiveIndex < getSize(o.primitives); ++primitiveIndex)
        {
            const auto& p = o.primitives[primitiveIndex];
            if (o.selectionPrimitiveIndex && static_cast<Index>(*o.selectionPrimitiveIndex) == primitiveIndex)
            {
                continue;
            }
            if (p.vertices.size() < 3)
            {
                continue;
            }
            bool valid = true;
            for (auto index : p.vertices)
            {
                valid = valid && index < o.vertices.size();
            }
            if (!valid)
            {
                continue;
            }

            // The same winding the triangles below use, so the normal points
            // the way the rest of the pipeline expects.
            auto first = vertexToVector(o.vertices[p.vertices.front()]);
            auto second = vertexToVector(o.vertices[p.vertices[2]]);
            auto third = vertexToVector(o.vertices[p.vertices[1]]);
            auto faceNormal = (second - first).cross(third - first).normalizedOr(Vector3f(0.0f, 1.0f, 0.0f));

            for (auto index : p.vertices)
            {
                vertexNormals[index] = vertexNormals[index] + faceNormal;
                ++vertexNormalCounts[index];
            }
        }
        for (std::size_t i = 0; i < vertexNormals.size(); ++i)
        {
            if (vertexNormalCounts[i] > 0)
            {
                vertexNormals[i] = vertexNormals[i] / static_cast<float>(vertexNormalCounts[i]);
            }
            else
            {
                vertexNormals[i] = Vector3f(0.0f, 1.0f, 0.0f);
            }
        }

        for (Index primitiveIndex = 0; primitiveIndex < getSize(o.primitives); ++primitiveIndex)
        {
            const auto& p = o.primitives[primitiveIndex];

            // The selection plate is not part of the model. (The original
            // skips primitive 0 whenever a selection primitive is declared,
            // which on the wreckage models -- where the plate is not
            // primitive 0 -- drops a real face and draws the plate; skipping
            // the declared index is the saner reading of the same intent,
            // recorded as a deliberate difference.)
            if (o.selectionPrimitiveIndex && static_cast<Index>(*o.selectionPrimitiveIndex) == primitiveIndex)
            {
                continue;
            }

            // handle textured quads
            if (p.vertices.size() == 4 && p.textureName)
            {
                auto textureBounds = getTextureRegion(atlasMap, teamAtlasMap, *(p.textureName));

                // The original does not split a textured quad into two
                // triangles: it scan-converts the whole quad, interpolating
                // the texture along both edge chains, which on a
                // non-parallelogram face gives a smooth bilinear-style warp.
                // Two affine triangles kink the mapping along the diagonal
                // instead -- glaring on the solar collector, whose panels are
                // trapezoids under a strongly striped texture. So a skewed
                // quad is tessellated as a bilinear patch; a parallelogram
                // needs no help, since there the two mappings agree.
                auto v0 = vertexToVector(o.vertices[p.vertices[0]]);
                auto v1 = vertexToVector(o.vertices[p.vertices[1]]);
                auto v2 = vertexToVector(o.vertices[p.vertices[2]]);
                auto v3 = vertexToVector(o.vertices[p.vertices[3]]);

                auto uv0 = textureBounds.region.topLeft();
                auto uv1 = textureBounds.region.topRight();
                auto uv2 = textureBounds.region.bottomRight();
                auto uv3 = textureBounds.region.bottomLeft();

                bool parallelogram = ((v0 + v2) - (v1 + v3)).lengthSquared() < 0.01f;
                int subdivisions = parallelogram ? 1 : 4;

                auto bilinearPosition = [&](float s, float t) {
                    return (v0 * ((1.0f - s) * (1.0f - t))) + (v1 * (s * (1.0f - t))) + (v2 * (s * t)) + (v3 * ((1.0f - s) * t));
                };
                auto bilinearUv = [&](float s, float t) {
                    return (uv0 * ((1.0f - s) * (1.0f - t))) + (uv1 * (s * (1.0f - t))) + (uv2 * (s * t)) + (uv3 * ((1.0f - s) * t));
                };

                // The patch's interior vertices are not the original's, so
                // they take a blend of the quad's four corner normals -- the
                // same bilinear the position and the texture coordinate use.
                // The quad's own corners keep their values exactly, which is
                // what the original interpolates between.
                const auto& n0 = vertexNormals[p.vertices[0]];
                const auto& n1 = vertexNormals[p.vertices[1]];
                const auto& n2 = vertexNormals[p.vertices[2]];
                const auto& n3 = vertexNormals[p.vertices[3]];
                auto bilinearNormal = [&](float s, float t) {
                    return (n0 * ((1.0f - s) * (1.0f - t))) + (n1 * (s * (1.0f - t))) + (n2 * (s * t)) + (n3 * ((1.0f - s) * t));
                };

                auto& target = textureBounds.isTeamColor ? m.teamFaces : m.faces;
                for (int row = 0; row < subdivisions; ++row)
                {
                    for (int col = 0; col < subdivisions; ++col)
                    {
                        auto s0 = static_cast<float>(col) / static_cast<float>(subdivisions);
                        auto s1 = static_cast<float>(col + 1) / static_cast<float>(subdivisions);
                        auto t0 = static_cast<float>(row) / static_cast<float>(subdivisions);
                        auto t1 = static_cast<float>(row + 1) / static_cast<float>(subdivisions);

                        Mesh::Vertex c00(bilinearPosition(s0, t0), bilinearUv(s0, t0), bilinearNormal(s0, t0));
                        Mesh::Vertex c10(bilinearPosition(s1, t0), bilinearUv(s1, t0), bilinearNormal(s1, t0));
                        Mesh::Vertex c11(bilinearPosition(s1, t1), bilinearUv(s1, t1), bilinearNormal(s1, t1));
                        Mesh::Vertex c01(bilinearPosition(s0, t1), bilinearUv(s0, t1), bilinearNormal(s0, t1));

                        target.emplace_back(c11, c10, c00);
                        target.emplace_back(c01, c11, c00);
                    }
                }

                continue;
            }

            // A textured face that is not a quad is never drawn by the
            // original -- its textured path handles quads alone -- so
            // drawing it flat here would invent a face TA does not show.
            if (p.textureName && p.vertices.size() != 4)
            {
                continue;
            }

            // handle other polygon types
            if (p.vertices.size() >= 3 && p.colorIndex)
            {
                auto texturePosition = getColorTexturePoint(atlasColorMap, *p.colorIndex);
                const auto& first = vertexToVector(o.vertices[p.vertices.front()]);
                for (Index i = getSize(p.vertices) - 1; i >= 2; --i)
                {
                    const auto& second = vertexToVector(o.vertices[p.vertices[i]]);
                    const auto& third = vertexToVector(o.vertices[p.vertices[i - 1]]);
                    Mesh::Triangle t(
                        Mesh::Vertex(first, texturePosition, vertexNormals[p.vertices.front()]),
                        Mesh::Vertex(second, texturePosition, vertexNormals[p.vertices[i]]),
                        Mesh::Vertex(third, texturePosition, vertexNormals[p.vertices[i - 1]]));
                    m.faces.push_back(t);
                }

                continue;
            }

            // just ignore degenerate faces (0, 1 or 2 vertices)
        }

        return m;
    }

    SelectionMesh selectionMeshFrom3do(GraphicsContext& graphics, const _3do::Object& o)
    {
        auto index = o.selectionPrimitiveIndex.value_or(0u);
        auto p = o.primitives.at(index);

        assert(p.vertices.size() == 4);
        Vector3f offset(vertexToVector(_3do::Vertex(o.x, o.y, o.z)));

        auto a = offset + vertexToVector(o.vertices[p.vertices[0]]);
        auto b = offset + vertexToVector(o.vertices[p.vertices[1]]);
        auto c = offset + vertexToVector(o.vertices[p.vertices[2]]);
        auto d = offset + vertexToVector(o.vertices[p.vertices[3]]);

        auto collisionMesh = CollisionMesh::fromQuad(a, b, c, d);
        auto selectionMesh = createSelectionMesh(graphics, a, b, c, d);

        return SelectionMesh{std::move(collisionMesh), std::move(selectionMesh)};
    }

    GlMesh createSelectionMesh(GraphicsContext& graphics, const Vector3f& a, const Vector3f& b, const Vector3f& c, const Vector3f& d)
    {
        const Vector3f color(0.325f, 0.875f, 0.310f);

        std::vector<GlColoredVertex> buffer{
            {a, color},
            {b, color},
            {c, color},
            {d, color}};

        return graphics.createColoredMesh(buffer, GL_STATIC_DRAW);
    }

    ShaderMesh convertMesh(GraphicsContext& graphics, const Mesh& mesh)
    {
        // The normals come in on the vertices, worked out per polygon by
        // meshFrom3do before anything was cut into triangles. Deriving them
        // here instead would weight each polygon by its triangle count.

        std::optional<GlMesh> texturedMesh;
        if (!mesh.faces.empty())
        {
            std::vector<GlTexturedNormalVertex> texturedVerticesBuffer;
            texturedVerticesBuffer.reserve(mesh.faces.size() * 3);

            for (const auto& t : mesh.faces)
            {
                texturedVerticesBuffer.emplace_back(t.a.position, t.a.textureCoord, t.a.normal);
                texturedVerticesBuffer.emplace_back(t.b.position, t.b.textureCoord, t.b.normal);
                texturedVerticesBuffer.emplace_back(t.c.position, t.c.textureCoord, t.c.normal);
            }

            texturedMesh = graphics.createTexturedNormalMesh(texturedVerticesBuffer, GL_STATIC_DRAW);
        }

        std::optional<GlMesh> teamTexturedMesh;
        if (!mesh.teamFaces.empty())
        {

            std::vector<GlTexturedNormalVertex> teamTexturedVerticesBuffer;
            teamTexturedVerticesBuffer.reserve(mesh.teamFaces.size() * 3);
            for (const auto& t : mesh.teamFaces)
            {
                teamTexturedVerticesBuffer.emplace_back(t.a.position, t.a.textureCoord, t.a.normal);
                teamTexturedVerticesBuffer.emplace_back(t.b.position, t.b.textureCoord, t.b.normal);
                teamTexturedVerticesBuffer.emplace_back(t.c.position, t.c.textureCoord, t.c.normal);
            }

            teamTexturedMesh = graphics.createTexturedNormalMesh(teamTexturedVerticesBuffer, GL_STATIC_DRAW);
        }

        return ShaderMesh(std::move(texturedMesh), std::move(teamTexturedMesh));
    }

    std::vector<UnitPieceDefinition> unitMeshFrom3do(const _3do::Object& o, const std::optional<std::string>& parent)
    {
        UnitPieceDefinition m;
        m.origin = Vector3x<SimScalar>(
            simScalarFromFixed(o.x),
            simScalarFromFixed(o.y),
            -simScalarFromFixed(o.z)); // flip to convert from left-handed to right-handed
        m.name = o.name;
        m.parent = parent;

        std::vector<UnitPieceDefinition> pieces{m};

        for (const auto& c : o.children)
        {
            auto childPieces = unitMeshFrom3do(c, o.name);
            for (auto& p : childPieces)
            {
                pieces.push_back(p);
            }
        }

        return pieces;
    }

    std::vector<UnitPieceDefinition> unitMeshFrom3do(const _3do::Object& o)
    {
        return unitMeshFrom3do(o, std::nullopt);
    }

    Vector3f vertexToVector(const _3do::Vertex& v)
    {
        return Vector3f(
            fromFixedPoint(v.x),
            fromFixedPoint(v.y),
            fromFixedPoint(-v.z)); // flip to convert from left-handed to right-handed
    }
}
