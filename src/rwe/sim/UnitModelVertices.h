#pragma once

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
    };
}
