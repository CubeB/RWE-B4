#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/SimVector.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    class MapTerrain;
    struct Projectile;

    /** BT-256: original map-origin window, with exclusive upper edges. */
    DiscreteRect featureBlastWindow(const MapTerrain& terrain, const SimVector& position, SimScalar radius, int width, int height);

    /** BT-256: shifted footprint centre and staged integer terrain sampling. */
    SimVector featureBlastPoint(const MapTerrain& terrain, Point cell, int width, int height);

    /** Caller has already checked unitsonly, paralyzer and remote ownership. */
    void applyFeatureBlastCell(GameSimulation& sim, Point cell, const SimVector& position, SimScalar radius,
        const Projectile& projectile, std::vector<FeatureId>& seen);
}
