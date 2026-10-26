#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/util.h>
#include <algorithm>
#include <optional>

namespace rwe
{
    namespace
    {
        struct CellBounds
        {
            unsigned int low;
            unsigned int high;
        };

        /** The low and high corners of the 2x2 block that starts at (x, y). */
        CellBounds cellBounds(const Grid<unsigned char>& heights, unsigned int x, unsigned int y)
        {
            const auto a = static_cast<unsigned int>(heights.get(x, y));
            const auto b = static_cast<unsigned int>(heights.get(x + 1, y));
            const auto c = static_cast<unsigned int>(heights.get(x, y + 1));
            const auto d = static_cast<unsigned int>(heights.get(x + 1, y + 1));
            return CellBounds{std::min({a, b, c, d}), std::max({a, b, c, d})};
        }
    }

    bool GameSimulation::isYardMapPlacementValid(
        const MovementClassDefinition& mc,
        const Grid<YardMapCell>& yardMap,
        bool yardMapContainsGeo,
        unsigned int x,
        unsigned int y,
        unsigned int waterLine,
        std::optional<PlayerId> viewer) const
    {
        // BT-51: 0x47D2E0 walks the footprint's mask bytes. The shape is the
        // movement class's, not the unit's, and the parser gives the yard map
        // the FBI's shape. Refuse a map too small to answer every cell rather
        // than read past it.
        if (!isInsideBuildableArea(x, y, mc.footprintX, mc.footprintZ)
            || static_cast<unsigned int>(yardMap.getWidth()) < mc.footprintX
            || static_cast<unsigned int>(yardMap.getHeight()) < mc.footprintZ)
        {
            return false;
        }

        // RWE-specific ground exclusion (BT-264). The original has no lava
        // flag to test; keeping it here preserves the behaviour RWE already
        // gives movement and building placement.
        if (terrain.hasLavaExclusion(DiscreteRect(static_cast<int>(x), static_cast<int>(y), static_cast<int>(mc.footprintX), static_cast<int>(mc.footprintZ))))
        {
            return false;
        }

        // The interface's stricter fog rule: every cell of the footprint must
        // have been explored, so a green box cannot survey the black. The
        // original samples the centre instead; see canBeBuiltAtAsSeenBy.
        if (viewer)
        {
            for (unsigned int dz = 0; dz < mc.footprintZ; ++dz)
            {
                for (unsigned int dx = 0; dx < mc.footprintX; ++dx)
                {
                    if (!isExploredBy(*viewer, terrain.heightmapIndexToWorldCenter(static_cast<int>(x + dx), static_cast<int>(y + dz))))
                    {
                        return false;
                    }
                }
            }
        }

        const auto seaLevel = simScalarToUInt(terrain.getSeaLevel());
        const auto& heights = terrain.getHeightMap();

        std::optional<unsigned int> supportLow;
        std::optional<unsigned int> supportHigh;
        std::optional<unsigned int> clampHigh;
        bool wantsVent = false;
        bool foundVent = false;

        for (unsigned int dz = 0; dz < mc.footprintZ; ++dz)
        {
            for (unsigned int dx = 0; dx < mc.footprintX; ++dx)
            {
                const auto mask = yardMapMask(yardMap.get(static_cast<unsigned int>(dx), static_cast<unsigned int>(dz)));
                if (mask == 0)
                {
                    // A '.' cell is not part of the building for any test.
                    continue;
                }

                const auto& cell = occupiedGrid.get(static_cast<int>(x + dx), static_cast<int>(y + dz));

                // 0x01: a building already claims this cell and blocks it.
                if ((mask & 0x01) != 0 && cell.buildingInfo && !cell.buildingInfo->passable)
                {
                    if (!viewer || canSeeUnit(*viewer, cell.buildingInfo->unit))
                    {
                        return false;
                    }
                }

                // 0x02/0x04: another unit stands here. The caller is always a
                // fresh placement, so there is no unit to excuse.
                if ((mask & (0x02 | 0x04)) != 0 && cell.mobileUnitId)
                {
                    if (!viewer || canSeeUnit(*viewer, *cell.mobileUnitId))
                    {
                        return false;
                    }
                }

                if (cell.featureId)
                {
                    const auto& feature = getFeature(*cell.featureId);
                    const auto& featureDefinition = getFeatureDefinition(feature.featureName);
                    if ((mask & 0x20) != 0 && featureDefinition.blocking)
                    {
                        return false;
                    }
                    if ((mask & 0x40) != 0 && featureDefinition.indestructible)
                    {
                        return false;
                    }
                    if ((mask & 0x80) != 0)
                    {
                        wantsVent = true;
                        foundVent = foundVent || featureDefinition.geothermal;
                    }
                }
                else if ((mask & 0x80) != 0)
                {
                    wantsVent = true;
                }

                // 0x08 is a support cell: both corners feed the spread. 0x10
                // is a clamp cell: only the high corner feeds its maximum.
                if ((mask & (0x08 | 0x10)) != 0)
                {
                    const auto bounds = cellBounds(heights, x + dx, y + dz);
                    if ((mask & 0x08) != 0)
                    {
                        supportLow = supportLow ? std::min(*supportLow, bounds.low) : bounds.low;
                        supportHigh = supportHigh ? std::max(*supportHigh, bounds.high) : bounds.high;
                    }
                    if ((mask & 0x10) != 0)
                    {
                        clampHigh = clampHigh ? std::max(*clampHigh, bounds.high) : bounds.high;
                    }
                }
            }
        }

        if ((wantsVent || yardMapContainsGeo) && !foundVent)
        {
            return false;
        }

        // The height the building will stand at: the lowest support corner,
        // or sea level minus the waterline when nothing supports it.
        unsigned int standingHeight;
        if (supportLow)
        {
            if (*supportHigh - *supportLow > mc.maxSlope)
            {
                return false;
            }
            standingHeight = *supportLow;
        }
        else
        {
            standingHeight = static_cast<unsigned int>(static_cast<unsigned char>(seaLevel - waterLine));
        }

        if (clampHigh && *clampHigh > standingHeight)
        {
            return false;
        }

        // The worst-case depth tests. The support low bound is the deepest
        // point; either running maximum is the shallowest.
        const auto sea = static_cast<int>(seaLevel);
        if (supportLow && static_cast<int>(*supportLow) < sea - static_cast<int>(mc.maxWaterDepth))
        {
            return false;
        }

        if (supportHigh || clampHigh)
        {
            const auto maxHigh = std::max(supportHigh.value_or(0u), clampHigh.value_or(0u));
            if (static_cast<int>(maxHigh) > sea - mc.signedMinWaterDepth())
            {
                return false;
            }
        }

        return true;
    }
}
