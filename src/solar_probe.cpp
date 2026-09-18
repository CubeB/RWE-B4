// Replays RWE's own unit-shading pipeline over a 3DO and prints the shade row
// each vertex of each polygon would receive, so the result can be compared
// against the byte-verified figures in docs/TOTALA-EXE-SHADING.md section 13.
//
// This exists because the shading has been "fixed" several times on reasoning
// alone and been wrong each time. The reference case is armsolar.3do: the exe
// draws primitive 13 (the LEFT panel) at rows 28-29 and primitive 12 (the
// RIGHT panel) at row 0, solid black, and any candidate convention that does
// not reproduce that is wrong.
//
// Usage: solar_probe <file.3do> [negateNormal] [zSign]
//   negateNormal : 1 (default) to dot with -n, 0 to dot with +n
//   zSign        : -1 (default) for RWE's flipped z, +1 for the exe's raw z
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <rwe/io/_3do/_3do.h>
#include <rwe/math/Vector3f.h>
#include <rwe/mesh_util.h>

using namespace rwe;

namespace
{
    /** meshFrom3do's winding, over RWE's vertexToVector (which flips z). */
    Vector3f faceNormal(const _3do::Object& o, const _3do::Primitive& p)
    {
        auto first = vertexToVector(o.vertices[p.vertices.front()]);
        auto second = vertexToVector(o.vertices[p.vertices[2]]);
        auto third = vertexToVector(o.vertices[p.vertices[1]]);
        return (second - first).cross(third - first).normalizedOr(Vector3f(0.0f, 1.0f, 0.0f));
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: solar_probe <file.3do> [negateNormal] [zSign]\n";
        return 1;
    }
    bool negateNormal = argc > 2 ? std::atoi(argv[2]) != 0 : true;
    auto zSign = argc > 3 ? static_cast<float>(std::atof(argv[3])) : -1.0f;

    Vector3f sun(-0.8f, 1.0f, 0.25f * zSign);
    std::cout << "sun = (" << sun.x << ", " << sun.y << ", " << sun.z << ")"
              << "   normal " << (negateNormal ? "negated" : "as-is") << "\n";

    std::ifstream file(argv[1], std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "could not open " << argv[1] << "\n";
        return 1;
    }
    auto objects = parse3doObjects(file, 0);

    // Every piece, children included: a unit's root is often a bare "base"
    // with the whole model hanging off it.
    std::vector<const _3do::Object*> pending;
    for (const auto& o : objects)
    {
        pending.push_back(&o);
    }
    while (!pending.empty())
    {
        const auto& o = *pending.back();
        pending.pop_back();
        for (const auto& c : o.children)
        {
            pending.push_back(&c);
        }
        std::cout << "== piece '" << o.name << "'  verts=" << o.vertices.size()
                  << " prims=" << o.primitives.size() << "\n";

        // Per-POLYGON averaged vertex normals, exactly as meshFrom3do builds
        // them: each polygon adds its unit normal once to every vertex index
        // it names, then divide by the count. Never renormalised.
        std::vector<Vector3f> vn(o.vertices.size(), Vector3f(0.0f, 0.0f, 0.0f));
        std::vector<int> vc(o.vertices.size(), 0);
        for (std::size_t pi = 0; pi < o.primitives.size(); ++pi)
        {
            const auto& p = o.primitives[pi];
            if (o.selectionPrimitiveIndex && *o.selectionPrimitiveIndex == pi)
            {
                continue;
            }
            if (p.vertices.size() < 3)
            {
                continue;
            }
            auto n = faceNormal(o, p);
            for (auto idx : p.vertices)
            {
                vn[idx] = vn[idx] + n;
                ++vc[idx];
            }
        }
        for (std::size_t i = 0; i < vn.size(); ++i)
        {
            vn[i] = vc[i] > 0 ? vn[i] / static_cast<float>(vc[i]) : Vector3f(0.0f, 1.0f, 0.0f);
        }

        for (std::size_t pi = 0; pi < o.primitives.size(); ++pi)
        {
            const auto& p = o.primitives[pi];
            if (p.vertices.size() < 3)
            {
                continue;
            }
            const auto& tex = p.textureName ? *p.textureName : (p.colorIndex ? "colour" + std::to_string(*p.colorIndex) : std::string("<none>"));

            float meanX = 0.0f;
            std::string rows;
            for (auto idx : p.vertices)
            {
                meanX += vertexToVector(o.vertices[idx]).x;
                auto n = vn[idx];
                auto d = (negateNormal ? -n : n).dot(sun);
                rows += " " + std::to_string(static_cast<int>(5.0f * d) & 31);
            }
            meanX /= static_cast<float>(p.vertices.size());

            std::cout << "   prim " << pi << "  tex=" << tex
                      << "  meanX=" << meanX
                      << (meanX < 0 ? " (LEFT) " : " (RIGHT)")
                      << "  rows:" << rows << "\n";
        }
    }
    return 0;
}
