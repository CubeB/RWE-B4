#pragma once

#include <memory>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <vector>

namespace rwe
{
    struct UnitModelVertices;

    struct UnitModelDefinition
    {
        SimScalar height;
        std::vector<UnitPieceDefinition> pieces;
        std::unordered_map<std::string, int> pieceIndicesByName;

        /**
         * Each piece's vertices, for the aim point (BT-60). Null for a model
         * that was not built from a 3DO. Behind a pointer to an incomplete
         * type so that this widely included header costs the translation
         * units that merely copy a definition nothing.
         */
        std::shared_ptr<const UnitModelVertices> vertices;
    };

    UnitModelDefinition createUnitModelDefinition(SimScalar height, std::vector<UnitPieceDefinition>&& pieces);
}
