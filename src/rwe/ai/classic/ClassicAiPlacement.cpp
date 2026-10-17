#include "ClassicAiPlacement.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <rwe/math/det_math.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/UnitDefinition.h>
#include <utility>

namespace rwe
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

        double toDouble(SimScalar s)
        {
            return static_cast<double>(simScalarToFloat(s));
        }

        int floorDiv(int a, int b)
        {
            int q = a / b;
            if ((a % b != 0) && ((a < 0) != (b < 0)))
            {
                --q;
            }
            return q;
        }

        unsigned int metalUnder(const GameSimulation& sim, const DiscreteRect& rect)
        {
            return sim.metalGrid.accumulate(sim.metalGrid.clipRegion(rect), 0u, std::plus<>());
        }

        /** The footprint of `def`, with the top-left cell at (cx, cz): the position that computeFootprintRegion maps back to it. */
        SimVector siteFor(const GameSimulation& sim, int cx, int cz, unsigned int footprintX, unsigned int footprintZ)
        {
            auto corner = sim.terrain.heightmapIndexToWorldCorner(cx, cz);
            auto x = corner.x + SimScalar(static_cast<float>(footprintX) * 8.0f);
            auto z = corner.z + SimScalar(static_cast<float>(footprintZ) * 8.0f);
            return SimVector(x, sim.terrain.getHeightAt(x, z), z);
        }

        bool canBuildAt(const GameSimulation& sim, const MovementClassDefinition& mc, const UnitDefinition& def, int x, int y)
        {
            if (x < 0 || y < 0)
            {
                return false;
            }
            return sim.canBeBuiltAt(mc, def.yardMap, def.yardMapContainsGeo, static_cast<unsigned int>(x), static_cast<unsigned int>(y));
        }
    }

    void ClassicPlacement::initialise(const GameSimulation& sim, ClassicRandom& rng)
    {
        if (ready)
        {
            return;
        }
        ready = true;

        // The resource-feature cache: built once and never rebuilt (S:121).
        resources.clear();
        for (const auto& [featureId, feature] : sim.features)
        {
            (void)featureId;
            const auto& definition = sim.getFeatureDefinition(feature.featureName);
            if (definition.metal != 0 && definition.indestructible)
            {
                resources.push_back(feature.position);
            }
        }

        // "The first has margin 3 and spacing rand(10) + 11 by rand(3) + 11;
        // the second has margin 6 and spacing rand(20) + 14 by rand(3) + 14.
        // Each is offset about the midpoint of its spacing."
        first.margin = 3;
        first.spacingX = static_cast<int>(rng.below(10)) + 11;
        first.spacingZ = static_cast<int>(rng.below(3)) + 11;
        first.offsetX = first.spacingX / 2;
        first.offsetZ = first.spacingZ / 2;

        second.margin = 6;
        second.spacingX = static_cast<int>(rng.below(20)) + 14;
        second.spacingZ = static_cast<int>(rng.below(3)) + 14;
        second.offsetX = second.spacingX / 2;
        second.offsetZ = second.spacingZ / 2;
    }

    std::optional<SimVector> ClassicPlacement::findSite(
        const GameSimulation& sim,
        const UnitDefinition& def,
        const SimVector& builderPosition,
        const SimVector& basePosition,
        ClassicRandom& rng)
    {
        const auto mc = sim.getAdHocMovementClass(def.movementCollisionInfo);
        const auto footprintX = static_cast<int>(mc.footprintX);
        const auto footprintZ = static_cast<int>(mc.footprintZ);

        // The shared radius grows by 160 each time a search is needed, up to
        // the larger map dimension, and drops to 0 on a success.
        const int mapMax = static_cast<int>(std::max(toDouble(sim.terrain.getWidthInWorldUnits()), toDouble(sim.terrain.getHeightInWorldUnits())));
        radius = std::min(radius + RadiusStep, std::max(mapMax, RadiusStep));

        // The proposed origin moves from the builder toward the base by at most the radius.
        double ox = toDouble(builderPosition.x);
        double oz = toDouble(builderPosition.z);
        {
            double dx = toDouble(basePosition.x) - ox;
            double dz = toDouble(basePosition.z) - oz;
            double length = std::sqrt(dx * dx + dz * dz);
            if (length <= static_cast<double>(radius))
            {
                ox = toDouble(basePosition.x);
                oz = toDouble(basePosition.z);
            }
            else if (length > 0.0)
            {
                ox += dx / length * static_cast<double>(radius);
                oz += dz / length * static_cast<double>(radius);
            }
        }
        const SimVector origin{SimScalar(static_cast<float>(ox)), builderPosition.y, SimScalar(static_cast<float>(oz))};

        // Extractors: when the map's threshold is below rand(255), walk the
        // cached deposits nearest first. [open] The register notes that
        // TOTALA-EXE-AI.md S:6 reads this comparison the other way round.
        if (def.extractsMetal != Metal(0.0f) && static_cast<std::uint32_t>(sim.surfaceMetal) < rng.below(255))
        {
            struct Candidate
            {
                double d2;
                std::size_t index;
            };
            std::vector<Candidate> order;
            order.reserve(resources.size());
            for (std::size_t i = 0; i < resources.size(); ++i)
            {
                double dx = toDouble(resources[i].x) - ox;
                double dz = toDouble(resources[i].z) - oz;
                order.push_back({dx * dx + dz * dz, i});
            }
            std::stable_sort(order.begin(), order.end(), [](const Candidate& a, const Candidate& b) { return a.d2 < b.d2; });

            std::optional<double> firstSuccessD2;
            unsigned int bestYield = 0;
            std::optional<SimVector> best;
            for (const auto& c : order)
            {
                // A window in SQUARED distance, not a 160-unit annulus.
                if (firstSuccessD2 && c.d2 > *firstSuccessD2 + static_cast<double>(RadiusStep))
                {
                    break;
                }
                const auto& position = resources[c.index];
                auto rect = sim.computeFootprintRegion(position, def.movementCollisionInfo);
                if (!canBuildAt(sim, mc, def, rect.x, rect.y))
                {
                    continue;
                }
                auto yield = metalUnder(sim, rect);
                if (yield == 0)
                {
                    continue;
                }
                if (!firstSuccessD2)
                {
                    firstSuccessD2 = c.d2;
                }
                if (yield > bestYield)
                {
                    bestYield = yield;
                    best = SimVector(position.x, position.y, position.z);
                }
            }
            if (best)
            {
                radius = 0;
                return best;
            }
        }

        // Everything else, and an extractor that fell through: up to 30
        // candidates at a random direction and distance, snapped to the chosen
        // lattice with jitter, accepted when the footprint is placeable and its
        // metal is no greater than surfaceMetal * footprintX * footprintZ * 2.
        // [open] The register gives no range for the distance; 320 plus the
        // shared radius is a choice, and so are the jitter of one cell and the
        // reading of "margin" as cells kept from the map's edge.
        // The second lattice is the one for `MinWaterDepth >= 0`, written as
        // the register writes it, on the signed word: an absent key is -10000
        // and takes the first lattice, a written one the second (BT-109).
        const auto& lattice = mc.signedMinWaterDepth() >= 0 ? second : first;
        const unsigned int metalLimit = static_cast<unsigned int>(sim.surfaceMetal) * static_cast<unsigned int>(footprintX) * static_cast<unsigned int>(footprintZ) * 2u;
        const int widthCells = sim.occupiedGrid.getWidth();
        const int heightCells = sim.occupiedGrid.getHeight();

        for (int trial = 0; trial < ScatterTrials; ++trial)
        {
            const float degrees = static_cast<float>(rng.below(360));
            const float radians = degrees * (Pi / 180.0f);
            const double distance = static_cast<double>(rng.below(static_cast<std::uint32_t>(320 + radius)));
            const double px = ox + static_cast<double>(detCos(radians)) * distance;
            const double pz = oz + static_cast<double>(detSin(radians)) * distance;

            auto rect = sim.computeFootprintRegion(
                SimVector(SimScalar(static_cast<float>(px)), origin.y, SimScalar(static_cast<float>(pz))),
                mc.footprintX,
                mc.footprintZ);

            // Snap the top-left cell to the nearest lattice node.
            int nodeX = lattice.offsetX + floorDiv(rect.x - lattice.offsetX + lattice.spacingX / 2, lattice.spacingX) * lattice.spacingX;
            int nodeZ = lattice.offsetZ + floorDiv(rect.y - lattice.offsetZ + lattice.spacingZ / 2, lattice.spacingZ) * lattice.spacingZ;

            // Jitter, then keep the margin clear of the map's edge.
            nodeX += static_cast<int>(rng.below(3)) - 1;
            nodeZ += static_cast<int>(rng.below(3)) - 1;
            nodeX = std::max(lattice.margin, std::min(nodeX, widthCells - footprintX - lattice.margin));
            nodeZ = std::max(lattice.margin, std::min(nodeZ, heightCells - footprintZ - lattice.margin));

            if (!canBuildAt(sim, mc, def, nodeX, nodeZ))
            {
                continue;
            }
            if (metalUnder(sim, DiscreteRect(nodeX, nodeZ, footprintX, footprintZ)) > metalLimit)
            {
                continue;
            }
            radius = 0;
            return siteFor(sim, nodeX, nodeZ, mc.footprintX, mc.footprintZ);
        }

        return std::nullopt;
    }
}
