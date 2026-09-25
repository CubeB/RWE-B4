#include "util.h"
#include <rwe/util/rwe_string.h>

namespace rwe
{
    Matrix4x<SimScalar> getPieceTransform(const std::string& pieceName, const UnitModelDefinition& modelDefinition, const std::vector<UnitMesh>& pieces)
    {
        assert(modelDefinition.pieces.size() == pieces.size());

        std::optional<std::string> parentPiece = pieceName;
        auto matrix = Matrix4x<SimScalar>::identity();

        // Parents are found by name, and a model can give an ancestor and one
        // of its descendants the same name. The name then finds the
        // descendant, whose parents lead back to it, and the walk went round
        // for ever -- in the simulation, on every peer. A real chain visits
        // each piece at most once, so no walk needs more steps than there are
        // pieces; one that does stops there with the transform so far, which
        // is the same wrong answer everywhere rather than a hang. Issue #75.
        auto stepsLeft = modelDefinition.pieces.size();

        do
        {
            if (stepsLeft-- == 0)
            {
                break;
            }

            auto pieceIndexIt = modelDefinition.pieceIndicesByName.find(toUpper(*parentPiece));
            if (pieceIndexIt == modelDefinition.pieceIndicesByName.end())
            {
                throw std::runtime_error("missing piece definition: " + *parentPiece);
            }

            const auto& pieceDef = modelDefinition.pieces.at(pieceIndexIt->second);

            parentPiece = pieceDef.parent;

            // A unit carries one piece state for each piece of its model, and
            // the model's numbering indexes them. A saved game can say
            // otherwise, and the check above is an assert; a piece with no
            // state is taken at rest rather than read from past the end.
            // Issue #75.
            static const UnitMesh restingPiece;
            const auto& pieceState = static_cast<std::size_t>(pieceIndexIt->second) < pieces.size()
                ? pieces[pieceIndexIt->second]
                : restingPiece;

            auto position = pieceDef.origin + pieceState.offset;
            auto rotationX = pieceState.rotationX;
            auto rotationY = pieceState.rotationY;
            auto rotationZ = pieceState.rotationZ;
            matrix = Matrix4x<SimScalar>::translation(position)
                * Matrix4x<SimScalar>::rotationZXY(
                    sin(rotationX),
                    cos(rotationX),
                    sin(rotationY),
                    cos(rotationY),
                    sin(rotationZ),
                    cos(rotationZ))
                * matrix;
        } while (parentPiece);

        return matrix;
    }

}
