#pragma once

#include <optional>
#include <rwe/pathfinding/PathFindingService.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/UnitWeapon.h>
#include <string>
#include <utility>

namespace rwe
{
    class GameScene;

    class UnitBehaviorService
    {
    private:
        GameSimulation* const sim;

    public:
        UnitBehaviorService(GameSimulation* sim);

        void onCreate(UnitId unitId);

        void updateWind(SimScalar windSpeed, SimAngle windDirection);

        void update(UnitId unitId);

        /**
         * Abandons whatever the unit is in the middle of (building, reclaiming),
         * running its StopBuilding script, so a new order starts from idle.
         */
        void interruptCurrentTask(UnitId unitId);

        // FIXME: shouldn't really be public
        SimVector getSweetSpot(UnitId id);
        std::optional<SimVector> tryGetSweetSpot(UnitId id);

    private:
        /** Returns true if the order has been completed. */
        bool handleOrder(UnitInfo unitInfo, const UnitOrder& moveOrder);

        /** Returns true if the order has been completed. */
        bool handleMoveOrder(UnitInfo unitInfo, const MoveOrder& moveOrder);

        /** Returns true if the order has been completed. */
        bool handleAttackOrder(UnitInfo unitInfo, const AttackOrder& attackOrder);

        /** Returns true if the order has been completed. */
        bool handleBuildOrder(UnitInfo unitInfo, const BuildOrder& buildOrder);

        /** Returns true if the order has been completed. */
        bool handleBuggerOffOrder(UnitInfo unitInfo, const BuggerOffOrder& buggerOffOrder);

        /** Returns true if the order has been completed. */
        bool handleCompleteBuildOrder(UnitInfo unitInfo, const CompleteBuildOrder& buildOrder);

        bool handleGuardOrder(UnitInfo unitInfo, const GuardOrder& guardOrder);

        bool handleReclaimOrder(UnitInfo unitInfo, const ReclaimOrder& reclaimOrder);

        bool handleRepairOrder(UnitInfo unitInfo, const RepairOrder& repairOrder);

        bool handlePatrolOrder(UnitInfo unitInfo, const PatrolOrder& patrolOrder);

        bool handleCaptureOrder(UnitInfo unitInfo, const CaptureOrder& captureOrder);

        bool handleLoadOrder(UnitInfo unitInfo, const LoadOrder& loadOrder);

        bool handleUnloadOrder(UnitInfo unitInfo, const UnloadOrder& unloadOrder);

        /** Steers an air unit to a point that may be below cruise height; true once it hovers there. */
        bool hoverTowards(UnitInfo unitInfo, const SimVector& point);

        /**
         * Starts an aircraft on one of the original's idle circuits, on a
         * bearing picked at random as it does. Arming it is separate from
         * flying it because the attack path has to arm the circuit on the tick
         * the target dies, when the aircraft may still be mid-run.
         */
        void beginAirLoiter(UnitInfo unitInfo, UnitState::AirLoiterState::Reason reason, const SimVector& anchor);

        /**
         * Flies one tick of the circuit around the given point, arming it
         * first if the aircraft is not already on one for this reason. The
         * bearing steps back by stepBase plus up to an eighth of a turn each
         * time the aircraft reaches its station.
         */
        void flyAirLoiterCircuit(UnitInfo unitInfo, UnitState::AirLoiterState::Reason reason, const SimVector& anchor, SimAngle stepBase);

        /** How far out an aircraft holds on one of those circuits. */
        SimScalar airLoiterRadius(UnitInfo unitInfo) const;

        /**
         * Gets a construction aircraft ready to work on something at the given
         * position: it takes off if it is sitting on the ground (the fabricator
         * only reaches from the air), breaks off a landing, and once airborne
         * flies the work pattern — a spell over the centre, then dwelling at
         * each of eight points on a ring of the given radius around the job,
         * clockwise, its body slowly turned to face the centre. Returns false
         * while it is still getting into position for the first time, so the
         * caller waits.
         *
         * Ground builders are always ready and it returns true for them.
         */
        bool prepareBuilderForWork(UnitInfo unitInfo, const SimVector& workPosition);

        /** Whether the builder's arm reaches the target's footprint from where it stands. */
        bool withinBuildReach(UnitInfo unitInfo, const UnitState& target) const;

        /** Whether this weapon can hit the unit at all (water weapons only reach units in the water). */
        bool weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& target) const;

        /**
         * What the unit's given weapon picks to shoot at of its own accord,
         * out of everything it can reach and its owner can see. Nothing if
         * there is no legal target.
         */
        std::optional<UnitId> chooseTarget(UnitId id, unsigned int weaponIndex);

        bool captureExistingUnit(UnitInfo unitInfo, UnitId targetUnitId);

        bool deployCaptureArm(UnitInfo unitInfo, UnitId targetUnitId);

        /** Nearest living enemy unit within range of the unit's primary weapon, if any. */
        std::optional<UnitId> findEnemyInWeaponRange(UnitInfo unitInfo) const;

        bool handleBuild(UnitInfo unitInfo, const std::string& unitType);

        void clearBuild(UnitInfo unitInfo);

        void updateWeapon(UnitId id, unsigned int weaponIndex);

        void updateWeaponStockpile(UnitId id, unsigned int weaponIndex);

        SimVector changeDirectionByRandomAngle(const SimVector& direction, SimAngle maxAngle);

        void tryFireWeapon(UnitId id, unsigned int weaponIndex);

        SimVector getUnitPositionWithCache(UnitState& s, UnitId unitId);

        void updateNavigation(UnitInfo unitInfo);

        void applyUnitSteering(UnitInfo unitInfo);
        void updateUnitRotation(UnitInfo unitInfo);
        void updateUnitSpeed(UnitInfo unitInfo);

        void updateGroundUnitPosition(UnitInfo unitInfo, const UnitPhysicsInfoGround& physics);
        void updateUnitPosition(UnitInfo unitInfo);

        bool tryApplyMovementToPosition(UnitInfo unitInfo, const SimVector& newPosition);

        std::optional<int> runCobQuery(UnitId id, const std::string& name);

        SimVector getAimingPoint(UnitId id, unsigned int weaponIndex);
        SimVector getLocalAimingPoint(UnitId id, unsigned int weaponIndex);

        SimVector getLocalFiringPoint(UnitId id, unsigned int weaponIndex);

        SimVector getNanoPoint(UnitId id);

        SimVector getPieceLocalPosition(UnitId id, unsigned int pieceId);
        SimVector getPiecePosition(UnitId id, unsigned int pieceId);

        SimAngle getPieceXZRotation(UnitId id, unsigned int pieceId);

        struct BuildPieceInfo
        {
            SimVector position;
            SimAngle rotation;
        };

        BuildPieceInfo getBuildPieceInfo(UnitId id);

        std::optional<SimVector> getTargetPosition(const UnitWeaponAttackTarget& target);

        PathDestination resolvePathDestination(UnitState& s, const MovingStateGoal& goal);

        void groundUnitMoveTo(UnitInfo unitInfo, const MovingStateGoal& goal);

        bool flyingUnitMoveTo(UnitInfo unitInfo, const MovingStateGoal& goal);

        bool navigateTo(UnitInfo unitInfo, const NavigationGoal& goal);

        void moveTo(UnitInfo unitInfo, const MovingStateGoal& goal);

        bool attackTarget(UnitInfo unitInfo, const AttackTarget& target);

        /**
         * Aircraft-specific attack target handler. Drives the AirMovementStateAttackRun
         * state machine: Approaching -> Engaging -> Departing -> (loop back or terminate).
         * Returns true when the order is satisfied and the unit should drop the order.
         */
        bool attackTargetAir(UnitInfo unitInfo, const AttackTarget& target);

        /**
         * Gunship attack handler, for the two units in the original data with
         * HoverAttack set. Closes to weapon range, then holds a ring at two
         * thirds of that range and shuttles 45 degrees back and forth around
         * it with the gun on the target.
         */
        bool hoverAttackTarget(UnitInfo unitInfo, const AttackTarget& target, const SimVector& targetPosition, SimScalar weaponMaxRange);

        /** Where the gunship goes next, and the bookkeeping that decides it. */
        SimVector nextHoverAttackStation(UnitInfo unitInfo, AirMovementStateHoverAttack& hover, const SimVector& targetPosition, SimScalar radius, SimScalar weaponMaxRange);

        bool buildUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position);

        bool reclaimTarget(UnitInfo unitInfo, std::variant<UnitId, FeatureId> target);

        UnitCreationStatus createNewUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position);

        bool buildExistingUnit(UnitInfo unitInfo, UnitId targetUnitId);

        void changeState(UnitState& unit, const UnitBehaviorState& newState);

        bool deployBuildArm(UnitInfo unitInfo, UnitId targetUnitId);

        bool deployReclaimArm(UnitInfo unitInfo, std::variant<UnitId, FeatureId> target);

        bool repairExistingUnit(UnitInfo unitInfo, UnitId targetUnitId);

        bool deployRepairArm(UnitInfo unitInfo, UnitId targetUnitId);

        bool climbToCruiseAltitude(UnitInfo unitInfo);

        bool descendToGroundLevel(UnitInfo unitInfo);

        void transitionFromGroundToAir(UnitInfo unitInfo);
        bool tryTransitionFromAirToGround(UnitInfo unitInfo);

        bool flyTowardsGoal(UnitInfo unitInfo, const MovingStateGoal& goal);
    };
}
