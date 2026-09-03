// Reads a 3DO and reports, per piece and per polygon, the texture bound to it
// and the shade the unit shader would give it. Written to settle whether the
// solar collector's dark panel is lighting or painted into the texture.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <rwe/io/_3do/_3do.h>
#include <rwe/math/Vector3f.h>

using namespace rwe;

namespace
{
    Vector3f vertexToVector(const _3do::Vertex& v)
    {
        return Vector3f(
            static_cast<float>(v.x) / 65536.0f,
            static_cast<float>(v.y) / 65536.0f,
            static_cast<float>(v.z) / 65536.0f);
    }

    // The shader's own arithmetic.
    float shadeIntensity(const Vector3f& n)
    {
        auto light = Vector3f(-0.8f, 1.0f, -0.25f).normalizedOr(Vector3f(0, 1, 0));
        auto nn = n.normalizedOr(Vector3f(0, 1, 0));
        float t = std::clamp(nn.dot(light), -1.0f, 1.0f);
        float row = std::clamp(14.55f + (16.0f * t), 0.0f, 31.0f);
        return 0.06875f * row;
    }

    void walk(const _3do::Object& o, const std::string& indent)
    {
        std::cout << indent << "piece '" << o.name << "' offset=(" << o.x << "," << o.y << "," << o.z
                  << ") verts=" << o.vertices.size() << " prims=" << o.primitives.size() << "\n";

        for (std::size_t i = 0; i < o.primitives.size(); ++i)
        {
            const auto& p = o.primitives[i];
            if (o.selectionPrimitiveIndex && *o.selectionPrimitiveIndex == i)
            {
                continue;
            }
            if (p.vertices.size() < 3)
            {
                continue;
            }
            // meshFrom3do's winding, reversed order.
            auto count = p.vertices.size();
            auto a = vertexToVector(o.vertices[p.vertices[count - 1]]);
            auto b = vertexToVector(o.vertices[p.vertices[count - 2]]);
            auto c = vertexToVector(o.vertices[p.vertices[count - 3]]);
            auto normal = (b - a).cross(c - a).normalizedOr(Vector3f(1, 0, 0));

            std::cout << indent << "   poly " << i
                      << " tex=" << (p.textureName ? *p.textureName : std::string("<none>"))
                      << " n=(" << normal.x << "," << normal.y << "," << normal.z << ")"
                      << " shade=" << shadeIntensity(normal) << "\n";
        }

        // The same piece with per-vertex averaged normals, the way
        // convertMesh smooths them, to see what smoothing does to the panels.
        std::map<std::tuple<float, float, float>, Vector3f> sums;
        auto key = [](const Vector3f& v) { return std::make_tuple(v.x, v.y, v.z); };
        for (std::size_t i = 0; i < o.primitives.size(); ++i)
        {
            const auto& p = o.primitives[i];
            if ((o.selectionPrimitiveIndex && *o.selectionPrimitiveIndex == i) || p.vertices.size() < 3)
            {
                continue;
            }
            auto count = p.vertices.size();
            auto a0 = vertexToVector(o.vertices[p.vertices[count - 1]]);
            auto b0 = vertexToVector(o.vertices[p.vertices[count - 2]]);
            auto c0 = vertexToVector(o.vertices[p.vertices[count - 3]]);
            auto n = (b0 - a0).cross(c0 - a0).normalizedOr(Vector3f(1, 0, 0));
            for (auto idx : p.vertices)
            {
                auto v = vertexToVector(o.vertices[idx]);
                auto it = sums.find(key(v));
                if (it == sums.end())
                {
                    sums.emplace(key(v), n);
                }
                else
                {
                    it->second = it->second + n;
                }
            }
        }
        for (std::size_t i = 0; i < o.primitives.size(); ++i)
        {
            const auto& p = o.primitives[i];
            if ((o.selectionPrimitiveIndex && *o.selectionPrimitiveIndex == i) || p.vertices.size() < 3)
            {
                continue;
            }
            float lo = 99.0f;
            float hi = -99.0f;
            for (auto idx : p.vertices)
            {
                auto v = vertexToVector(o.vertices[idx]);
                auto sm = sums[key(v)].normalizedOr(Vector3f(0, 1, 0));
                float sh = shadeIntensity(sm);
                lo = std::min(lo, sh);
                hi = std::max(hi, sh);
            }
            std::cout << indent << "   SMOOTHED poly " << i
                      << " tex=" << (p.textureName ? *p.textureName : std::string("<none>"))
                      << " shade " << lo << " .. " << hi << "" << std::endl;
        }

        for (const auto& c : o.children)
        {
            walk(c, indent + "  ");
        }
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: solar_probe <file.3do>\n";
        return 1;
    }
    std::ifstream file(argv[1], std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "could not open " << argv[1] << "\n";
        return 1;
    }
    auto objects = parse3doObjects(file, 0);
    for (const auto& o : objects)
    {
        walk(o, "");
    }
    return 0;
}
