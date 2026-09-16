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
        /**
         * Returns true if the order has been completed.
         *
         * The order is handed over by non-const reference because a
         * capture order accumulates its progress in itself, the way the
         * original's mission record does -- see CaptureOrder.
         */
        bool handleOrder(UnitInfo unitInfo, UnitOrder& order);

        /** Returns true if the order has been completed. */
        bool handleMoveOrder(UnitInfo unitInfo, const MoveOrder& moveOrder);

        /** Returns true if the order has been completed. */
        bool handleAttackOrder(UnitInfo unitInfo, AttackOrder& attackOrder);

        /** Returns true if the order has been completed. */
        bool handleBuildOrder(UnitInfo unitInfo, const BuildOrder& buildOrder);

        /** Returns true if the order has been completed. */
        bool handleBuggerOffOrder(UnitInfo unitInfo, const BuggerOffOrder& buggerOffOrder);

        /** Returns true if the order has been completed. */
        bool handleCompleteBuildOrder(UnitInfo unitInfo, const CompleteBuildOrder& buildOrder);

        bool handleGuardOrder(UnitInfo unitInfo, const GuardOrder& guardOrder);

        bool handleReclaimOrder(UnitInfo unitInfo, const ReclaimOrder& reclaimOrder);

        bool handleResurrectOrder(UnitInfo unitInfo, ResurrectOrder& resurrectOrder);

        /**
         * The unit type a corpse came from, or nothing if it did not come
         * from one.
         *
         * A string operation rather than a table (0x404F36): the feature
         * definition's own name truncated at the first underscore, looked up
         * as a unit type. `armsolar_dead` gives `armsolar`. A name with no
         * underscore, or whose prefix is not a unit type, cannot be
         * resurrected -- and `armsolar_heap`, the second-stage wreck, also
         * gives `armsolar`, which is what the code says whether or not it was
         * meant.
         */

        bool handleRepairOrder(UnitInfo unitInfo, const RepairOrder& repairOrder);

        bool handlePatrolOrder(UnitInfo unitInfo, const PatrolOrder& patrolOrder);

        bool handleDgunOrder(UnitInfo unitInfo, const DgunOrder& order);

        bool handleLandOnAirBaseOrder(UnitInfo unitInfo, const LandOnAirBaseOrder& order);

        /**
         * A damaged aircraft part-way through work it is willing to abandon
         * pushes a trip to a repair pad in front of that work, so the work
         * comes back once it is whole again.
         */
        void maybeBreakOffToRepairPad(UnitInfo unitInfo);

        /** Hands steering back from an attack run to ordinary flight, keeping the speed. */
        void dropAirAttackRun(UnitInfo unitInfo);

        /**
         * The nearest thing a construction unit on patrol should clear away,
         * or nothing. This is the original's only automatic reclaim and the
         * only reader of a feature's `autoreclaimable` bit -- see
         * TOTALA-EXE.md §97.
         */
        std::optional<FeatureId> findFeatureToAutoReclaim(UnitInfo unitInfo);

        /**
         * How empty a store has to be before a patrolling builder goes looking
         * for something to reclaim into it: the 0.2 at 0x4FC950.
         */
        static constexpr float AutoReclaimWantedFraction = 0.2f;

        bool handleCaptureOrder(UnitInfo unitInfo, CaptureOrder& captureOrder);

        bool handleLoadOrder(UnitInfo unitInfo, const LoadOrder& loadOrder);

        bool handleUnloadOrder(UnitInfo unitInfo, UnloadOrder& unloadOrder);

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
         * A builder on the ground points itself at the job instead, and is
         * not ready until it has come round far enough -- the turn every one
         * of the original's work missions makes. One with no turn rate at all
         * is ready at once, since it has no way to comply.
         */
        bool prepareBuilderForWork(UnitInfo unitInfo, const SimVector& workPosition);

        /** Whether the builder's arm reaches the target's footprint from where it stands. */
        bool withinBuildReach(UnitInfo unitInfo, const UnitState& target) const;

        /**
         * The same reach against a footprint that need not belong to a unit,
         * so that a feature -- a wreck, a tree -- is measured the same way.
         */
        bool withinBuildReachOfRect(UnitInfo unitInfo, const DiscreteRect& rect) const;

        /** Whether this weapon can hit the unit at all (water weapons only reach units in the water, ordinary ones only reach units out of it). */
        bool weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& attacker, const UnitState& target) const;

        /**
         * The two questions the original's target chooser answers, told apart
         * by its third argument (0x40B7B0). Both run the same search; they
         * differ in how far it looks and in whether NoChaseCategory has a say.
         */
        enum class TargetSearchMode
        {
            /** Argument 1: what an armed weapon may open fire on where it stands. */
            WeaponRange,

            /** Argument 0: whether to break off and go and find a fight. */
            SightDistance,
        };

        /**
         * What the unit's given weapon picks to shoot at of its own accord,
         * out of everything it can reach and its owner can see. Nothing if
         * there is no legal target.
         */
        std::optional<UnitId> chooseTarget(UnitId id, unsigned int weaponIndex, TargetSearchMode mode = TargetSearchMode::WeaponRange);

        bool captureExistingUnit(UnitInfo unitInfo, CaptureOrder& captureOrder);

        bool deployCaptureArm(UnitInfo unitInfo, CaptureOrder& captureOrder);

        /**
         * Whether there is anything worth breaking off for: the choice the
         * primary weapon would make, but over everything inside the unit's own
         * SightDistance rather than its weapon's range, and with
         * NoChaseCategory allowed to rule candidates out.
         */
        std::optional<UnitId> findEnemyToEngage(UnitInfo unitInfo);

        bool handleBuild(UnitInfo unitInfo, const std::string& unitType);

        void clearBuild(UnitInfo unitInfo);

        void updateWeapon(UnitId id, unsigned int weaponIndex);

        void updateWeaponStockpile(UnitId id, unsigned int weaponIndex);

        SimVector changeDirectionByRandomAngle(const SimVector& direction, SimAngle spread);

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

        std::optional<SimVector> getTargetPosition(const AttackTarget& target);

        PathDestination resolvePathDestination(UnitState& s, const MovingStateGoal& goal);

        void groundUnitMoveTo(UnitInfo unitInfo, const MovingStateGoal& goal);

        bool flyingUnitMoveTo(UnitInfo unitInfo, const MovingStateGoal& goal);

        bool navigateTo(UnitInfo unitInfo, const NavigationGoal& goal);

        /** Stows the nanolathe arm once a job has been over for long enough; see UnitState::armStowDueTime. */
        void updatePendingArmStow(UnitInfo unitInfo);

        void moveTo(UnitInfo unitInfo, const MovingStateGoal& goal);

        /**
         * Where a ground attacker should walk while it is out of range of a
         * unit target: not the target's centre, which is what sends a whole
         * group to the same cell and jams the pathfinder's single request
         * queue (issue #66), but a point on the line from the target's
         * centre through the attacker's own position, clamped to just inside
         * weapon range (or to the target's footprint plus a margin, for a
         * short-ranged weapon against something large). Cached on the
         * attacker's own navigation state and only recomputed once the
         * target has actually moved, so the goal stays stable tick to tick
         * while the attacker's exact position keeps moving underneath it --
         * see the .cpp for why that stability matters.
         *
         * A function of the attacker's and target's current positions and
         * the target's definition only, so every peer computes the same
         * point.
         */
        NavigationGoal attackApproachGoal(UnitInfo unitInfo, UnitId targetId, const SimVector& targetPosition, const WeaponDefinition& weaponDefinition) const;

        /**
         * Where an ordered attacker should believe its target is, updating the
         * remembered position as it goes.
         *
         * The original does not track a unit through the fog: an attack order
         * on something that goes out of sight stays on the last position the
         * attacker's owner actually saw it at, and picks the target up again
         * -- moved, if it has moved -- when that ground is visible once more.
         * Tested against the running game; the routine in the exe that does it
         * has not been found, so this is behaviour rather than transcription.
         *
         * canSeeUnit is the question, deliberately, and not canDetectUnit: a
         * radar contact is a blip and not a target, and the radar picture is
         * recomputed for one player a tick, so it could not feed a
         * deterministic decision even if the original wanted it to.
         *
         * Nothing here depends on who is at the keyboard -- it asks about the
         * attacking unit's own owner -- so every peer resolves the same
         * position.
         */
        std::optional<SimVector> resolveAttackTargetPosition(UnitInfo unitInfo, const AttackTarget& target, std::optional<SimVector>& lastSeenPosition);

        bool attackTarget(UnitInfo unitInfo, const AttackTarget& target, std::optional<SimVector>& lastSeenPosition);

        /**
         * Aircraft-specific attack target handler. Drives the AirMovementStateAttackRun
         * state machine: Approaching -> Engaging -> Departing -> (loop back or terminate).
         * Returns true when the order is satisfied and the unit should drop the order.
         */
        bool attackTargetAir(UnitInfo unitInfo, const AttackTarget& target, const std::optional<SimVector>& targetPosition);

        /**
         * Walks a crawling bomb onto its target and detonates it. The original's
         * ATTACK_KAMIKAZE mission handler, 0x403336 / 0x4032B4.
         */
        bool kamikazeRun(UnitInfo unitInfo, const AttackTarget& target, const std::optional<SimVector>& targetPosition);

        /**
         * Works out which speed band the unit is now in and, if it has changed,
         * runs the script the original would. TotalA.exe 0x43DA70.
         */
        void updateMoveRateBand(UnitInfo unitInfo);

        /**
         * Gunship attack handler, for the two units in the original data with
         * HoverAttack set. Closes to weapon range, then holds a ring at two
         * thirds of that range and shuttles 45 degrees back and forth around
         * it with the gun on the target.
         */
        bool hoverAttackTarget(UnitInfo unitInfo, const AttackTarget& target, const SimVector& targetPosition, SimScalar weaponMaxRange);

        /** The original's AirToAir: a fighter against another aircraft. */
        bool dogfightTarget(UnitInfo unitInfo, const AttackTarget& target, UnitId targetId, SimScalar weaponMaxRange);

        /** Where the gunship goes next, and the bookkeeping that decides it. */
        SimVector nextHoverAttackStation(UnitInfo unitInfo, AirMovementStateHoverAttack& hover, const SimVector& targetPosition, SimScalar radius, SimScalar weaponMaxRange);

        bool buildUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position);

        bool reclaimTarget(UnitInfo unitInfo, std::variant<UnitId, FeatureId> target);

        UnitCreationStatus createNewUnit(UnitInfo unitInfo, const std::string& unitType, const SimVector& position);

        bool buildExistingUnit(UnitInfo unitInfo, UnitId targetUnitId, std::optional<UnitId> standNextTo = std::nullopt);

        void changeState(UnitState& unit, const UnitBehaviorState& newState);

        bool deployBuildArm(UnitInfo unitInfo, UnitId targetUnitId);

        bool deployReclaimArm(UnitInfo unitInfo, std::variant<UnitId, FeatureId> target);

        bool repairExistingUnit(UnitInfo unitInfo, UnitId targetUnitId);

        bool deployRepairArm(UnitInfo unitInfo, UnitId targetUnitId);

        bool climbToCruiseAltitude(UnitInfo unitInfo);

        /**
         * Where a descending aircraft is trying to stop. The terrain, unless
         * it is coming down on a repair pad it holds an order for -- then the
         * pad's own `QueryLandingPad` piece, which is twenty world units up on
         * an ARMASP. See TOTALA-EXE.md §94.
         */
        /**
         * Whether this unit's script raises a nanolathe arm at all. None of
         * the shipped air repair pads or carriers does, so nothing may wait on
         * their build stance. See TOTALA-EXE.md §94.
         */
        bool unitHasBuildArm(const UnitState& unit) const;

        /**
         * The pad this aircraft is coming down on and the exact point on it,
         * or nothing if it is landing on open ground. The point is the world
         * position of the pad's own `QueryLandingPad` piece -- twenty world
         * units above the base on an ARMASP. See TOTALA-EXE.md §94.
         */
        std::optional<std::pair<UnitId, SimVector>> airBaseLandingPoint(UnitInfo unitInfo);

        bool descendToGroundLevel(UnitInfo unitInfo);

        void transitionFromGroundToAir(UnitInfo unitInfo);
        bool tryTransitionFromAirToGround(UnitInfo unitInfo, std::optional<UnitId> pad = std::nullopt);

        bool flyTowardsGoal(UnitInfo unitInfo, const MovingStateGoal& goal);
    };
}
