#include "_3do.h"
#include <algorithm>
#include <rwe/io/io_util.h>
#include <set>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        /**
         * Limits on a model a mod supplies. Each object names its child and
         * its next sibling by file offset, and nothing stopped one naming
         * itself or an ancestor: a self-sibling looped for ever, and a
         * self-child recursed until the stack ran out. The counts sized
         * loops and allocations before a byte behind them was read. TA's
         * models have tens of pieces and a few hundred vertices, and a vertex
         * index is sixteen bits, which is the one real ceiling. Issue #75.
         */
        constexpr std::size_t MaxObjects = 4096;
        constexpr unsigned int MaxDepth = 64;
        constexpr std::uint32_t MaxVerticesPerObject = 65536;
        constexpr std::uint32_t MaxPrimitivesPerObject = 65536;
        constexpr std::uint32_t MaxVerticesPerPrimitive = 256;

        struct ParseState
        {
            std::set<std::streamoff> visited;
            unsigned int depth{0};
        };

        template <typename T>
        T readChecked(std::istream& stream)
        {
            auto value = readRaw<T>(stream);
            if (stream.fail())
            {
                throw std::runtime_error("3DO file is truncated");
            }
            return value;
        }

        std::vector<_3do::Object> parseObjects(std::istream& stream, std::istream::pos_type offset, ParseState& state)
        {
            if (++state.depth > MaxDepth)
            {
                throw std::runtime_error("3DO object tree is nested too deep");
            }

            std::vector<_3do::Object> outputObjects;

            do
            {
                if (!state.visited.insert(static_cast<std::streamoff>(offset)).second || state.visited.size() > MaxObjects)
                {
                    throw std::runtime_error("3DO object tree loops back on itself or is too large");
                }

                stream.seekg(offset);
                auto object = readChecked<_3doObject>(stream);

                if (object.magicNumber != _3doMagicNumber)
                {
                    throw std::runtime_error("3DO object has the wrong magic number");
                }
                if (object.numberOfVertices > MaxVerticesPerObject || object.numberOfPrimitives > MaxPrimitivesPerObject)
                {
                    throw std::runtime_error("3DO object has too many vertices or primitives");
                }

                _3do::Object outputObject;

                outputObject.x = object.xFromParent;
                outputObject.y = object.yFromParent;
                outputObject.z = object.zFromparent;

                if (object.selectionPrimitiveOffset == -1)
                {
                    outputObject.selectionPrimitiveIndex = std::nullopt;
                }
                else
                {
                    outputObject.selectionPrimitiveIndex = object.selectionPrimitiveOffset;
                }

                stream.seekg(object.nameOffset);
                outputObject.name = readNullTerminatedString(stream);

                stream.seekg(object.verticesOffset);
                for (unsigned int i = 0; i < object.numberOfVertices; ++i)
                {
                    auto v = readChecked<_3doVertex>(stream);
                    outputObject.vertices.push_back(_3do::Vertex{v.x, v.y, v.z});
                }

                stream.seekg(object.primitivesOffset);
                std::vector<_3doPrimitive> ps;
                ps.reserve(object.numberOfPrimitives);
                for (unsigned int i = 0; i < object.numberOfPrimitives; ++i)
                {
                    ps.push_back(readChecked<_3doPrimitive>(stream));
                }

                for (const auto& p : ps)
                {
                    _3do::Primitive outP;

                    if (p.isColored)
                    {
                        outP.colorIndex = p.colorIndex;
                    }

                    if (p.textureNameOffset != 0)
                    {
                        stream.seekg(p.textureNameOffset);
                        outP.textureName = readNullTerminatedString(stream);
                    }
                    else
                    {
                        outP.textureName = std::nullopt;
                    }

                    if (p.numberOfVertices > MaxVerticesPerPrimitive)
                    {
                        throw std::runtime_error("3DO primitive has too many vertices");
                    }
                    stream.seekg(p.verticesOffset);
                    for (unsigned int j = 0; j < p.numberOfVertices; ++j)
                    {
                        outP.vertices.push_back(readChecked<uint16_t>(stream));
                    }

                    // Every consumer indexes the object's vertices with these
                    // and the palette with the colour, checked by assert
                    // alone. A primitive that names a vertex the object does
                    // not have keeps its place, so that the selection plate's
                    // index still points where it did, but loses its vertices,
                    // which every consumer already skips. A flat face whose
                    // colour is past the palette loses the colour, and is not
                    // drawn. (A textured face's colour field is never read,
                    // and in the shipped models it is often not a colour at
                    // all, so it is left as it is.) Issue #75.
                    if (std::any_of(outP.vertices.begin(), outP.vertices.end(), [&](auto index) { return index >= outputObject.vertices.size(); }))
                    {
                        outP.vertices.clear();
                    }
                    if (!outP.textureName && outP.colorIndex && *outP.colorIndex >= 256)
                    {
                        outP.colorIndex = std::nullopt;
                    }

                    outputObject.primitives.push_back(std::move(outP));
                }

                if (object.firstChildOffset != 0)
                {
                    auto children = parseObjects(stream, object.firstChildOffset, state);
                    std::move(children.begin(), children.end(), std::back_inserter(outputObject.children));
                }

                outputObjects.push_back(std::move(outputObject));
                offset = std::streampos(object.siblingOffset);
            } while (offset != std::streampos(0));

            --state.depth;
            return outputObjects;
        }
    }

    std::vector<_3do::Object> parse3doObjects(std::istream& stream, std::istream::pos_type offset)
    {
        ParseState state;
        return parseObjects(stream, offset, state);
    }
}
