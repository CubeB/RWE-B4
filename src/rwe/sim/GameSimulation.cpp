#include "GameSimulation.h"
#include <algorithm>
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
    bool GamePlayerInfo::addResourceDelta(const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal)
    {
        recordDesire(apparentEnergy);
        recordDesire(apparentMetal);

        // Spending is checked against what is actually on hand right now, so
        // when the stockpile is empty work carries on at the rate income
        // arrives instead of stopping for a whole second and then bursting.
        auto energyOk = canAfford(actualEnergy);
        auto metalOk = canAfford(actualMetal);
        if (!energyOk)
        {
            energyStalled = true;
        }
        if (!metalOk)
        {
            metalStalled = true;
        }
        if (!energyOk || !metalOk)
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

    bool GamePlayerInfo::canAfford(const rwe::Energy& delta) const
    {
        if (delta >= Energy(0))
        {
            return true;
        }
        auto available = energy + energyProductionBuffer - actualEnergyConsumptionBuffer;
        return available + delta >= Energy(0);
    }

    bool GamePlayerInfo::canAfford(const rwe::Metal& delta) const
    {
        if (delta >= Metal(0))
        {
            return true;
        }
        auto available = metal + metalProductionBuffer - actualMetalConsumptionBuffer;
        return available + delta >= Metal(0);
    }

    void GamePlayerInfo::acceptResource(const rwe::Energy& energy)
    {
        if (energy >= Energy(0))
        {
            energyProductionBuffer += energy;
        }
        else
        {
            actualEnergyConsumptionBuffer -= energy;
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
            actualMetalConsumptionBuffer -= metal;
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

        // Features are placed at full health. TA keeps a feature's hit points in
        // its `damage` key; features that omit it (most vegetation) come out of
        // the TDF reader with 1, so any hit at all destroys them.
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
        // Using *current* rather than maximum hit points means softening a rock up
        // with a few shots genuinely speeds up salvaging it, and a corpse that has
        // been shelled since it fell is quicker to clear than a fresh one. The
        // payout is unaffected: reclaimFeature always hands over the full
        // metal/energy, spread across whatever work total applies.
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
        // Total work shrinks if the feature is shot while it is being reclaimed.
        // Clamping the progress already made keeps the credit below from going
        // negative; the worst case is that the last sliver of value is not paid
        // out, never that a player is paid twice for the same feature.
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

    void GameSimulation::applyDamageToFeature(FeatureId featureId, unsigned int damagePoints)
    {
        if (damagePoints == 0)
        {
            // A shot that lands too far away to do anything must not flatten
            // the zero-hit-point scenery it happens to reach.
            return;
        }

        auto featureRef = tryGetFeature(featureId);
        if (!featureRef)
        {
            return;
        }

        auto& feature = featureRef->get();
        const auto& featureDefinition = getFeatureDefinition(feature.featureName);

        if (featureDefinition.indestructible)
        {
            return;
        }

        if (feature.hitPoints > damagePoints)
        {
            feature.hitPoints -= damagePoints;
            return;
        }

        // Out of hit points: fall back to the wreck of a wreck, or vanish.
        replaceFeature(featureId, featureDefinition.featureDead);
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

    Point GameSimulation::visionCellAt(const SimVector& position) const
    {
        auto tile = terrain.worldToHeightmapCoordinate(position);
        auto cells = PlayerVisibility::VisionCellSizeInTiles;
        // Floor division so that positions just off the map's edge stay outside the grid.
        auto x = tile.x >= 0 ? tile.x / cells : -1;
        auto y = tile.y >= 0 ? tile.y / cells : -1;
        return Point(x, y);
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
        return playerVisibility.at(player.value).isOnRadar(visionCellAt(position));
    }

    bool GameSimulation::canSeeUnit(PlayerId viewer, UnitId unitId) const
    {
        const auto& unit = getUnitState(unitId);
        return unit.isOwnedBy(viewer) || isVisibleTo(viewer, unit.position);
    }

    bool GameSimulation::canDetectUnit(PlayerId viewer, UnitId unitId) const
    {
        const auto& unit = getUnitState(unitId);
        return unit.isOwnedBy(viewer) || isVisibleTo(viewer, unit.position) || isOnRadarOf(viewer, unit.position);
    }

    void GameSimulation::updateVisibility()
    {
        for (auto& v : playerVisibility)
        {
            v.clearCurrent();
        }

        // World units per vision cell; sight and radar ranges are in world units.
        auto cellWorldUnits = static_cast<unsigned int>(simScalarToUInt(MapTerrain::HeightTileWidthInWorldUnits)) * PlayerVisibility::VisionCellSizeInTiles;
        auto toCells = [&](unsigned int worldDistance) {
            return static_cast<int>((worldDistance + cellWorldUnits - 1) / cellWorldUnits);
        };

        for (const auto& [unitId, unit] : units)
        {
            if (unit.isDead())
            {
                continue;
            }
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
            auto& vis = playerVisibility.at(unit.owner.value);
            auto cell = visionCellAt(unit.position);

            if (unitDefinition.sightDistance > 0)
            {
                // Eyes sit a little above the unit; ground is seen if the line
                // to a point just above it is not blocked by higher ground.
                vis.revealCircleWithLineOfSight(cell, toCells(unitDefinition.sightDistance), visionHeights, EyeHeightAboveGround, SightTargetHeightAboveGround);
            }
            else
            {
                // Even a blind unit knows where it is standing.
                vis.revealCircle(cell, 0);
            }

            // Radar needs the unit switched on if it can be switched at all.
            auto radarActive = !unitDefinition.onOffable || unit.activated;
            if (unitDefinition.radarDistance > 0 && radarActive && !unit.isBeingBuilt(unitDefinition))
            {
                vis.radarCircle(cell, toCells(unitDefinition.radarDistance));
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

    Projectile GameSimulation::createProjectileFromWeapon(
        PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker, std::optional<SimVector> inheritedVelocity)
    {
        return createProjectileFromWeapon(owner, weapon.weaponType, position, direction, distanceToTarget, targetUnit, attacker, inheritedVelocity);
    }

    Projectile GameSimulation::createProjectileFromWeapon(PlayerId owner, const std::string& weaponType, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker, std::optional<SimVector> inheritedVelocity)
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

        return projectile;
    }

    void GameSimulation::spawnProjectile(PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker, std::optional<SimVector> inheritedVelocity)
    {
        projectiles.emplace(createProjectileFromWeapon(owner, weapon, position, direction, distanceToTarget, targetUnit, attacker, inheritedVelocity));
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

        unit.addEnergyDelta(apparentEnergy);
        unit.addMetalDelta(apparentMetal);
        return player.addResourceDelta(apparentEnergy, apparentMetal, actualEnergy, actualMetal);
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

    void GameSimulation::applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker)
    {
        auto& unit = getUnitState(unitId);
        if (unit.hitPoints <= damagePoints)
        {
            const auto& unitDefinition = unitDefinitions.at(unit.unitType);
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

        std::unordered_set<UnitId> seenUnits;

        // Features hit by the blast, gathered during the sweep and damaged
        // afterwards: destroying one edits the occupancy grid (and may place a
        // featureDead in the same cells) which we must not do mid-traversal.
        //
        // Weapon damage is listed per unit armour category and features belong to
        // none of them, so they take the weapon's DEFAULT damage -- the same number
        // a unit with no category-specific entry would take. A weapon that declares
        // no DEFAULT has no number that applies to scenery, so it leaves it alone
        // (rather than throwing, which is what Projectile::getDamage would do).
        std::vector<FeatureId> seenFeatures;
        auto defaultDamageIt = projectile.damage.find("DEFAULT");
        auto rawFeatureDamage = defaultDamageIt == projectile.damage.end() ? 0u : defaultDamageIt->second;

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

          // check if a unit (or feature) is there
          auto occupiedType = occupiedGrid.get(coords);

          if (auto f = occupiedType.featureId; rawFeatureDamage > 0 && f && std::find(seenFeatures.begin(), seenFeatures.end(), *f) == seenFeatures.end())
          {
              seenFeatures.push_back(*f);
          }

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
          auto damageScale = std::clamp(1_ss - (rweSqrt(unitDistanceSquared) / radius), 0_ss, 1_ss);
          auto rawDamage = projectile.getDamage(unit.unitType);
          auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * damageScale);
          applyDamage(*u, scaledDamage, projectile.attacker); });

        // Apply damage to features caught in the blast, with the same linear
        // falloff over damageRadius that units get.
        //
        // Note on hitDensity: it does *not* scale incoming damage. Checked against
        // the extracted TA data: hitdensity=0 appears on 179 features and every one
        // of them is blocking=0 (smudges, steam vents), while every corpse and
        // every rock is hitdensity=100 and foliage sits at 5-10. It tracks how
        // solid a thing is for collision purposes, not how much damage it absorbs;
        // treating it as a damage multiplier would make blocking=0 scenery
        // invulnerable rather than transparent. Left out deliberately.
        for (auto featureId : seenFeatures)
        {
            auto featureRef = tryGetFeature(featureId);
            if (!featureRef)
            {
                continue;
            }
            const auto& feature = featureRef->get();
            const auto& featureDefinition = getFeatureDefinition(feature.featureName);
            if (featureDefinition.indestructible)
            {
                continue;
            }

            // Measure to the feature's footprint rather than its centre, so a
            // blast landing on the edge of a big rock hurts it properly.
            Rectangle2x<SimScalar> featureRectangle(
                Vector2x<SimScalar>(feature.position.x, feature.position.z),
                Vector2x<SimScalar>(
                    (intToSimScalar(featureDefinition.footprintX) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss,
                    (intToSimScalar(featureDefinition.footprintZ) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));
            auto featureDistanceSquared = featureRectangle.distanceSquared(Vector2x<SimScalar>(position.x, position.z));
            if (featureDistanceSquared > radiusSquared)
            {
                continue;
            }

            auto damageScale = radius <= 0_ss
                ? 1_ss
                : std::clamp(1_ss - (rweSqrt(featureDistanceSquared) / radius), 0_ss, 1_ss);
            applyDamageToFeature(featureId, simScalarToUInt(SimScalar(rawFeatureDamage) * damageScale));
        }

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
            auto damageScale = std::clamp(1_ss - (rweSqrt(unitDistanceSquared) / radius), 0_ss, 1_ss);
            auto rawDamage = projectile.getDamage(unit.unitType);
            auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * damageScale);
            applyDamage(flyingUnitId, scaledDamage, projectile.attacker);
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
                });

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
            std::uniform_int_distribution<int> speedDist(minWindSpeed, maxWindSpeed);
            auto currentWindSpeed = speedDist(rng);

            // the new wind direction is a random angle
            std::uniform_int_distribution<int> directionDist(MinAngle.value, MaxAngle.value);
            auto currentWindDirection = SimAngle(directionDist(rng));

            currentWindGenerationFactor = SimScalar(currentWindSpeed) / SimScalar(MaxUtilizableWindSpeed);

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

            for (Index i = 0; i < getSize(players); ++i)
            {
                auto& player = players[i];
                // Brutal computer players get a little extra for every unit of income.
                auto bonus = resourceBonusFor(PlayerId(i));
                player.metal += Metal(player.metalProductionBuffer.value * bonus);
                player.metalProductionBuffer = Metal(0);
                player.energy += Energy(player.energyProductionBuffer.value * bonus);
                player.energyProductionBuffer = Energy(0);

                if (player.metal > Metal(0))
                {
                    player.metal -= player.actualMetalConsumptionBuffer;
                    player.actualMetalConsumptionBuffer = Metal(0);
                    player.metalStalled = false;
                }
                else
                {
                    player.metalStalled = true;
                }

                player.previousDesiredMetalConsumptionBuffer = player.desiredMetalConsumptionBuffer;
                player.desiredMetalConsumptionBuffer = Metal(0);

                if (player.energy > Energy(0))
                {
                    player.energy -= player.actualEnergyConsumptionBuffer;
                    player.actualEnergyConsumptionBuffer = Energy(0);
                    player.energyStalled = false;
                }
                else
                {
                    player.energyStalled = true;
                }

                player.previousDesiredEnergyConsumptionBuffer = player.desiredEnergyConsumptionBuffer;
                player.desiredEnergyConsumptionBuffer = Energy(0);

                if (player.metal > player.maxMetal)
                {
                    player.metal = player.maxMetal;
                }

                if (player.energy > player.maxEnergy)
                {
                    player.energy = player.maxEnergy;
                }
            }

            for (auto& entry : units)
            {
                const auto& unitId = entry.first;
                auto& unit = entry.second;
                const auto& unitDefinition = unitDefinitions.at(unit.unitType);

                unit.resetResourceBuffers();

                if (!unit.isBeingBuilt(unitDefinition))
                {
                    addResourceDelta(unitId, unitDefinition.energyMake, unitDefinition.metalMake);
                }

                if (unit.activated)
                {
                    if (unitDefinition.windGenerator != Energy(0))
                    {
                        // generate energy from wind
                        addResourceDelta(unitId, unitDefinition.windGenerator * currentWindGenerationFactor, Metal(0));
                    }

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

                    unit.isSufficientlyPowered = addResourceDelta(unitId, -unitDefinition.energyUse, -unitDefinition.metalUse);
                }
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

                // Buildings go up with a slight random twist, up to five
                // degrees either way, so a base looks placed by hand rather
                // than stamped out on a grid. Factories are left square:
                // their exit pads and roll-off assume the stock facing.
                std::optional<SimAngle> spawnRotation;
                const auto& newUnitDefinition = unitDefinitions.at(s->unitType);
                if (!newUnitDefinition.isMobile && !newUnitDefinition.builder)
                {
                    // Five degrees is 1/72 of a turn.
                    const int fiveDegrees = 65536 / 72;
                    std::uniform_int_distribution<int> twist(-fiveDegrees, fiveDegrees);
                    spawnRotation = SimAngle(static_cast<uint16_t>(twist(rng)));
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

        updateSelfDestructs();

        updateProjectiles();

        updateBurningFeatures();

        processVictoryCondition();

        deleteDeadUnits();

        deleteDeadProjectiles();

        spawnNewUnits();

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
