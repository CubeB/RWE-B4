#pragma once

#include <optional>
#include <rwe/ai/classic/ClassicRandom.h>
#include <rwe/sim/SimVector.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct UnitDefinition;

    /**
     * Where the original computer player puts a building
     * (TOTALA-EXE-COMPUTER-PLAYER.md S:121 "Placement").
     *
     * It is not a pure function of the builder's position: the two lattices'
     * sizes are drawn once, the shared search radius grows by 160 each time a
     * search is needed and is only reset by a success, and the resource-cell
     * cache is built once and never rebuilt, so a deposit whose wreck has been
     * cleared stays in it.
     */
    class ClassicPlacement
    {
    public:
        /** A randomised lattice: margin in cells, node spacing in cells, and the node offset. */
        struct Lattice
        {
            int margin{0};
            int spacingX{1};
            int spacingZ{1};
            int offsetX{0};
            int offsetZ{0};
        };

        static constexpr int RadiusStep = 160;
        static constexpr int ScatterTrials = 30;

        /**
         * Builds the resource-cell cache from the map's features (nonzero
         * metal value, indestructible) and draws both lattices. Once; a second
         * call does nothing.
         */
        void initialise(const GameSimulation& sim, ClassicRandom& rng);

        bool initialised() const { return ready; }

        /**
         * A site for `def`, or nothing. `builderPosition` is where the builder
         * stands and `basePosition` the weighted base; the proposed origin
         * moves from the one toward the other by at most the shared radius.
         * Extractors try the deposit cache first (when the map's surface metal
         * is below rand(255)); everything else, and an extractor that found
         * nothing, scatters up to 30 trials onto the lattice, the second one
         * for `MinWaterDepth >= 0`.
         */
        std::optional<SimVector> findSite(
            const GameSimulation& sim,
            const UnitDefinition& def,
            const SimVector& builderPosition,
            const SimVector& basePosition,
            ClassicRandom& rng);

        int sharedRadius() const { return radius; }
        const Lattice& firstLattice() const { return first; }
        const Lattice& secondLattice() const { return second; }
        const std::vector<SimVector>& resourceCells() const { return resources; }

    private:
        bool ready{false};
        Lattice first;
        Lattice second;
        int radius{0};
        std::vector<SimVector> resources;
    };
}
