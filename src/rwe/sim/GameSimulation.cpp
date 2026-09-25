#include "GameSimulation.h"
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/SimRandom.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <chrono>
#include <rwe/util/SimpleLogger.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/util.h>
#include <rwe/sim/sim_prof.h>
#include <rwe/util/Index.h>
#include <rwe/util/collection_util.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>
#include <type_traits>
#include <random>
#include <unordered_set>

namespace rwe
{
    namespace
    {

        /**
         * What a builder does about a build site that is occupied when it
         * comes to put the unit down. The original tries again every thirty
         * ticks and gives up after ten goes, announcing "Waiting for target
         * area to clear" as it starts and "Target area was blocked" when it
         * runs out -- the two captions in the `cant` table at 403cdf/414020
         * and 403d10/414055, from the site check 0x47D2E0 called through
         * 0x47DB70 at unit-creation time.
         */
        constexpr unsigned int BlockedSiteRetryTicks = 30;
        constexpr unsigned int BlockedSiteAttempts = 10;

        /**
         * Records how a unit died for the arena report. The attacker's type
         * and owner are read while it still exists; a blow from nothing (a
         * scuttle, a reclaim, a game-end wipe) leaves them empty. Pure
         * observation -- see UnitDeathObservation.
         */
        void recordUnitDeath(GameSimulation& sim, UnitId unitId, const std::string& cause, std::optional<UnitId> attacker)
        {
            UnitDeathObservation observation;
            observation.cause = cause;
            if (attacker && *attacker != unitId)
            {
                if (auto attackerUnit = sim.tryGetUnitState(*attacker))
                {
                    observation.killerType = attackerUnit->get().unitType;
                    observation.killerPlayer = attackerUnit->get().owner;
                }
            }
            sim.unitDeathObservations[unitId.value] = std::move(observation);
        }

        /**
         * The wire's death cause for the engine's own tag: the high nibble of
         * a 0x0c, in the vocabulary of the eleven decoded causes
         * (TOTALA-EXE-WRECKS.md, "What each death cause is"). A kill with no
         * observation recorded is an ordinary weapon hit -- the only paths
         * that reach killUnit without writing one are tests and the game-end
         * wipe, and the wipe writes its own.
         */
        unsigned int demoDeathCause(const GameSimulation& sim, UnitId unitId)
        {
            auto it = sim.unitDeathObservations.find(unitId.value);
            if (it == sim.unitDeathObservations.end())
            {
                return 1;
            }

            const auto& cause = it->second.cause;
            if (cause == "self_destruct")
            {
                return 3;
            }
            if (cause == "reclaimed")
            {
                return 5;
            }
            if (cause == "carrier_died")
            {
                return 6;
            }
            if (cause == "unfinished")
            {
                return 9;
            }
            return 1;
        }

        /** The corpse level a dead unit will actually leave: 0 when it leaves nothing. */
        unsigned int demoCorpseLevel(const UnitState& unit)
        {
            if (auto dead = std::get_if<UnitState::LifeStateDead>(&unit.lifeState); dead != nullptr && dead->leaveCorpse)
            {
                return dead->corpseLevel;
            }
            return 0;
        }

    }

    bool GamePlayerInfo::addResourceDelta(const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal)
    {
        recordDesire(apparentEnergy);
        recordDesire(apparentMetal);

        // Nothing here asks whether the money is on hand. The original decides
        // that once a second for the whole player at once, and a consumer's only
        // gate in between is whether it is still paying off the last shortfall.
        if (inResourceDebt())
        {
            return false;
        }

        acceptResource(actualEnergy);
        acceptResource(actualMetal);
        return true;
    }

    void GamePlayerInfo::recordDesire(const rwe::Energy& energy)
    {
        if (energy < Energy(0))
        {
            desiredEnergyConsumptionBuffer -= energy;
        }
    }

    void GamePlayerInfo::recordDesire(const rwe::Metal& metal)
    {
        if (metal < Metal(0))
        {
            desiredMetalConsumptionBuffer -= metal;
        }
    }

    bool GamePlayerInfo::inResourceDebt() const
    {
        return energyDebt > Energy(0) || metalDebt > Metal(0);
    }

    void GamePlayerInfo::acceptResource(const rwe::Energy& energy)
    {
        if (energy >= Energy(0))
        {
            energyProductionBuffer += energy;
        }
        else
        {
            energyRequestBuffer -= energy;
        }
    }

    void GamePlayerInfo::acceptResource(const rwe::Metal& metal)
    {
        if (metal >= Metal(0))
        {
            metalProductionBuffer += metal;
        }
        else
        {
            metalRequestBuffer -= metal;
        }
    }

    bool PathRequest::operator==(const PathRequest& rhs) const
    {
        return unitId == rhs.unitId;
    }

    bool PathRequest::operator!=(const PathRequest& rhs) const
    {
        return !(rhs == *this);
    }

    GameSimulation::GameSimulation(MapTerrain&& terrain, unsigned char surfaceMetal, int minWindSpeed, int maxWindSpeed)
        : terrain(std::move(terrain)),
          occupiedGrid(this->terrain.getHeightMap().getWidth() - 1, this->terrain.getHeightMap().getHeight() - 1, OccupiedCell()),
          metalGrid(this->terrain.getHeightMap().getWidth() - 1, this->terrain.getHeightMap().getHeight() - 1, surfaceMetal),
          surfaceMetal(surfaceMetal),
          visionHeights(computeVisionHeights(this->terrain.getHeightMap(), static_cast<unsigned char>(std::min(simScalarToUInt(this->terrain.getSeaLevel()), 255u)))),
          losTables(generateLosTables(DefaultLosTableCount - 1)),
          explored(
              (this->terrain.getHeightMap().getWidth() + PlayerVisibility::VisionCellSizeInTiles - 1) / PlayerVisibility::VisionCellSizeInTiles,
              (this->terrain.getHeightMap().getHeight() + PlayerVisibility::VisionCellSizeInTiles - 1) / PlayerVisibility::VisionCellSizeInTiles,
              static_cast<ExploredMask>(0)),
          geoGrid(this->terrain.getHeightMap().getWidth() - 1, this->terrain.getHeightMap().getHeight() - 1, false),
          minWindSpeed(minWindSpeed),
          maxWindSpeed(maxWindSpeed),
          nextWindSpeedChange(gameTime)
    {
    }

    // Out-of-line because `aiControllers` holds unique_ptr<AiPlayerController>
    // and AiPlayerController is forward-declared in the header. Move-assign
    // is intentionally absent (declared `=delete` in the header) because the
    // simulation has const wind-speed members.
    GameSimulation::~GameSimulation() = default;
    GameSimulation::GameSimulation(GameSimulation&&) noexcept = default;

    void GameSimulation::attachDemoRecorder(std::unique_ptr<DemoRecorder> recorder)
    {
        demoRecorder = std::move(recorder);
    }

    void GameSimulation::addAiController(PlayerId playerId, std::unique_ptr<AiPlayerController> controller)
    {
        auto [it, inserted] = aiControllers.emplace(playerId, std::move(controller));
        if (inserted)
        {
            // Maintain a sorted insertion of the new player id so per-tick
            // iteration is deterministic regardless of hash bucket layout.
            auto pos = std::lower_bound(
                aiPlayerOrder.begin(),
                aiPlayerOrder.end(),
                playerId,
                [](PlayerId a, PlayerId b) { return a.value < b.value; });
            aiPlayerOrder.insert(pos, playerId);
        }
        else
        {
            // Re-registration replaces the controller but does not change
            // the iteration order.
            it->second = std::move(controller);
        }
    }

    std::vector<PlayerCommand> GameSimulation::takeAiCommandsForPlayer(PlayerId playerId)
    {
        auto it = aiPendingCommands.find(playerId);
        if (it == aiPendingCommands.end())
        {
            return {};
        }
        auto out = std::move(it->second);
        it->second.clear();
        return out;
    }

    void GameSimulation::runAiControllers()
    {
        for (PlayerId playerId : aiPlayerOrder)
        {
            auto controllerIt = aiControllers.find(playerId);
            if (controllerIt == aiControllers.end() || !controllerIt->second)
            {
                continue;
            }

            // Skip dead AI players — no point spending tick budget on them.
            const auto& player = getPlayer(playerId);
            if (player.status != GamePlayerStatus::Alive)
            {
                continue;
            }

            // Find or create the per-player command buffer. We append to it
            // (rather than overwrite) so multiple managers within the same
            // tick can each contribute commands without trampling each
            // other. GameScene drains this buffer once per scene tick.
            auto& buf = aiPendingCommands[playerId];
            controllerIt->second->tick(*this, buf);
        }
    }

    std::optional<FeatureId> GameSimulation::addFeature(MapFeature&& newFeature)
    {
        const auto& featureDefinition = getFeatureDefinition(newFeature.featureName);

        auto footprintRegion = computeFootprintRegion(newFeature.position, featureDefinition.footprintX, featureDefinition.footprintZ);

        if (anyFeatureOccupies(footprintRegion))
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

        auto position = feature.position;
        auto rotation = feature.rotation;
        auto reclamate = featureDefinition.featureReclamate;
        auto featureType = feature.featureName;

        deleteFeature(featureId);

        events.push_back(FeatureReclaimedEvent{featureType, position});

        if (reclamate)
        {
            addFeature(MapFeature{*reclamate, position, rotation});
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

        auto position = featureRef->get().position;
        auto rotation = featureRef->get().rotation;

        deleteFeature(id);

        if (!replacement)
        {
            return std::nullopt;
        }
        return addFeature(MapFeature{*replacement, position, rotation});
    }

    void GameSimulation::igniteFeature(FeatureId id)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return;
        }
        auto& feature = featureRef->get();
        const auto& featureDefinition = getFeatureDefinition(feature.featureName);
        if (!featureDefinition.flamable || feature.burningUntil)
        {
            return;
        }

        const auto ticksPerSecond = static_cast<unsigned int>(SimTicksPerSecond);
        auto minTicks = featureDefinition.burnMin * ticksPerSecond;
        auto maxTicks = std::max(featureDefinition.burnMax, featureDefinition.burnMin) * ticksPerSecond;
        feature.burningUntil = gameTime + GameTime(std::max(1u, randomBetween(rng, minTicks, maxTicks)));
        feature.nextSpark = gameTime + GameTime(std::max(1u, featureDefinition.sparkTime) * ticksPerSecond);
    }

    void GameSimulation::tryIgniteFeaturesInRadius(const SimVector& position, SimScalar radius, unsigned int chancePercent)
    {
        if (chancePercent == 0 || radius <= 0_ss)
        {
            return;
        }

        auto minPoint = terrain.worldToHeightmapCoordinate(SimVector(position.x - radius, position.y, position.z - radius));
        auto maxPoint = terrain.worldToHeightmapCoordinate(SimVector(position.x + radius, position.y, position.z + radius));
        auto minCell = occupiedGrid.clampToCoords(minPoint);
        auto maxCell = occupiedGrid.clampToCoords(maxPoint);
        auto region = GridRegion::fromCoordinates(minCell, maxCell);

        // Collect in grid order so the rolls below happen in the same order on every machine.
        std::vector<FeatureId> candidates;
        region.forEach([&](const auto& coords) {
            auto featureId = occupiedGrid.get(coords).featureId;
            if (!featureId || std::find(candidates.begin(), candidates.end(), *featureId) != candidates.end())
            {
                return;
            }
            candidates.push_back(*featureId);
        });

        auto radiusSquared = radius * radius;
        for (auto id : candidates)
        {
            auto featureRef = tryGetFeature(id);
            if (!featureRef)
            {
                continue;
            }
            const auto& feature = featureRef->get();
            const auto& featureDefinition = getFeatureDefinition(feature.featureName);
            if (!featureDefinition.flamable || feature.burningUntil)
            {
                continue;
            }
            auto dx = feature.position.x - position.x;
            auto dz = feature.position.z - position.z;
            if ((dx * dx) + (dz * dz) > radiusSquared)
            {
                continue;
            }
            if (randomBetween(rng, 1u, 100u) <= chancePercent)
            {
                igniteFeature(id);
            }
        }
    }

    void GameSimulation::updateBurningFeatures()
    {
        std::vector<FeatureId> burning;
        for (const auto& [id, feature] : features)
        {
            if (feature.burningUntil)
            {
                burning.push_back(id);
            }
        }

        const auto ticksPerSecond = static_cast<unsigned int>(SimTicksPerSecond);
        for (auto id : burning)
        {
            auto featureRef = tryGetFeature(id);
            if (!featureRef)
            {
                continue;
            }
            auto& feature = featureRef->get();
            const auto& featureDefinition = getFeatureDefinition(feature.featureName);

            if (gameTime >= *feature.burningUntil)
            {
                replaceFeature(id, featureDefinition.featureBurnt);
                continue;
            }

            if (gameTime >= feature.nextSpark)
            {
                feature.nextSpark = gameTime + GameTime(std::max(1u, featureDefinition.sparkTime) * ticksPerSecond);

                // The burn weapon's blast radius says how far the fire reaches.
                auto radius = 32_ss;
                if (auto it = weaponDefinitions.find(toUpper(featureDefinition.burnWeapon)); it != weaponDefinitions.end() && it->second.damageRadius > 0_ss)
                {
                    radius = it->second.damageRadius;
                }
                tryIgniteFeaturesInRadius(feature.position, radius, featureDefinition.spreadChance);
            }
        }
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

    bool GameSimulation::canLoadUnitIntoTransport(UnitId transportId, UnitId unitId) const
    {
        auto transportRef = tryGetUnitState(transportId);
        auto targetRef = tryGetUnitState(unitId);
        if (!transportRef || !targetRef || transportId == unitId)
        {
            return false;
        }
        const auto& transport = transportRef->get();
        const auto& target = targetRef->get();
        if (!transport.isAlive() || !target.isAlive() || target.carriedBy)
        {
            return false;
        }

        const auto& transportDefinition = unitDefinitions.at(transport.unitType);
        const auto& targetDefinition = unitDefinitions.at(target.unitType);

        // CantBeTransported is the first question the original asks and it
        // is about the passenger alone: a unit that names it is refused by
        // every transport there is, however much room the transport has.
        if (targetDefinition.cantBeTransported || !targetDefinition.isMobile)
        {
            return false;
        }

        // The transport must say canload; and a unit carrying cargo of its
        // own can never itself be picked up.
        if (!transportDefinition.canLoad || !target.carriedUnits.empty())
        {
            return false;
        }

        // Capacity is a flat headcount -- a big unit takes one slot or does
        // not fit at all -- and an air transport carries exactly one
        // whatever its FBI says: the original's VTOL pickup aborts while
        // anything is attached, which is why the Atlas's
        // transportcapacity=5 has never meant five.
        auto capacity = transportDefinition.canFly ? 1u : transportDefinition.effectiveTransportCapacity();
        if (transport.carriedUnits.size() >= capacity)
        {
            return false;
        }

        // The size gate is the passenger's footprint X alone against
        // transportsize. Air transports refuse ships through this gate:
        // every ship in the game is wider than transportsize 3.
        auto [footprintX, footprintZ] = getFootprintXZ(targetDefinition.movementCollisionInfo);
        if (transportDefinition.transportSize > 0 && footprintX > transportDefinition.transportSize)
        {
            return false;
        }

        // A landed aircraft may ride; an airborne one may not.
        if (std::holds_alternative<UnitPhysicsInfoAir>(target.physics))
        {
            return false;
        }

        // A sea or hover transport refuses anything that needs water under
        // it -- ships and submarines.
        auto targetMovement = getAdHocMovementClass(targetDefinition.movementCollisionInfo);
        if (!transportDefinition.canFly && targetMovement.minWaterDepth > 0)
        {
            return false;
        }

        // No transport of any kind lifts a unit whose top is below the
        // surface.
        auto targetModelHeight = unitModelDefinitions.at(targetDefinition.objectName).height;
        if (target.position.y + targetModelHeight <= terrain.getSeaLevel())
        {
            return false;
        }

        if (target.isBeingBuilt(targetDefinition))
        {
            return false;
        }

        return true;
    }

    bool GameSimulation::loadUnitIntoTransport(UnitId transportId, UnitId unitId, const std::string& piece)
    {
        auto transportRef = tryGetUnitState(transportId);
        auto unitRef = tryGetUnitState(unitId);
        if (!transportRef || !unitRef)
        {
            return false;
        }
        auto& transport = transportRef->get();
        auto& unit = unitRef->get();
        if (unit.carriedBy || !unit.isAlive() || !transport.isAlive() || transportId == unitId)
        {
            return false;
        }

        // Leave the ground.
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        if (auto region = occupiedGrid.tryToRegion(footprintRect))
        {
            occupiedGrid.forEach(*region, [&](auto& cell) {
                if (cell.mobileUnitId == unitId)
                {
                    cell.mobileUnitId = std::nullopt;
                }
            });
        }
        flyingUnitsSet.erase(unitId);

        unit.carriedBy = transportId;
        unit.carriedPiece = piece;

        // Aboard, a unit is cargo: it forgets what it was doing, so it does
        // not set off for an old move marker the moment it is put down again.
        unit.orders.clear();
        unit.navigationState.desiredDestination = std::nullopt;
        unit.navigationState.state = NavigationStateIdle();
        unit.clearWeaponTargets();
        unit.orders.clear();
        unit.clearWeaponTargets();
        unit.behaviourState = UnitBehaviorStateIdle();
        unit.navigationState = NavigationStateInfo{};

        // And it stops. A unit picked up mid-stride kept the speed it was
        // walking at, and since nothing on board runs its physics the number
        // was still sitting there when it was set down again -- so it coasted
        // a world unit or two away from where the transport put it, in
        // whatever direction the transport happened to be facing.
        if (auto* ground = std::get_if<UnitPhysicsInfoGround>(&unit.physics); ground != nullptr)
        {
            ground->currentSpeed = 0_ss;
            ground->steeringInfo = SteeringInfo{unit.rotation, 0_ss};
        }

        transport.carriedUnits.push_back(unitId);
        return true;
    }

    std::optional<UnloadSpot> GameSimulation::findUnloadSpot(UnitId unitId, const SimVector& position) const
    {
        auto unitRef = tryGetUnitState(unitId);
        if (!unitRef)
        {
            return std::nullopt;
        }
        const auto& unitDefinition = unitDefinitions.at(unitRef->get().unitType);
        auto mc = getAdHocMovementClass(unitDefinition.movementCollisionInfo);

        // Nearest clear footprint to the drop point, searching outwards ring by ring.
        auto centre = terrain.worldToHeightmapCoordinate(position);
        auto halfX = static_cast<int>(mc.footprintX / 2);
        auto halfZ = static_cast<int>(mc.footprintZ / 2);
        std::optional<DiscreteRect> spot;
        for (int ring = 0; ring <= 12 && !spot; ++ring)
        {
            for (int dy = -ring; dy <= ring && !spot; ++dy)
            {
                for (int dx = -ring; dx <= ring && !spot; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dy)) != ring)
                    {
                        continue;
                    }
                    auto x = centre.x + dx - halfX;
                    auto y = centre.y + dy - halfZ;
                    if (x < 0 || y < 0 || x + static_cast<int>(mc.footprintX) > occupiedGrid.getWidth() || y + static_cast<int>(mc.footprintZ) > occupiedGrid.getHeight())
                    {
                        continue;
                    }
                    if (canBeBuiltAt(mc, std::nullopt, false, static_cast<unsigned int>(x), static_cast<unsigned int>(y)))
                    {
                        spot = DiscreteRect(x, y, mc.footprintX, mc.footprintZ);
                    }
                }
            }
        }
        if (!spot)
        {
            return std::nullopt;
        }

        auto corner = terrain.heightmapIndexToWorldCorner(spot->x, spot->y);
        SimVector newPosition(
            corner.x + (SimScalar(static_cast<float>(mc.footprintX)) * MapTerrain::HeightTileWidthInWorldUnits / 2_ss),
            0_ss,
            corner.z + (SimScalar(static_cast<float>(mc.footprintZ)) * MapTerrain::HeightTileHeightInWorldUnits / 2_ss));
        newPosition.y = terrain.getHeightAt(newPosition.x, newPosition.z);
        if (unitDefinition.floater || unitDefinition.canHover)
        {
            newPosition.y = rweMax(newPosition.y, terrain.getSeaLevel());
        }
        return UnloadSpot{*spot, newPosition};
    }

    bool GameSimulation::unloadUnitFromTransport(UnitId transportId, UnitId unitId, const SimVector& position)
    {
        auto transportRef = tryGetUnitState(transportId);
        auto unitRef = tryGetUnitState(unitId);
        if (!transportRef || !unitRef || unitRef->get().carriedBy != transportId)
        {
            return false;
        }
        auto& transport = transportRef->get();
        auto& unit = unitRef->get();

        auto spot = findUnloadSpot(unitId, position);
        if (!spot)
        {
            return false;
        }

        if (auto region = occupiedGrid.tryToRegion(spot->footprint))
        {
            occupiedGrid.forEach(*region, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
        }

        unit.position = spot->position;
        unit.previousPosition = spot->position;
        unit.rotation = transport.rotation;
        unit.previousRotation = transport.rotation;
        unit.carriedBy = std::nullopt;
        unit.carriedPiece.clear();
        transport.carriedUnits.erase(std::remove(transport.carriedUnits.begin(), transport.carriedUnits.end(), unitId), transport.carriedUnits.end());
        return true;
    }

    void GameSimulation::attachUnitToTransportPiece(UnitId transportId, UnitId unitId, const std::string& piece)
    {
        auto unitRef = tryGetUnitState(unitId);
        if (!unitRef)
        {
            return;
        }
        auto& unit = unitRef->get();
        if (unit.carriedBy == transportId)
        {
            unit.carriedPiece = piece;
            return;
        }
        if (unit.carriedBy || !unit.isAlive() || !unit.isOwnedBy(getUnitState(transportId).owner))
        {
            return;
        }
        loadUnitIntoTransport(transportId, unitId, piece);
    }

    void GameSimulation::dropUnitFromTransport(UnitId transportId, UnitId unitId)
    {
        auto unitRef = tryGetUnitState(unitId);
        if (!unitRef || unitRef->get().carriedBy != transportId)
        {
            return;
        }
        unloadUnitFromTransport(transportId, unitId, unitRef->get().position);
    }

    void GameSimulation::updateCarriedUnits()
    {
        for (auto& [unitId, unit] : units)
        {
            if (!unit.carriedBy)
            {
                continue;
            }
            auto transportRef = tryGetUnitState(*unit.carriedBy);
            if (!transportRef)
            {
                continue;
            }
            const auto& transport = transportRef->get();

            auto attachPoint = transport.position;
            if (!unit.carriedPiece.empty() && transport.findPiece(unit.carriedPiece))
            {
                attachPoint = getUnitPiecePosition(*unit.carriedBy, unit.carriedPiece);
            }

            const auto& transportDefinition = unitDefinitions.at(transport.unitType);
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            bool onScriptPiece = !unit.carriedPiece.empty() && transport.findPiece(unit.carriedPiece).has_value();
            if (transportDefinition.canFly)
            {
                // Slung underneath: the top of the unit meets the transport's grip.
                attachPoint.y -= unitModelDefinitions.at(unitDefinition.objectName).height;
            }
            else if (!onScriptPiece)
            {
                // On deck: each unit gets its own slot along the ship's length.
                const auto& carried = transport.carriedUnits;
                auto slot = std::find(carried.begin(), carried.end(), unitId) - carried.begin();
                auto offset = (SimScalar(static_cast<float>(slot)) - (SimScalar(static_cast<float>(carried.size()) - 1.0f) / 2_ss)) * 14_ss;
                attachPoint += UnitState::toDirection(transport.rotation) * offset;
                attachPoint.y += 4_ss;
            }

            unit.previousPosition = unit.position;
            unit.position = attachPoint;
            unit.previousRotation = unit.rotation;
            unit.rotation = transport.rotation;
        }
    }

    void GameSimulation::releaseTransportLinks(UnitId unitId)
    {
        releaseTransportLinks(unitId, std::nullopt);
    }

    void GameSimulation::releaseTransportLinks(UnitId unitId, std::optional<UnitId> attacker)
    {
        auto& unit = getUnitState(unitId);
        if (unit.carriedBy)
        {
            if (auto transportRef = tryGetUnitState(*unit.carriedBy))
            {
                auto& carried = transportRef->get().carriedUnits;
                carried.erase(std::remove(carried.begin(), carried.end(), unitId), carried.end());
            }
            // carriedBy stays set so the dead unit is not cleared from ground it never occupied.
        }

        // Whatever it was carrying goes down with it -- the original deals
        // each passenger 30000 armour-ignoring damage credited to whoever
        // killed the transport, so the kills count for the attacker.
        auto carried = unit.carriedUnits;
        unit.carriedUnits.clear();
        for (auto carriedId : carried)
        {
            auto carriedRef = tryGetUnitState(carriedId);
            if (carriedRef && carriedRef->get().isAlive())
            {
                recordUnitDeath(*this, carriedId, "carrier_died", attacker);
                killUnit(carriedId, attacker);
            }
        }
    }

    bool GameSimulation::reclaimUnitStep(UnitId targetId, PlayerId reclaimer, unsigned int damage)
    {
        auto unitRef = tryGetUnitState(targetId);
        if (!unitRef || unitRef->get().isDead())
        {
            return true;
        }

        auto& unit = unitRef->get();
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        // The bite comes straight off the hit points. It does not go through
        // applyDamage: that path ends in killUnit, a wreck and an explosion,
        // and a reclaimed unit leaves none of those. Whether the original's
        // cause-5 damage skips armour the way its cause-10 repair does is
        // not read; it is applied bare here.
        auto before = unit.hitPoints;
        auto after = damage >= before ? 0u : before - damage;
        unit.hitPoints = after;

        // Only the share of the cost that has actually been built can be
        // recovered, and it is handed back as the unit comes apart. (The
        // original pays trunc((1 - progress) * buildcostmetal) in one lump as
        // the unit dies, metal only, at 0x402666; RWE's incremental credit of
        // both resources is its own and is recorded in TOTALA-EXE-ECONOMY.md.)
        auto investedFraction = unitDefinition.buildTime == 0
            ? 1.0f
            : std::min(1.0f, static_cast<float>(unit.buildTimeCompleted) / static_cast<float>(unitDefinition.buildTime));
        auto maxHitPoints = std::max(1u, unitDefinition.maxHitPoints);
        auto removedFraction = static_cast<float>(before - after) / static_cast<float>(maxHitPoints);
        Metal metalDelta(unitDefinition.buildCostMetal.value * investedFraction * removedFraction);
        Energy energyDelta(unitDefinition.buildCostEnergy.value * investedFraction * removedFraction);
        getPlayer(reclaimer).addResourceDelta(energyDelta, metalDelta, energyDelta, metalDelta);

        if (after > 0)
        {
            return false;
        }

        // Reclaimed units vanish quietly: no wreck, no explosion.
        unit.markAsDeadNoCorpse();
        recordUnitDeath(*this, targetId, "reclaimed", std::nullopt);

        // Death cause 5, and the one place the original checks who is doing it
        // before moving a counter: 0x486899 tests the recorded killer against
        // the victim's own owner and only then falls into the Losses increment.
        // Recycling your own base is not a loss; having it eaten by an enemy
        // builder is. (The original also rejects player 0xA, its neutral slot,
        // which RWE has no equivalent of.)
        if (reclaimer != unit.owner)
        {
            getPlayer(unit.owner).unitsLost += 1;
        }

        events.push_back(UnitDiedEvent{targetId, unit.unitType, unit.position, UnitDiedEvent::DeathType::Deleted});
        if (demoRecorder)
        {
            // Cause 5, reclaimed: it leaves nothing, and the corpus reads
            // severity 0 and level 0 on every cause-5 death.
            demoRecorder->unitDied(*this, targetId, std::nullopt, 0, 5, 0);
        }
        return true;
    }

    unsigned int GameSimulation::computeCaptureTime(const UnitState& target) const
    {
        const auto& definition = unitDefinitions.at(target.unitType);

        // 0x404313: BuildCostEnergy*30*0.0005 - BuildCostMetal*30*(-1/140) - (-150),
        // truncated. Done here as one exact rational instead of three float
        // multiplies, because the simulation is lockstep and a float chain is
        // the last thing it needs: 0.015 = 3/200 and 30/140 = 3/14, so the
        // common denominator is 1400. Checked against the float arithmetic
        // over all 189 shipped FBIs -- every one agrees to the tick.
        auto energy = static_cast<std::uint64_t>(definition.buildCostEnergy.value);
        auto metal = static_cast<std::uint64_t>(definition.buildCostMetal.value);
        auto ticks = ((21 * energy) + (300 * metal) + (150 * 1400)) / 1400;

        // 0x40438A: sixty seconds is the ceiling, and eight of the 189 shipped
        // units reach it -- ARMCOM, CORCOM, ARMGATE, CORGATE, ARMCKFUS,
        // ARMBRTHA, CORINT and CORFMD. Everything else lands between 155
        // ticks (ARMDRAG) and 1790 (ARMFUS, CORFUS).
        ticks = std::min<std::uint64_t>(ticks, MaxCaptureTicks);

        // 0x4043A9: scaled by how healthy the target is, half time at zero.
        auto maxHitPoints = static_cast<std::uint64_t>(definition.maxHitPoints);
        if (maxHitPoints > 0)
        {
            auto hitPoints = static_cast<std::uint64_t>(target.hitPoints);
            ticks = ((hitPoints + maxHitPoints) * ticks) / (2 * maxHitPoints);
        }

        // 0x4043CD: and by the target's veterancy, +10% per five kills.
        ticks = (((static_cast<std::uint64_t>(target.kills) / 5) + 10) * ticks) / 10;

        return static_cast<unsigned int>(std::max<std::uint64_t>(1, ticks));
    }

    bool GameSimulation::captureUnit(UnitId targetId, PlayerId captor, std::optional<UnitId> captorUnitId)
    {
        auto unitRef = tryGetUnitState(targetId);
        if (!unitRef || unitRef->get().isDead())
        {
            return true;
        }
        auto& unit = unitRef->get();
        if (unit.isOwnedBy(captor))
        {
            return true;
        }

        auto previousOwner = unit.owner;

        // Before the change, so the recorder can still read the old owner from
        // its own table and tell the two blocks apart. D10's answer is the
        // original's own: a cause-4 death for the old id and a new unit in the
        // captor's block.
        if (demoRecorder)
        {
            demoRecorder->unitCaptured(*this, targetId, captor);
        }

        unit.owner = captor;

        // The spatial index carries owners so a target search can drop its
        // own side cheaply, and this is the only thing in the game that
        // rewrites one.
        invalidateUnitSpatialIndex();

        // The unit changes hands with a clean slate: whatever it was doing
        // for its old owner stops, and it must not keep shooting at its new
        // friends.
        unit.orders.clear();
        unit.buildOrderUnitId = std::nullopt;
        unit.behaviourState = UnitBehaviorStateIdle();
        unit.clearWeaponTargets();

        events.push_back(UnitCapturedEvent{targetId, previousOwner, captor, captorUnitId});
        return true;
    }

    float GameSimulation::resourceBonusFor(PlayerId playerId) const
    {
        auto it = aiControllers.find(playerId);
        if (it == aiControllers.end() || !it->second)
        {
            return 1.0f;
        }
        return it->second->getProfile().resourceCheatMultiplier.value;
    }

    void GameSimulation::toggleSelfDestruct(UnitId unitId)
    {
        auto unitRef = tryGetUnitState(unitId);
        if (!unitRef || unitRef->get().isDead())
        {
            return;
        }
        auto& unit = unitRef->get();

        if (unit.selfDestructTime)
        {
            unit.selfDestructTime = std::nullopt;
        }
        else
        {
            startSelfDestruct(unitId);
        }
    }

    void GameSimulation::startSelfDestruct(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        if (unit.isDead() || unit.selfDestructTime)
        {
            return;
        }
        // One step a second (0x4020F6), from the definition's own count; an
        // explicit 0 goes off at once with no countdown (0x402053).
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        unit.selfDestructTime = gameTime + GameTime(unitDefinition.selfDestructCountdown * SimTicksPerSecond);
    }

    void GameSimulation::selfDestructUnit(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        // Self-destruction leaves nothing to reclaim.
        unit.markAsDeadNoCorpse();
        unit.selfDestructTime = std::nullopt;
        getPlayer(unit.owner).unitsLost += 1;

        // The owner is filled in like any other death: the scene needs it to
        // decide whether the blast is one the local player is entitled to
        // see, and by the time it reads the event the unit is gone.
        recordUnitDeath(*this, unitId, "self_destruct", std::nullopt);
        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, UnitDiedEvent::DeathType::SelfDestructed, unit.owner});
        if (demoRecorder)
        {
            // Cause 3, self-destruct: RWE leaves nothing to reclaim, so the
            // record is the level-0 shape the corpus gives the skipped-script
            // causes rather than the wreck the original's Killed might pick.
            demoRecorder->unitDied(*this, unitId, std::nullopt, 0, 3, 0);
        }

        const auto& explosion = unitDefinition.selfDestructAs.empty() ? unitDefinition.explodeAs : unitDefinition.selfDestructAs;
        if (!explosion.empty())
        {
            auto impactType = unit.position.y < terrain.getSeaLevel() ? ImpactType::Water : ImpactType::Normal;
            auto projectile = createProjectileFromWeapon(ProjectileSpawn{
                .owner = unit.owner,
                .weaponType = explosion,
                .position = unit.position,
                .direction = SimVector(0_ss, -1_ss, 0_ss),
                .distanceToTarget = 0_ss,
                .targetUnit = std::nullopt,
                .attacker = std::nullopt,
            });
            doProjectileImpact(projectile, impactType);
        }
    }

    void GameSimulation::updateSelfDestructs()
    {
        std::vector<UnitId> due;
        for (const auto& [unitId, unit] : units)
        {
            if (unit.isAlive() && unit.selfDestructTime && gameTime >= *unit.selfDestructTime)
            {
                due.push_back(unitId);
            }
        }
        for (auto unitId : due)
        {
            // A unit may already have been killed by an earlier explosion in this loop.
            if (getUnitState(unitId).isAlive())
            {
                selfDestructUnit(unitId);
            }
        }
    }

    void GameSimulation::updateNanoframeDecay()
    {
        std::vector<UnitId> gone;

        for (auto& entry : units)
        {
            auto& unit = entry.second;
            if (unit.isDead())
            {
                continue;
            }

            const auto& unitDefinition = unitDefinitions.at(unit.unitType);

            if (!unit.isBeingBuilt(unitDefinition))
            {
                // The mission ends when the frame does. A unit that was
                // finished, or that started life finished, carries no timer.
                unit.nanoframeDecayTime = std::nullopt;
                unit.nanoframeWorkedOn = false;
                unit.nanoframeDecayRemainder = 0;
                continue;
            }

            if (!unit.nanoframeDecayTime)
            {
                // Ordinarily the timer is wound in trySpawnUnit, where the
                // original installs the mission. This covers a frame that
                // arrived some other way -- a save written before frames
                // decayed, or a test assembling a UnitState by hand.
                unit.nanoframeDecayTime = gameTime + GameTime(NanoframeDecayGraceTicks);
                continue;
            }

            if (gameTime < *unit.nanoframeDecayTime)
            {
                continue;
            }

            if (unit.nanoframeWorkedOn)
            {
                // Somebody built on me this period. Look again in a second.
                unit.nanoframeWorkedOn = false;
                unit.nanoframeDecayRemainder = 0;
                unit.nanoframeDecayTime = gameTime + GameTime(NanoframeDecayCheckTicks);
                continue;
            }

            unit.nanoframeDecayTime = gameTime + GameTime(NanoframeDecayTicks);

            // `buildtime * 11 / buildCostEnergy`, carrying what did not
            // divide. A frame that costs no energy at all has nothing to
            // divide by; in the original that is a division by zero whose
            // result runs the remaining fraction straight back to 1, so the
            // frame goes in one step.
            auto energyCost = static_cast<unsigned int>(unitDefinition.buildCostEnergy.value);
            auto step = unit.buildTimeCompleted;
            if (energyCost > 0)
            {
                auto numerator = (unitDefinition.buildTime * NanoframeDecayTicks) + unit.nanoframeDecayRemainder;
                step = numerator / energyCost;
                unit.nanoframeDecayRemainder = numerator % energyCost;
            }

            if (unit.removeBuildProgress(unitDefinition, step))
            {
                gone.push_back(entry.first);
            }
        }

        for (auto unitId : gone)
        {
            // TA kills the frame with `DamageUnit(self, self, 30000, cause 9)`,
            // and cause 9 is one of the three the death routine short-circuits:
            // no wreck, no `Killed` script, no explosion. The unit is simply
            // taken off the board -- and not counted, either: the dispatch at
            // 0x48688C takes only causes 1 to 6, so nobody's Losses move for a
            // frame that rotted away.
            removeUnfinishedUnit(unitId);
        }
    }

    std::optional<UnitWeapon> tryCreateWeapon(const GameSimulation& sim, const std::string& weaponType)
    {
        if (sim.weaponDefinitions.find(toUpper(weaponType)) == sim.weaponDefinitions.end())
        {
            return std::nullopt;
        }

        UnitWeapon weapon;
        weapon.weaponType = toUpper(weaponType);
        return weapon;
    }

    std::vector<UnitMesh> createUnitMeshes(const GameSimulation& sim, const std::string& objectName)
    {
        const auto& def = sim.unitModelDefinitions.at(objectName);

        const auto& pieceDefs = def.pieces;

        std::vector<UnitMesh> pieces(pieceDefs.size());
        for (Index i = 0; i < getSize(pieces); ++i)
        {
            pieces[i].name = pieceDefs[i].name;
        }

        return pieces;
    }

    UnitState createUnit(
        GameSimulation& simulation,
        const std::string& unitType,
        PlayerId owner,
        const SimVector& position,
        std::optional<SimAngle> rotation)
    {
        const auto& unitDefinition = simulation.unitDefinitions.at(unitType);

        auto meshes = createUnitMeshes(simulation, unitDefinition.objectName);
        auto modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);

        // Every piece of every model starts shaded, mobile or not: the
        // piece-list builder does `or byte [ebx+0x28],0x4` unconditionally
        // (0x45AF31, TOTALA-EXE-SHADING.md S:11), and a script has to say
        // DONT_SHADE to turn it off. RWE used to clear the flag for anything
        // mobile, which meant the whole of the shading work reached buildings
        // and nothing else.
        //
        // The opt-out is not theoretical: 290 of the 714 shipped scripts call
        // DONT_SHADE and not one calls SHADE, so the data already says where
        // shading is unwanted -- canopies, glass, the pieces authored to be
        // read flat -- and it can only say so if the default is on.

        const auto& script = simulation.unitScriptDefinitions.at(unitType);
        auto cobEnv = std::make_unique<CobEnvironment>(&script);
        UnitState unit(meshes, std::move(cobEnv));
        unit.unitType = toUpper(unitType);
        unit.owner = owner;
        unit.position = position;
        unit.previousPosition = position;

        // The original seeds both standing orders from the definition here and
        // never consults it again, so anything that moves them afterwards --
        // the buttons, a script -- sticks for the life of the unit.
        unit.moveOrders = unitDefinition.standingMoveOrder;
        unit.fireOrders = unitDefinition.standingFireOrder;

        // DELIBERATE DIVERGENCE (see docs/TOTALA-EXE.md section 83).
        //
        // A bomber that ships on Hold Fire is given Fire At Will instead. In
        // the shipped data ARMTHUND and CORSHAD are `StandingFireOrder=0`
        // while ARMPNIX and CORHURC are `2`, which is the whole reason a
        // patrolling Phoenix bombs what it passes over and a patrolling
        // Thunder flies by: the patrol handler's engage check is Fire At Will
        // exactly, so for the level-one bombers it never runs.
        //
        // Changed here rather than in the patrol check so the divergence is
        // visible and reversible -- the fire-order button reads Fire At Will,
        // and a player who wants the original sets Hold Fire by hand. The
        // bombers then take the same path the Phoenix already takes instead
        // of a second one written for them.
        if (unit.fireOrders == UnitFireOrders::HoldFire && unitDefinition.canFly)
        {
            auto carriesABomb = [&](const std::string& weaponName) {
                if (weaponName.empty())
                {
                    return false;
                }
                auto it = simulation.weaponDefinitions.find(toUpper(weaponName));
                return it != simulation.weaponDefinitions.end()
                    && std::holds_alternative<ProjectilePhysicsTypeBomb>(it->second.physicsType);
            };

            if (carriesABomb(unitDefinition.weapon1)
                || carriesABomb(unitDefinition.weapon2)
                || carriesABomb(unitDefinition.weapon3))
            {
                unit.fireOrders = UnitFireOrders::FireAtWill;
            }
        }

        // Init_Cloaked is seeded in the same breath by the original, out of the
        // same flags dword. It only asks for the cloak; whether the unit gets
        // one is still settled a second at a time by the energy.
        unit.cloakRequested = unitDefinition.initCloaked;

        if (rotation)
        {
            unit.rotation = *rotation;
            unit.previousRotation = *rotation;
        }
        else if (unitDefinition.isMobile)
        {
            // spawn the unit facing the other way
            unit.rotation = HalfTurn;
            unit.previousRotation = HalfTurn;
        }

        if (!unitDefinition.weapon1.empty())
        {
            unit.weapons[0] = tryCreateWeapon(simulation, unitDefinition.weapon1);
        }
        if (!unitDefinition.weapon2.empty())
        {
            unit.weapons[1] = tryCreateWeapon(simulation, unitDefinition.weapon2);
        }
        if (!unitDefinition.weapon3.empty())
        {
            unit.weapons[2] = tryCreateWeapon(simulation, unitDefinition.weapon3);
        }

        return unit;
    }

    SimScalar GameSimulation::computeBuildHeight(const UnitDefinition& unitDefinition, const DiscreteRect& footprint) const
    {
        // A cell the building stands on: the original tests yardmap bit 3,
        // which is set for the characters o O c f y G and clear for the water
        // ones C Y w and for a gap.
        auto standsOn = [](YardMapCell cell) {
            switch (cell)
            {
                case YardMapCell::Ground:
                case YardMapCell::GroundPassableWhenClosed:
                case YardMapCell::GroundGeoPassableWhenOpen:
                case YardMapCell::GroundNoFeature:
                case YardMapCell::GroundPassable:
                case YardMapCell::Geo:
                    return true;
                default:
                    return false;
            }
        };

        const auto& heights = terrain.getHeightMap();

        auto lowestLandCorner = std::optional<SimScalar>();
        for (int dy = 0; dy < footprint.height; ++dy)
        {
            for (int dx = 0; dx < footprint.width; ++dx)
            {
                auto x = footprint.x + dx;
                auto y = footprint.y + dy;
                if (x < 0 || y < 0)
                {
                    continue;
                }
                auto ux = static_cast<unsigned int>(x);
                auto uy = static_cast<unsigned int>(y);
                if (ux + 1 >= static_cast<unsigned int>(heights.getWidth()) || uy + 1 >= static_cast<unsigned int>(heights.getHeight()))
                {
                    continue;
                }

                // No yardmap at all means the whole footprint is ground,
                // which is what a unit without one occupies.
                if (unitDefinition.yardMap)
                {
                    auto ux2 = static_cast<unsigned int>(dx);
                    auto uy2 = static_cast<unsigned int>(dy);
                    if (ux2 >= static_cast<unsigned int>(unitDefinition.yardMap->getWidth()) || uy2 >= static_cast<unsigned int>(unitDefinition.yardMap->getHeight()))
                    {
                        continue;
                    }
                    if (!standsOn(unitDefinition.yardMap->get(ux2, uy2)))
                    {
                        continue;
                    }
                }

                // The lowest of the cell's four corners, as 0x47D8D2 takes
                // `cell+0x6` -- the low corner the terrain record already
                // holds.
                auto corner = rweMin(
                    rweMin(intToSimScalar(heights.get(ux, uy)), intToSimScalar(heights.get(ux + 1, uy))),
                    rweMin(intToSimScalar(heights.get(ux, uy + 1)), intToSimScalar(heights.get(ux + 1, uy + 1))));

                lowestLandCorner = lowestLandCorner ? rweMin(*lowestLandCorner, corner) : corner;
            }
        }

        if (lowestLandCorner)
        {
            return *lowestLandCorner;
        }

        // Nothing in the footprint stands on the ground: a water building.
        return terrain.getSeaLevel() - intToSimScalar(static_cast<int>(unitDefinition.waterLine));
    }

    std::optional<UnitId> GameSimulation::trySpawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<SimAngle> rotation)
    {
        auto unit = createUnit(*this, unitType, owner, position, rotation);
        const auto& unitDefinition = unitDefinitions.at(unitType);

        if (!unitDefinition.isMobile)
        {
            // A building is levelled onto its footprint the moment it is
            // created (0x47DDC0 immediately before CreateUnit), and nothing
            // moves it afterwards -- the per-tick height routine returns at
            // once for anything without a mover.
            auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            unit.position.y = computeBuildHeight(unitDefinition, footprint);
            unit.previousPosition.y = unit.position.y;
        }
        else if (unitDefinition.floater || unitDefinition.canHover)
        {
            unit.position.y = rweMax(terrain.getSeaLevel(), unit.position.y);
            unit.previousPosition.y = unit.position.y;
        }
        else if (!unitDefinition.canFly)
        {
            // And a mobile ground or sea unit is settled onto the terrain
            // inside CreateUnit too (0x486109 calling 0x48A870), before it is
            // ever drawn. Without that a submarine appears at the height of
            // the shipyard's build piece and drops to the sea bed on its
            // first step, which is what the play-test saw.
            unit.position.y = terrain.getHeightAt(unit.position.x, unit.position.z);
            unit.previousPosition.y = unit.position.y;
        }

        // TODO: if we failed to add the unit throw some warning
        auto unitId = tryAddUnit(std::move(unit));

        if (unitId)
        {
            // Give the unit its demo id before anything else can look at it:
            // a build's 0x09 needs the frame to have one, and an already
            // complete unit -- a commander, a resurrection -- needs it to
            // appear in the 0x2c round robin at all.
            if (demoRecorder)
            {
                demoRecorder->unitCreated(*this, *unitId);
            }

            UnitBehaviorService(this).onCreate(*unitId);

            // The original hangs a `GetBuilt` mission off the frame as it is
            // placed, and that mission's first act is to set a timer. A unit
            // that arrives already finished -- a start-position commander --
            // never gets one.
            auto& spawnedUnit = getUnitState(*unitId);
            if (spawnedUnit.isBeingBuilt(unitDefinition))
            {
                spawnedUnit.nanoframeDecayTime = gameTime + GameTime(NanoframeDecayGraceTicks);
            }

            events.push_back(UnitSpawnedEvent{*unitId});
        }

        return unitId;
    }

    std::optional<UnitId> GameSimulation::tryAddUnit(UnitState&& unit)
    {
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        if (isCollisionAt(footprintRect))
        {
            return std::nullopt;
        }

        auto unitId = units.emplace(std::move(unit));
        const auto& insertedUnit = units.tryGet(unitId)->get();

        // The spatial index has never heard of this one, and a search that
        // consulted it would not find the unit at all.
        invalidateUnitSpatialIndex();

        auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);

        if (unitDefinition.isMobile)
        {
            occupiedGrid.forEach(*footprintRegion, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
        }
        else
        {
            assert(!!unitDefinition.yardMap);
            occupiedGrid.forEach2(footprintRegion->x, footprintRegion->y, *unitDefinition.yardMap, [&](auto& cell, const auto& yardMapCell) {
                cell.buildingInfo = OccupiedCellBuildingInfo{unitId, isPassable(yardMapCell, insertedUnit.yardOpen)};
            });
        }

        return unitId;
    }

    bool GameSimulation::isInsideBuildableArea(unsigned int x, unsigned int y, unsigned int footprintX, unsigned int footprintZ) const
    {
        // The first thing 0x47D2E0 does, before it looks at a single cell
        // (0x47D302-0x47D352): the footprint's corner must be at least one
        // cell in from the top and the left -- `cmp cx,1 / jl refuse` for
        // each axis -- and its far side must stop short of the last cell,
        // `x + footprintX >= width` refusing at 0x47D339. So nothing is ever
        // built touching the edge of the map, on any side. RWE had no such
        // rule, and both the players and the computer could plant a
        // building flush against the boundary.
        auto width = static_cast<unsigned int>(occupiedGrid.getWidth());
        auto height = static_cast<unsigned int>(occupiedGrid.getHeight());
        return x >= 1u && y >= 1u && x + footprintX < width && y + footprintZ < height;
    }

    bool GameSimulation::canBeBuiltAt(const rwe::MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y) const
    {
        if (!isInsideBuildableArea(x, y, mc.footprintX, mc.footprintZ))
        {
            return false;
        }

        if (isCollisionAt(DiscreteRect(x, y, mc.footprintX, mc.footprintZ)))
        {
            return false;
        }

        if (!isGridPointWalkable(terrain, mc, x, y))
        {
            return false;
        }

        if (yardMapContainsGeo && yardMap && !containsAnyGeoMatch(*yardMap, x, y))
        {
            return false;
        }

        return true;
    }

    bool GameSimulation::canBeBuiltAtAsSeenBy(const MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y, PlayerId player) const
    {
        if (!isInsideBuildableArea(x, y, mc.footprintX, mc.footprintZ))
        {
            return false;
        }

        // Ground nobody on this side has ever looked at. The box is red over
        // all of it, whatever the terrain underneath happens to be.
        //
        // This is the other half of the fog rule below, and it pulls the
        // opposite way on purpose. Below, an undiscovered occupant is
        // ignored, because refusing the placement would announce that
        // something is standing there. Here the whole square is undiscovered,
        // so a box that went green would announce that the ground is flat and
        // clear -- a free survey of the map, taken by sweeping the cursor
        // across the black with a building on it, which is a good deal more
        // than one hidden tank. Reported from play on Coast To Coast: placing
        // into unexplored ground showed "green where it can be placed and red
        // where it cant, even though the land is unexplored".
        //
        // Every cell of the footprint, not its centre: a building half on
        // known ground is half a survey.
        for (unsigned int cellZ = y; cellZ < y + mc.footprintZ; ++cellZ)
        {
            for (unsigned int cellX = x; cellX < x + mc.footprintX; ++cellX)
            {
                if (!isExploredBy(player, terrain.heightmapIndexToWorldCenter(static_cast<int>(cellX), static_cast<int>(cellZ))))
                {
                    return false;
                }
            }
        }

        auto region = occupiedGrid.tryToRegion(DiscreteRect(x, y, mc.footprintX, mc.footprintZ));
        if (!region)
        {
            return false;
        }

        // A unit the player has not seen does not stand in the way of the
        // placement box, because letting it would say that it is there. A
        // feature does: a wreck or a tree is part of the ground rather than
        // somebody's secret, and the report this answers was about buildings.
        auto blocked = occupiedGrid.any(*region, [&](const auto& cell) {
            if (cell.mobileUnitId && canSeeUnit(player, *cell.mobileUnitId))
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable && canSeeUnit(player, cell.buildingInfo->unit))
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                if (getFeatureDefinition(f.featureName).blocking)
                {
                    return true;
                }
            }
            return false;
        });
        if (blocked)
        {
            return false;
        }

        if (!isGridPointWalkable(terrain, mc, x, y))
        {
            return false;
        }

        if (yardMapContainsGeo && yardMap && !containsAnyGeoMatch(*yardMap, x, y))
        {
            return false;
        }

        return true;
    }

    DiscreteRect GameSimulation::computeFootprintRegion(const SimVector& position, unsigned int footprintX, unsigned int footprintZ) const
    {
        auto halfFootprintX = SimScalar(footprintX * MapTerrain::HeightTileWidthInWorldUnits.value / 2);
        auto halfFootprintZ = SimScalar(footprintZ * MapTerrain::HeightTileHeightInWorldUnits.value / 2);
        SimVector topLeft(
            position.x - halfFootprintX,
            position.y,
            position.z - halfFootprintZ);

        auto cell = terrain.worldToHeightmapCoordinateNearest(topLeft);

        return DiscreteRect(cell.x, cell.y, footprintX, footprintZ);
    }

    DiscreteRect GameSimulation::computeFootprintRegion(const SimVector& position, const UnitDefinition::MovementCollisionInfo& collisionInfo) const
    {
        auto [footprintX, footprintZ] = getFootprintXZ(collisionInfo);
        return computeFootprintRegion(position, footprintX, footprintZ);
    }

    bool GameSimulation::anyFeatureOccupies(const DiscreteRect& rect) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return occupiedGrid.any(*region, [&](const auto& cell) {
            return cell.featureId.has_value();
        });
    }

    bool GameSimulation::containsAnyGeoMatch(const Grid<YardMapCell>& yardMap, unsigned int x, unsigned int y) const
    {
        // Iterate explicitly with per-cell bounds checks against the underlying
        // vector size. The build cursor can hover with a footprint that extends
        // past geoGrid's edge (geoGrid is one cell smaller than the heightmap),
        // and Grid::any2 / Grid::get would otherwise walk off the end.
        const int geoW = geoGrid.getWidth();
        const int geoH = geoGrid.getHeight();
        const int yardW = yardMap.getWidth();
        const int yardH = yardMap.getHeight();
        const int baseX = static_cast<int>(x);
        const int baseY = static_cast<int>(y);
        const auto& geoVec = geoGrid.getVector();
        const size_t geoSize = geoVec.size();

        for (int dy = 0; dy < yardH; ++dy)
        {
            const int gy = baseY + dy;
            if (gy < 0 || gy >= geoH)
            {
                continue;
            }
            for (int dx = 0; dx < yardW; ++dx)
            {
                const int gx = baseX + dx;
                if (gx < 0 || gx >= geoW)
                {
                    continue;
                }
                const size_t idx = static_cast<size_t>(gy) * static_cast<size_t>(geoW) + static_cast<size_t>(gx);
                if (idx >= geoSize)
                {
                    continue;
                }
                if (geoVec[idx] && isGeo(yardMap.get(dx, dy)))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return isCollisionAt(*region);
    }

    bool GameSimulation::isCollisionAt(const GridRegion& region) const
    {
        return occupiedGrid.any(region, [&](const auto& cell) {
            if (cell.mobileUnitId)
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable)
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                const auto& def = getFeatureDefinition(f.featureName);
                if (def.blocking)
                {
                    return true;
                }
            }

            return false;
        });
    }

    bool GameSimulation::isCollisionAtIgnoringBuilding(const GridRegion& region, UnitId building) const
    {
        return occupiedGrid.any(region, [&](const auto& cell) {
            if (cell.mobileUnitId)
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable && cell.buildingInfo->unit != building)
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                const auto& def = getFeatureDefinition(f.featureName);
                if (def.blocking)
                {
                    return true;
                }
            }

            return false;
        });
    }

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect, UnitId self) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return region->any([&](const GridCoordinates& c) { return cellBlocksUnit(occupiedGrid.get(c), self); });
    }

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect, UnitId self, const DiscreteRect& passableRegion) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return region->any([&](const GridCoordinates& c) {
            // The unit is already standing on this cell, so stepping across it
            // is not entering an obstacle. Everything else blocks as usual.
            if (passableRegion.contains(Point(c.x, c.y)))
            {
                return false;
            }
            return cellBlocksUnit(occupiedGrid.get(c), self);
        });
    }

    bool GameSimulation::cellBlocksUnit(const OccupiedCell& cell, UnitId self) const
    {
        if (cell.mobileUnitId && *cell.mobileUnitId != self)
        {
            return true;
        }
        if (cell.buildingInfo && !cell.buildingInfo->passable)
        {
            return true;
        }
        if (cell.featureId)
        {
            const auto& f = getFeature(*cell.featureId);
            const auto& def = getFeatureDefinition(f.featureName);
            if (def.blocking)
            {
                return true;
            }
        }

        return false;
    }

    bool GameSimulation::isYardmapBlocked(unsigned int x, unsigned int y, const Grid<YardMapCell>& yardMap, bool open, UnitId self) const
    {
        return occupiedGrid.any2(x, y, yardMap, [&](const auto& cell, const auto& yardMapCell) {
            if (isPassable(yardMapCell, open))
            {
                return false;
            }

            if (cell.mobileUnitId)
            {
                return true;
            }
            if (cell.buildingInfo && !cell.buildingInfo->passable && cell.buildingInfo->unit != self)
            {
                return true;
            }
            if (cell.featureId)
            {
                const auto& f = getFeature(*cell.featureId);
                const auto& def = getFeatureDefinition(f.featureName);
                if (def.blocking)
                {
                    return true;
                }
            }

            return false;
        });
    }

    bool GameSimulation::isAdjacentToObstacle(const DiscreteRect& rect) const
    {
        DiscreteRect top(rect.x - 1, rect.y - 1, rect.width + 2, 1);
        DiscreteRect bottom(rect.x - 1, rect.y + rect.width, rect.width + 2, 1);
        DiscreteRect left(rect.x - 1, rect.y, 1, rect.height);
        DiscreteRect right(rect.x + rect.width, rect.y, 1, rect.height);
        return isCollisionAt(top)
            || isCollisionAt(bottom)
            || isCollisionAt(left)
            || isCollisionAt(right);
    }

    void GameSimulation::showObject(UnitId unitId, const std::string& name)
    {
        auto mesh = getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().visible = true;
        }
    }

    void GameSimulation::hideObject(UnitId unitId, const std::string& name)
    {
        auto mesh = getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().visible = false;
        }
    }

    void GameSimulation::enableShading(UnitId unitId, const std::string& name)
    {
        auto mesh = getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().shaded = true;
        }
    }

    void GameSimulation::disableShading(UnitId unitId, const std::string& name)
    {
        auto mesh = getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().shaded = false;
        }
    }

    void GameSimulation::enableCaching(UnitId unitId, const std::string& name)
    {
        auto mesh = getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().cached = true;
        }
    }

    void GameSimulation::disableCaching(UnitId unitId, const std::string& name)
    {
        auto mesh = getUnitState(unitId).findPiece(name);
        if (mesh)
        {
            mesh->get().cached = false;
        }
    }

    UnitState& GameSimulation::getUnitState(UnitId id)
    {
        auto it = units.find(id);
        assert(it != units.end());
        return it->second;
    }

    const UnitState& GameSimulation::getUnitState(UnitId id) const
    {
        auto it = units.find(id);
        assert(it != units.end());
        return it->second;
    }

    UnitInfo GameSimulation::getUnitInfo(UnitId id)
    {
        auto& state = getUnitState(id);
        const auto& definition = unitDefinitions.at(state.unitType);
        return UnitInfo(id, &state, &definition);
    }

    ConstUnitInfo GameSimulation::getUnitInfo(UnitId id) const
    {
        auto& state = getUnitState(id);
        const auto& definition = unitDefinitions.at(state.unitType);
        return ConstUnitInfo(id, &state, &definition);
    }

    std::optional<std::reference_wrapper<UnitState>> GameSimulation::tryGetUnitState(UnitId id)
    {
        return tryFind(units, id);
    }

    std::optional<std::reference_wrapper<const UnitState>> GameSimulation::tryGetUnitState(UnitId id) const
    {
        return tryFind(units, id);
    }

    std::optional<std::reference_wrapper<const UnitState>> GameSimulation::tryGetUnitState(CobUnitId id) const
    {
        return tryFind(units, UnitId(id.value));
    }

    bool GameSimulation::unitExists(UnitId id) const
    {
        auto it = units.find(id);
        return it != units.end();
    }

    MapFeature& GameSimulation::getFeature(FeatureId id)
    {
        auto it = features.find(id);
        assert(it != features.end());
        return it->second;
    }

    const MapFeature& GameSimulation::getFeature(FeatureId id) const
    {
        auto it = features.find(id);
        assert(it != features.end());
        return it->second;
    }

    std::optional<std::reference_wrapper<MapFeature>> GameSimulation::tryGetFeature(FeatureId id)
    {
        return tryFind(features, id);
    }

    std::optional<std::reference_wrapper<const MapFeature>> GameSimulation::tryGetFeature(FeatureId id) const
    {
        return tryFind(features, id);
    }

    GamePlayerInfo& GameSimulation::getPlayer(PlayerId player)
    {
        return players.at(player.value);
    }

    const GamePlayerInfo& GameSimulation::getPlayer(PlayerId player) const
    {
        return players.at(player.value);
    }

    void GameSimulation::moveObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar position, SimScalar speed)
    {
        getUnitState(unitId).moveObject(name, axis, position, speed);
    }

    void GameSimulation::moveObjectNow(UnitId unitId, const std::string& name, SimAxis axis, SimScalar position)
    {
        getUnitState(unitId).moveObjectNow(name, axis, position);
    }

    void GameSimulation::turnObject(UnitId unitId, const std::string& name, SimAxis axis, SimAngle angle, SimScalar speed)
    {
        getUnitState(unitId).turnObject(name, axis, angle, speed);
    }

    void GameSimulation::turnObjectNow(UnitId unitId, const std::string& name, SimAxis axis, SimAngle angle)
    {
        getUnitState(unitId).turnObjectNow(name, axis, angle);
    }

    void GameSimulation::spinObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar speed, SimScalar acceleration)
    {
        getUnitState(unitId).spinObject(name, axis, speed, acceleration);
    }

    void GameSimulation::stopSpinObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar deceleration)
    {
        getUnitState(unitId).stopSpinObject(name, axis, deceleration);
    }

    bool GameSimulation::isPieceMoving(UnitId unitId, const std::string& name, SimAxis axis) const
    {
        return getUnitState(unitId).isMoveInProgress(name, axis);
    }

    bool GameSimulation::isPieceTurning(UnitId unitId, const std::string& name, SimAxis axis) const
    {
        return getUnitState(unitId).isTurnInProgress(name, axis);
    }

    std::optional<SimVector> GameSimulation::intersectLineWithTerrain(const Line3x<SimScalar>& line) const
    {
        return terrain.intersectLine(line);
    }

    void GameSimulation::moveUnitOccupiedArea(const DiscreteRect& oldRect, const DiscreteRect& newRect, UnitId unitId)
    {
        auto oldRegion = occupiedGrid.tryToRegion(oldRect);
        assert(!!oldRegion);
        auto newRegion = occupiedGrid.tryToRegion(newRect);
        assert(!!newRegion);

        occupiedGrid.forEach(*oldRegion, [](auto& cell) { cell.mobileUnitId = std::nullopt; });
        occupiedGrid.forEach(*newRegion, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
    }

    void GameSimulation::requestPath(UnitId unitId)
    {
        // A search that outlived its tick is still the unit's turn: it holds
        // the head of the queue, and a unit that asks again for the same place
        // while it runs must leave it there. Moving it to the back would put a
        // different unit at the head and break the scheduler's assumption that
        // the search belongs to the request at the head (and the save format,
        // which reads a suspended search off the head). If the unit's goal has
        // moved on, the search in flight is thrown away and the request is
        // queued normally below.
        if (pathFindingService.onPathRequested(*this, unitId))
        {
            return;
        }

        PathRequest request{unitId};

        // If the unit is already in the queue for a path,
        // we'll assume that they no longer care about their old request
        // and that their new request is for some new path,
        // so we'll move them to the back of the queue for fairness.
        //
        // Unless they are at the FRONT. A search is sliced across ticks, and
        // the pathfinder's half-finished search always belongs to the front
        // of this queue: it says so with an assertion when the search
        // completes, and pops the front to retire it. Moving the front unit
        // to the back broke that -- in a Debug build the assertion, in a
        // Release build the wrong request popped and the right one left
        // behind for a unit whose path had already been delivered. It took a
        // unit asking twice while its own search was suspended, which needs
        // a busy queue and a goal that moves: thirty hulls sailing at a
        // remembered enemy is both. Left at the front, a changed destination
        // is already handled where the search is resumed, which abandons it
        // and starts again; an unchanged one simply finishes.
        auto it = std::find(pathRequests.begin(), pathRequests.end(), request);
        if (it != pathRequests.end())
        {
            if (it == pathRequests.begin())
            {
                return;
            }
            pathRequests.erase(it);
        }

        pathRequests.push_back(PathRequest{unitId});
    }

    SimVector toMissileDirection(SimAngle heading, SimAngle pitch)
    {
        auto horizontal = cos(pitch);
        return SimVector(sin(heading) * horizontal, sin(pitch), cos(heading) * horizontal);
    }

    SimVector computeWindVector(SimAngle direction, int speed)
    {
        // The original builds the map's wind vector once, each time the wind
        // changes (0x490CA4-0x490D35): X from the routine at 0x4b70ef, Z from
        // the one at 0x4b7123, each negated and then doubled.
        //
        // Which of those is sin and which is cos is worth spelling out,
        // because docs/TOTALA-EXE.md had it backwards until this was ported.
        // The two routines are identical but for the table index, which the
        // second advances by 0x4000 -- a quarter turn. Both read 0x509f00,
        // whose first entry is 0 and which peaks at 0x2000, so that table is a
        // SINE table of amplitude 8192: the first routine is sin and the
        // second cos, not the reverse. Each multiplies by the speed and then
        // does shrd ..., 0xd -- a >>13 that exactly cancels the 8192 -- so a
        // routine returns trig(direction) * speed.
        //
        // That happens to be RWE's own convention too; see toMissileDirection
        // directly above, which puts sin in X and cos in Z for the same reason.
        //
        // The components the original stores are 16.16 fixed point, like a
        // position, which is what the 65536 converts out of. At Brain Coral's
        // maxwindspeed of 3000 the wind carries a shell 0.092 world units a
        // tick -- around six units over a Crusader shell's flight, against a
        // damage radius of 24. Enough to matter, not enough to dominate.
        //
        // There is no Y term. The word at globals+0x37ED0 is never written
        // anywhere in the binary, so the wind is strictly horizontal.
        auto magnitude = SimScalar(-2 * speed) / 65536_ss;
        return SimVector(sin(direction) * magnitude, 0_ss, cos(direction) * magnitude);
    }

    /**
     * TA hangs a cruise missile on two constants of its own rather than on
     * anything the weapon says: it holds 700 world units up and only gives up
     * the cruise for the target once it is within 1024 of where it was aimed
     * (0x49B3E0).
     */
    static const SimScalar CruiseAltitude = 700_ss;
    static const SimScalar CruiseHandoverDistance = 1024_ss;

    /**
     * How far off the nose the target may get before a `burnblow` missile gives
     * up and detonates where it is: 27000 of a 65536 turn, a little under 150
     * degrees (0x49B5AA).
     */
    static const SimAngle BurnBlowAbortAngle = SimAngle(27000);

    Projectile GameSimulation::createProjectileFromWeapon(const ProjectileSpawn& spawn)
    {
        const auto& weaponType = spawn.weapon ? spawn.weapon->weaponType : spawn.weaponType;
        const auto& weaponDefinition = weaponDefinitions.at(weaponType);
        const auto& owner = spawn.owner;
        const auto& position = spawn.position;
        const auto& direction = spawn.direction;
        const auto& targetUnit = spawn.targetUnit;
        const auto& attacker = spawn.attacker;
        const auto& inheritedVelocity = spawn.inheritedVelocity;
        const auto& targetPosition = spawn.targetPosition;
        const auto& targetProjectile = spawn.targetProjectile;

        Projectile projectile;
        projectile.weaponType = weaponType;
        projectile.owner = owner;
        projectile.attacker = attacker;
        projectile.position = position;
        projectile.previousPosition = position;
        projectile.origin = position;
        projectile.velocity = direction * weaponDefinition.velocity;
        if (inheritedVelocity)
        {
            // Bombs released from bombers inherit the aircraft's velocity at
            // release. Without this, bombs would fall straight down from the
            // firing piece while the bomber has already moved past, causing
            // visible misses. (See bombsight handling in tryFireWeapon.)
            projectile.velocity = projectile.velocity + *inheritedVelocity;
        }

        projectile.lastSmoke = gameTime;

        projectile.damage = weaponDefinition.damage;

        projectile.damageRadius = weaponDefinition.damageRadius;
        projectile.edgeEffectiveness = weaponDefinition.edgeEffectiveness;

        // How long a straight-flying round lives, and the order the original
        // asks the questions in (0x49C942). It works the life out of the
        // weapon's own `range` and `weaponvelocity` -- `(range << 16) /
        // velocity` in the 16.16 the parser stored the speed as, which is
        // simply "how many ticks to fly the full range" -- and only falls back
        // on `weapontimer` for a weapon that declares no velocity to divide
        // by. So `weapontimer` is a fallback rather than an override, and RWE
        // had it the other way round: ARM_DISINTEGRATOR names both, and the
        // four seconds it asks for are not what the original uses.
        //
        // The number is the *range*, not the distance to whatever was aimed
        // at. Nothing shortens the flight to suit the target: the round is
        // launched along a direction fixed at the muzzle and flies its whole
        // 240 units whether the target is at 30 or at 239. For an ordinary
        // weapon that is only visible on a miss; for the D-gun, whose blast
        // is not consumed by going off (see noExplode), it is the whole
        // reason the trail carries on past what it was fired at.
        auto lineOfSight = std::holds_alternative<ProjectilePhysicsTypeLineOfSight>(weaponDefinition.physicsType);
        if (lineOfSight && weaponDefinition.velocity > 0_ss)
        {
            // Done in the original's fixed point rather than in floats, so
            // that a range that divides exactly by the speed gives the round
            // number rather than one tick less to a rounding error.
            auto rangeFixed = static_cast<int64_t>(simScalarToFixed(weaponDefinition.maxRange));
            auto velocityFixed = static_cast<int64_t>(simScalarToFixed(weaponDefinition.velocity));
            auto lifeTicks = static_cast<unsigned int>(std::max<int64_t>(rangeFixed / velocityFixed, 1));
            projectile.dieOnFrame = gameTime + GameTime(lifeTicks);
        }
        else if (weaponDefinition.weaponTimer)
        {
            auto randomDecay = weaponDefinition.randomDecay.value().value;
            auto randomVal = randomBelow(rng, randomDecay + 1u);
            projectile.dieOnFrame = gameTime + *weaponDefinition.weaponTimer - GameTime(randomDecay / 2) + GameTime(randomVal);
        }

        projectile.createdAt = gameTime;
        projectile.groundBounce = weaponDefinition.groundBounce;

        projectile.targetUnit = targetUnit;
        projectile.targetPosition = targetPosition;
        projectile.targetProjectile = targetProjectile;

        if (auto selfProp = std::get_if<ProjectilePhysicsTypeSelfPropelled>(&weaponDefinition.physicsType))
        {
            // A missile leaves at `startvelocity`; failing that it leaves at the
            // top speed if it has no motor to build up with, and otherwise from a
            // standstill (0x49C980). A vertical launch points straight up and is
            // not moving at all, so the whole climb comes out of the motor
            // (0x49CC20).
            if (selfProp->startVelocity != 0_ss)
            {
                projectile.speed = selfProp->startVelocity;
            }
            else if (selfProp->acceleration == 0_ss)
            {
                projectile.speed = selfProp->maxVelocity;
            }
            else
            {
                projectile.speed = 0_ss;
            }

            if (selfProp->vLaunch)
            {
                projectile.heading = SimAngle(0);
                projectile.pitch = QuarterTurn;
                projectile.velocity = SimVector(0_ss, 0_ss, 0_ss);
            }
            else
            {
                auto flat = SimVector(direction.x, 0_ss, direction.z);
                projectile.heading = UnitState::toRotation(flat);
                projectile.pitch = atan2(direction.y, flat.length());
                projectile.velocity = toMissileDirection(projectile.heading, projectile.pitch) * projectile.speed;
            }

            // The burn is `range / weaponvelocity` ticks -- how long the missile
            // would take to fly its whole range at the speed cap -- unless the
            // weapon says `noautorange`, in which case it is `weapontimer`, and
            // that is what times a vertical launch's climb (0x49C920). Running
            // out is not death: without `burnblow` the missile coasts on, which
            // is why one fired at the far edge of its range arrives unguided.
            auto burn = weaponDefinition.weaponTimer.value_or(GameTime(0));
            if (selfProp->autoRange)
            {
                burn = GameTime(simScalarToUInt(weaponDefinition.maxRange / selfProp->maxVelocity));
            }
            projectile.motorOutFrame = gameTime + burn;
            projectile.dieOnFrame = std::nullopt;
        }

        return projectile;
    }

    void GameSimulation::spawnProjectile(const ProjectileSpawn& spawn)
    {
        projectiles.emplace(createProjectileFromWeapon(spawn));
    }

    std::vector<int> dealStartPositions(const std::vector<int>& startPositions, StartLocationMode mode, std::minstd_rand& rng)
    {
        auto result = startPositions;

        if (mode == StartLocationMode::Fixed)
        {
            return result;
        }

        // Fisher-Yates, drawing each index straight out of the simulation's
        // own generator. It is walked from the back so that the very first
        // draw can still place any element anywhere, which an unshuffled
        // forward pass cannot.
        for (std::size_t i = result.size(); i > 1; --i)
        {
            auto j = static_cast<std::size_t>(rng() % static_cast<std::minstd_rand::result_type>(i));
            std::swap(result[i - 1], result[j]);
        }

        return result;
    }

    WinStatus GameSimulation::computeWinStatus() const
    {
        // The first player still standing, and whether anybody still
        // standing is on a different side from them. Allies do not fight
        // each other, so a rule that waits for one player to be left waits
        // for ever in a team game: a 2v2 whose losing pair had both been
        // wiped out went on running until whatever time limit was over it,
        // with the winners walking around an empty map.
        //
        // Two players are on different sides unless both name the same team.
        // A player on no team is nobody's ally, including of another player
        // on no team, which is what makes a free-for-all behave exactly as
        // it did.
        std::optional<PlayerId> livingPlayer;
        std::optional<int> livingTeam;
        for (Index i = 0; i < getSize(players); ++i)
        {
            const auto& p = players[i];

            if (p.status != GamePlayerStatus::Alive)
            {
                continue;
            }

            if (!livingPlayer)
            {
                livingPlayer = PlayerId(i);
                livingTeam = p.teamId;
                continue;
            }

            if (!livingTeam || !p.teamId || *livingTeam != *p.teamId)
            {
                // Somebody is left who is not on their side.
                return WinStatusUndecided();
            }
        }

        if (livingPlayer)
        {
            // Named by their lowest player id where a team has won, because
            // WinStatusWon carries one player. Everything that reads it
            // wants somebody to credit, and the arena report prints every
            // player's standing anyway.
            return WinStatusWon{*livingPlayer};
        }

        return WinStatusDraw();
    }

    bool GameSimulation::addResourceDelta(const UnitId& unitId, const Energy& energy, const Metal& metal, ResourceDebtGate gate)
    {
        return addResourceDelta(unitId, energy, metal, energy, metal, gate);
    }

    bool GameSimulation::addResourceDelta(const UnitId& unitId, const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal, ResourceDebtGate gate)
    {
        auto& unit = getUnitState(unitId);
        auto& player = getPlayer(unit.owner);

        // The unit that asked is the one that is told yes or no, and the one
        // that carries any debt: a builder hands its own economy block to the
        // request at 0x41BC5A. The player-level buffers are the running totals
        // the settle and the resource display need.
        player.recordDesire(apparentEnergy);
        player.recordDesire(apparentMetal);

        // Income is never refused. The original does not ask for what a unit
        // makes, it adds it straight into the unit's block (0x4016E2, 0x401749),
        // so a generator that happens to owe for something else still earns.
        if (apparentEnergy >= Energy(0))
        {
            player.energyProductionBuffer += apparentEnergy;
        }
        if (apparentMetal >= Metal(0))
        {
            player.metalProductionBuffer += apparentMetal;
        }

        return unit.addResourceDelta(apparentEnergy, apparentMetal, actualEnergy, actualMetal, gate);
    }

    bool GameSimulation::chargeStockpile(const UnitId& unitId, const Energy& energy, const Metal& metal)
    {
        auto& unit = getUnitState(unitId);
        auto& player = getPlayer(unit.owner);

        if (player.energy < energy || player.metal < metal)
        {
            return false;
        }

        player.energy -= energy;
        player.metal -= metal;

        // Booked as consumption for the display, on the player and on the
        // unit, the way a request is; nothing goes into the request buffers,
        // so the settle never sees it.
        player.recordDesire(-energy);
        player.recordDesire(-metal);
        unit.addEnergyDelta(-energy);
        unit.addMetalDelta(-metal);
        return true;
    }

    bool GameSimulation::addEnergyRequest(const UnitId& unitId, const Energy& amount)
    {
        auto& unit = getUnitState(unitId);
        auto& player = getPlayer(unit.owner);

        // 0x401180, in its own order: the demand goes onto the block before
        // the answer is worked out, so a refused request still shows up in the
        // resource bars as something the player wanted and did not get.
        player.recordDesire(-amount);
        unit.addEnergyDelta(-amount);

        if (unit.energyDebt > Energy(0))
        {
            return false;
        }

        unit.energyRequestBuffer += amount;
        return true;
    }

    bool GameSimulation::trySetYardOpen(const UnitId& unitId, bool open)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);

        assert(!!unitDefinition.yardMap);
        if (isYardmapBlocked(footprintRegion->x, footprintRegion->y, *unitDefinition.yardMap, open, unitId))
        {
            return false;
        }

        occupiedGrid.forEach2(footprintRegion->x, footprintRegion->y, *unitDefinition.yardMap, [&](auto& cell, const auto& yardMapCell) {
            cell.buildingInfo = OccupiedCellBuildingInfo{unitId, isPassable(yardMapCell, open)};
        });

        unit.yardOpen = open;

        return true;
    }

    void GameSimulation::emitBuggerOff(const UnitId& unitId)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        emitBuggerOff(computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo));
    }

    void GameSimulation::emitBuggerOff(const DiscreteRect& footprintRect)
    {
        auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);
        if (!footprintRegion)
        {
            return;
        }

        occupiedGrid.forEach(*footprintRegion, [&](const auto& e) {
            if (e.mobileUnitId)
            {
                tellToBuggerOff(*e.mobileUnitId, footprintRect);
            }
        });
    }

    void GameSimulation::tellToBuggerOff(const UnitId& unitId, const DiscreteRect& rect)
    {
        auto& unit = getUnitState(unitId);
        if (unit.orders.empty())
        {
            unit.addOrder(BuggerOffOrder(rect));
        }
    }

    GameHash GameSimulation::computeHash() const
    {
        return computeHashOf(*this);
    }

    void GameSimulation::activateUnit(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        unit.activate();
        events.push_back(UnitActivatedEvent{unitId});
    }

    void GameSimulation::deactivateUnit(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        unit.deactivate();
        events.push_back(UnitDeactivatedEvent{unitId});
    }

    void GameSimulation::modifyStockpileQueue(UnitId unitId, int count)
    {
        auto& unit = getUnitState(unitId);
        for (auto& weapon : unit.weapons)
        {
            if (!weapon || !weaponDefinitions.at(weapon->weaponType).stockpile)
            {
                continue;
            }

            weapon->queuedRounds = std::max(0, weapon->queuedRounds + count);
            if (weapon->queuedRounds == 0)
            {
                // Cancelling the last one throws away the part-built round and
                // refunds nothing: the original holds the progress on the order
                // (0x402BD4, [order+0x3E]) and the order goes with the count.
                weapon->stockpileProgress = 0;
                weapon->stockpileStepDelay = 0;
            }
            return;
        }
    }

    std::optional<std::reference_wrapper<const UnitWeapon>> GameSimulation::tryGetStockpileWeapon(UnitId unitId) const
    {
        const auto& unit = getUnitState(unitId);
        for (const auto& weapon : unit.weapons)
        {
            if (weapon && weaponDefinitions.at(weapon->weaponType).stockpile)
            {
                return std::cref(*weapon);
            }
        }
        return std::nullopt;
    }

    bool GameSimulation::isWithinCoverage(const SimVector& launcher, const SimVector& point, SimScalar coverage)
    {
        return rweAbs(launcher.x - point.x) <= coverage && rweAbs(launcher.z - point.z) <= coverage;
    }

    std::optional<ProjectileId> GameSimulation::findInterceptTarget(UnitId launcherId, unsigned int weaponIndex) const
    {
        const auto& launcher = getUnitState(launcherId);
        const auto& weapon = launcher.weapons[weaponIndex];
        if (!weapon)
        {
            return std::nullopt;
        }

        // No round, no search. The original reads the magazine byte before it
        // so much as looks at the projectile list (0x49D13C), so an anti-nuke
        // that has not finished building one does not go through the motions.
        if (weapon->stockedRounds <= 0)
        {
            return std::nullopt;
        }

        const auto& weaponDefinition = weaponDefinitions.at(weapon->weaponType);

        for (const auto& entry : projectiles)
        {
            const auto& candidate = entry.second;
            if (candidate.isDead || candidate.owner == launcher.owner)
            {
                continue;
            }

            const auto& candidateWeapon = weaponDefinitions.at(candidate.weaponType);
            if (!candidateWeapon.targetable)
            {
                continue;
            }

            // Where the missile is going, not where it is. That is the whole
            // point of an area defence: the anti-nuke engages a nuke aimed at
            // the box it guards, however far away the nuke happens to be at
            // this moment, and ignores one merely passing overhead.
            if (!candidate.targetPosition || !isWithinCoverage(launcher.position, *candidate.targetPosition, weaponDefinition.coverage))
            {
                continue;
            }

            // One interceptor per missile. The original settles this by
            // walking every projectile's target slot rather than by marking
            // the missile (0x49D1AE), so a round already in the air is what
            // holds the claim and losing it frees the target again.
            bool alreadyClaimed = false;
            for (const auto& other : projectiles)
            {
                if (!other.second.isDead && other.second.targetProjectile == entry.first)
                {
                    alreadyClaimed = true;
                    break;
                }
            }
            if (alreadyClaimed)
            {
                continue;
            }

            return entry.first;
        }

        return std::nullopt;
    }

    void GameSimulation::detonateProjectilesInBlast(std::optional<ProjectileId> source, const SimVector& position, SimScalar radius)
    {
        // Gathered before anything is detonated: an impact can kill a unit,
        // and a dying unit spawns its own explosion, so the projectile list
        // must not be walked across a call that might add to it.
        std::vector<ProjectileId> caught;
        for (const auto& entry : projectiles)
        {
            if (entry.second.isDead || (source && entry.first == *source))
            {
                continue;
            }
            if (position.distanceSquared(entry.second.position) < radius * radius)
            {
                caught.push_back(entry.first);
            }
        }

        for (const auto& caughtId : caught)
        {
            auto entry = projectiles.tryGet(caughtId);
            if (!entry || entry->get().isDead)
            {
                continue;
            }

            // Marked dead before it goes off, so that its own blast -- if it
            // was another interceptor -- cannot come back round to it.
            entry->get().isDead = true;
            auto copy = entry->get();
            doProjectileImpact(copy, ImpactType::Normal, caughtId);
            events.push_back(ProjectileDiedEvent{caughtId, copy.weaponType, copy.position, ProjectileDiedEvent::DeathType::NormalImpact});
        }
    }

    void GameSimulation::quietlyKillUnit(UnitId unitId)
    {
        quietlyKillUnit(unitId, true);
    }

    void GameSimulation::removeUnfinishedUnit(UnitId unitId)
    {
        recordUnitDeath(*this, unitId, "unfinished", std::nullopt);
        quietlyKillUnit(unitId, false);
        if (demoRecorder)
        {
            // Cause 9, an unfinished unit removed: no script, no corpse.
            demoRecorder->unitDied(*this, unitId, std::nullopt, 0, 9, 0);
        }
    }

    void GameSimulation::quietlyKillUnit(UnitId unitId, bool countAsLoss)
    {
        auto& unit = getUnitState(unitId);
        unit.markAsDeadNoCorpse();
        if (countAsLoss)
        {
            getPlayer(unit.owner).unitsLost += 1;
        }
        releaseTransportLinks(unitId);
        // No explosion or wreck, but the scene still has to hear about it so
        // it drops the unit from the selection, hover state and GUI caches.
        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, UnitDiedEvent::DeathType::Deleted});
    }

    Matrix4x<SimScalar> GameSimulation::getUnitPieceLocalTransform(UnitId unitId, const std::string& pieceName) const
    {
        const auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        const auto& modelDef = unitModelDefinitions.at(unitDefinition.objectName);
        return getPieceTransform(pieceName, modelDef, unit.pieces);
    }

    Matrix4x<SimScalar> GameSimulation::getUnitPieceTransform(UnitId unitId, const std::string& pieceName) const
    {
        const auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        const auto& modelDef = unitModelDefinitions.at(unitDefinition.objectName);
        auto pieceTransform = getPieceTransform(pieceName, modelDef, unit.pieces);
        return unit.getTransform() * pieceTransform;
    }

    SimVector GameSimulation::getUnitPiecePosition(UnitId unitId, const std::string& pieceName) const
    {
        const auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        const auto& modelDef = unitModelDefinitions.at(unitDefinition.objectName);
        auto pieceTransform = getPieceTransform(pieceName, modelDef, unit.pieces);
        return unit.getTransform() * pieceTransform * SimVector(0_ss, 0_ss, 0_ss);
    }

    void GameSimulation::setMoveOrders(UnitId unitId, UnitMovementOrders orders)
    {
        getUnitState(unitId).moveOrders = orders;
    }

    void GameSimulation::setCloakRequested(UnitId unitId, bool value)
    {
        getUnitState(unitId).cloakRequested = value;
    }

    void GameSimulation::setBuildStance(UnitId unitId, bool value)
    {
        getUnitState(unitId).inBuildStance = value;
    }

    void GameSimulation::setYardOpen(UnitId unitId, bool value)
    {
        trySetYardOpen(unitId, value);
    }

    void GameSimulation::setBuggerOff(UnitId unitId, bool value)
    {
        getUnitState(unitId).buggerOffActive = value;
        if (value)
        {
            emitBuggerOff(unitId);
        }
    }

    MovementClassDefinition GameSimulation::getAdHocMovementClass(const UnitDefinition::MovementCollisionInfo& info) const
    {
        return match(
            info,
            [&](const UnitDefinition::AdHocMovementClass& mc) {
                // An FBI names no BadSlope, so a class built from one gets
                // the original's seeded default: half the corresponding max.
                return MovementClassDefinition{
                    "",
                    mc.footprintX,
                    mc.footprintZ,
                    mc.minWaterDepth,
                    mc.maxWaterDepth,
                    mc.maxSlope,
                    mc.maxWaterSlope,
                    mc.maxSlope / 2,
                    mc.maxWaterSlope / 2};
            },
            [&](const UnitDefinition::NamedMovementClass& mc) {
                return movementClassDatabase.getMovementClass(mc.movementClassId);
            });
    }

    std::pair<unsigned int, unsigned int> GameSimulation::getFootprintXZ(const UnitDefinition::MovementCollisionInfo& info) const
    {
        return match(
            info,
            [&](const UnitDefinition::AdHocMovementClass& mc) {
                return std::make_pair(mc.footprintX, mc.footprintZ);
            },
            [&](const UnitDefinition::NamedMovementClass& mc) {
                const auto& mcDef = movementClassDatabase.getMovementClass(mc.movementClassId);
                return std::make_pair(mcDef.footprintX, mcDef.footprintZ);
            });
    }

    SimVector rotateTowards(const SimVector& v, const SimVector& target, SimScalar maxAngle)
    {
        auto normV = v.normalizedOr(SimVector(0_ss, 0_ss, 1_ss));
        auto targetDirection = target.normalizedOr(SimVector(0_ss, 0_ss, 1_ss));
        auto cross = normV.cross(targetDirection);
        auto dot = normV.dot(targetDirection);
        auto angle = rweMin(rweAcos(dot), angularToRadians(maxAngle));

        return Matrix4x<SimScalar>::rotationAxisAngle(cross, angle) * v;
    }

    bool projectileCollides(const GameSimulation& sim, const Projectile& projectile, const OccupiedCell& cellValue)
    {
        if (cellValue.mobileUnitId)
        {
            const auto& unit = sim.getUnitState(*cellValue.mobileUnitId);

            if (unit.isOwnedBy(projectile.owner))
            {
                return false;
            }

            const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
            const auto& modelDefinition = sim.unitModelDefinitions.at(unitDefinition.objectName);

            if (projectile.position.y < unit.position.y || projectile.position.y > unit.position.y + modelDefinition.height)
            {
                return false;
            }

            return true;
        }

        if (cellValue.buildingInfo && !cellValue.buildingInfo->passable)
        {
            const auto& unit = sim.getUnitState(cellValue.buildingInfo->unit);

            if (unit.isOwnedBy(projectile.owner))
            {
                return false;
            }

            const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
            const auto& modelDefinition = sim.unitModelDefinitions.at(unitDefinition.objectName);

            if (projectile.position.y < unit.position.y || projectile.position.y > unit.position.y + modelDefinition.height)
            {
                return false;
            }

            return true;
        }

        if (cellValue.featureId)
        {
            const auto& feature = sim.getFeature(*cellValue.featureId);
            const auto& featureDefinition = sim.getFeatureDefinition(feature.featureName);

            if (projectile.position.y < feature.position.y || projectile.position.y > feature.position.y + featureDefinition.height)
            {
                return false;
            }

            return true;
        }


        return false;
    }

    bool projectileCollidesWithUnit(const GameSimulation& sim, const Projectile& projectile, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);

        if (unit.isOwnedBy(projectile.owner))
        {
            return false;
        }

        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);

        auto footprintRect = sim.computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto heightMapPos = sim.terrain.worldToHeightmapCoordinate(projectile.position);

        if (!footprintRect.contains(heightMapPos))
        {
            return false;
        }

        const auto& modelDefinition = sim.unitModelDefinitions.at(unitDefinition.objectName);

        if (projectile.position.y < unit.position.y || projectile.position.y > unit.position.y + modelDefinition.height)
        {
            return false;
        }

        return true;
    }

    struct ProjectileCollisionInfoTerrain
    {
    };
    struct ProjectileCollisionInfoOutOfBounds
    {
    };
    struct ProjectileCollisionInfoSea
    {
    };
    struct ProjectileCollisionInfoUnitOrFeatureOrBuilding
    {
    };
    using ProjectileCollisionInfo = std::variant<
        ProjectileCollisionInfoOutOfBounds,
        ProjectileCollisionInfoTerrain,
        ProjectileCollisionInfoSea,
        ProjectileCollisionInfoUnitOrFeatureOrBuilding>;

    std::optional<ProjectileCollisionInfo> checkProjectileCollision(const GameSimulation& simulation, const Projectile& projectile)
    {
        auto terrainHeight = simulation.terrain.tryGetHeightAt(projectile.position.x, projectile.position.z);
        if (!terrainHeight)
        {
            return ProjectileCollisionInfoOutOfBounds();
        }

        auto seaLevel = simulation.terrain.getSeaLevel();

        // test collision with sea; torpedoes and the like live in the water
        auto weaponIt = simulation.weaponDefinitions.find(projectile.weaponType);
        bool waterWeapon = weaponIt != simulation.weaponDefinitions.end() && weaponIt->second.waterWeapon;
        if (!waterWeapon && seaLevel > *terrainHeight && projectile.position.y <= seaLevel)
        {
            return ProjectileCollisionInfoSea();
        }
        else if (projectile.position.y <= *terrainHeight)
        {
            return ProjectileCollisionInfoTerrain();
        }
        else
        {
            auto heightMapPos = simulation.terrain.worldToHeightmapCoordinate(projectile.position);
            auto cellValue = simulation.occupiedGrid.tryGet(heightMapPos);
            if (cellValue)
            {
                auto collides = projectileCollides(simulation, projectile, cellValue->get());
                if (collides)
                {
                    return ProjectileCollisionInfoUnitOrFeatureOrBuilding();
                }
            }

            for (auto unitId : simulation.flyingUnitsSet)
            {
                if (projectileCollidesWithUnit(simulation, projectile, unitId))
                {
                    return ProjectileCollisionInfoUnitOrFeatureOrBuilding();
                }
            }
        }

        return std::nullopt;
    }

    BoundingBox3x<SimScalar> GameSimulation::createBoundingBox(const UnitState& unit) const
    {
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        const auto& modelDefinition = unitModelDefinitions.at(unitDefinition.objectName);
        auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto min = SimVector(SimScalar(footprint.x), unit.position.y, SimScalar(footprint.y));
        auto max = SimVector(SimScalar(footprint.x + footprint.width), unit.position.y + modelDefinition.height, SimScalar(footprint.y + footprint.height));
        auto worldMin = terrain.heightmapToWorldSpace(min);
        auto worldMax = terrain.heightmapToWorldSpace(max);
        return BoundingBox3x<SimScalar>::fromMinMax(worldMin, worldMax);
    }

    void GameSimulation::killUnit(UnitId unitId)
    {
        killUnit(unitId, std::nullopt);
    }

    std::optional<unsigned int> readCorpseLevel(const CobThread& thread)
    {
        // Killed(severity, corpsetype): the level is the second parameter,
        // which is local 1.
        const std::vector<int>* locals = nullptr;
        if (!thread.returnLocals.empty())
        {
            locals = &thread.returnLocals;
        }
        else if (!thread.callStack.empty())
        {
            locals = &thread.callStack.top().locals;
        }

        if (locals == nullptr || locals->size() < 2)
        {
            return std::nullopt;
        }

        // The original masks to four bits (0x486D69), so a script writing
        // nonsense cannot walk the chain forever.
        auto level = static_cast<unsigned int>((*locals)[1]) & 0xFu;
        if (level == 0)
        {
            return std::nullopt;
        }

        return level;
    }

    int computeKilledSeverity(unsigned int overkill, unsigned int maxHitPoints)
    {
        auto scaled = maxHitPoints == 0 ? 0u : (100u * overkill) / maxHitPoints;
        return static_cast<int>(std::clamp(scaled / 2u, 1u, 100u));
    }

    void GameSimulation::killUnit(UnitId unitId, std::optional<UnitId> attacker)
    {
        // Nothing measured the blow: a scuttling, a transport going down with
        // its cargo, a script asking to die. The original's own severity
        // arithmetic degenerates to its floor for these, and the shipped
        // ladders read that as the intact wreck.
        killUnit(unitId, attacker, 0u);
    }

    void GameSimulation::killUnit(UnitId unitId, std::optional<UnitId> attacker, unsigned int overkill)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        unit.markAsDead();
        getPlayer(unit.owner).unitsLost += 1;
        releaseTransportLinks(unitId, attacker);

        // Credit the kill to the attacker, if any. Skip if the attacker is
        // dead or no longer exists, and never credit a unit for killing itself
        // (suicide / explodeAs).
        //
        // The veterancy count is the original's `unit+0xB8`, incremented at
        // 0x4869CA, and it is fussier than RWE used to be about what earns it.
        // Two tests stand in front of that increment and this used to fail
        // both, on a comment that claimed the opposite of what the binary does:
        //
        //   0x4869A7  the victim's build progress must be zero -- an
        //             unfinished nanoframe is worth nothing to whoever
        //             flattens it;
        //   0x4869BA  the attacker's recorded player (`victim+0xF4`) must
        //             differ from the victim's own owner (`victim+0xFF`), so
        //             **friendly fire earns no veterancy at all**. Shooting
        //             your own units was a way to farm the damage bonus of S:5
        //             and the reload bonus of S:3965; it is not.
        //
        // The player-level tallies are gated the same way, and by the same two
        // tests. The death-cause jump table at 0x486E64 is decoded now (issue
        // #51): its weapon-kill entry raises the victim owner's Losses
        // (`player+0xFE`) unconditionally, then raises the killer's Kills
        // (`player+0xFC`) at 0x486906 behind exactly the pair above -- the
        // victim must be finished and the killer must not be the victim's own
        // owner. So a player's Kills column on the end-of-game chart counts
        // neither friendly fire nor flattened nanoframes, and RWE's counted
        // both.
        std::optional<PlayerId> killerOwner;
        if (attacker && *attacker != unitId)
        {
            auto attackerUnit = tryGetUnitState(*attacker);
            if (attackerUnit && attackerUnit->get().isAlive())
            {
                auto sameSide = attackerUnit->get().owner == unit.owner;
                if (!sameSide && !unit.isBeingBuilt(unitDefinition))
                {
                    attackerUnit->get().kills += 1;
                    getPlayer(attackerUnit->get().owner).unitsKilled += 1;
                }
                killerOwner = attackerUnit->get().owner;
            }
        }

        auto deathType = unit.position.y < terrain.getSeaLevel() ? UnitDiedEvent::DeathType::WaterExploded : UnitDiedEvent::DeathType::NormalExploded;
        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, deathType, unit.owner, killerOwner});

        // The severity is `clamp(1, 100, (100*overkill/maxdamage + X) / 2)`,
        // where X is `unit+0xF7` -- the one term in this formula with no
        // known writer anywhere in the binary, so it is taken as zero.
        // What the script does with the number is its own business: a
        // shipped `Killed` is a three-band ladder that picks a corpse
        // level from it and throws a different amount of the unit about
        // on the way.
        const int severity = computeKilledSeverity(overkill, unitDefinition.maxHitPoints);

        // Run the script's Killed(severity, corpsetype) now, while the unit
        // still exists: the piece explosions it fires before its first sleep
        // become debris. Anything it does after a sleep is lost, as the unit
        // is removed at the end of the tick.
        if (unit.cobEnvironment)
        {
            auto killedThread = unit.cobEnvironment->createThread("Killed", {severity, 0});
            runUnitCobScripts(*this, unitId);

            // What the ladder decided. A thread that ran to its return has
            // its locals in returnLocals; one that stopped at a sleep still
            // has them on its call stack, and either way it is still alive
            // here -- a finished thread is not reaped until the next pass.
            if (killedThread)
            {
                if (auto level = readCorpseLevel(**killedThread))
                {
                    if (auto deadState = std::get_if<UnitState::LifeStateDead>(&unit.lifeState))
                    {
                        deadState->corpseLevel = *level;
                    }
                }
            }
        }

        // An `isfeature` unit always leaves its wreck, at level 1, whatever its
        // `Killed` ladder asked for. The original reaches this by forcing the
        // death cause to 7 -- 0x41B9FE and 0x486167 both test bit 24 of
        // `def+0x241`, which is `isfeature`, and write the cause onto the unit
        // -- and the packer then short-circuits the level at 0x486525. Cause 7
        // also clears `mayBurn` at 0x486D66; trySpawnFeature carries that
        // through as the isFeature half of WreckSpawnedEvent::mayBurn, so
        // such a wreck never gets the plume. See TOTALA-EXE-WRECKS.md, "What
        // each death cause is".
        //
        // Before the nanoframe rule below, deliberately: 0x486525 runs ahead of
        // 0x4865D2, so a half-built fort still leaves nothing.
        if (unitDefinition.isFeature)
        {
            if (auto deadState = std::get_if<UnitState::LifeStateDead>(&unit.lifeState); deadState != nullptr)
            {
                deadState->leaveCorpse = true;
                deadState->corpseLevel = 1;
            }
        }

        // Whatever the script asked for, a unit that was still a nanoframe
        // leaves nothing behind: 0x4865D2 clears the corpse flag outright when
        // the remaining build fraction is non-zero, after the `Killed` script
        // has run and before the wreck would be spawned. Shoot a half-built
        // factory and there is nothing to reclaim.
        if (unit.isBeingBuilt(unitDefinition))
        {
            if (auto deadState = std::get_if<UnitState::LifeStateDead>(&unit.lifeState); deadState != nullptr)
            {
                deadState->leaveCorpse = false;
            }
        }

        if (!unitDefinition.explodeAs.empty())
        {
            auto impactType = unit.position.y < terrain.getSeaLevel() ? ImpactType::Water : ImpactType::Normal;
            // The explodeAs projectile is environmental: deaths it causes
            // are not credited to anyone (the original unit is already dead).
            auto projectile = createProjectileFromWeapon(ProjectileSpawn{
                .owner = unit.owner,
                .weaponType = unitDefinition.explodeAs,
                .position = unit.position,
                .direction = SimVector(0_ss, -1_ss, 0_ss),
                .distanceToTarget = 0_ss,
                .targetUnit = std::nullopt,
                .attacker = std::nullopt,
            });
            doProjectileImpact(projectile, impactType);
        }

        // After the corpse rules have all had their say, so the level the
        // record carries is the one the spawner will use.
        if (demoRecorder)
        {
            demoRecorder->unitDied(
                *this,
                unitId,
                attacker,
                static_cast<unsigned int>(severity),
                demoDeathCause(*this, unitId),
                demoCorpseLevel(unit));
        }
    }

    void GameSimulation::applyDamage(UnitId unitId, unsigned int damagePoints)
    {
        applyDamage(unitId, damagePoints, std::nullopt);
    }

    namespace
    {
        /**
         * TA buckets a unit's kill count in fives and stops at the fifth tier
         * (TotalA.exe 0x489BF3 and 0x499DAE do the same division and clamp).
         * Nothing in the original ever exposes the tier directly; it only ever
         * scales damage.
         */
        int64_t veterancyTier(unsigned int kills)
        {
            return std::min<int64_t>(5, kills / 5);
        }
    }

    void GameSimulation::applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker)
    {
        applyDamage(unitId, damagePoints, attacker, false);
    }

    void GameSimulation::applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker, bool paralyzer, std::optional<PlayerId> sourceOwner)
    {
        {
            // Scored by the music evaluator; carries owners so the scene
            // does not have to chase ids that may be dead by the time it
            // drains the queue.
            std::optional<PlayerId> attackerOwner;
            if (attacker)
            {
                if (auto attackerUnit = tryGetUnitState(*attacker))
                {
                    attackerOwner = attackerUnit->get().owner;
                }
            }
            events.push_back(UnitDamagedEvent{unitId, getUnitState(unitId).owner, attackerOwner, paralyzer});
        }

        // The figure on the wire is the one that arrived, before veterancy and
        // armour scale it: the corpus's modal damage per (shooter, slot) is the
        // weapon's own [DAMAGE] default.
        if (demoRecorder)
        {
            demoRecorder->damageApplied(*this, unitId, attacker, damagePoints, sourceOwner);
        }

        if (attacker)
        {
            // The original shoots back from inside the damage message
            // handler, before the hit points come off, which is the only
            // thing that makes return fire a firing mode rather than a
            // second name for hold fire.
            returnFire(unitId, *attacker);
        }

        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        int64_t damage = damagePoints;

        // The whole of the original's damage arithmetic happens here, in the
        // order TotalA.exe does it. First the attacker's veterancy bonus
        // (0x499DAE, +6% per tier), which is applied in the weapon path and so
        // only when we know who fired.
        if (attacker)
        {
            if (auto attackerUnit = tryGetUnitState(*attacker); attackerUnit)
            {
                damage = (damage * (100 + 6 * veterancyTier(attackerUnit->get().kills))) / 100;
            }
        }

        // Then armour (0x489BC3). A unit only counts as armoured while its
        // script says so, and a big enough hit ignores armour outright: that
        // threshold is why the D-gun goes through a closed solar collector.
        if (unit.armored && damage < ArmourBypassDamage)
        {
            damage = (damage * unitDefinition.damageModifier) >> 16;
        }

        // Then the victim's own veterancy (0x489BF3, -4% per tier), which
        // applies to every kind of damage, weapon or not.
        damage = (damage * (100 - 4 * veterancyTier(unit.kills))) / 100;

        damagePoints = static_cast<unsigned int>(std::max<int64_t>(0, damage));

        // A paralyzer hit never takes hit points off anything: 0x489DEB sends
        // damage type 2 down its own path and returns before reaching the
        // subtraction at 0x489EB1. What the number buys is time, in ticks --
        // which is why the EMP missile's 1800 against a Krogoth is a minute of
        // paralysis rather than a kill.
        if (paralyzer)
        {
            if (unitDefinition.immuneToParalyzer || damagePoints == 0)
            {
                return;
            }

            // Hits stack: the original adds the new duration to what is left
            // (0x489E69) and the order handler then clamps the total to 1800
            // ticks, sixty seconds (0x402D33).
            auto remaining = unit.paralyzedUntil && *unit.paralyzedUntil > gameTime
                ? unit.paralyzedUntil->value - gameTime.value
                : 0u;
            remaining = std::min(remaining + damagePoints, MaxParalysisTicks);
            unit.paralyzedUntil = gameTime + GameTime(remaining);

            // Entering the stun drops every weapon's aim (0x402D46-0x402D5F).
            unit.clearWeaponTargets();
            return;
        }

        if (unit.hitPoints <= damagePoints)
        {
            recordUnitDeath(*this, unitId, "weapon", attacker);
            if (unit.isBeingBuilt(unitDefinition))
            {
                // Units that are still under construction
                // die quietly without a corpse.
                // FIXME: units in TA that are not actively receiving build input
                // die with an explosion, even though they leave no corpse.
                //
                // This is still death cause 1, an ordinary weapon kill, so the
                // owner takes the loss -- and the attacker is credited nothing,
                // which used to be an accident of the code path and is now the
                // rule: both the veterancy counter and the player's Kills sit
                // behind the build-progress test at 0x4869A7. See §5.
                quietlyKillUnit(unitId);
                if (demoRecorder)
                {
                    // A nanoframe runs no Killed script, so it leaves no corpse
                    // and has no severity: cause 1 with both nibbles empty,
                    // which is what the corpus's cause-9 and cause-5 deaths
                    // read too.
                    demoRecorder->unitDied(*this, unitId, attacker, 0, 1, 0);
                }
            }
            else
            {
                // What the blow had left over once the unit's remaining hit
                // points were paid for. The original works the severity out
                // of exactly this.
                killUnit(unitId, attacker, damagePoints - unit.hitPoints);
            }
        }
        else
        {
            unit.hitPoints -= damagePoints;
        }
    }

    namespace
    {
        /**
         * A blast too small to be worth spreading. The original checks
         * AreaOfEffect against sixteen (0x49A049) and, under that, skips the
         * falloff entirely for a flat 1.0 (0x49A05B). Our radius is already
         * half the AreaOfEffect, so the same test is eight.
         *
         * This is not a rounding detail. Forty-three of the hundred and
         * thirty-six shipped weapons are inside it, including the twenty-eight
         * at AreaOfEffect eight -- the machine guns and light lasers that do
         * most of the shooting in a game -- and under the quadratic curve a
         * hit landing six units off centre would otherwise come out at a
         * sixteenth strength.
         */
        const SimScalar SmallestSpreadingBlastRadius = 8_ss;

        /**
         * The original's blast falloff (TotalA.exe 0x49A3B6). The curve is
         * quadratic rather than linear, and it lands on EdgeEffectiveness at
         * the rim instead of on zero, so a weapon that names an edge value
         * keeps most of its bite right out to the edge of the blast while an
         * ordinary one drops away far faster than a straight line would.
         */
        SimScalar blastDamageScale(SimScalar distance, SimScalar radius, SimScalar edgeEffectiveness)
        {
            if (radius <= SmallestSpreadingBlastRadius)
            {
                return 1_ss;
            }

            auto t = std::clamp(1_ss - (distance / radius), 0_ss, 1_ss);
            return (t * t * (1_ss - edgeEffectiveness)) + edgeEffectiveness;
        }
    }

    void GameSimulation::applyDamageInRadius(const SimVector& position, SimScalar radius, const Projectile& projectile)
    {
        auto minX = position.x - radius;
        auto maxX = position.x + radius;
        auto minZ = position.z - radius;
        auto maxZ = position.z + radius;

        auto minPoint = terrain.worldToHeightmapCoordinate(SimVector(minX, position.y, minZ));
        auto maxPoint = terrain.worldToHeightmapCoordinate(SimVector(maxX, position.y, maxZ));
        auto minCell = occupiedGrid.clampToCoords(minPoint);
        auto maxCell = occupiedGrid.clampToCoords(maxPoint);

        assert(minCell.x <= maxCell.x);
        assert(minCell.y <= maxCell.y);

        auto radiusSquared = radius * radius;

        // Whether this shot stuns instead of hurting. The original reads the
        // flag off the weapon definition at the moment it applies the hit
        // (0x499E20) and turns it into damage type 2.
        auto weaponIt = weaponDefinitions.find(projectile.weaponType);
        auto paralyzer = weaponIt != weaponDefinitions.end() && weaponIt->second.paralyzer;

        // Exactly one unit walks away from a blast for free, and it is the one
        // that fired it (0x49A259 compares each candidate against the
        // projectile's stored firing unit and skips on a match). There is no
        // allegiance test anywhere else on this path: the blast hurts allies
        // and teammates at full strength, and the original merely books the
        // result into two separate tallies by owner, which would be pointless
        // if own-damage did not happen. So friendly fire is the rule and the
        // firer is the single exception.
        //
        // It is not a nicety for the D-gun, it is what makes the weapon usable
        // at all: a round that goes off without being consumed detonates from
        // the muzzle outwards, and the first of those blasts is standing on the
        // commander.
        //
        // A blast with no firer -- a dying unit's explodeAs, a feature going up
        // -- exempts nobody, which is why a commander that disintegrates
        // something at arm's length can still be hurt by what it killed.
        auto firer = projectile.attacker;

        std::unordered_set<UnitId> seenUnits;
        std::unordered_set<FeatureId> seenFeatures;

        // Blasts hurt features too: force-attacking a wreck field to clear a
        // lane is a standing part of play, and controlling one is an economy
        // in itself. A feature's own `damage` key is its hit points; a feature
        // blown to nothing breaks down to its featureDead form the way a
        // burnt one does. `damagesFeatures` is a mod's switch to exempt a
        // weapon; the shipped data sets it on nothing.
        auto damagesFeatures = weaponIt == weaponDefinitions.end() || weaponIt->second.damagesFeatures;

        auto region = GridRegion::fromCoordinates(minCell, maxCell);

        region.forEach([&](const auto& coords) {
          auto cellCenter = terrain.heightmapIndexToWorldCenter(coords.x, coords.y);
          Rectangle2x<SimScalar> cellRectangle(
              Vector2x<SimScalar>(cellCenter.x, cellCenter.z),
              Vector2x<SimScalar>(MapTerrain::HeightTileWidthInWorldUnits / 2_ss, MapTerrain::HeightTileHeightInWorldUnits / 2_ss));
          auto cellDistanceSquared = cellRectangle.distanceSquared(Vector2x<SimScalar>(position.x, position.z));
          if (cellDistanceSquared > radiusSquared)
          {
              return;
          }

          if (damagesFeatures && !paralyzer)
          {
              const auto& cell = occupiedGrid.get(coords);
              if (cell.featureId && seenFeatures.find(*cell.featureId) == seenFeatures.end())
              {
                  seenFeatures.insert(*cell.featureId);
                  if (auto featureRef = tryGetFeature(*cell.featureId))
                  {
                      auto& feature = featureRef->get();
                      const auto& featureDefinition = getFeatureDefinition(feature.featureName);
                      // The original's blast-on-feature routine (0x4244B0,
                      // TOTALA-EXE.md §24) asks one thing of the feature:
                      // that it is not `indestructible`. Blocking, reclaimable
                      // and the rest never enter into it, so a scar decal or
                      // a bush takes the hit like a wreck does. It adds the
                      // weapon's default damage, unscaled by distance, to what
                      // the feature has already taken, and breaks the feature
                      // to its featuredead form the moment that reaches the
                      // definition's `damage` -- which for a feature that
                      // omits the key is zero, so the first hit takes it.
                      if (!featureDefinition.indestructible)
                      {
                          auto damage = projectile.getDamage(std::string());
                          if (damage >= feature.hitPoints)
                          {
                              // What stands in its place lands on the cell
                              // just walked and is not met again by this
                              // blast, as in the original's per-cell walk;
                              // without that a heap that names no damage
                              // would go in the same shell that made it.
                              if (auto replacement = replaceFeature(*cell.featureId, featureDefinition.featureDead))
                              {
                                  seenFeatures.insert(*replacement);
                              }
                          }
                          else
                          {
                              feature.hitPoints -= damage;
                          }
                      }
                  }
              }
          }

          auto occupiedType = occupiedGrid.get(coords);

          auto u = occupiedType.mobileUnitId;
          if (!u && occupiedType.buildingInfo && !occupiedType.buildingInfo->passable)
          {
              u = occupiedType.buildingInfo->unit;
          }
          if (!u)
          {
              return;
          }

          auto pair = seenUnits.insert(*u);
          if (!pair.second)
          {
              return;
          }

          // the firer is exempt from its own blast (0x49A259)
          if (firer && *firer == *u)
          {
              return;
          }

          const auto& unit = getUnitState(*u);

          if (unit.isDead())
          {
              return;
          }

          auto unitDistanceSquared = createBoundingBox(unit).distanceSquared(position);
          if (unitDistanceSquared > radiusSquared)
          {
              return;
          }

          auto damageScale = blastDamageScale(rweSqrt(unitDistanceSquared), radius, projectile.edgeEffectiveness);
          auto rawDamage = projectile.getDamage(unit.unitType);
          auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * damageScale);
          applyDamage(*u, scaledDamage, projectile.attacker, paralyzer, projectile.owner); });

        for (const auto& flyingUnitId : flyingUnitsSet)
        {
            const auto& unit = getUnitState(flyingUnitId);

            if (!unit.isAlive())
            {
                continue;
            }

            // the firer is exempt from its own blast (0x49A259)
            if (firer && *firer == flyingUnitId)
            {
                continue;
            }

            auto unitDistanceSquared = createBoundingBox(unit).distanceSquared(position);
            if (unitDistanceSquared > radiusSquared)
            {
                continue;
            }

            auto damageScale = blastDamageScale(rweSqrt(unitDistanceSquared), radius, projectile.edgeEffectiveness);
            auto rawDamage = projectile.getDamage(unit.unitType);
            auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * damageScale);
            applyDamage(flyingUnitId, scaledDamage, projectile.attacker, paralyzer, projectile.owner);
        }
    }

    void GameSimulation::doProjectileImpact(const Projectile& projectile, ImpactType /*impactType*/, std::optional<ProjectileId> projectileId)
    {
        applyDamageInRadius(projectile.position, projectile.damageRadius, projectile);

        if (auto it = weaponDefinitions.find(projectile.weaponType); it != weaponDefinitions.end() && it->second.fireStarter > 0)
        {
            tryIgniteFeaturesInRadius(projectile.position, std::max(projectile.damageRadius, 16_ss), it->second.fireStarter);
        }

        // The piece that actually kills a nuke. An interceptor's warhead is not
        // aimed at the missile so much as detonated near it, and what does the
        // damage is that the blast takes every projectile inside it with it
        // (0x49A664) -- which is also why one anti-nuke can clear a salvo.
        if (auto it = weaponDefinitions.find(projectile.weaponType); it != weaponDefinitions.end() && it->second.interceptor)
        {
            detonateProjectilesInBlast(projectileId, projectile.position, projectile.damageRadius);
        }
    }

    std::optional<SimVector> GameSimulation::getSelfPropelledAimPoint(const Projectile& projectile, const ProjectilePhysicsTypeSelfPropelled& p)
    {
        // A cruise missile ignores whatever it was fired at until it is nearly
        // there and flies at its cruise altitude over the aim point instead,
        // which is what makes it come in flat and then drop (0x49B455).
        if (p.cruise && projectile.targetPosition)
        {
            if (projectile.position.distanceSquared(*projectile.targetPosition) > CruiseHandoverDistance * CruiseHandoverDistance)
            {
                return SimVector(projectile.targetPosition->x, CruiseAltitude, projectile.targetPosition->z);
            }
        }

        // A target projectile comes ahead of a unit target (0x49B47A returns
        // `target + 4`, the missile's live position, before it looks at
        // anything else). This is the case section 7 recorded as decoded but
        // unfillable, because nothing could set the slot yet.
        if (projectile.targetProjectile)
        {
            if (auto target = projectiles.tryGet(*projectile.targetProjectile); target && !target->get().isDead)
            {
                return target->get().position;
            }
        }

        if (projectile.targetUnit)
        {
            if (auto targetUnit = tryGetUnitState(*projectile.targetUnit); targetUnit)
            {
                return targetUnit->get().position;
            }
        }

        return projectile.targetPosition;
    }

    bool GameSimulation::updateSelfPropelledProjectile(Projectile& projectile, const ProjectilePhysicsTypeSelfPropelled& p)
    {
        if (projectile.motorOutFrame && *projectile.motorOutFrame <= gameTime)
        {
            if (p.burnBlow)
            {
                // Torpedoes and depth charges go off at the end of their run
                // rather than sinking to the seabed (0x49BAC3).
                return false;
            }

            // TA takes one tick of gravity on the changeover without rebuilding
            // the velocity from the attitude, so a vertical launch is still
            // climbing on the tick it turns over.
            projectile.velocity.y -= 112_ss / (30_ss * 30_ss);

            if (p.twoPhase && !projectile.secondPhase)
            {
                projectile.secondPhase = true;
                projectile.motorOutFrame = gameTime + p.flightTime;
                if (!p.tracks)
                {
                    // A missile that only has guidance, not tracking, finishes at
                    // the place it was aimed at and stops caring who was standing
                    // there (0x49BB34).
                    projectile.targetUnit = std::nullopt;
                }
            }
            else
            {
                projectile.motorOut = true;
            }
            return true;
        }

        if (projectile.speed < p.maxVelocity)
        {
            projectile.speed = rweMin(projectile.speed + p.acceleration, p.maxVelocity);
        }

        // The launch phase of a two-phase missile is flown blind whatever
        // `guidance` says; the second phase steers whether it says so or not.
        if (p.twoPhase ? projectile.secondPhase : p.guidance)
        {
            if (auto aimPoint = getSelfPropelledAimPoint(projectile, p); aimPoint)
            {
                auto toTarget = *aimPoint - projectile.position;
                auto flat = SimVector(toTarget.x, 0_ss, toTarget.z);
                auto wantHeading = UnitState::toRotation(flat);
                auto wantPitch = atan2(toTarget.y, flat.length());

                // A `burnblow` weapon that has let the target get round behind
                // it gives up and detonates rather than turning after it.
                auto lostTheTarget = angleBetween(projectile.heading, wantHeading).value > BurnBlowAbortAngle.value
                    || angleBetween(projectile.pitch, wantPitch).value > BurnBlowAbortAngle.value;
                if (p.burnBlow && lostTheTarget)
                {
                    return false;
                }

                projectile.heading = turnTowards(projectile.heading, wantHeading, p.turnRate);
                projectile.pitch = turnTowards(projectile.pitch, wantPitch, p.turnRate);
            }
        }

        projectile.velocity = toMissileDirection(projectile.heading, projectile.pitch) * projectile.speed;
        return true;
    }

    void GameSimulation::updateProjectiles()
    {
        for (auto& projectileEntry : projectiles)
        {
            const auto& id = projectileEntry.first;
            auto& projectile = projectileEntry.second;

            const auto& weaponDefinition = weaponDefinitions.at(projectile.weaponType);

            if (projectile.dieOnFrame && *projectile.dieOnFrame <= gameTime)
            {
                projectile.isDead = true;
                events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position});
                continue;
            }

            bool detonated = false;
            match(
                weaponDefinition.physicsType,
                [&](const ProjectilePhysicsTypeBallistic&) {
                    // Wind goes onto the position and gravity onto the
                    // velocity, which is the shape of 0x49BD10: the wind is a
                    // displacement the shell never accumulates, so a long
                    // flight drifts linearly rather than curving away.
                    projectile.position += currentWindVector;
                    projectile.velocity.y -= 112_ss / (30_ss * 30_ss);
                },
                [&](const ProjectilePhysicsTypeBomb&) {
                    // Bombs follow the same gravity model as ballistic
                    // projectiles. Their initial velocity is inherited from
                    // the aircraft at release time; gravity does the rest.
                    // They take the wind from that same branch of 0x49BD10,
                    // which is why a bomber's aim is now slightly off downwind
                    // -- the release trigger (bombReleaseTrigger) does not
                    // model the wind, and neither does the original's.
                    projectile.position += currentWindVector;
                    projectile.velocity.y -= 112_ss / (30_ss * 30_ss);
                },
                [&](const ProjectilePhysicsTypeLineOfSight&) {

                },
                [&](const ProjectilePhysicsTypeTracking& t) {
                    if (!projectile.targetUnit)
                    {
                        return;
                    }
                    auto targetUnit = tryGetUnitState(*projectile.targetUnit);
                    if (!targetUnit)
                    {
                        projectile.targetUnit = std::nullopt;
                        return;
                    }
                    auto vectorToTarget = (targetUnit->get().position - projectile.position);
                    projectile.velocity = rotateTowards(projectile.velocity, vectorToTarget, t.turnRate);
                },
                [&](const ProjectilePhysicsTypeSelfPropelled& p) {
                    detonated = !updateSelfPropelledProjectile(projectile, p);
                });

            if (detonated)
            {
                doProjectileImpact(projectile, ImpactType::Normal);
                projectile.isDead = true;
                events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::NormalImpact});
                continue;
            }

            projectile.previousPosition = projectile.position;
            projectile.position += projectile.velocity;

            // The interceptor's fuse. Its collision check carries one extra
            // clause (0x49B106): if it is inside its own areaofeffect of the
            // missile it is chasing it goes off there and then, rather than
            // waiting to run into something. Strictly inside -- the original's
            // `jge` at 0x49B1A4 skips at exactly the radius.
            if (projectile.targetProjectile)
            {
                auto target = projectiles.tryGet(*projectile.targetProjectile);
                if (target && !target->get().isDead
                    && projectile.position.distanceSquared(target->get().position) < projectile.damageRadius * projectile.damageRadius)
                {
                    projectile.isDead = true;
                    doProjectileImpact(projectile, ImpactType::Normal, id);
                    events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::NormalImpact});
                    continue;
                }
            }

            // `noexplode` does not stop the round going off; it stops the
            // detonation consuming it. The detonation routine's first act is
            // to mark the projectile dead (0x499EDE) and this flag skips that
            // one line, so the round keeps its position and its velocity and
            // is back here next tick to test the next cell along. Ploughing
            // into a hillside it therefore goes off once a tick until its life
            // runs out -- the trail a disintegrator leaves is up to thirty-six
            // separate full-strength blasts, not one.
            //
            // Out of bounds is not one of them: that path never reaches the
            // detonation routine, so a round that leaves the map is gone.
            auto noExplode = weaponDefinition.noExplode;

            auto collisionInfo = checkProjectileCollision(*this, projectile);
            if (collisionInfo)
            {
                match(
                    *collisionInfo,
                    [&](const ProjectileCollisionInfoOutOfBounds&) {
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::OutOfBounds});
                    },
                    [&](const ProjectileCollisionInfoSea&) {
                        doProjectileImpact(projectile, ImpactType::Water);
                        if (noExplode)
                        {
                            events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, true});
                            return;
                        }
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::WaterImpact});
                    },
                    [&](const ProjectileCollisionInfoTerrain&) {
                        if (projectile.groundBounce)
                        {
                            projectile.velocity.y = 0_ss;
                            projectile.position.y = projectile.previousPosition.y;
                        }
                        else
                        {
                            doProjectileImpact(projectile, ImpactType::Normal);
                            if (noExplode)
                            {
                                events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, false});
                                return;
                            }
                            projectile.isDead = true;
                            events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::NormalImpact});
                        }
                    },
                    [&](const ProjectileCollisionInfoUnitOrFeatureOrBuilding&) {
                        doProjectileImpact(projectile, ImpactType::Normal);
                        if (noExplode)
                        {
                            events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, false});
                            return;
                        }
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::NormalImpact});
                    });
            }
        }
    }

    void GameSimulation::killPlayer(PlayerId playerId)
    {
        getPlayer(playerId).status = GamePlayerStatus::Dead;
        for (auto& p : units)
        {
            auto& unit = p.second;
            if (unit.isDead())
            {
                continue;
            }

            if (!unit.isOwnedBy(playerId))
            {
                continue;
            }

            recordUnitDeath(*this, p.first, "self_destruct", std::nullopt);
            killUnit(p.first);
        }
    }

    void GameSimulation::processVictoryCondition()
    {
        if (commanderDeathMode == CommanderDeathMode::GameEnds)
        {
            for (const auto& p : units)
            {
                const auto& unitDefinition = unitDefinitions.at(p.second.unitType);
                if (unitDefinition.commander && p.second.isDead())
                {
                    killPlayer(p.second.owner);
                }
            }

            return;
        }

        // Commander Dies: Game Continues. The commander is an ordinary unit --
        // it still dies and still explodes, it simply no longer takes the
        // player with it -- so a player is out only once nothing of theirs is
        // left standing at all. Buildings are units here, so "no live units"
        // already means "no units and no buildings".
        //
        // The test is "had one, has none" rather than plainly "has none",
        // and that is deliberate. This runs before deleteDeadUnits, so a unit
        // killed this tick is still in the map and still owned; requiring one
        // means a player who has not been given a unit yet cannot be
        // eliminated before it arrives, which is otherwise exactly what
        // happens on the battle harness's first tick.
        std::vector<bool> hasLiving(players.size(), false);
        std::vector<bool> lostOneThisTick(players.size(), false);
        for (const auto& p : units)
        {
            auto owner = p.second.owner.value;
            if (owner >= players.size())
            {
                continue;
            }

            if (p.second.isDead())
            {
                lostOneThisTick[owner] = true;
            }
            else
            {
                hasLiving[owner] = true;
            }
        }

        for (Index i = 0; i < getSize(players); ++i)
        {
            if (players[i].status != GamePlayerStatus::Alive)
            {
                continue;
            }

            if (!hasLiving[i] && lostOneThisTick[i])
            {
                killPlayer(PlayerId(i));
            }
        }
    }

    void GameSimulation::updateWind()
    {
        if (gameTime >= nextWindSpeedChange)
        {
            // the wind speed will last between 5 and 14 seconds before changing
            nextWindSpeedChange = gameTime + GameTime(randomBetween(rng, 5, 14) * SimTicksPerSecond);

            auto currentWindSpeed = randomBetween(rng, minWindSpeed, std::max(minWindSpeed, maxWindSpeed));

            auto currentWindDirection = SimAngle(static_cast<uint16_t>(randomBetween(rng, MinAngle.value, MaxAngle.value)));

            // A generator gets the wind as a fraction of the speed the game
            // considers a full gale, and no more than all of it however hard
            // the map says the wind blows (TotalA.exe 0x490D5E).
            currentWindGenerationFactor = rweMin(1_ss, SimScalar(currentWindSpeed) / SimScalar(MaxUtilizableWindSpeed));

            // The same draw also aims the wind that pushes shells about. The
            // speed and direction above are locals, so this vector is the only
            // thing that outlives the change.
            currentWindVector = computeWindVector(currentWindDirection, currentWindSpeed);

            UnitBehaviorService(this).updateWind(currentWindGenerationFactor, currentWindDirection);
        }
    }

    void GameSimulation::updateResources()
    {
        // run resource updates once per second
        if (gameTime % GameTime(SimTicksPerSecond) == GameTime(0))
        {
            for (auto& player : players)
            {
                player.maxEnergy = Energy(0);
                player.maxMetal = Metal(0);
            }

            for (auto& entry : units)
            {
                auto& unit = entry.second;
                const auto& unitDefinition = unitDefinitions.at(unit.unitType);
                if (!unit.isBeingBuilt(unitDefinition))
                {
                    auto& playerInfo = getPlayer(unit.owner);
                    if (unitDefinition.commander)
                    {
                        playerInfo.maxMetal += playerInfo.startingMetal;
                        playerInfo.maxEnergy += playerInfo.startingEnergy;
                    }
                    else
                    {
                        playerInfo.maxMetal += unitDefinition.metalStorage;
                        playerInfo.maxEnergy += unitDefinition.energyStorage;
                    }
                }
            }

            // The make-and-use pass comes before the settle, so a generator's
            // output is available to the same second that pays for the work
            // beside it. The order inside a unit matters too: the original
            // decides whether the unit is powered from its energy draw and then
            // gates the metal it makes on that same answer, rather than on last
            // second's.
            for (auto& entry : units)
            {
                const auto& unitId = entry.first;
                auto& unit = entry.second;
                const auto& unitDefinition = unitDefinitions.at(unit.unitType);

                if (unit.activated)
                {
                    // The settle sweep's gate reads the unit's energy owed and
                    // never its metal (0x4013F9, 0x40164F), unlike the request
                    // routine a builder goes through (0x4011C0), which tests
                    // both. So a unit that owes metal but has the energy stays
                    // powered. TOTALA-EXE.md section 111.
                    unit.isSufficientlyPowered = addResourceDelta(unitId, -unitDefinition.energyUse, -unitDefinition.metalUse, ResourceDebtGate::EnergyOnly);

                    if (unit.isSufficientlyPowered)
                    {
                        if (unitDefinition.extractsMetal != Metal(0))
                        {
                            auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
                            auto metalValue = metalGrid.accumulate(metalGrid.clipRegion(footprint), 0u, std::plus<>());
                            addResourceDelta(unitId, Energy(0), Metal(metalValue * unitDefinition.extractsMetal.value));
                        }

                        if (unitDefinition.makesMetal != Metal(0))
                        {
                            addResourceDelta(unitId, Energy(0), unitDefinition.makesMetal);
                        }
                    }

                    // Wind and tidal are not gated on being powered: they are
                    // what makes the power.
                    if (unitDefinition.windGenerator != Energy(0))
                    {
                        addResourceDelta(unitId, unitDefinition.windGenerator * currentWindGenerationFactor, Metal(0));
                    }

                    if (unitDefinition.tidalGenerator != Energy(0))
                    {
                        addResourceDelta(unitId, unitDefinition.tidalGenerator * SimScalar(static_cast<float>(tidalStrength)), Metal(0));
                    }
                }

                // Cloak is paid for out of the same second's energy as
                // everything else. It is all or nothing: the original compares
                // the whole cost against the stockpile and, when it does not
                // cover it, takes nothing and leaves the unit visible rather
                // than running the player into a stall.
                if (unitDefinition.cloakable && unit.cloakRequested && !unit.isBeingBuilt(unitDefinition) && gameTime >= unit.cloakSuppressedUntil)
                {
                    // Moving costs the other number. The original reads the
                    // unit's move-rate band, which is zero exactly when it is
                    // standing still, so this is the same test the COB
                    // StartMoving and StopMoving callbacks use.
                    auto moving = !areCloserThan(unit.previousPosition, unit.position, 0.1_ssf);
                    // Truncated to a whole number first, which is what the
                    // original compares and takes (0x4017CB, 0x40182F): a
                    // cost of 200.7 against a stock of 200.5 cloaks and costs
                    // 200.
                    auto cost = Energy(std::trunc((moving ? unitDefinition.cloakCostMoving : unitDefinition.cloakCost).value));

                    // Cloak does not go through the ordinary request-and-settle
                    // path, which pays every consumer a share of whatever there
                    // is and carries the rest as debt. The original checks the
                    // whole cost against the stock on the spot and, when it
                    // will not cover it, takes nothing and leaves the unit
                    // visible. Half a cloak is not a thing, and a unit that
                    // cannot afford one should not be dragging the player into
                    // debt for it either; nor does owing for something else
                    // stop a unit that can pay from cloaking.
                    unit.cloaked = chargeStockpile(unitId, cost, Metal(0));
                }
                else
                {
                    unit.cloaked = false;
                }
                if (!unit.isBeingBuilt(unitDefinition))
                {
                    addResourceDelta(unitId, unitDefinition.energyMake, unitDefinition.metalMake);
                }
            }

            // Now settle. Everything asked for this second is pooled, and one
            // fraction per resource decides what share of it every consumer
            // gets, so a shortfall slows the whole player down evenly instead of
            // starving whoever happens to be last in the list. Debt already owed
            // is paid before anything new.
            for (Index i = 0; i < getSize(players); ++i)
            {
                auto& player = players[i];
                // Brutal computer players get a little extra for every unit of income.
                auto bonus = resourceBonusFor(PlayerId(i));

                // Pool the player's own block with every unit it owns, in that
                // order, which is the order the original adds them up in. The
                // sum sizes the fractions; each unit rolls its own debt against
                // them afterwards.
                auto metalDebt = player.metalDebt;
                auto energyDebt = player.energyDebt;
                auto metalRequested = player.metalRequestBuffer;
                auto energyRequested = player.energyRequestBuffer;
                for (const auto& entry : units)
                {
                    const auto& unit = entry.second;
                    if (!unit.isOwnedBy(PlayerId(i)))
                    {
                        continue;
                    }
                    metalDebt += unit.metalDebt;
                    energyDebt += unit.energyDebt;
                    metalRequested += unit.metalRequestBuffer;
                    energyRequested += unit.energyRequestBuffer;
                }

                ResourceAccount metalAccount{
                    player.metal.value,
                    player.metalProductionBuffer.value,
                    player.metalProduced.value,
                    player.metalExcess.value,
                    player.maxMetal.value,
                    player.metalDebt.value,
                    player.metalRequestBuffer.value,
                    metalDebt.value,
                    metalRequested.value,
                };
                ResourceAccount energyAccount{
                    player.energy.value,
                    player.energyProductionBuffer.value,
                    player.energyProduced.value,
                    player.energyExcess.value,
                    player.maxEnergy.value,
                    player.energyDebt.value,
                    player.energyRequestBuffer.value,
                    energyDebt.value,
                    energyRequested.value,
                };

                auto metalSettlement = settleResourceAccount(metalAccount, bonus);
                auto energySettlement = settleResourceAccount(energyAccount, bonus);

                player.metal = Metal(metalSettlement.stockpile);
                player.energy = Energy(energySettlement.stockpile);
                player.metalStalled = metalSettlement.stalled;
                player.energyStalled = energySettlement.stalled;

                // The chart's "produced" and "excess" columns: income counted
                // as it arrives and after the difficulty bonus, and what the
                // cap threw away at the end of the second.
                player.metalProduced = Metal(metalSettlement.lifetimeProduced);
                player.energyProduced = Energy(energySettlement.lifetimeProduced);
                player.metalExcess = Metal(metalSettlement.lifetimeExcess);
                player.energyExcess = Energy(energySettlement.lifetimeExcess);

                for (auto& entry : units)
                {
                    auto& unit = entry.second;
                    if (!unit.isOwnedBy(PlayerId(i)))
                    {
                        continue;
                    }
                    unit.settleResources(
                        energySettlement.requestFraction,
                        energySettlement.debtFraction,
                        metalSettlement.requestFraction,
                        metalSettlement.debtFraction);
                }

                player.metalDebt = Metal(metalSettlement.debt);
                player.energyDebt = Energy(energySettlement.debt);
                player.metalRequestBuffer = Metal(0);
                player.energyRequestBuffer = Energy(0);

                // The display copies are this second's income after the
                // handicap, which is the figure the chart was given.
                player.previousMetalProductionBuffer = Metal(metalSettlement.produced);
                player.previousEnergyProductionBuffer = Energy(energySettlement.produced);
                player.metalProductionBuffer = Metal(0);
                player.energyProductionBuffer = Energy(0);

                player.previousDesiredMetalConsumptionBuffer = player.desiredMetalConsumptionBuffer;
                player.previousDesiredEnergyConsumptionBuffer = player.desiredEnergyConsumptionBuffer;
                player.desiredMetalConsumptionBuffer = Metal(0);
                player.desiredEnergyConsumptionBuffer = Energy(0);
            }
        }
    }

    struct CorpseSpawnInfo
    {
        std::string featureName;
        SimVector position;
        SimAngle rotation;
        /** The dead unit's `isfeature`: its wreck stays where it fell. */
        bool isFeature;
    };

    /**
     * A wreck sinks at a fixed 0.175 world units a tick -- 5.25 a second --
     * rather than accelerating: 0x486416 writes the constant -11468 in 16.16
     * straight into the feature's velocity, and 0x42428C re-asserts it every
     * tick the wreck is under the surface. Deep water on the shipped naval
     * maps is 55 to 85 units, so an open-ocean wreck takes ten to sixteen
     * seconds to reach the bottom; in shallows it is down in under half a
     * second.
     */
    static const SimScalar WreckSinkSpeed(0.174988f);

    void GameSimulation::trySpawnFeature(const std::string& featureType, const SimVector& position, SimAngle rotation, bool isFeature)
    {
        auto featureId = tryGetFeatureDefinitionId(featureType);
        if (!featureId)
        {
            // A unit whose corpse feature is missing from the game data simply
            // leaves no wreck; that is not worth crashing over.
            return;
        }
        auto feature = MapFeature{*featureId, position, rotation};

        // The wreck is placed at the height the unit died at, on land and at
        // sea alike -- there is no clamp anywhere in the original's corpse
        // spawner. What decides whether it sinks is the ground BENEATH it,
        // not its own height, and the test is against the terrain rather than
        // the water's surface, so ground exactly at sea level counts as wet
        // (0x4863E9 jumps on greater-than only).
        //
        // A unit flagged `isfeature` is exempt. Six units set it; the two that
        // matter are the floating dragon's teeth, whose wreck is meant to stay
        // on the surface -- the original carving out exactly the one thing
        // built to float is the clearest evidence the sink rule is real.
        auto ground = terrain.getHeightAt(position.x, position.z);
        auto wet = ground <= terrain.getSeaLevel() && !isFeature;
        if (wet)
        {
            feature.velocity = SimVector(0_ss, -WreckSinkSpeed, 0_ss);
        }

        addFeature(std::move(feature));

        // The same test decides the plume: the wet branch zeroes mayBurn
        // (0x486420) and cause 7 clears it for an isfeature unit, and the
        // scene lights the thirty-second plume off whatever is left. The
        // original fires it even when the placement itself was refused (the
        // flag is cleared inside the feature != null test), which is why this
        // does not wait on addFeature succeeding.
        events.push_back(WreckSpawnedEvent{position, !wet && !isFeature});
    }

    void GameSimulation::updateFallingFeatures()
    {
        // Only wreckage dropped into water is ever moving, so the common case
        // is one comparison per feature. The original retires a feature from
        // its active list the moment the velocity reaches zero and never looks
        // at it again (0x42421A); this is the same, without the list.
        for (auto& [featureId, feature] : features)
        {
            if (feature.velocity == SimVector(0_ss, 0_ss, 0_ss))
            {
                continue;
            }

            feature.position += feature.velocity;

            auto ground = terrain.getHeightAt(feature.position.x, feature.position.z);
            if (feature.position.y <= ground)
            {
                // Landed. 0x424262.
                feature.position.y = ground;
                feature.velocity = SimVector(0_ss, 0_ss, 0_ss);
            }
            else if (feature.position.y < terrain.getSeaLevel())
            {
                // Under the surface: a terminal speed re-asserted every tick,
                // with any sideways drift killed. 0x42428C-0x424299.
                feature.velocity = SimVector(0_ss, -WreckSinkSpeed, 0_ss);
            }
            else
            {
                // Still in the air above the water: the map's gravity.
                // 0x4242A5.
                feature.velocity.y -= 112_ss / (30_ss * 30_ss);
            }
        }
    }

    void GameSimulation::deleteDeadUnits()
    {
        std::vector<CorpseSpawnInfo> corpsesToSpawn;

        for (auto it = units.begin(); it != units.end();)
        {
            const auto& unit = it->second;
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            auto deadState = std::get_if<UnitState::LifeStateDead>(&unit.lifeState);
            if (deadState == nullptr)
            {
                ++it;
                continue;
            }

            if (deadState->leaveCorpse && !unitDefinition.corpse.empty())
            {
                // Walk one step down the featuredead chain for each level
                // above the first (0x4863A7). Running off the end is not an
                // error: it is how a hard enough death leaves nothing.
                auto corpse = tryGetFeatureDefinitionId(unitDefinition.corpse);
                for (unsigned int level = 1; corpse && level < deadState->corpseLevel; ++level)
                {
                    corpse = getFeatureDefinition(*corpse).featureDead;
                }

                if (corpse)
                {
                    corpsesToSpawn.push_back(CorpseSpawnInfo{
                        getFeatureDefinition(*corpse).name,
                        unit.position,
                        unit.rotation,
                        unitDefinition.isFeature});
                }
            }

            auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);

            // A unit that died off the map holds no ground to give back. That
            // is only ever an aircraft -- nothing on the ground can leave --
            // and it is a real case rather than a defensive one: an attack
            // run carries an edge check for the same reason, and a dogfight
            // breaks two weapon ranges out, which off a corner is over the
            // edge. The assertion stands where it is still an invariant.
            assert(!!footprintRegion || isFlying(unit.physics));

            // Out of the flying set first, and whatever else is true of it:
            // the projectile pass walks that set and asks for each unit by
            // id, so an entry left behind by a dead aircraft is a lookup for
            // a unit that is not there any more.
            if (unitDefinition.isMobile && isFlying(unit.physics))
            {
                flyingUnitsSet.erase(it->first);
            }

            if (unit.carriedBy || !footprintRegion)
            {
                // It died in a transport's grip, or off the map entirely:
                // either way it is holding no ground to give back.
            }
            else if (unitDefinition.isMobile)
            {
                if (!isFlying(unit.physics))
                {
                    occupiedGrid.forEach(*footprintRegion, [](auto& cell) { cell.mobileUnitId = std::nullopt; });
                }
            }
            else
            {
                occupiedGrid.forEach(*footprintRegion, [&](auto& cell) {
                  if (cell.buildingInfo && cell.buildingInfo->unit == it->first)
                  {
                      cell.buildingInfo = std::nullopt;
                  } });
            }

            // Removing a unit frees its slot for the next one to be built,
            // so an index that still names it could hand a search an id that
            // now belongs to somebody else.
            invalidateUnitSpatialIndex();

            // The recorder has to hear about the removal before the slot is
            // reused, and it is reused inside this same tick: spawnNewUnits
            // runs a few phases further down. A recorder that only noticed at
            // the end of the tick would still name the old unit for a new one
            // standing in its id.
            if (demoRecorder)
            {
                demoRecorder->unitRemoved(it->first);
            }

            it = units.erase(it);
        }

        for (const auto& spawnInfo : corpsesToSpawn)
        {
            trySpawnFeature(spawnInfo.featureName, spawnInfo.position, spawnInfo.rotation, spawnInfo.isFeature);
        }
    }

    void GameSimulation::deleteDeadProjectiles()
    {
        for (auto it = projectiles.begin(); it != projectiles.end();)
        {
            const auto& projectile = it->second;
            if (projectile.isDead)
            {
                it = projectiles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::optional<std::string> GameSimulation::resurrectedUnitType(const std::string& featureName) const
    {
        auto underscore = featureName.find('_');
        if (underscore == std::string::npos || underscore == 0)
        {
            return std::nullopt;
        }

        auto candidate = toUpper(featureName.substr(0, underscore));
        if (unitDefinitions.find(candidate) == unitDefinitions.end())
        {
            return std::nullopt;
        }

        return candidate;
    }

    UnitCreationStatus GameSimulation::retryBlockedSite(UnitId unitId, const UnitCreationStatusPending& pending)
    {
        // Said once, at the front of the run of tries, and once more when the
        // tries run out. Between the two the builder simply waits: the
        // original's site check refuses quietly on the intervening attempts.
        if (pending.attempts == 0)
        {
            events.push_back(UnitCannotComplyEvent{unitId, "Waiting for target area to clear"});
        }

        auto attempts = pending.attempts + 1;
        if (attempts >= BlockedSiteAttempts)
        {
            events.push_back(UnitCannotComplyEvent{unitId, "Target area was blocked"});
            return UnitCreationStatusFailed();
        }

        return UnitCreationStatusPending{attempts, gameTime + GameTime(BlockedSiteRetryTicks)};
    }

    void GameSimulation::spawnNewUnits()
    {
        for (const auto& unitId : unitCreationRequests)
        {
            auto unit = tryGetUnitState(unitId);
            if (!unit)
            {
                continue;
            }

            if (auto s = std::get_if<UnitBehaviorStateCreatingUnit>(&unit->get().behaviourState); s != nullptr)
            {

                if (!std::holds_alternative<UnitCreationStatusPending>(s->status))
                {
                    continue;
                }

                // Waiting out the gap between two tries at a blocked site.
                if (gameTime < std::get<UnitCreationStatusPending>(s->status).nextAttempt)
                {
                    continue;
                }

                // A building goes up somewhere inside the arc its own FBI
                // names, so a base looks placed by hand rather than stamped
                // out on a grid. The arc is per-unit and ranges over a factor
                // of thirty-two, from a vehicle plant that barely moves to a
                // light laser tower that can end up facing any quarter, and
                // the yards and aircraft plants name none at all, which is
                // what keeps their roll-off square. The original spreads the
                // draw uniformly over the whole arc and offsets it by half,
                // and it leaves the facing alone for an arc narrower than
                // two, which is why the fortification walls that ask for
                // zero come out in a dead straight line.
                std::optional<SimAngle> spawnRotation;
                const auto& newUnitDefinition = unitDefinitions.at(s->unitType);
                if (!newUnitDefinition.isMobile && newUnitDefinition.buildAngle.value >= 2)
                {
                    auto twist = randomBelow(rng, newUnitDefinition.buildAngle.value);
                    spawnRotation = SimAngle(static_cast<uint16_t>(twist)) - SimAngle(newUnitDefinition.buildAngle.value / 2);
                }

                auto newUnitId = trySpawnUnit(s->unitType, s->owner, s->position, spawnRotation);
                if (!newUnitId)
                {
                    // Occupied. Wait for it to clear and say so, rather than
                    // dropping the order on the spot with only a log line --
                    // which is what this did, and why a builder whose site was
                    // briefly straddled by a passing unit silently gave up.
                    // The site is the new unit's footprint, not the builder's
                    // own: a mobile unit parked where the frame is to go is
                    // outside the builder's cells and would never hear about
                    // it otherwise.
                    emitBuggerOff(computeFootprintRegion(s->position, newUnitDefinition.movementCollisionInfo));
                    s->status = retryBlockedSite(unitId, std::get<UnitCreationStatusPending>(s->status));
                    continue;
                }

                if (demoRecorder)
                {
                    demoRecorder->buildStarted(*this, unitId, *newUnitId);
                }

                events.push_back(UnitStartedBuildingEvent{unitId});

                s->status = UnitCreationStatusDone{*newUnitId};
            }

            if (auto s = std::get_if<FactoryBehaviorStateCreatingUnit>(&unit->get().factoryState); s != nullptr)
            {
                if (!std::holds_alternative<UnitCreationStatusPending>(s->status))
                {
                    continue;
                }

                // As above: the yard waits out the gap between tries.
                if (gameTime < std::get<UnitCreationStatusPending>(s->status).nextAttempt)
                {
                    continue;
                }

                auto newUnitId = trySpawnUnit(s->unitType, s->owner, s->position, s->rotation);
                if (!newUnitId)
                {
                    // A factory had it worst of all: this set Failed without
                    // even a log line, and handleBuild dropped straight back
                    // into Building, which re-requested the same blocked spot
                    // on the next tick and every tick after it. It is also the
                    // case the sweep exists for: a hull left standing on the
                    // pad is nobody's order, so without this the yard gives up
                    // on a queue entry every time and the blocker never moves.
                    const auto& blockedDefinition = unitDefinitions.at(s->unitType);
                    emitBuggerOff(computeFootprintRegion(s->position, blockedDefinition.movementCollisionInfo));
                    s->status = retryBlockedSite(unitId, std::get<UnitCreationStatusPending>(s->status));
                    continue;
                }

                // A factory's frame is a build start like a builder's: the
                // 0x09 names the frame and the 0x12 names the yard that
                // finished it.
                if (demoRecorder)
                {
                    demoRecorder->buildStarted(*this, unitId, *newUnitId);
                }

                s->status = UnitCreationStatusDone{*newUnitId};
            }

            // A resurrection asks for its unit from here for the same reason
            // the two above do: the behaviour pass that finished the job was
            // iterating `units`, and creating one appends to the deque behind
            // it. Unlike those two the corpse is still standing, so the work
            // the handler used to do inline happens here instead -- the type
            // off the corpse's name, the position and facing off the corpse,
            // then the corpse, then the unit. The corpse has to go first: it
            // is `blocking`, and a unit will not be placed on an occupied
            // footprint.
            if (auto s = std::get_if<UnitBehaviorStateResurrecting>(&unit->get().behaviourState); s != nullptr)
            {
                auto featureRef = tryGetFeature(s->target);
                if (!featureRef)
                {
                    continue;
                }

                const auto& feature = featureRef->get();
                auto unitType = resurrectedUnitType(getFeatureDefinition(feature.featureName).name);
                if (!unitType)
                {
                    continue;
                }

                auto position = feature.position;
                auto rotation = feature.rotation;
                auto owner = unit->get().owner;
                deleteFeature(s->target);

                auto newUnitId = trySpawnUnit(*unitType, owner, position, rotation);
                if (!newUnitId)
                {
                    // The original prints "Unable to create any more units"
                    // and tries again in 300 ticks, but it still has its
                    // corpse to try with. Ours is spent, so the job ends and
                    // the order is dropped on the next tick, when the handler
                    // finds no feature.
                    continue;
                }

                // 0x405219/0x405226: complete rather than a nanoframe, and on
                // exactly one hit point -- it has to be repaired afterwards or
                // a stiff breeze finishes it.
                auto& newUnit = getUnitState(*newUnitId);
                const auto& newUnitDefinition = unitDefinitions.at(newUnit.unitType);
                newUnit.buildTimeCompleted = newUnitDefinition.buildTime;
                newUnit.hitPoints = 1;
            }
        }

        unitCreationRequests.clear();
    }

    void GameSimulation::updateSelfRepair()
    {
        // `healtime` is hit points a second, but the original does not heal
        // every tick: 0x48AF3D runs the whole thing only when the game tick
        // is a multiple of eight (`test BYTE PTR [..],0x7`) and then adds
        // `healtime * 8 / 30` points, truncated. That works out at roughly
        // `healtime` a second while arriving in visible steps -- a commander
        // with the shipped 27 gets 7 points every eight ticks, 26.25 a
        // second -- and reproducing the granularity matters more than the
        // average, because it is what the health bar does.
        if (gameTime.value % 8u != 0u)
        {
            return;
        }

        for (auto& entry : units)
        {
            auto& unit = entry.second;
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            if (unitDefinition.healTime == 0 || unit.isDead() || unit.isBeingBuilt(unitDefinition))
            {
                continue;
            }

            if (unit.hitPoints >= unitDefinition.maxHitPoints)
            {
                continue;
            }

            // Free, as all repair is in RWE. The original charges the mending
            // against the owner's stores; nothing else here does, and making
            // this the one exception would be more surprising than useful.
            auto amount = (unitDefinition.healTime * 8u) / 30u;
            unit.hitPoints = std::min(unitDefinition.maxHitPoints, unit.hitPoints + amount);
        }
    }

#ifdef RWE_ENABLE_SIMPROF
    namespace
    {
        // Temporary: per-phase tick timing, reported every two seconds.
        // The slots live in sim_prof.h so the behaviour pass can add its own
        // without a second reporting mechanism; they are zeroed rather than
        // erased so a reference held at a call site stays good.
        std::chrono::steady_clock::time_point profLastReport = std::chrono::steady_clock::now();
        int profTicks = 0;

        void profReport()
        {
            ++profTicks;
            auto now = std::chrono::steady_clock::now();
            auto span = std::chrono::duration<double, std::milli>(now - profLastReport).count();
            if (span < 2000.0)
            {
                return;
            }
            std::string line;
            for (auto& [name, total] : simProfTotals)
            {
                line += " " + name + "=" + std::to_string(static_cast<int>(total / (profTicks == 0 ? 1 : profTicks) * 1000.0)) + "us";
                total = 0.0;
            }
            LOG_INFO << "SIMPROF ticks=" << profTicks << line;
            profTicks = 0;
            profLastReport = now;
        }
    }
#endif

    void GameSimulation::tick()
    {
        gameTime += GameTime(1);

        // AI runs first so any commands it emits this tick can be drained
        // by GameScene before unit behaviour runs next tick. This mirrors
        // the human input pipeline: human commands are processed via
        // PlayerCommandService at the *start* of tryTickGame, and AI
        // commands take the same channel.
        {
            RWE_SIMPROF("ai");
            runAiControllers();
        }

        updateWind();

        updateResources();

        {
            RWE_SIMPROF("path");
            pathFindingService.update(*this);
        }

        // The three unit passes are timed separately -- each is its own
        // scope because RWE_SIMPROF names its variables, one to a scope.
        {
            RWE_SIMPROF("behaviour");

            // Nothing a handler does may add a unit while this walk is in
            // progress: the map is a deque underneath, growing it invalidates
            // the iterator the loop is holding, and the ++ that follows is
            // undefined. Every creation path defers to spawnNewUnits for that
            // reason and deleteDeadUnits waits its turn the same way.
            //
            // Nothing enforced it, which is how resurrect came to create its
            // unit inline and stand for a day: an ordinary build survives it,
            // and it only shows up where the standard library checks its own
            // iterators. Worse, it hid best in the games most likely to be
            // played -- once anything has died there is a free slot to fill,
            // and filling one does not grow the deque or invalidate anything.
            [[maybe_unused]] auto generationBefore = units.generation();

            for (auto& entry : units)
            {
                UnitBehaviorService(this).update(entry.first);
            }

            assert(units.generation() == generationBefore && "a unit was created during the behaviour pass; defer it to spawnNewUnits");
        }

        {
            RWE_SIMPROF("pieces");
            for (auto& entry : units)
            {
                for (auto& piece : entry.second.pieces)
                {
                    piece.update(SimScalar(SimMillisecondsPerTick) / 1000_ss);
                }
            }
        }

        {
            RWE_SIMPROF("cob");
            for (auto& entry : units)
            {
                runUnitCobScripts(*this, entry.first);
            }
        }

        {
            RWE_SIMPROF("carried");
            updateCarriedUnits();
        }

        {
            RWE_SIMPROF("selfrepair");
            updateSelfRepair();
        }

        // After the behaviour pass, which is where a builder stakes its claim
        // on the frame it is working on for this period.
        updateNanoframeDecay();

        updateSelfDestructs();

        {
            RWE_SIMPROF("projectiles");
            updateProjectiles();
        }

        updateBurningFeatures();

        // Before the dead are cleared, so a wreck spawned this tick first
        // moves on the next one, as the original does.
        updateFallingFeatures();

#ifdef RWE_ENABLE_SIMPROF
        profReport();
#endif

        updateFeatureRegrowth();

        processVictoryCondition();

        {
            RWE_SIMPROF("deletedead");
            deleteDeadUnits();
        }

        deleteDeadProjectiles();

        spawnNewUnits();

        updateCloakSuppression();

        {
            RWE_SIMPROF("visibility");
            updateVisibility();
        }

        // Last, once every phase has left its mark on the tick: the recorder
        // reads the end-of-tick state, which is the state TA's sender would
        // have serialised. Nothing here can change what the tick did.
        if (demoRecorder)
        {
            demoRecorder->endOfTick(*this);
        }
    }

    std::optional<FeatureDefinitionId> GameSimulation::tryGetFeatureDefinitionId(const std::string& featureName) const
    {
        if (auto it = featureNameIndex.find(toUpper(featureName)); it != featureNameIndex.end())
        {
            return it->second;
        }

        return std::nullopt;
    }

    const FeatureDefinition& GameSimulation::getFeatureDefinition(FeatureDefinitionId featureDefinitionId) const
    {
        return featureDefinitions.get(featureDefinitionId);
    }
}
