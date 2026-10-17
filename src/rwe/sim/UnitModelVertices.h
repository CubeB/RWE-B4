#pragma once

#include <array>
#include <optional>
#include <rwe/sim/SimVector.h>
#include <vector>

namespace rwe
{
    /**
     * Every piece's own vertices, in the piece's local space and in RWE's
     * right-handed convention (the 3DO z negated, as the piece origins are),
     * numbered like UnitModelDefinition::pieces. The aim point of BT-60 is the
     * bounding box of a piece's vertices once posed, so the simulation has to
     * keep them. A model built by hand in a test has none, which makes its
     * aim point the unit's own position.
     */
    struct UnitModelVertices
    {
        std::vector<std::vector<SimVector>> perPiece;

        /**
         * The first four vertices of the root object's selection primitive,
         * in the root's local space and the same right-handed convention, in
         * the primitive's own order (BT-41, 0x48A490). A ground unit is
         * aligned to the ground under these four points and not under its
         * footprint's corners. Empty for a model whose root has no selection
         * primitive, which the original leaves unaligned.
         */
        std::optional<std::array<SimVector, 4>> selectionRectangle;
    };
}
