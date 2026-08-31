#pragma once

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/grid/DiscreteRect.h>
#include <rwe/math/Matrix4x.h>
#include <rwe/pathfinding/UnitPath.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimAxis.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitMesh.h>
#include <rwe/sim/UnitMovementOrders.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitWeapon.h>
#include <tuple>
#include <variant>

namespace rwe
{
    struct PathFollowingInfo
    {
        UnitPath path;
        GameTime pathCreationTime;
        std::vector<SimVector>::const_iterator currentWaypoint;
        explicit PathFollowingInfo(UnitPath&& path, GameTime creationTime)
            : path(std::move(path)), pathCreationTime(creationTime), currentWaypoint(this->path.waypoints.begin()) {}
    };

    struct NavigationGoalLandingLocation
    {
    };

    using NavigationGoal = std::variant<UnitId, FeatureId, SimVector, DiscreteRect, NavigationGoalLandingLocation>;

    using MovingStateGoal = std::variant<UnitId, SimVector, DiscreteRect>;

    using PathDestination = std::variant<SimVector, DiscreteRect>;

    struct UnitBehaviorStateIdle
    {
    };

    struct UnitCreationStatusPending
    {
    };

    struct UnitCreationStatusDone
    {
        UnitId unitId;
    };

    struct UnitCreationStatusFailed
    {
    };

    using UnitCreationStatus = std::variant<UnitCreationStatusPending, UnitCreationStatusFailed, UnitCreationStatusDone>;
    struct UnitBehaviorStateCreatingUnit
    {
        std::string unitType;
        PlayerId owner;
        SimVector position;
        UnitCreationStatus status{UnitCreationStatusPending()};
    };

    struct UnitBehaviorStateBuilding
    {
        UnitId targetUnit;
        std::optional<SimVector> nanoParticleOrigin;
    };

    struct UnitBehaviorStateReclaiming
    {
        std::variant<UnitId, FeatureId> target;
        std::optional<SimVector> nanoParticleOrigin;
    };

    using UnitBehaviorState = std::variant<
        UnitBehaviorStateIdle,
        UnitBehaviorStateCreatingUnit,
        UnitBehaviorStateBuilding,
        UnitBehaviorStateReclaiming>;

    struct NavigationStateIdle
    {
    };

    struct NavigationStateMoving
    {
        MovingStateGoal movementGoal;
        PathDestination pathDestination;
        std::optional<PathFollowingInfo> path;
        bool pathRequested;

        /**
         * Set when the goal cannot be reached: the closest reachable point,
         * which then counts as the destination for arrival purposes.
         */
        std::optional<SimVector> reachableDestination;
    };

    struct NavigationStateMovingToLandingSpot
    {
        SimVector landingLocation;
    };

    using NavigationState = std::variant<NavigationStateIdle, NavigationStateMoving, NavigationStateMovingToLandingSpot>;

    struct UnitPositionCache
    {
        UnitId unitId;
        SimVector position;
        GameTime cachedAtTime;
    };

    struct NavigationStateInfo
    {
        std::optional<NavigationGoal> desiredDestination;
        std::optional<UnitPositionCache> unitPositionCache;
        NavigationState state;
    };

    struct FactoryBehaviorStateIdle
    {
    };

    struct FactoryBehaviorStateCreatingUnit
    {
        std::string unitType;
        PlayerId owner;
        SimVector position;
        SimAngle rotation;
        UnitCreationStatus status{UnitCreationStatusPending()};
    };

    struct FactoryBehaviorStateBuilding
    {
        // the vector is the origin of nano particles
        std::optional<std::pair<UnitId, std::optional<SimVector>>> targetUnit;
    };

    using FactoryBehaviorState = std::variant<FactoryBehaviorStateIdle, FactoryBehaviorStateCreatingUnit, FactoryBehaviorStateBuilding>;

    UnitOrder createMoveOrder(const SimVector& destination);

    UnitOrder createAttackOrder(UnitId target);

    UnitOrder createAttackGroundOrder(const SimVector& target);

    bool isWater(YardMapCell cell);

    bool isGeo(YardMapCell cell);

    bool isPassable(YardMapCell cell, bool yardMapOpen);

    struct SteeringInfo
    {
        /** The angle we are trying to steer towards. */
        SimAngle targetAngle{0};

        /** The speed we are trying to accelerate/decelerate to */
        SimScalar targetSpeed{0};

        /** True if the unit should attempt to take off into the air */
        bool shouldTakeOff{false};
    };

    struct UnitPhysicsInfoGround
    {
        SteeringInfo steeringInfo;

        /**
         * Rate at which the unit is travelling forwards in game units/tick.
         */
        SimScalar currentSpeed{0};
    };

    struct AirMovementStateFlying
    {
        std::optional<SimVector> targetPosition;

        /** True if the unit should attempt to land at current position. */
        bool shouldLand{false};

        /**
         * Rate at which the unit is moving in game units/tick.
         */
        SimVector currentVelocity{0_ss, 0_ss, 0_ss};
    };

    struct AirMovementStateTakingOff
    {
        /** Where the aircraft is heading while it climbs; it moves off as soon as it leaves the ground. */
        std::optional<SimVector> targetPosition;

        SimVector currentVelocity{0_ss, 0_ss, 0_ss};
    };

    struct AirMovementStateLanding
    {
        bool landingFailed{false};
        bool shouldAbort{false};
    };

    /**
     * State for Total Annihilation-style aircraft attack runs.
     *
     * The attack run is divided into three phases:
     *  - Approaching: fly toward the target at cruise altitude with no braking.
     *  - Engaging:    weapons-hot, hold the run-out heading so the unit blows
     *                 through the target rather than orbiting it.
     *  - Departing:   continue past the target along runOutDirection until far
     *                 enough away to loop back (or terminate).
     */
    struct AirMovementStateAttackRun
    {
        enum class Phase
        {
            Approaching,
            Engaging,
            Departing,
        };

        /** Target unit or ground location. Mirrors AttackOrder::target. */
        AttackTarget target;

        /** The most recently observed XZ position of the target (at cruise altitude on Y). */
        SimVector lastKnownTargetPos{0_ss, 0_ss, 0_ss};

        /** Captured at the start of Engaging. Unit XZ direction; defines the flyby line. */
        SimVector runOutDirection{0_ss, 0_ss, 1_ss};

        /** How far past the target to fly before looping or terminating. */
        SimScalar runOutDistance{0_ss};

        Phase phase{Phase::Approaching};

        /** Bombs let go on the current pass; a run drops a stick of three once the sight opens. */
        unsigned int bombsDroppedThisPass{0};

        /** Current air velocity in game units/tick. */
        SimVector currentVelocity{0_ss, 0_ss, 0_ss};

        AirMovementStateAttackRun() : target(SimVector(0_ss, 0_ss, 0_ss)) {}
        explicit AirMovementStateAttackRun(const AttackTarget& t) : target(t) {}
    };

    /**
     * State for Total Annihilation's gunships. The Brawler and the Rapier are
     * the only two units in the original data with HoverAttack set, and the
     * original gives them their own mission rather than an attack run.
     *
     * A gunship does not fly runs. It closes until it is one weapon range out,
     * then works its way around the target on a ring of two thirds of that
     * range, moving exactly 45 degrees per pass and alternating side, so it
     * shuttles between two stations rather than circling. Its gun takes the
     * target on arrival and holds it for the rest of the engagement.
     */
    struct AirMovementStateHoverAttack
    {
        enum class Phase
        {
            /** Closing on a point half way in, thrown off to one side so a flight does not stack up. */
            Closing,
            /** On the ring, swinging from one station to the next. This is where it stays. */
            Swinging,
        };

        /** Target unit or ground location. Mirrors AttackOrder::target. */
        AttackTarget target;

        /**
         * Where it is flying right now. Worked out once when it arrives and
         * then left alone: the station is a fixed point on the map, not one
         * that slides along with the target.
         */
        SimVector station{0_ss, 0_ss, 0_ss};

        /** Which way round the ring the next swing goes; the first one is negative. */
        bool swingPositive{false};

        /** Arrivals in a row with the target out of reach. Two of them send it to a fresh bearing. */
        unsigned int outOfRangeArrivals{0};

        Phase phase{Phase::Closing};

        /** Current air velocity in game units/tick. */
        SimVector currentVelocity{0_ss, 0_ss, 0_ss};

        AirMovementStateHoverAttack() : target(SimVector(0_ss, 0_ss, 0_ss)) {}
        explicit AirMovementStateHoverAttack(const AttackTarget& t) : target(t) {}
    };

    using AirMovementState = std::variant<AirMovementStateTakingOff, AirMovementStateFlying, AirMovementStateLanding, AirMovementStateAttackRun, AirMovementStateHoverAttack>;

    struct UnitPhysicsInfoAir
    {
        AirMovementState movementState{AirMovementStateTakingOff()};

        /**
         * Bank angle in radians, and the lagged acceleration it is computed
         * from. An aircraft leans because it is accelerating sideways, so the
         * bank is a consequence of the flying rather than something asked for:
         * see updateUnitSpeed for the formula, which is the original's.
         */
        SimScalar roll{0_ss};
        SimScalar previousRoll{0_ss};
        SimVector bankAccum{0_ss, 0_ss, 0_ss};
    };

    using UnitPhysicsInfo = std::variant<UnitPhysicsInfoGround, UnitPhysicsInfoAir>;

    bool isFlying(const UnitPhysicsInfo& physics);

    class UnitState
    {
    public:
        struct LifeStateAlive
        {
        };
        struct LifeStateDead
        {
            bool leaveCorpse;
        };
        using LifeState = std::variant<LifeStateAlive, LifeStateDead>;

    public:
        std::string unitType;
        std::vector<UnitMesh> pieces;
        std::unordered_map<std::string, int> pieceNameToIndices;
        SimVector position;
        SimVector previousPosition;
        std::unique_ptr<CobEnvironment> cobEnvironment;
        PlayerId owner;

        /**
         * Anticlockwise rotation of the unit around the Y axis in radians.
         * The other two axes of rotation are normally determined
         * by the normal of the terrain the unit is standing on.
         */
        SimAngle rotation{0};
        SimAngle previousRotation{0};

        UnitPhysicsInfo physics{UnitPhysicsInfoGround()};

        unsigned int hitPoints{0};

        LifeState lifeState{LifeStateAlive()};

        std::deque<UnitOrder> orders;
        UnitBehaviorState behaviourState;

        NavigationStateInfo navigationState;

        /**
         * State we remember related to the current order.
         * This is cleared every time an order is completed.
         * Right now this is just a unit ID for build orders.
         */
        std::optional<UnitId> buildOrderUnitId;

        bool inBuildStance{false};
        bool yardOpen{false};

        /**
         * True if the unit attempted to move last frame
         * and its movement was limited (or prevented entirely) by a collision.
         */
        bool inCollision{false};

        std::array<std::optional<UnitWeapon>, 3> weapons;

        UnitFireOrders fireOrders{UnitFireOrders::FireAtWill};
        UnitMovementOrders moveOrders{UnitMovementOrders::Maneuver};

        bool cobBusy{false};
        bool buggerOffActive{false};
        bool armored{false};

        /**
         * Number of enemy units this unit has killed.
         * Used by the COB VeteranLevel query to compute veterancy tier.
         * Self-damage / friendly-fire kills are counted (matches TA behavior).
         * Environmental deaths (e.g., feature damage) do not credit anyone.
         */
        unsigned int kills{0};

        unsigned int buildTimeCompleted{0};

        /** Reclaim work applied to this unit so far, see GameSimulation::reclaimUnit. */
        unsigned int reclaimProgress{0};

        /** Capture work applied to this unit so far, see GameSimulation::captureUnit. */
        unsigned int captureProgress{0};

        /** When set, the game time at which this unit will self-destruct. */
        std::optional<GameTime> selfDestructTime;

        /** The transport carrying this unit, if any. While set the unit does nothing and follows the transport. */
        std::optional<UnitId> carriedBy;

        /** The transport piece this unit hangs from (empty: the transport's own position). */
        std::string carriedPiece;

        /** Units this transport is carrying. */
        std::vector<UnitId> carriedUnits;

        /** The unit a transport's TransportPickup / TransportDrop script is currently handling, and when it began. */
        std::optional<UnitId> transportScriptTarget;
        GameTime transportScriptStartedAt{0};

        /**
         * Where a construction aircraft stands in its work pattern: a spell
         * over the centre of the job, then dwelling at each of eight points
         * on a ring around it, clockwise.
         */
        struct AirWorkOrbitState
        {
            /** The centre of the job the pattern is flown around. */
            SimVector workPosition;

            /** Bearing of the current station as seen from the job, measured the way the original does. */
            SimAngle bearing{0};

            /** False until the first station has been chosen. */
            bool started{false};
        };
        std::optional<AirWorkOrbitState> airWorkOrbit;

        /**
         * Set each tick while a construction aircraft holds station: it turns
         * towards this point at a slow fixed rate instead of chasing its
         * flight path. Cleared at the start of every behaviour update.
         */
        std::optional<SimVector> slowFacePoint;


        bool activated{false};
        bool isSufficientlyPowered{false};

        Energy energyProductionBuffer{0};
        Metal metalProductionBuffer{0};
        Energy previousEnergyConsumptionBuffer{0};
        Metal previousMetalConsumptionBuffer{0};
        Energy energyConsumptionBuffer{0};
        Metal metalConsumptionBuffer{0};

        std::deque<std::pair<std::string, int>> buildQueue;
        FactoryBehaviorState factoryState;

        static SimAngle toRotation(const SimVector& direction);

        static SimVector toDirection(SimAngle rotation);

        UnitState(const std::vector<UnitMesh>& pieces, std::unique_ptr<CobEnvironment>&& cobEnvironment);

        bool isBeingBuilt(const UnitDefinition& unitDefinition) const;

        unsigned int getBuildPercentLeft(const UnitDefinition& unitDefinition) const;

        float getPreciseCompletePercent(const UnitDefinition& unitDefinition) const;

        struct BuildCostInfo
        {
            unsigned int workerTime;
            Energy energyCost;
            Metal metalCost;
        };

        BuildCostInfo getBuildCostInfo(const UnitDefinition& unitDefinition, unsigned int buildTimeContribution);

        bool addBuildProgress(const UnitDefinition& unitDefinition, unsigned int buildTimeContribution);

        void moveObject(const std::string& pieceName, SimAxis axis, SimScalar targetPosition, SimScalar speed);

        void moveObjectNow(const std::string& pieceName, SimAxis axis, SimScalar targetPosition);

        void turnObject(const std::string& pieceName, SimAxis axis, SimAngle targetAngle, SimScalar speed);

        void turnObjectNow(const std::string& pieceName, SimAxis axis, SimAngle targetAngle);

        void spinObject(const std::string& pieceName, SimAxis axis, SimScalar speed, SimScalar acceleration);

        void stopSpinObject(const std::string& pieceName, SimAxis axis, SimScalar deceleration);

        bool isMoveInProgress(const std::string& pieceName, SimAxis axis) const;

        bool isTurnInProgress(const std::string& pieceName, SimAxis axis) const;

        bool isOwnedBy(PlayerId playerId) const;

        bool isAlive() const;
        bool isDead() const;

        void markAsDead();
        void markAsDeadNoCorpse();

        void finishBuilding(const UnitDefinition& unitDefinition);

        void clearOrders();

        void replaceOrders(const std::deque<UnitOrder>& newOrders);

        void addOrder(const UnitOrder& order);

        void setWeaponTarget(unsigned int weaponIndex, UnitId target);
        void setWeaponTarget(unsigned int weaponIndex, const SimVector& target);
        void clearWeaponTarget(unsigned int weaponIndex);
        void clearWeaponTargets();

        /**
         * Changes the firing mode, dropping whatever the unit had picked out
         * for itself if the new mode is not fire at will. The original does
         * the same the moment the order arrives, so a unit put on return fire
         * stops shooting until something hits it.
         */
        void setFireOrders(UnitFireOrders orders);

        Matrix4x<SimScalar> getTransform() const;
        Matrix4x<SimScalar> getInverseTransform() const;

        bool isSelectableBy(const UnitDefinition& unitDefinition, PlayerId player) const;

        void activate();

        void deactivate();

        Metal getMetalMake() const;
        Energy getEnergyMake() const;
        Metal getMetalUse() const;
        Energy getEnergyUse() const;

        void addEnergyDelta(const Energy& energy);
        void addMetalDelta(const Metal& metal);

        void resetResourceBuffers();

        void modifyBuildQueue(const std::string& buildUnitType, int count);

        std::unordered_map<std::string, int> getBuildQueueTotals() const;

        int getBuildQueueTotal(const std::string& unitType) const;

        enum class NanolatheDirection
        {
            Forward,
            Reverse,
        };

        std::optional<std::tuple<std::variant<UnitId, FeatureId>, SimVector, NanolatheDirection>> getActiveNanolatheTarget() const;

        std::optional<std::reference_wrapper<const UnitMesh>> findPiece(const std::string& pieceName) const;

        std::optional<std::reference_wrapper<UnitMesh>> findPiece(const std::string& pieceName);
    };
}
