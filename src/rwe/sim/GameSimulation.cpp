#include "GameSimulation.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <random>
#include <rwe/ai/AiController.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/FeatureBlast.h>
#include <rwe/sim/FactoryConstruction.h>
#include <rwe/sim/GameHash_util.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/MissionScripts.h>
#include <rwe/sim/MixedOwnership.h>
#include <rwe/sim/ResourceSharing.h>
#include <rwe/sim/SimRandom.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/SimulationOwnership.h>
#include <rwe/sim/TaLiveSenderHooks.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/movement.h>
#include <rwe/sim/sim_prof.h>
#include <rwe/sim/util.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/collection_util.h>
#include <rwe/util/match.h>
#include <rwe/util/rwe_string.h>
#include <type_traits>
#include <unordered_set>

namespace rwe
{
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

        /** `push 0xf` before the timer call at 0x4028A2. */
        constexpr unsigned int FactoryPadRetryTicks = 15;

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
            if (cause == "acid_water")
            {
                // 0x48AF32 hands cause 11 to the damage choke point.
                return 11;
            }
            if (cause == "quit")
            {
                // A peer leaving: TA's own quit death, cause 8.
                return 8;
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

        /** A unit has died with this cause and corpse level; both the demo recorder and the mixed-ownership outbox want it. */
        void notifyUnitDied(
            GameSimulation& sim,
            UnitId unitId,
            std::optional<UnitId> killer,
            unsigned int severity,
            unsigned int cause,
            unsigned int corpseLevel)
        {
            if (sim.demoRecorder)
            {
                sim.demoRecorder->unitDied(sim, unitId, killer, severity, cause, corpseLevel);
            }
            recordLocalDeath(sim, unitId, killer, severity, cause, corpseLevel);
        }

        /**
         * Whether a round belongs to a Remote player, and so is a record of
         * what that machine decided rather than something to resolve here: the
         * damage reaches a Local unit as its owner's 0x0b, and nothing it hits
         * on this side of the world changes until the owner's records say so.
         * A round naming no player this game has is nobody's.
         */
        bool isRemoteRound(const GameSimulation& sim, const Projectile& projectile)
        {
            return projectile.owner.value < sim.players.size() && !simulatesLocally(sim, projectile.owner);
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

    // Out-of-line because `aiControllers` holds unique_ptr<AiController>
    // and AiController is forward-declared in the header. Move-assign
    // is intentionally absent (declared `=delete` in the header) because the
    // simulation has const wind-speed members.
    GameSimulation::~GameSimulation() = default;
    GameSimulation::GameSimulation(GameSimulation&&) noexcept = default;

    void GameSimulation::attachDemoRecorder(std::unique_ptr<DemoRecorder> recorder)
    {
        demoRecorder = std::move(recorder);
    }

    void GameSimulation::setTaLiveSender(TaLiveSenderHooks hooks)
    {
        taLiveSender = hooks;
    }

    void GameSimulation::addAiController(PlayerId playerId, std::unique_ptr<AiController> controller)
    {
        // A player whose units another machine decides is never given one.
        if (!simulatesLocally(*this, playerId))
        {
            return;
        }

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
                    if (canBeBuiltAt(mc, std::nullopt, false, static_cast<unsigned int>(x), static_cast<unsigned int>(y), 0u))
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
        newPosition.y = computeGroundUnitHeight(terrain, unitDefinition, newPosition.x, newPosition.z);
        return UnloadSpot{*spot, newPosition};
    }

    bool GameSimulation::dropCarriedUnitInPlace(UnitId transportId, UnitId unitId)
    {
        auto transportRef = tryGetUnitState(transportId);
        auto unitRef = tryGetUnitState(unitId);
        if (!transportRef || !unitRef || unitRef->get().carriedBy != transportId || !unitRef->get().isAlive())
        {
            return false;
        }
        auto& transport = transportRef->get();
        auto& unit = unitRef->get();

        // The drop-legality test (0x47DB70, mode 1) at the passenger's own
        // cell: occupancy, then water depth and slope for its movement
        // class. No ring search and no nearest-fit; a refusal changes
        // nothing and the script tries again (BT-75).
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        auto mc = getAdHocMovementClass(unitDefinition.movementCollisionInfo);
        auto footprint = computeFootprintRegion(unit.position, unitDefinition.movementCollisionInfo);
        auto region = occupiedGrid.tryToRegion(footprint);
        if (!region || isCollisionAt(*region) || !isGridPointWalkable(terrain, mc, static_cast<unsigned int>(footprint.x), static_cast<unsigned int>(footprint.y)))
        {
            return false;
        }

        occupiedGrid.forEach(*region, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
        unit.occupancyStamp = gameTime;

        unit.position.y = computeGroundUnitHeight(terrain, unitDefinition, unit.position.x, unit.position.z);
        unit.previousPosition = unit.position;
        unit.rotation = transport.rotation;
        unit.previousRotation = transport.rotation;
        unit.carriedBy = std::nullopt;
        unit.carriedPiece.clear();
        transport.carriedUnits.erase(std::remove(transport.carriedUnits.begin(), transport.carriedUnits.end(), unitId), transport.carriedUnits.end());
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

        auto spot = findUnloadSpot(unitId, position);
        if (!spot)
        {
            return false;
        }

        if (auto region = occupiedGrid.tryToRegion(spot->footprint))
        {
            occupiedGrid.forEach(*region, [unitId](auto& cell) { cell.mobileUnitId = unitId; });
        }
        unit.occupancyStamp = gameTime;

        unit.position = spot->position;
        unit.previousPosition = spot->position;
        unit.rotation = transport.rotation;
        unit.previousRotation = transport.rotation;
        unit.carriedBy = std::nullopt;
        unit.carriedPiece.clear();
        transport.carriedUnits.erase(std::remove(transport.carriedUnits.begin(), transport.carriedUnits.end(), unitId), transport.carriedUnits.end());
        return true;
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

        // A Remote player's unit is the owner's to damage; a local builder
        // cannot take it apart. Report the step finished so the order does
        // not spin on a target it can never consume.
        if (!simulatesLocally(*this, unit.owner))
        {
            return true;
        }

        // The bite comes straight off the hit points. It does not go through
        // applyDamage: that path ends in killUnit, a wreck and an explosion,
        // and a reclaimed unit leaves none of those. Whether the original's
        // cause-5 damage skips armour the way its cause-10 repair does is
        // not read; it is applied bare here.
        // Reclaiming is damage in the original, cause 5 through the same
        // applier (0x489CE0), so it tells a watching WAITFORATTACK too.
        if (missionScripts)
        {
            missionScripts->unitDamaged(targetId);
        }

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
        if (missionRules)
        {
            missionRules->unitDying(*this, targetId, unit.owner);
        }
        unit.markAsDeadNoCorpse();
        cancelFactoryProductOnDeath(*this, targetId);
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
        // Cause 5, reclaimed: it leaves nothing, and the corpus reads
        // severity 0 and level 0 on every cause-5 death.
        notifyUnitDied(*this, targetId, std::nullopt, 0, 5, 0);
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

        if (taLiveSender.attached())
        {
            taLiveSender.unitCaptured(taLiveSender.context, *this, targetId, captor);
        }

        // The original's owner change is an event and then a cause-4 death of
        // the old unit, the captor's copy already made, and the mission rules
        // hear both: CaptureUnitType the first, and every kill rule the second.
        if (missionRules)
        {
            missionRules->unitChangingOwner(*this, targetId);
        }

        unit.owner = captor;

        if (missionRules)
        {
            missionRules->unitDying(*this, targetId, previousOwner);
        }

        // The captor's copy is a new unit: selectable like every new one
        // (0x485B61), with no list and no mission Immunity (the spawner is
        // what sets that, 0x488475).
        unit.heldByMission = false;
        unit.immune = false;
        if (missionScripts)
        {
            missionScripts->unitChangedOwner(targetId);
        }

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

        // BT-73 (0x488570). An owner change is not an owner swap: the
        // original builds a new unit for the new owner and copies across the
        // health, build-left, angles, stockpiles and activation mask, and
        // nothing else -- not the kill count, not the order list, not the
        // standing postures. The creation routine's postures are then cleared
        // outright (flag bits 18 to 21): Hold Position and Hold Fire, for any
        // recipient simulated on this machine, human or computer. A captured
        // veteran is therefore a recruit that stands still and does not shoot
        // until told to. (The kill reset rests on the field being absent from
        // the copy list: medium confidence.)
        //
        // The unit is changed in place, so everything above is what the
        // rebuild amounts to for state that is hashed. Two things are not
        // reproduced: the unit keeps its id, and its script keeps running
        // where a new unit would start a fresh instance.
        unit.kills = 0;
        if (simulatesLocally(*this, captor))
        {
            unit.moveOrders = UnitMovementOrders::HoldPosition;
            unit.fireOrders = UnitFireOrders::HoldFire;
        }

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
        if (unit.isDead())
        {
            return;
        }

        if (missionRules)
        {
            missionRules->unitDying(*this, unitId, unit.owner);
        }

        // Self-destruction leaves nothing to reclaim.
        unit.markAsDeadNoCorpse();
        cancelFactoryProductOnDeath(*this, unitId);
        unit.selfDestructTime = std::nullopt;
        getPlayer(unit.owner).unitsLost += 1;

        // The owner is filled in like any other death: the scene needs it to
        // decide whether the blast is one the local player is entitled to
        // see, and by the time it reads the event the unit is gone.
        recordUnitDeath(*this, unitId, "self_destruct", std::nullopt);
        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, UnitDiedEvent::DeathType::SelfDestructed, unit.owner});
        // Cause 3, self-destruct: RWE leaves nothing to reclaim, so the
        // record is the level-0 shape the corpus gives the skipped-script
        // causes rather than the wreck the original's Killed might pick.
        notifyUnitDied(*this, unitId, std::nullopt, 0, 3, 0);

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
            if (!simulatesLocally(*this, unit.owner))
            {
                continue;
            }

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

            if (!simulatesLocally(*this, unit.owner))
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

            // TOTALA-EXE-ECONOMY S:134: decay returns metal, without truncation.
            // Income receives the owner's handicap at the ordinary settle.
            auto removed = std::min(step, unit.buildTimeCompleted);
            if (unitDefinition.buildTime > 0)
            {
                addResourceDelta(entry.first, Energy(0), Metal(unitDefinition.buildCostMetal.value * static_cast<float>(removed) / static_cast<float>(unitDefinition.buildTime)));
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


    std::vector<int> startPositionsForSlots(const std::vector<std::size_t>& filledSlots, bool keepSeatNumbers)
    {
        std::vector<int> positions;
        positions.reserve(filledSlots.size());
        for (std::size_t k = 0; k < filledSlots.size(); ++k)
        {
            positions.push_back(static_cast<int>(keepSeatNumbers ? filledSlots[k] + 1 : k + 1));
        }
        return positions;
    }

    WinStatus GameSimulation::computeWinStatus() const
    {
        // A mission is decided by its own rules and nothing else: a victory
        // is P0's, a defeat P1's.
        if (missionRules)
        {
            if (!missionRules->outcome)
            {
                return WinStatusUndecided();
            }
            const auto& winner = *missionRules->outcome == MissionOutcome::Victory ? missionRules->human : missionRules->computer;
            if (!winner)
            {
                return WinStatusNoSurvivors();
            }
            return WinStatusWon{*winner};
        }

        // #699 supplement: no allied victory during a respawn wait. RWE
        // deliberately offers the same endless mode in skirmish too.
        if (commanderDeathMode == CommanderDeathMode::Deathmatch)
        {
            return WinStatusUndecided();
        }

        // Corrected BT-89 supplement: multiplayer does not remain in an
        // AI-only match after every participating human is eliminated.
        if (!skirmishVictoryRules)
        {
            bool hadHuman = false;
            bool livingHuman = false;
            for (const auto& player : players)
            {
                if (player.type == GamePlayerType::Human)
                {
                    hadHuman = hadHuman || player.hasCreatedUnits;
                    livingHuman = livingHuman || player.status == GamePlayerStatus::Alive;
                }
            }
            if (hadHuman && !livingHuman)
            {
                return WinStatusNoSurvivors{};
            }
        }

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
        //
        // A seat that never held a unit (BT-193): in a multiplayer game it
        // blocks victory, being alive, until it has; in a skirmish it
        // contributes nothing. The skirmish rule needs somebody to have
        // fielded something first, so that the first seconds of a game whose
        // units are still being placed are not read as everyone being out.
        const auto skipsNeverFielded = skirmishVictoryRules
            && std::any_of(players.begin(), players.end(), [](const GamePlayerInfo& q) { return q.hasCreatedUnits; });
        std::optional<PlayerId> livingPlayer;
        std::optional<int> livingTeam;
        for (Index i = 0; i < getSize(players); ++i)
        {
            const auto& p = players[i];

            if (p.status != GamePlayerStatus::Alive)
            {
                continue;
            }

            if (skipsNeverFielded && !p.hasCreatedUnits)
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

        return WinStatusNoSurvivors();
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
        // Script and event only on a real change (BT-66).
        if (unit.activate())
        {
            events.push_back(UnitActivatedEvent{unitId});
        }
    }

    void GameSimulation::deactivateUnit(UnitId unitId)
    {
        auto& unit = getUnitState(unitId);
        if (unit.deactivate())
        {
            events.push_back(UnitDeactivatedEvent{unitId});
        }
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

    namespace
    {
        /**
         * The interceptor's distance measure (0x49B106, 0x49A664): each axis
         * of the 16.16 difference is cut to its whole-unit part (an arithmetic
         * shift, so it floors) before it is squared, and the three squares are
         * added. Cutting before squaring makes the result up to one whole unit
         * of each axis smaller than the true squared distance. Compared
         * strictly against the full `areaofeffect` squared.
         */
        SimScalar wholeUnitDistanceSquared(const SimVector& a, const SimVector& b)
        {
            auto whole = [](SimScalar v) { return SimScalar(std::floor(v.value)); };
            auto dx = whole(a.x - b.x);
            auto dy = whole(a.y - b.y);
            auto dz = whole(a.z - b.z);
            return dx * dx + dy * dy + dz * dz;
        }
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
            if (wholeUnitDistanceSquared(position, entry.second.position) < radius * radius)
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
        if (getUnitState(unitId).isDead())
        {
            return;
        }
        recordUnitDeath(*this, unitId, "unfinished", std::nullopt);
        quietlyKillUnit(unitId, false);
        // Cause 9, an unfinished unit removed: no script, no corpse.
        notifyUnitDied(*this, unitId, std::nullopt, 0, 9, 0);
    }

    void GameSimulation::quietlyKillUnit(UnitId unitId, bool countAsLoss)
    {
        auto& unit = getUnitState(unitId);
        // A unit dies once. The original's death handler turns away one that
        // is not alive (0x486706) before it does anything else, and this and
        // the other ways a unit is killed do the same: a unit killed earlier
        // in the tick is still in the map until deleteDeadUnits, and a second
        // death would count a second loss and raise a second event.
        if (unit.isDead())
        {
            return;
        }
        if (missionRules)
        {
            missionRules->unitDying(*this, unitId, unit.owner);
        }
        unit.markAsDeadNoCorpse();
        cancelFactoryProductOnDeath(*this, unitId);
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
                    mc.maxWaterSlope / 2,
                    mc.minWaterDepthPresent};
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

    bool projectileCollides(const GameSimulation& sim, const Projectile& projectile, const OccupiedCell& cellValue, bool unitsOnly, std::optional<UnitId>& struckUnit)
    {
        if (cellValue.mobileUnitId)
        {
            const auto& unit = sim.getUnitState(*cellValue.mobileUnitId);

            // BT-253: a unit of the projectile's own side in the cell does not
            // end the test. The building and feature slots of the same cell
            // are still asked, so a round fired over friendly ground units
            // still strikes the building or wreck beneath them.
            if (!unit.isOwnedBy(projectile.owner))
            {
                const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
                const auto& modelDefinition = sim.unitModelDefinitions.at(unitDefinition.objectName);

                // BT-253: only the top is tested. A round below the unit's
                // origin (a shot into a pit, or past a unit standing high on a
                // slope) is still inside it. The flying-unit test keeps both
                // bounds.
                if (projectile.position.y > unit.position.y + modelDefinition.height)
                {
                    return false;
                }

                struckUnit = *cellValue.mobileUnitId;
                return true;
            }
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

            // BT-253: the top only, as for a mobile unit above.
            if (projectile.position.y > unit.position.y + modelDefinition.height)
            {
                return false;
            }

            struckUnit = cellValue.buildingInfo->unit;
            return true;
        }

        // `unitsonly` is tested between the unit slots and the feature, so a
        // round carrying it passes through a wreck as it passes through the
        // ground and the sea (NAVAL-CONSTRUCTION-DGUN-FINDINGS.md, "Water
        // detonations do not consume this projectile").
        if (cellValue.featureId && !unitsOnly)
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
        /**
         * The unit that was struck, when it was one. The detonation routine
         * is handed it for the two unit-slot hits and nothing for a feature
         * (0x49B090), and it decides both the single-target damage path
         * (BT-112) and whether the water art is used (BT-9).
         */
        std::optional<UnitId> struckUnit;
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

        // The original's stop tests run in a fixed order (0x49B090): the
        // square's units, then `unitsonly`, then the ground at 0x49B36D, then
        // the sea at 0x49B3A1. So a round that dips below sea level over a
        // wading unit's footprint hits the unit rather than the water, and one
        // that passes both surfaces in a tick over a wet square has struck the
        // ground -- which a `groundbounce` round bounces off (issue #350).
        // Which art a detonation throws up is decided by the square, not by
        // this order; see isSquareUnderSea.
        auto weaponIt = simulation.weaponDefinitions.find(projectile.weaponType);
        auto unitsOnly = weaponIt != simulation.weaponDefinitions.end() && weaponIt->second.unitsOnly;

        auto heightMapPos = simulation.terrain.worldToHeightmapCoordinate(projectile.position);
        auto cellValue = simulation.occupiedGrid.tryGet(heightMapPos);
        std::optional<UnitId> struckUnit;
        if (cellValue && projectileCollides(simulation, projectile, cellValue->get(), unitsOnly, struckUnit))
        {
            return ProjectileCollisionInfoUnitOrFeatureOrBuilding{struckUnit};
        }

        for (auto unitId : simulation.flyingUnitsSet)
        {
            if (projectileCollidesWithUnit(simulation, projectile, unitId))
            {
                return ProjectileCollisionInfoUnitOrFeatureOrBuilding{unitId};
            }
        }

        // `unitsonly` sits between the last unit test and the first surface
        // test (0x49B294), and a round carrying it comes straight back out
        // here. It does not go on to test the ground or the sea, so a Mind Gun
        // round flies through a hillside and through the water and only ever
        // detonates on something alive. Nothing else about it changes: it
        // still explodes on a unit, still hurts one, and still dies when its
        // time or its range runs out.
        if (unitsOnly)
        {
            return std::nullopt;
        }

        if (projectile.position.y <= *terrainHeight)
        {
            return ProjectileCollisionInfoTerrain();
        }

        auto seaLevel = simulation.terrain.getSeaLevel();

        bool waterWeapon = weaponIt != simulation.weaponDefinitions.end() && weaponIt->second.waterWeapon;
        // Strictly below: a round exactly at sea level does not trigger the
        // sea test (NAVAL-CONSTRUCTION-DGUN-FINDINGS.md).
        if (!waterWeapon && !simulation.noSeaLevelTrigger && seaLevel > *terrainHeight && projectile.position.y < seaLevel)
        {
            return ProjectileCollisionInfoSea();
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

    int computeKilledSeverity(unsigned int overkill, unsigned int maxHitPoints, unsigned int previousHealthPercent)
    {
        // 0x486578-0x4865AB: the overkill percentage is an unsigned division
        // of 100 * the deficit by the maximum health, the severity is the
        // floor of half its sum with unit+0xF7, and the result is raised to 1
        // and cut to 100. Both terms are non-negative, so the halving is an
        // ordinary floor.
        auto scaled = maxHitPoints == 0
            ? std::uint64_t(0)
            : (std::uint64_t(100) * overkill) / maxHitPoints;
        auto severity = (scaled + previousHealthPercent) / 2u;
        return static_cast<int>(std::clamp<std::uint64_t>(severity, 1u, 100u));
    }

    namespace
    {
        /**
         * Once a second the original's unit pass copies unit+0xF6 into
         * unit+0xF7 and then stores the unit's health percentage in unit+0xF6
         * (0x48AE71-0x48AEAF), on every tick whose number is a multiple of
         * thirty. RWE numbers its first tick 1, as the original does.
         *
         * The sample in unit+0xF7 is the second term of the kill severity, so
         * a kill reads the health the unit had thirty to fifty-nine ticks
         * earlier: a unit at full health for the last second that is flattened
         * by one blow has 100 there whatever the overkill.
         */
        constexpr unsigned int HealthSampleInterval = 30;

        void sampleUnitHealth(GameSimulation& sim)
        {
            if (sim.gameTime.value % HealthSampleInterval != 0)
            {
                return;
            }

            for (auto& entry : sim.units)
            {
                auto& unit = entry.second;
                if (unit.isDead())
                {
                    continue;
                }

                const auto maxHitPoints = sim.unitDefinitions.at(unit.unitType).maxHitPoints;
                unit.previousHealthPercentSample = unit.healthPercentSample;
                unit.healthPercentSample = maxHitPoints == 0
                    ? 0u
                    : static_cast<unsigned int>(std::min<std::uint64_t>(100u, (std::uint64_t(100) * unit.hitPoints) / maxHitPoints));
            }
        }
    }

    void GameSimulation::killUnit(UnitId unitId, std::optional<UnitId> attacker)
    {
        // Nothing measured the blow: a scuttling, a transport going down with
        // its cargo, a script asking to die. The original's own severity
        // arithmetic degenerates to its floor for these, and the shipped
        // ladders read that as the intact wreck.
        killUnitWithSeverity(unitId, attacker, 0u, false);
    }

    void GameSimulation::killUnit(UnitId unitId, std::optional<UnitId> attacker, unsigned int overkill)
    {
        killUnitWithSeverity(unitId, attacker, overkill, true);
    }

    void GameSimulation::killUnitWithSeverity(UnitId unitId, std::optional<UnitId> attacker, unsigned int overkill, bool measured)
    {
        auto& unit = getUnitState(unitId);
        const auto& unitDefinition = unitDefinitions.at(unit.unitType);
        if (unit.isDead())
        {
            return;
        }

        if (missionRules)
        {
            missionRules->unitDying(*this, unitId, unit.owner);
        }
        unit.markAsDead();
        cancelFactoryProductOnDeath(*this, unitId);
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

        // An unfinished unit sets off nothing: the death weapon is skipped
        // while the build-left fraction is non-zero (0x486D2F-0x486D40,
        // BT-45), and the scene's blast and screen shake hang off the event
        // type, so it reports a plain deletion. Its pieces still fly (below).
        const bool unfinished = unit.isBeingBuilt(unitDefinition);
        auto deathType = unfinished
            ? UnitDiedEvent::DeathType::Deleted
            : (unit.position.y < terrain.getSeaLevel() ? UnitDiedEvent::DeathType::WaterExploded : UnitDiedEvent::DeathType::NormalExploded);
        events.push_back(UnitDiedEvent{unitId, unit.unitType, unit.position, deathType, unit.owner, killerOwner});

        // The severity is `clamp(1, 100, (100*overkill/maxdamage + X) / 2)`,
        // where X is `unit+0xF7`: the unit's health percentage as sampled
        // every thirtieth tick, one sample back (BT-43,
        // BYTE-TACTICS-SPEC-ROOM-FINDINGS.md; sampleUnitHealth). A kill
        // that no blow measured -- a scuttling, a transport going down --
        // has no such second term here and keeps the floor of 1; the
        // original queries no script at all for a unit that still has
        // health, which this does not yet reproduce.
        // What the script does with the number is its own business: a
        // shipped `Killed` is a three-band ladder that picks a corpse
        // level from it and throws a different amount of the unit about
        // on the way.
        const int severity = computeKilledSeverity(
            overkill,
            unitDefinition.maxHitPoints,
            measured ? unit.previousHealthPercentSample : 0u);

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

        if (!unitDefinition.explodeAs.empty() && !unfinished)
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
        notifyUnitDied(
            *this,
            unitId,
            attacker,
            static_cast<unsigned int>(severity),
            demoDeathCause(*this, unitId),
            demoCorpseLevel(unit));
    }

    void GameSimulation::updateWaterDamage()
    {
        // Both map values must be set, and it is the same tick for every unit:
        // the game's tick counter a multiple of thirty (0x48AEDF-0x48AF04).
        if (waterDamage <= 0 || gameTime.value % static_cast<unsigned int>(SimTicksPerSecond) != 0)
        {
            return;
        }

        auto seaLevel = terrain.getSeaLevel();
        std::vector<UnitId> inTheSea;
        for (const auto& [unitId, unit] : units)
        {
            if (unit.isDead())
            {
                continue;
            }

            // A hovercraft rides over it (canhover, bit 12 of def+0x241).
            if (unitDefinitions.at(unit.unitType).canHover)
            {
                continue;
            }

            // The integer part of its height, the high word of the 16.16 y,
            // at or below sea level: ships, submarines and anything wading.
            // An aircraft over the water is above it.
            if (SimScalar(std::floor(unit.position.y.value)) > seaLevel)
            {
                continue;
            }

            inTheSea.push_back(unitId);
        }

        // 0x489BB0(no attacker, unit, waterdamage, cause 11), after the scan,
        // so that nothing the damage sets off changes who is in the sea.
        for (auto unitId : inTheSea)
        {
            if (!getUnitState(unitId).isDead())
            {
                applyDamage(unitId, static_cast<unsigned int>(waterDamage), std::nullopt, false, std::nullopt, "acid_water");
            }
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

    std::pair<int, int> computeHitByWeaponArguments(SimAngle direction)
    {
        // Only the high byte of the angle is kept (0x499CD0), so the direction
        // has 256 steps, and the sine table holds 8192 for 1.0 (0x509F00).
        // The cosine is the sine a quarter turn on.
        auto step = SimAngle(static_cast<std::uint16_t>(direction.value & 0xFF00u));
        auto tableValue = [](SimAngle a) {
            return static_cast<std::int64_t>(std::lround(sin(a).value * 8192.0f));
        };
        auto scaled = [](std::int64_t t) {
            // Times 400 over 8192, rounded to nearest; the shift is a floor,
            // so half a unit is added first.
            return static_cast<int>((t * 400 + 4096) >> 13);
        };
        return {scaled(tableValue(step + QuarterTurn)), scaled(tableValue(step))};
    }

    int computeTakeDamagePercent(unsigned int hitPoints, unsigned int maxHitPoints)
    {
        if (maxHitPoints == 0)
        {
            return 0;
        }
        return static_cast<int>(std::min<std::uint64_t>(100u, (std::uint64_t(100) * hitPoints) / maxHitPoints));
    }

    void GameSimulation::applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker, bool paralyzer, std::optional<PlayerId> sourceOwner, const char* deathCause, bool reportedByOwner, std::optional<SimVector> burstPosition)
    {
        // Damage to a Remote player's unit is the owner's to report. When this
        // machine ran the hit, its attacker side shows the damage and records a
        // 0x0b for the owner, but it never kills: only the owner's 0x0c, and
        // the full-state record that overwrites the health, settle the unit.
        // Damage that arrived from the stream goes through applyRemoteDamage.
        if (!simulatesLocally(*this, getUnitState(unitId).owner))
        {
            applyLocalDamageToRemoteUnit(*this, unitId, attacker, damagePoints, paralyzer, sourceOwner);
            return;
        }

        // What a Remote player's things do to one of ours is that owner's to
        // report (docs/TA-NETWORK.md, "Damage and death"), and it reaches us as
        // a 0x0b applied through applyIncomingDamage. Resolving it here as well
        // would count it twice, and it is what stands between a puppet's own
        // explosions and its display rounds and our side of the world. A source
        // that names no player is nobody's round and lands as it always did.
        auto remoteSource = sourceOwner && sourceOwner->value < players.size() && !simulatesLocally(*this, *sourceOwner);
        if (remoteSource && !reportedByOwner)
        {
            return;
        }

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

        // The same handler's first act is to tell whoever is watching the
        // unit that it was hit (0x406F89), whoever the attacker, which is
        // what a mission unit's WAITFORATTACK is waiting for.
        if (missionScripts && !getUnitState(unitId).isDead())
        {
            missionScripts->unitDamaged(unitId);
        }

        if (attacker)
        {
            // Who hit it last, and when: an escort of this unit reacts to it
            // (unit+0xF0, BT-98).
            auto& hit = getUnitState(unitId);
            hit.lastAttacker = *attacker;
            hit.lastAttackedAt = gameTime;

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
            // BT-49: a lethal burn credits the previous attacker without
            // treating that unit as this hit's source or applying its bonus.
            auto creditedAttacker = attacker;
            if (!creditedAttacker && std::string_view(deathCause) == "feature_burn")
            {
                creditedAttacker = unit.lastAttacker;
            }
            recordUnitDeath(*this, unitId, deathCause, creditedAttacker);

            // What the blow had left over once the unit's remaining hit
            // points were paid for. The original works the severity out of
            // exactly this.
            //
            // A unit still under construction goes the same way (BT-45): it
            // is death cause 1, an ordinary weapon kill, so the owner takes
            // the loss and the Killed script runs, its pieces flying off as
            // debris. What the unfinished state withholds is the corpse, the
            // death weapon's blast and the attacker's credit, and
            // killUnitWithSeverity says where each of those three goes.
            killUnit(unitId, creditedAttacker, damagePoints - unit.hitPoints);
        }
        else
        {
            unit.hitPoints -= damagePoints;

            // A hit that leaves the unit standing tells its script (0x489CE0,
            // BT-54), for ordinary weapon damage only: HitByWeapon with the
            // direction the burst came from, then TakeDamage with the health
            // left. A solar collector's and a radar tower's HitByWeapon
            // switch the unit off for eight seconds, so this is economy as
            // well as animation. A script without the function costs nothing.
            if (burstPosition && unit.cobEnvironment)
            {
                auto toBurst = *burstPosition - unit.position;
                auto bearing = UnitState::toRotation(SimVector(toBurst.x, 0_ss, toBurst.z)) - unit.rotation;
                auto [cosine, sine] = computeHitByWeaponArguments(bearing);
                unit.cobEnvironment->createThread("HitByWeapon", {cosine, sine});
                unit.cobEnvironment->createThread("TakeDamage", {computeTakeDamagePercent(unit.hitPoints, unitDefinition.maxHitPoints)});
            }
        }
    }

    namespace
    {
        /**
         * The largest blast that takes the single-target path. The original
         * checks AreaOfEffect against sixteen (0x49A049) and, at or under it
         * AND with a unit that was struck (0x49A053), hurts that unit alone
         * at scale 1.0. Our radius is already half the AreaOfEffect, so the
         * same test is eight.
         *
         * Forty-three of the hundred and thirty-six shipped weapons are
         * inside it, including the twenty-eight at AreaOfEffect eight -- the
         * machine guns and light lasers that do most of the shooting. A round
         * of one of them that strikes nothing (a miss into the ground, a
         * death blast) is NOT exempt from the falloff: it goes to the area
         * routine with this radius like any other blast (0x49A0A7, BT-112).
         */
        const SimScalar SmallestSpreadingBlastRadius = 8_ss;

        /**
         * How many units and features the area routine remembers (the
         * compares at 0x49A28F and 0x49A5FA). An object already in its list
         * is skipped. One met once the list is full is not recorded but is
         * still processed, so it is hit again for every further map square it
         * covers inside the blast (BT-112).
         */
        constexpr std::size_t BlastUnitListCapacity = 20;

        /** True when the id is already listed; otherwise lists it if there is room and says false. */
        template <typename Id>
        bool alreadyListedOrRecord(std::vector<Id>& list, const Id& id, std::size_t capacity)
        {
            if (std::find(list.begin(), list.end(), id) != list.end())
            {
                return true;
            }
            if (list.size() < capacity)
            {
                list.push_back(id);
            }
            return false;
        }

        /**
         * The original's blast falloff (TotalA.exe 0x49A3B6). The curve is
         * quadratic rather than linear, and it lands on EdgeEffectiveness at
         * the rim instead of on zero, so a weapon that names an edge value
         * keeps most of its bite right out to the edge of the blast while an
         * ordinary one drops away far faster than a straight line would.
         */
        SimScalar blastDamageScale(SimScalar distance, SimScalar radius, SimScalar edgeEffectiveness)
        {
            auto t = std::clamp(1_ss - (distance / radius), 0_ss, 1_ss);
            return (t * t * (1_ss - edgeEffectiveness)) + edgeEffectiveness;
        }

        /**
         * The scale a unit takes from a blast, or nothing if the blast does not
         * reach it. The distance the original compares and curves is a whole
         * number of world units, the integer half of its 16.16 distance to the
         * unit's box (0x49A397), so it is floored here before either use, and
         * it has to be strictly less than the radius: a unit at the radius
         * takes nothing even from a weapon whose edge effectiveness would give
         * the rim some damage. NAVAL-CONSTRUCTION-DGUN-FINDINGS.md, "Splash
         * can hurt ships and submarines". Nothing here looks at the water.
         */
        std::optional<SimScalar> blastDamageScaleForUnit(SimScalar distanceSquared, SimScalar radius, SimScalar edgeEffectiveness)
        {
            auto wholeDistance = SimScalar(std::floor(rweSqrt(distanceSquared).value));
            if (wholeDistance >= radius)
            {
                return std::nullopt;
            }

            return blastDamageScale(wholeDistance, radius, edgeEffectiveness);
        }
    }

    void GameSimulation::applyDamageInRadius(const SimVector& position, SimScalar radius, const Projectile& projectile, const char* deathCause)
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

        std::vector<UnitId> seenUnits;
        std::vector<FeatureId> seenFeatures;

        // Blasts hurt features too: force-attacking a wreck field to clear a
        // lane is a standing part of play, and controlling one is an economy
        // in itself. A feature's own `damage` key is its hit points; a feature
        // blown to nothing breaks down to its featureDead form the way a
        // burnt one does. `damagesFeatures` is a mod's switch to exempt a
        // weapon; the shipped data sets it on nothing.
        auto damagesFeatures = weaponIt == weaponDefinitions.end() || weaponIt->second.damagesFeatures;

        // A Remote player's round takes nothing off a feature either: the wreck
        // it breaks up is one its owner's machine decided, and this copy is
        // corrected by the records that owner sends.
        auto remoteSource = isRemoteRound(*this, projectile);

        const auto featureWindow = featureBlastWindow(terrain, position, radius, occupiedGrid.getWidth(), occupiedGrid.getHeight());
        const auto left = std::min(minCell.x, featureWindow.left());
        const auto top = std::min(minCell.y, featureWindow.top());
        const auto right = std::max(maxCell.x + 1, featureWindow.right());
        const auto bottom = std::max(maxCell.y + 1, featureWindow.bottom());
        const auto region = GridRegion(left, top, right - left, bottom - top);

        region.forEach([&](const auto& coords) {
          // BT-256/257: feature reach has its own window and no cell-rectangle
          // rejection. Preserve existing feature-before-unit ordering (BT-260
          // records the adjacent, still-unported original ordering difference).
          if (damagesFeatures && !paralyzer && !remoteSource && featureWindow.contains(Point(coords.x, coords.y)))
          {
              applyFeatureBlastCell(*this, Point(coords.x, coords.y), position, radius, projectile, seenFeatures);
          }

          // The union must not widen the existing unit sweep or its eligibility.
          if (coords.x < minCell.x || coords.x > maxCell.x || coords.y < minCell.y || coords.y > maxCell.y)
          {
              return;
          }
          const auto cellCenter = terrain.heightmapIndexToWorldCenter(coords.x, coords.y);
          const Rectangle2x<SimScalar> cellRectangle(
              Vector2x<SimScalar>(cellCenter.x, cellCenter.z),
              Vector2x<SimScalar>(MapTerrain::HeightTileWidthInWorldUnits / 2_ss, MapTerrain::HeightTileHeightInWorldUnits / 2_ss));
          if (cellRectangle.distanceSquared(Vector2x<SimScalar>(position.x, position.z)) > radiusSquared)
          {
              return;
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

          if (alreadyListedOrRecord(seenUnits, *u, BlastUnitListCapacity))
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

          auto damageScale = blastDamageScaleForUnit(unitDistanceSquared, radius, projectile.edgeEffectiveness);
          if (!damageScale)
          {
              return;
          }
          auto rawDamage = projectile.getDamage(unit.unitType);
          auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * *damageScale);
          applyDamage(*u, scaledDamage, projectile.attacker, paralyzer, projectile.owner, deathCause, false, projectile.position); });

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

            auto damageScale = blastDamageScaleForUnit(unitDistanceSquared, radius, projectile.edgeEffectiveness);
            if (!damageScale)
            {
                continue;
            }
            auto rawDamage = projectile.getDamage(unit.unitType);
            auto scaledDamage = simScalarToUInt(SimScalar(rawDamage) * *damageScale);
            applyDamage(flyingUnitId, scaledDamage, projectile.attacker, paralyzer, projectile.owner, deathCause, false, projectile.position);
        }
    }

    void GameSimulation::doProjectileImpact(const Projectile& projectile, ImpactType /*impactType*/, std::optional<ProjectileId> projectileId, std::optional<UnitId> struckUnit)
    {
        // A Remote player's round sets nothing off here: not our units, which
        // its owner's 0x0b accounts for, and not a feature, which is the fire
        // starter's to ignite.
        if (!isRemoteRound(*this, projectile))
        {
            if (struckUnit && projectile.damageRadius <= SmallestSpreadingBlastRadius)
            {
                // areaofeffect 16 or less AND a unit that was struck (0x49A049,
                // 0x49A053): one hit at scale 1.0 on that unit and nothing
                // else, neighbours and features included. A round with no
                // unit, or a bigger blast, goes through the area routine.
                // BT-112.
                auto weaponIt = weaponDefinitions.find(projectile.weaponType);
                auto paralyzer = weaponIt != weaponDefinitions.end() && weaponIt->second.paralyzer;
                if (auto struck = tryGetUnitState(*struckUnit); struck && !struck->get().isDead())
                {
                    applyDamage(*struckUnit, projectile.getDamage(struck->get().unitType), projectile.attacker, paralyzer, projectile.owner, "weapon", false, projectile.position);
                }
            }
            else
            {
                applyDamageInRadius(projectile.position, projectile.damageRadius, projectile);
            }

        }

        // The piece that actually kills a nuke. An interceptor's warhead is not
        // aimed at the missile so much as detonated near it, and what does the
        // damage is that the blast takes every projectile inside it with it
        // (0x49A664) -- which is also why one anti-nuke can clear a salvo.
        if (auto it = weaponDefinitions.find(projectile.weaponType); it != weaponDefinitions.end() && it->second.interceptor)
        {
            // The full areaofeffect, not the halved radius the unit-damage
            // routine uses (0x49A664; BT-111): damageRadius is half of it.
            detonateProjectilesInBlast(projectileId, projectile.position, projectile.damageRadius * 2_ss);
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

        // With nothing live to follow, a cruise weapon steers at its stored
        // destination with the height set to the higher of the ground there
        // and sea level (0x49B3E0, ground read at 0x485070), so the final
        // approach comes down to the surface and not to wherever the point
        // was stored. A weapon that does not cruise gets the point as it is
        // (BT-115).
        if (p.cruise && projectile.targetPosition)
        {
            auto point = *projectile.targetPosition;
            auto ground = terrain.tryGetHeightAt(point.x, point.z).value_or(terrain.getSeaLevel());
            point.y = rweMax(ground, terrain.getSeaLevel());
            return point;
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
            projectile.velocity.y -= gravityPerTick();

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

            // The whole-unit height this round had when its update began, for
            // the water-entry splash at the end of it (BT-14). A local: the
            // comparison is between this update's start and its end, so no
            // state has to persist between ticks.
            const auto wholeHeightAtStart = std::floor(projectile.position.y.value);

            bool detonated = false;
            match(
                weaponDefinition.physicsType,
                [&](const ProjectilePhysicsTypeBallistic&) {
                    // Wind goes onto the position and gravity onto the
                    // velocity, which is the shape of 0x49BD10: the wind is a
                    // displacement the shell never accumulates, so a long
                    // flight drifts linearly rather than curving away.
                    projectile.position += currentWindVector;
                    projectile.velocity.y -= gravityPerTick();
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
                    projectile.velocity.y -= gravityPerTick();
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
            // `jge` at 0x49B1A4 skips at exactly the radius -- and the radius
            // is the full areaofeffect, 96 for every shipped interceptor, in
            // whole units on each axis (BT-111).
            if (projectile.targetProjectile)
            {
                auto target = projectiles.tryGet(*projectile.targetProjectile);
                if (target && !target->get().isDead
                    && wholeUnitDistanceSquared(projectile.position, target->get().position) < (projectile.damageRadius * 2_ss) * (projectile.damageRadius * 2_ss))
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
                // Spray or earth by the square the round went off in -- but
                // only when no unit was struck (0x499EB0, BT-9). The routine
                // plays the water sound and requests the splash when the
                // square is wet AND it was handed no unit; a hull hit over a
                // wet square takes the ordinary hit sound and explosion, and
                // so does anything that is not given a unit's name. A round
                // into the shallows of a square with a dry corner throws up
                // earth.
                const auto* unitHit = std::get_if<ProjectileCollisionInfoUnitOrFeatureOrBuilding>(&*collisionInfo);
                const auto struckAUnit = unitHit != nullptr && unitHit->struckUnit.has_value();
                auto inWater = !struckAUnit && terrain.isSquareUnderSea(projectile.position.x, projectile.position.z);
                auto impactType = inWater ? ImpactType::Water : ImpactType::Normal;
                auto impactDeath = inWater ? ProjectileDiedEvent::DeathType::WaterImpact : ProjectileDiedEvent::DeathType::NormalImpact;
                match(
                    *collisionInfo,
                    [&](const ProjectileCollisionInfoOutOfBounds&) {
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, ProjectileDiedEvent::DeathType::OutOfBounds});
                    },
                    [&](const ProjectileCollisionInfoSea&) {
                        doProjectileImpact(projectile, impactType);
                        if (noExplode)
                        {
                            events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, inWater});
                            return;
                        }
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, impactDeath});
                    },
                    [&](const ProjectileCollisionInfoTerrain&) {
                        if (projectile.groundBounce)
                        {
                            // 0x49B37F: the fall turns into a rise a quarter
                            // as fast, and nothing else changes -- the round
                            // is not put back above the ground, so it can
                            // stay under it a tick or more, bouncing, and
                            // nothing goes off. The original's `vy >> 2` on
                            // 16.16 floors where a quarter here does not;
                            // they differ below 1/65536 of a unit.
                            projectile.velocity.y = -(projectile.velocity.y / 4_ss);
                        }
                        else
                        {
                            doProjectileImpact(projectile, impactType);
                            if (noExplode)
                            {
                                events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, inWater});
                                return;
                            }
                            projectile.isDead = true;
                            events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, impactDeath});
                        }
                    },
                    [&](const ProjectileCollisionInfoUnitOrFeatureOrBuilding& hit) {
                        doProjectileImpact(projectile, impactType, std::nullopt, hit.struckUnit);
                        if (noExplode)
                        {
                            events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, inWater});
                            return;
                        }
                        projectile.isDead = true;
                        events.push_back(ProjectileDiedEvent{id, projectile.weaponType, projectile.position, impactDeath});
                    });
            }

            // The entry splash (the tail of 0x49B720): a round still alive
            // after its collision test whose whole-unit height was strictly
            // above sea level when the update began and is at or below it now,
            // over a square that is under the sea, on a map whose sea trigger
            // is not switched off, throws up the weapon's splash. Art only.
            // A round the crossing step killed gets the impact's art instead,
            // and an ordinary shot that lands exactly on the surface gets this
            // first and its detonation a step later.
            if (!projectile.isDead && !noSeaLevelTrigger)
            {
                const auto seaWhole = std::floor(terrain.getSeaLevel().value);
                if (wholeHeightAtStart > seaWhole
                    && std::floor(projectile.position.y.value) <= seaWhole
                    && terrain.isSquareUnderSea(projectile.position.x, projectile.position.z))
                {
                    events.push_back(ProjectileDetonatedEvent{projectile.weaponType, projectile.position, true, true});
                }
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
        // A mission has no commander rule at all: the campaign setup zeroes
        // the commander-death mode (0x497474 copies g+0x39219, which the
        // mission reader has just set to 0 at 0x4363EA), so the wipe at
        // 0x48667E never runs, and CommanderKilled is the only way a
        // commander's death can matter.
        if (missionRules)
        {
            return;
        }

        if (commanderDeathMode != CommanderDeathMode::GameContinues)
        {
            std::set<PlayerId> wiped;
            // A wipe's secondary blast can kill another commander. Observe
            // those deaths before deleteDeadUnits removes their trigger.
            while (true)
            {
                std::set<PlayerId> pending;
                for (const auto& p : units)
                {
                    const auto& unitDefinition = unitDefinitions.at(p.second.unitType);
                    if (unitDefinition.commander && p.second.isDead() && wiped.count(p.second.owner) == 0)
                    {
                        pending.insert(p.second.owner);
                    }
                }
                if (pending.empty())
                {
                    break;
                }
                for (auto player : pending)
                {
                    wiped.insert(player);
                    wipeCommanderArmy(player);
                }
            }
        }

        // BT-83: defeat is zero live units in every mode. A very tough unit
        // can survive the 30000-damage wipe; losing that last survivor later
        // must still start defeat/respawn. In Continues the commander alone
        // simply never initiates the wipe above.
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

            // The minimum plus a draw below the span (BT-130, 0x4B6C30). The
            // draw is a modulo of the span, so the map's maximum is never
            // reached unless the two bounds are equal, and a span under two
            // draws nothing at all: the routine returns zero without
            // advancing the seed. randomBelow would draw for a span of one.
            const int windSpan = maxWindSpeed - minWindSpeed;
            const int currentWindSpeed = minWindSpeed + (windSpan < 2 ? 0 : static_cast<int>(randomBelow(rng, static_cast<unsigned int>(windSpan))));

            // Only a wind that blows is given a direction (BT-26, 0x490C40):
            // at speed zero no draw is made and the stale direction stays,
            // which the wind generators' SetDirection is still told.
            if (currentWindSpeed != 0)
            {
                currentWindDirection = SimAngle(static_cast<uint16_t>(randomBetween(rng, MinAngle.value, MaxAngle.value)));
            }

            // A generator gets the wind as a fraction of the speed the game
            // considers a full gale, and no more than all of it however hard
            // the map says the wind blows (TotalA.exe 0x490D5E).
            currentWindGenerationFactor = rweMin(1_ss, SimScalar(currentWindSpeed) / SimScalar(MaxUtilizableWindSpeed));

            // The same draw also aims the wind that pushes shells about.
            currentWindVector = computeWindVector(currentWindDirection, currentWindSpeed);

            UnitBehaviorService(this).updateWind(currentWindSpeed, currentWindDirection);
        }
    }


    void GameSimulation::tick()
    {
        gameTime += GameTime(1);
        const auto fireWind = currentWindVector;
        updateDroppedPlayers();

        // AI runs first so any commands it emits this tick can be drained
        // by GameScene before unit behaviour runs next tick. This mirrors
        // the human input pipeline: human commands are processed via
        // PlayerCommandService at the *start* of tryTickGame, and AI
        // commands take the same channel.
        {
            RWE_SIMPROF("ai");
            runAiControllers();
        }

        // Where the original's unit pass takes it, ahead of any kill a unit
        // suffers later in the tick.
        sampleUnitHealth(*this);

        // Before the settle, which the countdown the rules may start this
        // second freezes (0x4650A9 comes before 0x46554F in the same pass).
        if (missionRules)
        {
            missionRules->update(*this);
        }
        else
        {
            // Outside a mission the same pass runs the same countdown over
            // the skirmish win test (BT-82).
            updateEndCountdown();
        }

        updateResources();

        // After the player pass, as the original does it (0x490C40 follows
        // 0x464F80; BT-26): on a tick that is both a settle and a change the
        // settle has used the old fraction. Still ahead of the unit passes,
        // so the scripts it starts run this tick.
        updateWind();

        {
            RWE_SIMPROF("path");
            pathFindingService.update(*this);
        }

        // The three unit passes are timed separately -- each is its own
        // scope because RWE_SIMPROF names its variables, one to a scope.
        // A mission unit's list hands out its next order before the unit
        // looks at its queue.
        if (missionScripts)
        {
            missionScripts->update(*this);
            if (missionScripts->scripts.empty())
            {
                missionScripts.reset();
            }
        }

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

        updateWaterDamage();

        // After the behaviour pass, which is where a builder stakes its claim
        // on the frame it is working on for this period.
        updateNanoframeDecay();

        updateSelfDestructs();

        {
            RWE_SIMPROF("projectiles");
            updateProjectiles();
        }

        updateFeatureAnimations(fireWind);

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

        // Allies' automatic sharing is the last thing in the original's tick
        // (0x457D30; BT-27): the donor's stockpile falls at the end of tick T
        // and the gift reaches the receiver at the next settle, a second
        // later when T is a multiple of sixty. Sizes are taken on the
        // balances as the tick leaves them (ResourceSharing.h).
        runAutomaticSharing(*this);

        // Last, once every phase has left its mark on the tick: the recorder
        // reads the end-of-tick state, which is the state TA's sender would
        // have serialised. Nothing here can change what the tick did.
        if (demoRecorder)
        {
            demoRecorder->endOfTick(*this);
        }

        if (taLiveSender.attached())
        {
            taLiveSender.endOfTick(taLiveSender.context, *this);
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
