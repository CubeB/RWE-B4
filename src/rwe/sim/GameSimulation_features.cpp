#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/SimRandom.h>
#include <algorithm>
#include <cassert>
#include <optional>

namespace rwe
{
    std::optional<FeatureId> GameSimulation::addFeature(MapFeature&& newFeature)
    {
        const auto& featureDefinition = getFeatureDefinition(newFeature.featureName);

        auto footprintRegion = computeFootprintRegion(newFeature.position, featureDefinition.footprintX, featureDefinition.footprintZ);

        // BT-261/262: clear first, without force or rollback. Removing an
        // occupant may free the slot the incoming model needs.
        if (footprintRegion.right() > occupiedGrid.getWidth() || footprintRegion.bottom() > occupiedGrid.getHeight())
        {
            return std::nullopt;
        }
        for (int y = footprintRegion.top(); y < footprintRegion.bottom(); ++y)
        {
            for (int x = footprintRegion.left(); x < footprintRegion.right(); ++x)
            {
                if (x < 0 || y < 0)
                {
                    continue;
                }
                // BT-265: refuse during clearing, without rolling back earlier cells.
                if (terrain.hasLavaExclusion(DiscreteRect(x, y, 1, 1)))
                {
                    return std::nullopt;
                }
                if (auto occupant = occupiedGrid.get(x, y).featureId)
                {
                    if (getFeatureDefinition(getFeature(*occupant).featureName).indestructible)
                    {
                        return std::nullopt;
                    }
                    deleteFeature(*occupant);
                }
            }
        }
        if ((featureDefinition.hasModel || newFeature.animation != FeatureAnimation::None) && countStatefulFeatures() >= 2048)
        {
            return std::nullopt;
        }

        // TA keeps a feature's hit points in its `damage` key; features that omit
        // it (most vegetation) come out of the TDF reader with 1. A blast spends
        // them (doProjectileImpact, unless the feature is indestructible) and
        // computeFeatureReclaimWork reads them for how much bulk is left to haul.
        newFeature.hitPoints = featureDefinition.damage;

        auto featureId = FeatureId(features.emplace(std::move(newFeature)));
        writeFeatureToGrids(featureId, featureDefinition, footprintRegion);
        return featureId;
    }

    FeatureId GameSimulation::addFeatureInSlot(unsigned int slot, MapFeature&& newFeature)
    {
        // No occupancy check: the saved set was consistent when it was
        // written, and the check would only be asking whether a feature is
        // standing where it stood. The hit points come from the save too, so
        // they are not reset from the definition here.
        const auto& featureDefinition = getFeatureDefinition(newFeature.featureName);
        auto footprintRegion = computeFootprintRegion(newFeature.position, featureDefinition.footprintX, featureDefinition.footprintZ);
        auto featureId = FeatureId(features.emplaceInSlot(slot, std::move(newFeature)));
        writeFeatureToGrids(featureId, featureDefinition, footprintRegion);
        return featureId;
    }

    void GameSimulation::writeFeatureToGrids(FeatureId featureId, const FeatureDefinition& featureDefinition, const DiscreteRect& footprintRegion)
    {
        occupiedGrid.forEach(occupiedGrid.clipRegion(footprintRegion), [&](auto& cell) {
            cell.featureId = featureId;
        });

        if (!featureDefinition.blocking && featureDefinition.indestructible && featureDefinition.metal)
        {
            metalGrid.set(metalGrid.clipRegion(footprintRegion), featureDefinition.metal);
        }

        if (!featureDefinition.blocking && featureDefinition.indestructible && featureDefinition.geothermal)
        {
            geoGrid.set(geoGrid.clipRegion(footprintRegion), true);
        }
    }

    int computeMidpointHeight(const Grid<unsigned char>& heightmap, int x, int y)
    {
        assert(x < heightmap.getWidth() - 1);
        assert(y < heightmap.getHeight() - 1);

        auto p1 = static_cast<int>(heightmap.get(x, y));
        auto p2 = static_cast<int>(heightmap.get(x + 1, y));
        auto p3 = static_cast<int>(heightmap.get(x, y + 1));
        auto p4 = static_cast<int>(heightmap.get(x + 1, y + 1));
        return (p1 + p2 + p3 + p4) / 4;
    }

    SimVector computeFeaturePosition(
        const MapTerrain& terrain,
        const FeatureDefinition& featureDefinition,
        int x,
        int y)
    {
        const auto& heightmap = terrain.getHeightMap();

        int height = 0;
        if (x < heightmap.getWidth() - 1 && y < heightmap.getHeight() - 1)
        {
            height = computeMidpointHeight(heightmap, x, y);
        }

        auto position = terrain.heightmapIndexToWorldCorner(x, y);
        position.y = intToSimScalar(height);

        position.x += (intToSimScalar(featureDefinition.footprintX) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss;
        position.z += (intToSimScalar(featureDefinition.footprintZ) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss;

        return position;
    }

    std::optional<FeatureId> GameSimulation::addFeature(FeatureDefinitionId featureType, int heightmapX, int heightmapZ)
    {
        const auto& featureDefinition = getFeatureDefinition(featureType);
        auto resolvedPos = computeFeaturePosition(terrain, featureDefinition, heightmapX, heightmapZ);
        auto featureInstance = MapFeature{featureType, resolvedPos, fromRadians(RadiansAngle::fromUnwrappedAngle(Pif))};
        return addFeature(std::move(featureInstance));
    }

    unsigned int computeUnitReclaimStep(unsigned int workerTime, unsigned int kills, unsigned int targetMaxHitPoints, const Metal& targetBuildCostMetal)
    {
        // 0x438650: the veterancy factor (kills + 5) / 5 is an integer
        // division, the product is integer, and only the final scaling by
        // 300 and the build cost is done in floating point before the
        // truncation. The cost is floored at ten so a free unit does not
        // divide by nothing.
        auto veterancy = (kills + 5u) / 5u;
        auto product = static_cast<double>(workerTime) * static_cast<double>(veterancy) * static_cast<double>(targetMaxHitPoints) * 15.0;
        auto cost = std::max(targetBuildCostMetal.value, 10.0f);
        auto step = static_cast<unsigned int>(product / (300.0 * static_cast<double>(cost)));
        return std::max(1u, step);
    }

    unsigned int computeFeatureReclaimWork(const FeatureDefinition& definition, unsigned int currentHitPoints)
    {
        // How long a feature takes to reclaim is a function of how much there is
        // to carry away (its metal + energy) plus how much of it is still standing
        // (its remaining hit points). TA's own data makes the second term
        // necessary: greenworld [Rock] is worth only metal=100 but declares
        // damage=2000, while [Tree1] is worth energy=250 and declares no damage at
        // all (the TDF reader defaults that to 1). On value alone a boulder would
        // come apart faster than a tree, which
        // is plainly wrong; adding a share of the hit points puts the rock at
        // 100 + 2000/4 = 600 against the tree's 250 and a [Shrub1]'s 20.
        //
        // The quarter weighting is chosen because feature hit points run an order
        // of magnitude above feature value for terrain (rock: 100 metal / 2000 hp)
        // but roughly level with it for wreckage (armflash_dead: 85 metal / 500 hp,
        // armfus_dead: 4104 metal / 2480 hp). At 1/4 the value term still dominates
        // for wreckage -- a fusion plant's corpse is mostly a hauling job, 4104 +
        // 620 = 4724 -- while bulk alone still costs real time on scenery.
        //
        // The hit points read here are the feature's current ones, so a wreck
        // that has been shelled clears quicker than one that has not. The payout
        // does not depend on it either way: reclaimFeature always hands over the
        // full metal/energy, spread across whatever work total applies.
        return std::max(1u, definition.metal + definition.energy + (currentHitPoints / 4u));
    }

    void GameSimulation::deleteFeature(FeatureId id)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return;
        }

        const auto& feature = featureRef->get();
        const auto& featureDefinition = getFeatureDefinition(feature.featureName);
        auto footprintRegion = computeFootprintRegion(feature.position, featureDefinition.footprintX, featureDefinition.footprintZ);

        occupiedGrid.forEach(occupiedGrid.clipRegion(footprintRegion), [&](auto& cell) {
            if (cell.featureId == id)
            {
                cell.featureId = std::nullopt;
            }
        });

        // Metal and geothermal grids are only ever set by permanent
        // (indestructible, non-blocking) features, which are never deleted,
        // so there is nothing to undo there.

        features.remove(id);
    }

    bool GameSimulation::reclaimFeature(FeatureId featureId, PlayerId reclaimer, unsigned int workAmount)
    {
        auto featureRef = tryGetFeature(featureId);
        if (!featureRef)
        {
            // Already gone (someone else finished it, or it was destroyed).
            return true;
        }

        auto& feature = featureRef->get();
        const auto& featureDefinition = getFeatureDefinition(feature.featureName);
        if (!featureDefinition.reclaimable || workAmount == 0)
        {
            return false;
        }

        auto totalWork = computeFeatureReclaimWork(featureDefinition, feature.hitPoints);
        // Clamping the progress already made to the current total keeps the credit
        // below from going negative should the total ever shrink mid-reclaim; the
        // worst case is that the last sliver of value is not paid out, never that
        // a player is paid twice for the same feature.
        auto previousProgress = std::min(totalWork, feature.reclaimProgress);
        auto newProgress = std::min(totalWork, previousProgress + workAmount);
        feature.reclaimProgress = newProgress;

        // Credit the slice of the feature's value that this step of work earned.
        // Computed as a difference of cumulative amounts so that the total handed
        // out over the whole reclaim is exactly the feature's value.
        auto cumulative = [&](unsigned int amount, unsigned int progress) {
            return static_cast<float>(amount) * (static_cast<float>(progress) / static_cast<float>(totalWork));
        };
        Metal metalDelta(cumulative(featureDefinition.metal, newProgress) - cumulative(featureDefinition.metal, previousProgress));
        Energy energyDelta(cumulative(featureDefinition.energy, newProgress) - cumulative(featureDefinition.energy, previousProgress));
        getPlayer(reclaimer).addResourceDelta(energyDelta, metalDelta, energyDelta, metalDelta);

        if (newProgress < totalWork)
        {
            return false;
        }

        // BT-261: credit above precedes even a refused transition. Existing
        // work/payout arithmetic is unchanged by this capacity correction.
        if (!featureDefinition.hasModel && feature.animation != FeatureAnimation::None)
        {
            return true;
        }
        if (!featureDefinition.hasModel && !featureDefinition.reclaimFrameTicks.empty())
        {
            startFeatureAnimation(featureId, FeatureAnimation::Reclaim);
        }
        else
        {
            replaceFeature(featureId, featureDefinition.featureReclamate);
        }

        return true;
    }

    std::optional<FeatureId> GameSimulation::replaceFeature(FeatureId id, const std::optional<FeatureDefinitionId>& replacement)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return std::nullopt;
        }

        // Normal lifecycle replacement never gains force-removal semantics.
        if (getFeatureDefinition(featureRef->get().featureName).indestructible)
        {
            return std::nullopt;
        }

        auto position = featureRef->get().position;
        auto rotation = featureRef->get().rotation;

        deleteFeature(id);

        if (!replacement)
        {
            return std::nullopt;
        }
        return addFeature(MapFeature{*replacement, position, rotation});
    }

    void GameSimulation::updateFeatureRegrowth()
    {
        // The original's sweep, 0x4240A3-0x4241A3. One map square a tick, walking
        // the grid backwards; when the cursor runs off the bottom it is reloaded
        // with the last square and that tick is skipped. So a given square gets
        // one roll every width*height ticks -- on a 64x64-square map, once every
        // two and a quarter minutes.
        auto width = static_cast<int>(occupiedGrid.getWidth());
        auto height = static_cast<int>(occupiedGrid.getHeight());

        --featureRegrowthCursor;
        if (featureRegrowthCursor < 0)
        {
            featureRegrowthCursor = (width * height) - 1;
            return;
        }

        // The original divides the cursor by the map's *height* to get the row,
        // which is only the same as dividing by the width on a square map. That
        // is a bug rather than a rule -- every other square lookup in the binary,
        // 0x481550 included, indexes as y*width+x -- so RWE does it the right way.
        auto sourceX = featureRegrowthCursor % width;
        auto sourceY = featureRegrowthCursor / width;

        const auto& sourceCell = occupiedGrid.get(sourceX, sourceY);
        if (!sourceCell.featureId)
        {
            return;
        }

        // A square that is merely covered by a feature anchored elsewhere holds a
        // back-reference rather than a type, and the original skips it, so a wide
        // feature seeds once and not once per square it stands on.
        const auto& sourceFeature = getFeature(*sourceCell.featureId);
        const auto& definition = getFeatureDefinition(sourceFeature.featureName);
        auto footprint = computeFootprintRegion(sourceFeature.position, definition.footprintX, definition.footprintZ);
        if (footprint.x != sourceX || footprint.y != sourceY)
        {
            return;
        }

        // `reproduce` is a percentage, not a flag: the roll is rand(100) against it.
        if (definition.reproduce == 0)
        {
            return;
        }
        if (randomBelow(rng, 100) >= definition.reproduce)
        {
            return;
        }

        // The seed lands in a square drawn uniformly from a box `reproduceArea`
        // across centred on the parent, which for the shipped value of 6 is the
        // three squares either way.
        auto area = static_cast<int>(definition.reproduceArea);
        if (area <= 0)
        {
            return;
        }
        auto targetX = sourceX + static_cast<int>(randomBelow(rng, static_cast<unsigned int>(area))) - (area / 2);
        auto targetY = sourceY + static_cast<int>(randomBelow(rng, static_cast<unsigned int>(area))) - (area / 2);

        if (targetX < 0 || targetX >= width || targetY < 0 || targetY >= height)
        {
            return;
        }

        // Nothing grows out from under a unit standing on it (0x424189).
        if (sourceCell.mobileUnitId || sourceCell.buildingInfo)
        {
            return;
        }

        // The original insists the destination square is genuinely empty, not
        // merely free of a feature of its own, which is what stops a forest
        // creeping in under buildings and over the whole map.
        const auto& targetCell = occupiedGrid.get(targetX, targetY);
        if (targetCell.featureId || targetCell.mobileUnitId || targetCell.buildingInfo)
        {
            return;
        }

        addFeature(sourceFeature.featureName, targetX, targetY);
    }

}
