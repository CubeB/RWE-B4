#include "GameSimulation.h"
#include <algorithm>
#include <cmath>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/util.h>
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
         * How long a cloak stays off after an enemy has been inside
         * MinCloakDistance. Ninety ticks; the original stamps
         * `unit+0xB0 = tick + 0x5A` every tick one is that close.
         */
        constexpr unsigned int CloakSuppressionTicks = 90;

        /**
         * Whether any of the dishes or jammers of the given kind reaches the
         * point, measured in the map plane. Both lists carry their radius
         * already squared.
         */
        template <typename T>
        bool isInRangeOfAny(const std::vector<T>& sources, const SimVector& position, bool sonar)
        {
            for (const auto& source : sources)
            {
                if (source.sonar != sonar)
                {
                    continue;
                }

                auto dx = position.x - source.position.x;
                auto dz = position.z - source.position.z;
                if (((dx * dx) + (dz * dz)) <= source.rangeSquared)
                {
                    return true;
                }
            }

            return false;
        }
    }

    ResourceSettlement settleResourcePool(float supply, float debt, float requested)
    {
        // The original never reaches here with a negative supply, because the
        // stockpile it feeds in is the previous second's remainder and that is
        // floored at zero. Say so explicitly rather than divide by a debt of
        // nothing on the way to finding out.
        if (supply < 0.0f)
        {
            supply = 0.0f;
        }

        ResourceSettlement result{};

        float afterDebt;
        if (debt <= supply)
        {
            result.debtFraction = 1.0f;
            afterDebt = supply - debt;
        }
        else
        {
            result.debtFraction = supply / debt;
            afterDebt = 0.0f;
        }

        if (requested <= afterDebt)
        {
            result.requestFraction = 1.0f;
            result.remaining = afterDebt - requested;
        }
        else
        {
            result.requestFraction = afterDebt / requested;
            result.remaining = 0.0f;
        }

        result.stalled = result.debtFraction < 1.0f || result.requestFraction < 1.0f;
        return result;
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
        // it (most vegetation) come out of the TDF reader with 1. Nothing spends
        // these points -- weapons do not damage features -- they only tell
        // computeFeatureReclaimWork how much bulk there is to haul away.
        newFeature.hitPoints = featureDefinition.damage;

        auto featureId = FeatureId(features.emplace(std::move(newFeature)));

        auto& f = features.tryGet(featureId)->get();

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

        return featureId;
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
        // The hit points read here are the feature's current ones, but nothing
        // damages a feature: weapons leave wreckage and scenery alone, so in
        // practice this is always the full `damage` value the definition declared.
        // The payout does not depend on it either way: reclaimFeature always hands
        // over the full metal/energy, spread across whatever work total applies.
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

    void GameSimulation::replaceFeature(FeatureId id, const std::optional<FeatureDefinitionId>& replacement)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return;
        }

        auto position = featureRef->get().position;
        auto rotation = featureRef->get().rotation;

        deleteFeature(id);

        if (replacement)
        {
            addFeature(MapFeature{*replacement, position, rotation});
        }
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
        std::uniform_int_distribution<unsigned int> duration(minTicks, maxTicks);
        feature.burningUntil = gameTime + GameTime(std::max(1u, duration(rng)));
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
        std::uniform_int_distribution<unsigned int> roll(1, 100);
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
            if (roll(rng) <= chancePercent)
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
        std::uniform_int_distribution chanceDist(0, 99);
        if (chanceDist(rng) >= static_cast<int>(definition.reproduce))
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
        std::uniform_int_distribution areaDist(0, area - 1);
        auto targetX = sourceX + areaDist(rng) - (area / 2);
        auto targetY = sourceY + areaDist(rng) - (area / 2);

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
        transport.carriedUnits.push_back(unitId);
        return true;
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
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
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
            return false;
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

        if (auto region = occupiedGrid.tryToRegion(*spot))
        {
            occupiedGrid.forEach(*region, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
        }

        unit.position = newPosition;
        unit.previousPosition = newPosition;
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

        // Whatever it was carrying goes down with it.
        auto carried = unit.carriedUnits;
        unit.carriedUnits.clear();
        for (auto carriedId : carried)
        {
            auto carriedRef = tryGetUnitState(carriedId);
            if (carriedRef && carriedRef->get().isAlive())
            {
                killUnit(carriedId);
            }
        }
    }

    bool GameSimulation::reclaimUnit(UnitId targetId, PlayerId reclaimer, unsigned int workAmount)
    {
        auto unitRef = tryGetUnitState(targetId);
        if (!unitRef || unitRef->get().isDead())
        {
            return true;
        }
        if (workAmount == 0)
        {
            return false;
        }

        auto& unit = unitRef->get();
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        // Undoing a unit takes as much work as went into it, so a barely
        // started nanoframe is cleared in moments while a finished unit takes
        // its full build time.
        auto totalWork = std::max(1u, std::min(unitDefinition.buildTime, unit.buildTimeCompleted));
        auto previousProgress = unit.reclaimProgress;
        auto newProgress = std::min(totalWork, previousProgress + workAmount);
        unit.reclaimProgress = newProgress;

        // Only the share of the cost that has actually been built can be recovered.
        auto investedFraction = unitDefinition.buildTime == 0
            ? 1.0f
            : std::min(1.0f, static_cast<float>(unit.buildTimeCompleted) / static_cast<float>(unitDefinition.buildTime));

        auto cumulative = [&](float amount, unsigned int progress) {
            return amount * investedFraction * (static_cast<float>(progress) / static_cast<float>(totalWork));
        };
        Metal metalDelta(cumulative(unitDefinition.buildCostMetal.value, newProgress) - cumulative(unitDefinition.buildCostMetal.value, previousProgress));
        Energy energyDelta(cumulative(unitDefinition.buildCostEnergy.value, newProgress) - cumulative(unitDefinition.buildCostEnergy.value, previousProgress));
        getPlayer(reclaimer).addResourceDelta(energyDelta, metalDelta, energyDelta, metalDelta);

        if (newProgress < totalWork)
        {
            return false;
        }

        // Reclaimed units vanish quietly: no wreck, no explosion.
        unit.markAsDeadNoCorpse();
        events.push_back(UnitDiedEvent{targetId, unit.unitType, unit.position, UnitDiedEvent::DeathType::Deleted});
        return true;
    }

    bool GameSimulation::captureUnit(UnitId targetId, PlayerId captor, unsigned int workAmount)
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
        if (workAmount == 0)
        {
            return false;
        }

        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        auto totalWork = std::max(1u, unitDefinition.buildTime);
        unit.captureProgress = std::min(totalWork, unit.captureProgress + workAmount);
        if (unit.captureProgress < totalWork)
        {
            return false;
        }

        auto previousOwner = unit.owner;
        unit.owner = captor;
        unit.captureProgress = 0;

        // The unit changes hands with a clean slate: whatever it was doing
        // for its old owner stops, and it must not keep shooting at its new
        // friends.
        unit.orders.clear();
        unit.buildOrderUnitId = std::nullopt;
        unit.behaviourState = UnitBehaviorStateIdle();
        unit.clearWeaponTargets();

        events.push_back(UnitCapturedEvent{targetId, previousOwner, captor});
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
            unit.selfDestructTime = gameTime + GameTime(SelfDestructCountdownTicks);
        }
    }

    void GameSimulation::selfDestructUnit(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        // Self-destruction leaves nothing to reclaim.
        unit.markAsDeadNoCorpse();
        unit.selfDestructTime = std::nullopt;
        getPlayer(unit.owner).unitsLost += 1;

        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, UnitDiedEvent::DeathType::SelfDestructed});

        const auto& explosion = unitDefinition.selfDestructAs.empty() ? unitDefinition.explodeAs : unitDefinition.selfDestructAs;
        if (!explosion.empty())
        {
            auto impactType = unit.position.y < terrain.getSeaLevel() ? ImpactType::Water : ImpactType::Normal;
            auto projectile = createProjectileFromWeapon(unit.owner, explosion, unit.position, SimVector(0_ss, -1_ss, 0_ss), 0_ss, std::nullopt, std::nullopt);
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

    PlayerId GameSimulation::addPlayer(const GamePlayerInfo& info)
    {
        PlayerId id(players.size());
        players.push_back(info);

        const auto& heights = terrain.getHeightMap();
        auto cells = PlayerVisibility::VisionCellSizeInTiles;
        playerVisibility.emplace_back((heights.getWidth() + cells - 1) / cells, (heights.getHeight() + cells - 1) / cells);

        return id;
    }

    namespace
    {
        int heightMapSampleAt(const Grid<unsigned char>& heights, const SimVector& tile)
        {
            auto x = static_cast<int>(std::floor(tile.x.value));
            auto y = static_cast<int>(std::floor(tile.z.value));
            if (x < 0 || y < 0 || x >= heights.getWidth() || y >= heights.getHeight())
            {
                return 0;
            }

            return static_cast<int>(heights.get(x, y));
        }
    }

    int GameSimulation::terrainSampleHeightAt(const SimVector& position) const
    {
        return heightMapSampleAt(terrain.getHeightMap(), terrain.worldToHeightmapSpace(position));
    }

    Point GameSimulation::visionCellAt(const SimVector& position) const
    {
        // The projected-space transform. See the doc comment in the header:
        // the fog renderer must apply exactly this.
        auto tile = terrain.worldToHeightmapSpace(position);
        return heightmapToVisionCell(tile.x, tile.z, heightMapSampleAt(terrain.getHeightMap(), tile));
    }

    bool GameSimulation::isExploredBy(PlayerId player, const SimVector& position) const
    {
        return playerVisibility.at(player.value).isExplored(visionCellAt(position));
    }

    bool GameSimulation::isVisibleTo(PlayerId player, const SimVector& position) const
    {
        return playerVisibility.at(player.value).isVisible(visionCellAt(position));
    }

    bool GameSimulation::isOnRadarOf(PlayerId player, const SimVector& position) const
    {
        // No radar grid: this is a straight range test against the player's
        // active dishes, measured in the map plane.
        for (const auto& detector : playerVisibility.at(player.value).radarDetectors)
        {
            auto dx = position.x - detector.position.x;
            auto dz = position.z - detector.position.z;
            if (((dx * dx) + (dz * dz)) <= detector.rangeSquared)
            {
                return true;
            }
        }

        return false;
    }

    bool GameSimulation::canSeeUnit(PlayerId viewer, UnitId unitId) const
    {
        const auto& unit = getUnitState(unitId);
        if (unit.isOwnedBy(viewer))
        {
            return true;
        }

        // A cloaked unit is out of sight however well lit the ground under it
        // is. The original asks this in the same order, own units first, in the
        // predicate that decides whether to draw a unit at all.
        return !unit.cloaked && isVisibleTo(viewer, unit.position);
    }

    bool GameSimulation::canDetectUnit(PlayerId viewer, UnitId unitId) const
    {
        const auto& unit = getUnitState(unitId);
        if (unit.isOwnedBy(viewer))
        {
            return true;
        }

        if (unit.cloaked)
        {
            return false;
        }

        if (isVisibleTo(viewer, unit.position))
        {
            return true;
        }

        const auto& contacts = playerVisibility.at(viewer.value).radarContacts;
        return contacts.find(unitId) != contacts.end();
    }

    bool GameSimulation::weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& attacker, const UnitState& target) const
    {
        // 0x49ABB0, asked of every candidate before range comes into it. The
        // two branches are exclusive: a water weapon is judged entirely on
        // where its target is floating, and everything else has to get both
        // ends of the shot out of the water before the air rule is so much as
        // looked at.
        auto seaLevel = terrain.getSeaLevel();
        const auto& targetDefinition = unitDefinitions.at(target.unitType);

        if (weaponDefinition.waterWeapon)
        {
            // 0x49ABF9: a floater is exempt, so a torpedo still reaches a ship
            // riding on the surface.
            if (!targetDefinition.floater && target.position.y > seaLevel)
            {
                return false;
            }

            // 0x49AC20: a hovercraft sits on the water rather than in it, and
            // half its model height standing proud of the surface is what puts
            // it out of a torpedo's reach.
            if (targetDefinition.canHover && target.position.y + (modelHeightOf(targetDefinition) / 2_ss) > seaLevel)
            {
                return false;
            }

            return true;
        }

        // 0x49ACC3 and 0x49ACEA. "Out of the water" is measured to the top of
        // the model rather than to its origin, which is what makes a submerged
        // submarine unshootable by anything that is not a torpedo, and a
        // surfaced one shootable by everything. The shooter has to be up there
        // too: a submarine's deck gun does not fire from underneath.
        const auto& attackerDefinition = unitDefinitions.at(attacker.unitType);
        if (attacker.position.y + modelHeightOf(attackerDefinition) <= seaLevel)
        {
            return false;
        }

        if (target.position.y + modelHeightOf(targetDefinition) <= seaLevel)
        {
            return false;
        }

        // 0x49AD07, and it only points this way round. The original has no
        // rule anywhere that refuses an ordinary weapon an airborne target;
        // what holds a Peewee back is wpri_badTargetCategory, which is a
        // preference and lives in the choice rather than here.
        if (weaponDefinition.toAirWeapon && !isFlying(target.physics))
        {
            return false;
        }

        return true;
    }

    void GameSimulation::returnFire(UnitId victimId, UnitId attackerId)
    {
        if (victimId == attackerId)
        {
            return;
        }

        auto attackerRef = tryGetUnitState(attackerId);
        if (!attackerRef)
        {
            return;
        }
        const auto& attacker = attackerRef->get();
        if (attacker.isDead())
        {
            return;
        }

        auto& victim = getUnitState(victimId);
        if (victim.isDead() || victim.fireOrders == UnitFireOrders::HoldFire || victim.isOwnedBy(attacker.owner))
        {
            return;
        }

        const auto& victimDefinition = unitDefinitions.at(victim.unitType);
        if (victim.isBeingBuilt(victimDefinition))
        {
            return;
        }

        for (unsigned int i = 0; i < victim.weapons.size(); ++i)
        {
            const auto& weapon = victim.weapons[i];
            if (!weapon || !std::holds_alternative<UnitWeaponStateIdle>(weapon->state))
            {
                continue;
            }

            const auto& weaponDefinition = weaponDefinitions.at(weapon->weaponType);
            if (weaponDefinition.commandFire)
            {
                continue;
            }

            if (victim.position.distanceSquared(attacker.position) > weaponDefinition.maxRange * weaponDefinition.maxRange)
            {
                continue;
            }

            if (!weaponCanHitUnit(weaponDefinition, victim, attacker))
            {
                continue;
            }

            victim.setWeaponTarget(i, attackerId);
        }
    }

    SimScalar GameSimulation::modelHeightOf(const UnitDefinition& unitDefinition) const
    {
        if (auto model = unitModelDefinitions.find(unitDefinition.objectName); model != unitModelDefinitions.end())
        {
            return model->second.height;
        }

        return 0_ss;
    }

    void GameSimulation::updateVisibility()
    {
        for (auto& v : playerVisibility)
        {
            v.clearCurrent();
        }

        // World units per vision cell; sight and radar ranges are in world units.
        auto cellWorldUnits = static_cast<int>(simScalarToUInt(MapTerrain::HeightTileWidthInWorldUnits)) * PlayerVisibility::VisionCellSizeInTiles;
        auto seaLevel = static_cast<int>(std::min(simScalarToUInt(terrain.getSeaLevel()), 255u));

        for (const auto& [unitId, unit] : units)
        {
            if (unit.isDead())
            {
                continue;
            }
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            auto& vis = playerVisibility.at(unit.owner.value);

            // The eye sits at the top of the unit's model, never below the
            // water's surface, and the whole thing lives in the heightmap's
            // 0..255 range.
            auto modelHeight = static_cast<int>(std::floor(modelHeightOf(unitDefinition).value));
            auto groundLevel = std::max(static_cast<int>(std::floor(unit.position.y.value)), seaLevel + 1);
            auto eyeHeight = std::clamp(groundLevel + modelHeight, 0, 255);

            // Sight is capped: TA indexes its ray tables with
            // min(SightDistance / 32, numtables - 1), so terrain-mode sight
            // never reaches beyond 8 cells however large SightDistance is.
            auto radius = std::min(
                static_cast<int>(unitDefinition.sightDistance) / cellWorldUnits,
                losTables.maxRadius());

            vis.revealWithLineOfSight(visionCellAt(unit.position), radius, visionHeights, eyeHeight, losTables);

            // Radar, sonar and jamming all need the unit switched on if it can
            // be switched at all. Altitude extends radar; sonar is flat.
            auto detectorActive = (!unitDefinition.onOffable || unit.activated) && !unit.isBeingBuilt(unitDefinition);
            if (!detectorActive)
            {
                continue;
            }

            auto altitude = rweMax(unit.position.y, 0_ss);
            if (unitDefinition.radarDistance > 0)
            {
                auto range = intToSimScalar(static_cast<int>(unitDefinition.radarDistance)) + (2_ss * altitude);
                vis.radarDetectors.push_back(PlayerVisibility::RadarDetector{unit.position, range * range, false});
            }
            if (unitDefinition.sonarDistance > 0)
            {
                auto range = intToSimScalar(static_cast<int>(unitDefinition.sonarDistance));
                vis.radarDetectors.push_back(PlayerVisibility::RadarDetector{unit.position, range * range, true});
            }

            // A jammer works against everyone but its own owner. The original
            // builds this list while walking the same unit array, skipping only
            // the units of the player whose picture it is drawing, so an ally's
            // jammer blanks your radar exactly as an enemy's does.
            for (std::size_t i = 0; i < playerVisibility.size(); ++i)
            {
                if (i == unit.owner.value)
                {
                    continue;
                }

                auto& theirVis = playerVisibility[i];
                if (unitDefinition.radarDistanceJam > 0)
                {
                    auto range = intToSimScalar(static_cast<int>(unitDefinition.radarDistanceJam));
                    theirVis.radarJammers.push_back(PlayerVisibility::RadarJammer{unit.position, range * range, false});
                }
                if (unitDefinition.sonarDistanceJam > 0)
                {
                    auto range = intToSimScalar(static_cast<int>(unitDefinition.sonarDistanceJam));
                    theirVis.radarJammers.push_back(PlayerVisibility::RadarJammer{unit.position, range * range, true});
                }
            }
        }

        // Radar is a unit-versus-unit range query, not a grid: terrain never
        // blocks it and it reveals no ground, it only flags contacts.
        auto seaLevelScalar = terrain.getSeaLevel();
        for (std::size_t i = 0; i < playerVisibility.size(); ++i)
        {
            auto& vis = playerVisibility[i];
            if (vis.radarDetectors.empty())
            {
                continue;
            }

            PlayerId player(static_cast<unsigned int>(i));
            for (const auto& [unitId, unit] : units)
            {
                if (unit.isDead() || unit.isOwnedBy(player))
                {
                    continue;
                }

                // Stealth is absolute: the original's detection visitor drops
                // the unit before it measures anything.
                const auto& targetDefinition = unitDefinitions.at(unit.unitType);
                if (targetDefinition.stealth)
                {
                    continue;
                }

                // Which of the two contacts a unit can be is decided by where
                // it sits relative to the waterline: sonar finds anything at or
                // below it, radar anything whose model rises above it. A
                // half-submerged unit is both, and a unit on dry land at sea
                // level is too.
                auto modelTop = unit.position.y + modelHeightOf(targetDefinition);
                auto byRadar = modelTop >= seaLevelScalar && isInRangeOfAny(vis.radarDetectors, unit.position, false);
                auto bySonar = unit.position.y <= seaLevelScalar && isInRangeOfAny(vis.radarDetectors, unit.position, true);

                // Jamming runs after detection and wins over it, and it only
                // touches the contact it is aimed at: a radar jammer says
                // nothing about what sonar can hear.
                if (byRadar && isInRangeOfAny(vis.radarJammers, unit.position, false))
                {
                    byRadar = false;
                }
                if (bySonar && isInRangeOfAny(vis.radarJammers, unit.position, true))
                {
                    bySonar = false;
                }

                if (byRadar || bySonar)
                {
                    vis.radarContacts.insert(unitId);
                }
            }
        }
    }

    void GameSimulation::updateCloakSuppression()
    {
        // The original runs this in the same per-tick pass as the radar
        // picture: every cloakable unit asks whether a live enemy is standing
        // within MinCloakDistance of it, and if one is, its cloak is held off
        // for the next ninety ticks. The flag is a timestamp rather than a
        // latch, so the unit stays visible for three seconds after the enemy
        // walks away instead of blinking back the moment it is out of range.
        for (auto& entry : units)
        {
            auto& unit = entry.second;
            if (unit.isDead())
            {
                continue;
            }

            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            if (!unitDefinition.cloakable)
            {
                continue;
            }

            auto minDistance = intToSimScalar(static_cast<int>(unitDefinition.minCloakDistance));
            auto minDistanceSquared = minDistance * minDistance;

            for (const auto& otherEntry : units)
            {
                const auto& otherUnit = otherEntry.second;
                if (otherUnit.isDead() || otherUnit.isOwnedBy(unit.owner))
                {
                    continue;
                }

                auto dx = otherUnit.position.x - unit.position.x;
                auto dz = otherUnit.position.z - unit.position.z;
                if (((dx * dx) + (dz * dz)) <= minDistanceSquared)
                {
                    unit.cloakSuppressedUntil = gameTime + GameTime(CloakSuppressionTicks);
                    break;
                }
            }
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

        if (unitDefinition.isMobile)
        {
            // don't shade mobile units
            for (auto& m : meshes)
            {
                m.shaded = false;
            }
        }

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

        // add weapons
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

    std::optional<UnitId> GameSimulation::trySpawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<SimAngle> rotation)
    {
        auto unit = createUnit(*this, unitType, owner, position, rotation);
        const auto& unitDefinition = unitDefinitions.at(unitType);
        if (unitDefinition.floater || unitDefinition.canHover)
        {
            unit.position.y = rweMax(terrain.getSeaLevel(), unit.position.y);
            unit.previousPosition.y = unit.position.y;
        }

        // TODO: if we failed to add the unit throw some warning
        auto unitId = tryAddUnit(std::move(unit));

        if (unitId)
        {
            UnitBehaviorService(this).onCreate(*unitId);
            events.push_back(UnitSpawnedEvent{*unitId});
        }

        return unitId;
    }

    std::optional<UnitId> GameSimulation::tryAddUnit(UnitState&& unit)
    {
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        // set footprint area as occupied by the unit
        auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        if (isCollisionAt(footprintRect))
        {
            return std::nullopt;
        }

        auto unitId = units.emplace(std::move(unit));
        const auto& insertedUnit = units.tryGet(unitId)->get();

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

    bool GameSimulation::canBeBuiltAt(const rwe::MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y) const
    {
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

    bool GameSimulation::isCollisionAt(const DiscreteRect& rect, UnitId self) const
    {
        auto region = occupiedGrid.tryToRegion(rect);
        if (!region)
        {
            return true;
        }

        return occupiedGrid.any(*region, [&](const auto& cell) {
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
        });
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
        PathRequest request{unitId};

        // If the unit is already in the queue for a path,
        // we'll assume that they no longer care about their old request
        // and that their new request is for some new path,
        // so we'll move them to the back of the queue for fairness.
        auto it = std::find(pathRequests.begin(), pathRequests.end(), request);
        if (it != pathRequests.end())
        {
            pathRequests.erase(it);
        }

        pathRequests.push_back(PathRequest{unitId});
    }

    /**
     * The direction a missile with this attitude is pointing. TA builds its
     * velocity this way round every tick -- pitch first, then heading -- so a
     * missile always flies exactly where its nose points (0x49BA74).
     */
    SimVector toMissileDirection(SimAngle heading, SimAngle pitch)
    {
        auto horizontal = cos(pitch);
        return SimVector(sin(heading) * horizontal, sin(pitch), cos(heading) * horizontal);
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

    Projectile GameSimulation::createProjectileFromWeapon(
        PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker, std::optional<SimVector> inheritedVelocity, std::optional<SimVector> targetPosition)
    {
        return createProjectileFromWeapon(owner, weapon.weaponType, position, direction, distanceToTarget, targetUnit, attacker, inheritedVelocity, targetPosition);
    }

    Projectile GameSimulation::createProjectileFromWeapon(PlayerId owner, const std::string& weaponType, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker, std::optional<SimVector> inheritedVelocity, std::optional<SimVector> targetPosition)
    {
        const auto& weaponDefinition = weaponDefinitions.at(weaponType);

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

        if (weaponDefinition.weaponTimer)
        {
            auto randomDecay = weaponDefinition.randomDecay.value().value;
            std::uniform_int_distribution<unsigned int> dist(0, randomDecay);
            auto randomVal = dist(rng);
            projectile.dieOnFrame = gameTime + *weaponDefinition.weaponTimer - GameTime(randomDecay / 2) + GameTime(randomVal);
        }
        else if (std::holds_alternative<ProjectilePhysicsTypeLineOfSight>(weaponDefinition.physicsType))
        {
            projectile.dieOnFrame = gameTime + GameTime(simScalarToUInt(distanceToTarget / weaponDefinition.velocity) + 1);
        }

        projectile.createdAt = gameTime;
        projectile.groundBounce = weaponDefinition.groundBounce;

        projectile.targetUnit = targetUnit;
        projectile.targetPosition = targetPosition;

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

    void GameSimulation::spawnProjectile(PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker, std::optional<SimVector> inheritedVelocity, std::optional<SimVector> targetPosition)
    {
        projectiles.emplace(createProjectileFromWeapon(owner, weapon, position, direction, distanceToTarget, targetUnit, attacker, inheritedVelocity, targetPosition));
    }

    WinStatus GameSimulation::computeWinStatus() const
    {
        std::optional<PlayerId> livingPlayer;
        for (Index i = 0; i < getSize(players); ++i)
        {
            const auto& p = players[i];

            if (p.status == GamePlayerStatus::Alive)
            {
                if (livingPlayer)
                {
                    // multiple players are alive, the game is not over
                    return WinStatusUndecided();
                }
                else
                {
                    livingPlayer = PlayerId(i);
                }
            }
        }

        if (livingPlayer)
        {
            // one player is alive, declare them the winner
            return WinStatusWon{*livingPlayer};
        }

        // no players are alive, the game is a draw
        return WinStatusDraw();
    }

    bool GameSimulation::addResourceDelta(const UnitId& unitId, const Energy& energy, const Metal& metal)
    {
        return addResourceDelta(unitId, energy, metal, energy, metal);
    }

    bool GameSimulation::addResourceDelta(const UnitId& unitId, const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal)
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

        return unit.addResourceDelta(apparentEnergy, apparentMetal, actualEnergy, actualMetal);
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
        auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);
        assert(!!footprintRegion);

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

    void GameSimulation::quietlyKillUnit(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        unit.markAsDeadNoCorpse();
        getPlayer(unit.owner).unitsLost += 1;
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
                return MovementClassDefinition{
                    "",
                    mc.footprintX,
                    mc.footprintZ,
                    mc.minWaterDepth,
                    mc.maxWaterDepth,
                    mc.maxSlope,
                    mc.maxWaterSlope};
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

            // ignore if the projectile is above or below the unit
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

            // ignore if the projectile is above or below the unit
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

            // ignore if the projectile is above or below the feature
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

        // ignore if the projectile is above or below the unit
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
        // test collision with terrain
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
            // detect collision with something's footprint
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

            // detect collision with flying unit footprint
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

    void GameSimulation::killUnit(UnitId unitId, std::optional<UnitId> attacker)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);

        unit.markAsDead();
        getPlayer(unit.owner).unitsLost += 1;
        releaseTransportLinks(unitId);

        // Credit the kill to the attacker, if any.
        // Match TA behavior: friendly-fire kills count.
        // Skip if the attacker is dead or no longer exists, and never
        // credit a unit for killing itself (suicide / explodeAs).
        if (attacker && *attacker != unitId)
        {
            auto attackerUnit = tryGetUnitState(*attacker);
            if (attackerUnit && attackerUnit->get().isAlive())
            {
                attackerUnit->get().kills += 1;
                getPlayer(attackerUnit->get().owner).unitsKilled += 1;
            }
        }

        auto deathType = unit.position.y < terrain.getSeaLevel() ? UnitDiedEvent::DeathType::WaterExploded : UnitDiedEvent::DeathType::NormalExploded;
        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, deathType});

        // Run the script's Killed(severity, corpsetype) now, while the unit
        // still exists: the piece explosions it fires before its first sleep
        // become debris. Anything it does after a sleep is lost, as the unit
        // is removed at the end of the tick.
        if (unit.cobEnvironment)
        {
            const int severity = 50;
            unit.cobEnvironment->createThread("Killed", {severity, 0});
            runUnitCobScripts(*this, unitId);
        }

        if (!unitDefinition.explodeAs.empty())
        {
            auto impactType = unit.position.y < terrain.getSeaLevel() ? ImpactType::Water : ImpactType::Normal;
            // The explodeAs projectile is environmental: deaths it causes
            // are not credited to anyone (the original unit is already dead).
            auto projectile = createProjectileFromWeapon(unit.owner, unitDefinition.explodeAs, unit.position, SimVector(0_ss, -1_ss, 0_ss), 0_ss, std::nullopt, std::nullopt);
            doProjectileImpact(projectile, impactType);
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

    void GameSimulation::applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker, bool paralyzer)
    {
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
            if (unit.isBeingBuilt(unitDefinition))
            {
                // Units that are still under construction
                // die quietly without a corpse.
                // FIXME: units in TA that are not actively receiving build input
                // die with an explosion, even though they leave no corpse.
                // Note: under-construction kills are not credited to the attacker
                // because the unit dies via quietlyKillUnit which has no firing path.
                quietlyKillUnit(unitId);
            }
            else
            {
                killUnit(unitId, attacker);
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

        std::unordered_set<UnitId> seenUnits;

        // Blasts hurt units only. Wreckage and scenery are not damaged by weapons:
        // a wreck stays put until somebody reclaims it, and terrain features are
        // only ever removed by reclaim or by fire (see updateBurningFeatures).

        auto region = GridRegion::fromCoordinates(minCell, maxCell);

        // for each cell
        region.forEach([&](const auto& coords) {
          // check if it's in range
          auto cellCenter = terrain.heightmapIndexToWorldCenter(coords.x, coords.y);
          Rectangle2x<SimScalar> cellRectangle(
              Vector2x<SimScalar>(cellCenter.x, cellCenter.z),
              Vector2x<SimScalar>(MapTerrain::HeightTileWidthInWorldUnits / 2_ss, MapTerrain::HeightTileHeightInWorldUnits / 2_ss));
          auto cellDistanceSquared = cellRectangle.distanceSquared(Vector2x<SimScalar>(position.x, position.z));
          if (cellDistanceSquared > radiusSquared)
          {
              return;
          }

          // check if a unit is there
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

          // check if the unit was seen/mark as seen
          auto pair = seenUnits.insert(*u);
          if (!pair.second) // the unit was already present
          {
              return;
          }

          const auto& unit = getUnitState(*u);

          // skip dead units
          if (unit.isDead())
          {
              return;
          }

          // add in the third dimension component to distance,
          // check if we are still in range
          auto unitDistanceSquared = createBoundingBox(unit).distanceSquared(position);
          if (unitDistanceSquared > radiusSquared)
          {
              return;
          }

          // apply appropriate damage
          auto damageScale = blastDamageScale(rweSqrt(unitDistanceSquared), radius, projectile.edgeEffectiveness);
          auto rawDamage = projectile.getDamage(unit.unitType);
          auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * damageScale);
          applyDamage(*u, scaledDamage, projectile.attacker, paralyzer); });

        // Apply damage to flying units
        for (const auto& flyingUnitId : flyingUnitsSet)
        {
            const auto& unit = getUnitState(flyingUnitId);

            // skip units that are dying or dead
            if (!unit.isAlive())
            {
                continue;
            }

            // check if the unit is in range
            auto unitDistanceSquared = createBoundingBox(unit).distanceSquared(position);
            if (unitDistanceSquared > radiusSquared)
            {
                continue;
            }

            // apply appropriate damage
            auto damageScale = blastDamageScale(rweSqrt(unitDistanceSquared), radius, projectile.edgeEffectiveness);
            auto rawDamage = projectile.getDamage(unit.unitType);
            auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * damageScale);
            applyDamage(flyingUnitId, scaledDamage, projectile.attacker, paralyzer);
        }
    }

    void GameSimulation::doProjectileImpact(const Projectile& projectile, ImpactType impactType)
    {
        applyDamageInRadius(projectile.position, projectile.damageRadius, projectile);

        if (auto it = weaponDefinitions.find(projectile.weaponType); it != weaponDefinitions.end() && it->second.fireStarter > 0)
        {
            tryIgniteFeaturesInRadius(projectile.position, std::max(projectile.damageRadius, 16_ss), it->second.fireStarter);
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

            // remove if it's time to die
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
                    projectile.velocity.y -= 112_ss / (30_ss * 30_ss);
                },
                [&](const ProjectilePhysicsTypeBomb&) {
                    // Bombs follow the same gravity model as ballistic
                    // projectiles. Their initial velocity is inherited from
                    // the aircraft at release time; gravity does the rest.
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

            auto collisionInfo = checkProjectileCollision(*this, projectile);
            if (collisionInfo)
            {
                match(
                    *collisionInfo,
                    [&](const ProjectileCollisionInfoOutOfBounds&) {
                        // silently remove projectiles that go outside the map
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::OutOfBounds});
                    },
                    [&](const ProjectileCollisionInfoSea&) {
                        doProjectileImpact(projectile, ImpactType::Water);
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
                            projectile.isDead = true;
                            events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::NormalImpact});
                        }
                    },
                    [&](const ProjectileCollisionInfoUnitOrFeatureOrBuilding&) {
                        doProjectileImpact(projectile, ImpactType::Normal);
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

            killUnit(p.first);
        }
    }

    void GameSimulation::processVictoryCondition()
    {
        // if a commander died this frame, kill the player that owns it
        for (const auto& p : units)
        {
            const auto& unitDefinition = unitDefinitions.at(p.second.unitType);
            if (unitDefinition.commander && p.second.isDead())
            {
                killPlayer(p.second.owner);
            }
        }
    }

    void GameSimulation::updateWind()
    {
        if (gameTime >= nextWindSpeedChange)
        {
            // the wind speed will last between 5 and 14 seconds before changing
            std::uniform_int_distribution<int> durationDist(5, 14);
            nextWindSpeedChange = gameTime + GameTime(durationDist(rng) * SimTicksPerSecond);

            // the new wind speed is taken from a uniform distribution between the min and max speeds
            std::uniform_int_distribution<int> speedDist(minWindSpeed, std::max(minWindSpeed, maxWindSpeed));
            auto currentWindSpeed = speedDist(rng);

            // the new wind direction is a random angle
            std::uniform_int_distribution<int> directionDist(MinAngle.value, MaxAngle.value);
            auto currentWindDirection = SimAngle(directionDist(rng));

            // A generator gets the wind as a fraction of the speed the game
            // considers a full gale, and no more than all of it however hard
            // the map says the wind blows (TotalA.exe 0x490D5E).
            currentWindGenerationFactor = rweMin(1_ss, SimScalar(currentWindSpeed) / SimScalar(MaxUtilizableWindSpeed));

            UnitBehaviorService(this).updateWind(currentWindGenerationFactor, currentWindDirection);
        }
    }

    void GameSimulation::updateResources()
    {
        // run resource updates once per second
        if (gameTime % GameTime(SimTicksPerSecond) == GameTime(0))
        {
            // recalculate max energy and metal storage
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
                    unit.isSufficientlyPowered = addResourceDelta(unitId, -unitDefinition.energyUse, -unitDefinition.metalUse);

                    if (unit.isSufficientlyPowered)
                    {
                        // extract metal
                        if (unitDefinition.extractsMetal != Metal(0))
                        {
                            auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
                            auto metalValue = metalGrid.accumulate(metalGrid.clipRegion(footprint), 0u, std::plus<>());
                            addResourceDelta(unitId, Energy(0), Metal(metalValue * unitDefinition.extractsMetal.value));
                        }

                        // make metal
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
                    auto cost = moving ? unitDefinition.cloakCostMoving : unitDefinition.cloakCost;

                    // Cloak does not go through the ordinary request-and-settle
                    // path, which pays every consumer a share of whatever there
                    // is and carries the rest as debt. The original checks the
                    // whole cost against the stock on the spot (0x4017CB) and,
                    // when it will not cover it, takes nothing and leaves the
                    // unit visible. Half a cloak is not a thing, and a unit that
                    // cannot afford one should not be dragging the player into
                    // debt for it either.
                    if (getPlayer(unit.owner).energy >= cost)
                    {
                        addResourceDelta(unitId, -cost, Metal(0));
                        unit.cloaked = true;
                    }
                    else
                    {
                        unit.cloaked = false;
                    }
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

                player.metalProductionBuffer = Metal(player.metalProductionBuffer.value * bonus);
                player.energyProductionBuffer = Energy(player.energyProductionBuffer.value * bonus);

                auto metalSupply = player.metal + player.metalProductionBuffer;
                auto energySupply = player.energy + player.energyProductionBuffer;

                auto metalSettlement = settleResourcePool(metalSupply.value, metalDebt.value, metalRequested.value);
                auto energySettlement = settleResourcePool(energySupply.value, energyDebt.value, energyRequested.value);

                player.metal = Metal(metalSettlement.remaining);
                player.energy = Energy(energySettlement.remaining);
                player.metalStalled = metalSettlement.stalled;
                player.energyStalled = energySettlement.stalled;

                if (player.metal > player.maxMetal)
                {
                    player.metal = player.maxMetal;
                }

                if (player.energy > player.maxEnergy)
                {
                    player.energy = player.maxEnergy;
                }

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

                player.metalDebt = Metal(player.metalRequestBuffer.value * (1.0f - metalSettlement.requestFraction) + player.metalDebt.value * (1.0f - metalSettlement.debtFraction));
                player.energyDebt = Energy(player.energyRequestBuffer.value * (1.0f - energySettlement.requestFraction) + player.energyDebt.value * (1.0f - energySettlement.debtFraction));
                player.metalRequestBuffer = Metal(0);
                player.energyRequestBuffer = Energy(0);

                player.previousMetalProductionBuffer = player.metalProductionBuffer;
                player.previousEnergyProductionBuffer = player.energyProductionBuffer;
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
    };

    void GameSimulation::trySpawnFeature(const std::string& featureType, const SimVector& position, SimAngle rotation)
    {
        auto featureId = tryGetFeatureDefinitionId(featureType);
        if (!featureId)
        {
            // A unit whose corpse feature is missing from the game data simply
            // leaves no wreck; that is not worth crashing over.
            return;
        }
        auto feature = MapFeature{*featureId, position, rotation};

        addFeature(std::move(feature));
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
                corpsesToSpawn.push_back(CorpseSpawnInfo{
                    unitDefinition.corpse,
                    unit.position,
                    unit.rotation});
            }

            auto footprintRect = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
            auto footprintRegion = occupiedGrid.tryToRegion(footprintRect);
            assert(!!footprintRegion);
            if (unit.carriedBy)
            {
                // It died in a transport's grip: it holds no ground to give back.
            }
            else if (unitDefinition.isMobile)
            {
                if (isFlying(unit.physics))
                {
                    flyingUnitsSet.erase(it->first);
                }
                else
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

            it = units.erase(it);
        }

        for (const auto& spawnInfo : corpsesToSpawn)
        {
            trySpawnFeature(spawnInfo.featureName, spawnInfo.position, spawnInfo.rotation);
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
                    std::uniform_int_distribution<int> twist(0, newUnitDefinition.buildAngle.value - 1);
                    spawnRotation = SimAngle(static_cast<uint16_t>(twist(rng))) - SimAngle(newUnitDefinition.buildAngle.value / 2);
                }

                auto newUnitId = trySpawnUnit(s->unitType, s->owner, s->position, spawnRotation);
                if (!newUnitId)
                {
                    LOG_INFO << "Could not place " << s->unitType << " at " << s->position.x.value << "," << s->position.z.value << " for player " << s->owner.value << "; the build order is dropped";
                    s->status = UnitCreationStatusFailed();
                    continue;
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

                auto newUnitId = trySpawnUnit(s->unitType, s->owner, s->position, s->rotation);
                if (!newUnitId)
                {
                    s->status = UnitCreationStatusFailed();
                    continue;
                }

                s->status = UnitCreationStatusDone{*newUnitId};
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

    void GameSimulation::tick()
    {
        gameTime += GameTime(1);

        // AI runs first so any commands it emits this tick can be drained
        // by GameScene before unit behaviour runs next tick. This mirrors
        // the human input pipeline: human commands are processed via
        // PlayerCommandService at the *start* of tryTickGame, and AI
        // commands take the same channel.
        runAiControllers();

        updateWind();

        updateResources();

        pathFindingService.update(*this);

        // run unit scripts
        for (auto& entry : units)
        {
            auto unitId = entry.first;
            auto& unit = entry.second;

            UnitBehaviorService(this).update(unitId);

            for (auto& piece : unit.pieces)
            {
                piece.update(SimScalar(SimMillisecondsPerTick) / 1000_ss);
            }

            runUnitCobScripts(*this, unitId);
        }

        updateCarriedUnits();

        updateSelfRepair();

        updateSelfDestructs();

        updateProjectiles();

        updateBurningFeatures();

        updateFeatureRegrowth();

        processVictoryCondition();

        deleteDeadUnits();

        deleteDeadProjectiles();

        spawnNewUnits();

        updateCloakSuppression();

        updateVisibility();
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
