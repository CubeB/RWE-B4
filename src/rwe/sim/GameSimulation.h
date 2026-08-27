#pragma once

#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <memory>
#include <random>
#include <rwe/cob/CobUnitId.h>
#include <rwe/collections/SimpleVectorMap.h>
#include <rwe/collections/VectorMap.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/geometry/BoundingBox3x.h>
#include <rwe/pathfinding/PathFindingService.h>
#include <rwe/sim/FeatureDefinition.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/MapFeature.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/MovementClassId.h>
#include <rwe/sim/OccupiedGrid.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/ProjectileId.h>
#include <rwe/sim/SimAxis.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitState.h>
#include <set>
#include <unordered_map>
#include <vector>

namespace rwe
{
    class AiPlayerController;

    constexpr int MaxUtilizableWindSpeed = 5000;

    enum class GamePlayerStatus
    {
        Alive,
        Dead
    };

    enum class GamePlayerType
    {
        Human,
        Computer
    };

    /**
     * Total worker-time needed to fully reclaim a feature.
     * Mirrors build costs: a builder contributes workerTimePerTick per tick,
     * so a commander (worker time 180/s) clears a 400-metal rock in ~2 seconds.
     * Never zero, so valueless features can still be cleared.
     */
    unsigned int computeFeatureReclaimWork(const FeatureDefinition& definition);

    struct GamePlayerInfo
    {
        std::optional<std::string> name;
        GamePlayerType type;
        PlayerColorIndex color;
        GamePlayerStatus status;
        std::string side;

        Metal metal;
        Energy energy;

        Metal maxMetal;
        Energy maxEnergy;

        Metal startingMetal;
        Energy startingEnergy;

        bool metalStalled{false};
        bool energyStalled{false};

        /** Enemy (or, with friendly fire, any) units this player's units have destroyed. */
        unsigned int unitsKilled{0};
        /** Units this player has lost, by any cause. */
        unsigned int unitsLost{0};

        Metal desiredMetalConsumptionBuffer{0};
        Energy desiredEnergyConsumptionBuffer{0};

        Metal previousDesiredMetalConsumptionBuffer{0};
        Energy previousDesiredEnergyConsumptionBuffer{0};

        Metal actualMetalConsumptionBuffer{0};
        Energy actualEnergyConsumptionBuffer{0};

        Metal metalProductionBuffer{0};
        Energy energyProductionBuffer{0};

        /**
         * Tries to apply a resource change. Income is always accepted. Spending
         * is accepted only if the stockpile plus this second's income, less what
         * has already been spent this second, covers it; otherwise nothing is
         * taken, the player is flagged as stalled, and false is returned. Spending
         * that cannot be met is still recorded as demand for the resource display.
         */
        bool addResourceDelta(const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal);
        void recordDesire(const Energy& energy);
        void recordDesire(const Metal& metal);
        bool canAfford(const Energy& energy) const;
        bool canAfford(const Metal& metal) const;
        void acceptResource(const Energy& energy);
        void acceptResource(const Metal& metal);
    };

    struct PathRequest
    {
        UnitId unitId;

        bool operator==(const PathRequest& rhs) const;

        bool operator!=(const PathRequest& rhs) const;
    };

    struct WinStatusWon
    {
        PlayerId winner;
    };
    struct WinStatusDraw
    {
    };
    struct WinStatusUndecided
    {
    };
    using WinStatus = std::variant<WinStatusWon, WinStatusDraw, WinStatusUndecided>;

    struct FireWeaponEvent
    {
        std::string weaponType;
        /**
         * The number of this shot within the weapon's current burst.
         * If this is the first shot of the burst, it will be 0.
         */
        int shotNumber;
        SimVector firePoint;
    };

    struct UnitArrivedEvent
    {
        UnitId unitId;
    };

    struct UnitActivatedEvent
    {
        UnitId unitId;
    };

    struct UnitDeactivatedEvent
    {
        UnitId unitId;
    };

    struct UnitCompleteEvent
    {
        UnitId unitId;
    };

    struct UnitSpawnedEvent
    {
        UnitId unitId;
    };

    struct UnitDiedEvent
    {
        UnitId unitId;
        std::string unitType;
        SimVector position;
        enum class DeathType
        {
            NormalExploded,
            WaterExploded,
            Deleted,
            SelfDestructed,
        };
        DeathType deathType;
    };

    struct UnitStartedBuildingEvent
    {
        UnitId unitId;
    };

    struct EmitParticleFromPieceEvent
    {
        enum class SfxType
        {
            LightSmoke,
            BlackSmoke,
            Wake1,
        };

        SfxType sfxType;
        UnitId unitId;
        std::string pieceName;
    };

    struct ProjectileSpawnedEvent
    {
        ProjectileId projectileId;
    };

    struct ProjectileDiedEvent
    {
        ProjectileId projectileId;
        std::string weaponType;
        SimVector position;

        enum class DeathType
        {
            OutOfBounds,
            NormalImpact,
            WaterImpact,
            EndOfLife,
        };
        DeathType deathType;
    };

    struct UnitCapturedEvent
    {
        UnitId unitId;
        PlayerId previousOwner;
        PlayerId newOwner;
    };

    using GameEvent = std::variant<
        FireWeaponEvent,
        UnitArrivedEvent,
        UnitActivatedEvent,
        UnitDeactivatedEvent,
        UnitCompleteEvent,
        EmitParticleFromPieceEvent,
        UnitSpawnedEvent,
        UnitDiedEvent,
        UnitStartedBuildingEvent,
        ProjectileSpawnedEvent,
        ProjectileDiedEvent,
        UnitCapturedEvent>;


    struct UnitInfo
    {
        const UnitId id;
        UnitState* const state;
        const UnitDefinition* const definition;

        UnitInfo(UnitId id, UnitState* state, const UnitDefinition* definition)
            : id(id), state(state), definition(definition) {}
    };

    struct ConstUnitInfo
    {
        const UnitId id;
        const UnitState* const state;
        const UnitDefinition* const definition;

        ConstUnitInfo(UnitId id, const UnitState* state, const UnitDefinition* definition)
            : id(id), state(state), definition(definition) {}

        ConstUnitInfo(UnitInfo unitInfo)
            : id(unitInfo.id), state(unitInfo.state), definition(unitInfo.definition) {}
    };

    enum class ImpactType
    {
        Normal,
        Water
    };

    struct GameSimulation
    {
        std::minstd_rand rng;

        WinStatus gameStatus{WinStatusUndecided()};

        MapTerrain terrain;

        std::unordered_map<std::string, UnitDefinition> unitDefinitions;

        SimpleVectorMap<FeatureDefinition, FeatureDefinitionIdTag> featureDefinitions;
        std::unordered_map<std::string, FeatureDefinitionId> featureNameIndex;

        std::unordered_map<std::string, UnitModelDefinition> unitModelDefinitions;

        std::unordered_map<std::string, CobScript> unitScriptDefinitions;

        std::unordered_map<std::string, WeaponDefinition> weaponDefinitions;

        MovementClassDatabase movementClassDatabase;
        MovementClassCollisionService movementClassCollisionService;

        PathFindingService pathFindingService;

        OccupiedGrid occupiedGrid;
        std::set<UnitId> flyingUnitsSet;

        Grid<unsigned char> metalGrid;

        /** The metal value of ordinary ground; cells above this are metal patches. */
        unsigned char surfaceMetal;

        Grid<bool> geoGrid;

        std::vector<GamePlayerInfo> players;

        /** One entry per player, indexed by PlayerId. Derived state: rebuilt every tick, not hashed. */
        std::vector<PlayerVisibility> playerVisibility;

        VectorMap<MapFeature, FeatureIdTag> features;

        VectorMap<UnitState, UnitIdTag> units;

        VectorMap<Projectile, ProjectileIdTag> projectiles;

        std::deque<PathRequest> pathRequests;

        std::deque<UnitId> unitCreationRequests;

        GameTime gameTime{0};

        std::vector<GameEvent> events;

        SimScalar currentWindGenerationFactor{0_ss};

        const int minWindSpeed;

        const int maxWindSpeed;

        GameTime nextWindSpeedChange;

        // Computer-player controllers, owned by the simulation per the
        // locked decision in docs/ai-architecture-proposal.md §12-Q4.
        // Pointed-to via unique_ptr so the AI subsystem header is not
        // forced into every TU that pulls in GameSimulation.h.
        // Iterated in deterministic key order via `aiPlayerOrder`.
        std::unordered_map<PlayerId, std::unique_ptr<AiPlayerController>> aiControllers;

        // Sorted list of AI player ids that mirrors the `aiControllers` keys.
        // Maintained when AIs are registered so per-tick iteration order is
        // independent of the unordered_map's hash ordering.
        std::vector<PlayerId> aiPlayerOrder;

        // Commands the AI controllers want issued, keyed by the AI's
        // PlayerId. Drained each scene tick by GameScene::update via
        // takeAiCommandsForPlayer(). One element per tick — accumulate, then
        // drain, then refill.
        std::unordered_map<PlayerId, std::vector<PlayerCommand>> aiPendingCommands;

        explicit GameSimulation(MapTerrain&& terrain, unsigned char surfaceMetal, int minWindSpeed, int maxWindSpeed);

        // GameSimulation owns AiPlayerController via unique_ptr. We need an
        // explicit destructor because AiPlayerController is forward-declared
        // here; the definition becomes available only in GameSimulation.cpp,
        // which is where the destructor body is generated.
        ~GameSimulation();
        GameSimulation(GameSimulation&&) noexcept;
        // Move-assignment is disabled: the const minWindSpeed / maxWindSpeed
        // members make it ill-formed, and existing call sites only ever
        // move-construct (LoadingScene -> GameScene). Copy operations are
        // implicitly deleted by the unique_ptr storage.
        GameSimulation& operator=(GameSimulation&&) = delete;
        GameSimulation(const GameSimulation&) = delete;
        GameSimulation& operator=(const GameSimulation&) = delete;

        std::optional<FeatureId> addFeature(MapFeature&& newFeature);

        std::optional<FeatureId> addFeature(FeatureDefinitionId featureType, int heightmapX, int heightmapZ);

        /** Removes a feature and frees the grid cells it occupied. No-op if the id is stale. */
        void deleteFeature(FeatureId id);

        /**
         * Applies workAmount of reclaim work to a feature on behalf of a player,
         * crediting that player metal and energy in proportion to the work done.
         * Returns true when the feature is fully reclaimed (it is then removed
         * and any featureReclamate spawned in its place), or when it no longer
         * exists. Returns false if the feature is not reclaimable or work remains.
         */
        bool reclaimFeature(FeatureId featureId, PlayerId reclaimer, unsigned int workAmount);

        /**
         * Applies workAmount of reclaim work to a unit on behalf of a player.
         * Total work is the unit's buildTime. The player recovers the unit's
         * build cost scaled by how much of it had actually been built, paid out
         * progressively. Returns true when the unit is fully reclaimed (it is
         * then marked dead with no corpse) or no longer exists.
         */
        bool reclaimUnit(UnitId targetId, PlayerId reclaimer, unsigned int workAmount);

        /**
         * Applies workAmount of capture work to an enemy unit on behalf of a player.
         * Total work is the unit's buildTime. On completion the unit changes
         * owner, drops its orders and weapon targets, and a UnitCapturedEvent is
         * emitted. Returns true when the unit is captured, already owned by the
         * captor, or no longer exists.
         */
        bool captureUnit(UnitId targetId, PlayerId captor, unsigned int workAmount);

        /** Length of the self-destruct countdown, as in TA. */
        static constexpr unsigned int SelfDestructCountdownTicks = 5 * SimTicksPerSecond;

        /** Starts a unit's self-destruct countdown, or cancels it if one is already running. */
        void toggleSelfDestruct(UnitId unitId);

        /** Income multiplier for a player: 1 for everyone except cheating computer players. */
        float resourceBonusFor(PlayerId playerId) const;

        /** The vision grid cell containing a world position (may lie outside the grid). */
        Point visionCellAt(const SimVector& position) const;

        bool isExploredBy(PlayerId player, const SimVector& position) const;
        bool isVisibleTo(PlayerId player, const SimVector& position) const;
        bool isOnRadarOf(PlayerId player, const SimVector& position) const;

        /** True for the viewer's own units, and for other units standing in the viewer's line of sight. */
        bool canSeeUnit(PlayerId viewer, UnitId unitId) const;

        /** True when the unit can be seen, or is a radar contact. */
        bool canDetectUnit(PlayerId viewer, UnitId unitId) const;

        /**
         * Destroys a unit immediately with its SelfDestructAs explosion
         * (falling back to ExplodeAs). Leaves no wreck.
         */
        void selfDestructUnit(UnitId unitId);

        PlayerId addPlayer(const GamePlayerInfo& info);

        std::optional<UnitId> trySpawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<SimAngle> rotation);

        /**
         * Returns true if the unit was really added, false otherwise.
         * A unit might not be added because it violates collision constraints.
         */
        std::optional<UnitId> tryAddUnit(UnitState&& unit);

        /**
         * Returns true if a unit with the given movementclass attributes
         * could be built at given location on the map -- i.e. it is valid terrain
         * for the unit, it is not occupied by something else, and it contains geo if required.
         */
        bool canBeBuiltAt(const MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y) const;

        DiscreteRect computeFootprintRegion(const SimVector& position, unsigned int footprintX, unsigned int footprintZ) const;

        DiscreteRect computeFootprintRegion(const SimVector& position, const UnitDefinition::MovementCollisionInfo& collisionInfo) const;

        bool anyFeatureOccupies(const DiscreteRect& rect) const;

        bool containsAnyGeoMatch(const Grid<YardMapCell>& yardMap, unsigned int x, unsigned int y) const;

        bool isCollisionAt(const DiscreteRect& rect) const;

        bool isCollisionAt(const GridRegion& region) const;

        bool isCollisionAt(const DiscreteRect& rect, UnitId self) const;

        bool isYardmapBlocked(unsigned int x, unsigned int y, const Grid<YardMapCell>& yardMap, bool open, UnitId self) const;

        bool isAdjacentToObstacle(const DiscreteRect& rect) const;

        void showObject(UnitId unitId, const std::string& name);

        void hideObject(UnitId unitId, const std::string& name);

        void enableShading(UnitId unitId, const std::string& name);

        void disableShading(UnitId unitId, const std::string& name);

        UnitState& getUnitState(UnitId id);

        const UnitState& getUnitState(UnitId id) const;

        UnitInfo getUnitInfo(UnitId id);

        ConstUnitInfo getUnitInfo(UnitId id) const;

        std::optional<std::reference_wrapper<UnitState>> tryGetUnitState(UnitId id);

        std::optional<std::reference_wrapper<const UnitState>> tryGetUnitState(UnitId id) const;

        std::optional<std::reference_wrapper<const UnitState>> tryGetUnitState(CobUnitId id) const;

        bool unitExists(UnitId id) const;

        MapFeature& getFeature(FeatureId id);

        const MapFeature& getFeature(FeatureId id) const;

        std::optional<std::reference_wrapper<MapFeature>> tryGetFeature(FeatureId id);

        std::optional<std::reference_wrapper<const MapFeature>> tryGetFeature(FeatureId id) const;

        GamePlayerInfo& getPlayer(PlayerId player);

        const GamePlayerInfo& getPlayer(PlayerId player) const;

        void moveObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar position, SimScalar speed);

        void moveObjectNow(UnitId unitId, const std::string& name, SimAxis axis, SimScalar position);

        void turnObject(UnitId unitId, const std::string& name, SimAxis axis, SimAngle angle, SimScalar speed);

        void turnObjectNow(UnitId unitId, const std::string& name, SimAxis axis, SimAngle angle);

        void spinObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar speed, SimScalar acceleration);

        void stopSpinObject(UnitId unitId, const std::string& name, SimAxis axis, SimScalar deceleration);

        bool isPieceMoving(UnitId unitId, const std::string& name, SimAxis axis) const;

        bool isPieceTurning(UnitId unitId, const std::string& name, SimAxis axis) const;

        std::optional<SimVector> intersectLineWithTerrain(const Line3x<SimScalar>& line) const;

        void moveUnitOccupiedArea(const DiscreteRect& oldRect, const DiscreteRect& newRect, UnitId unitId);

        void requestPath(UnitId unitId);

        Projectile createProjectileFromWeapon(PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker = std::nullopt, std::optional<SimVector> inheritedVelocity = std::nullopt);

        Projectile createProjectileFromWeapon(PlayerId owner, const std::string& weaponType, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker = std::nullopt, std::optional<SimVector> inheritedVelocity = std::nullopt);

        void spawnProjectile(PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker = std::nullopt, std::optional<SimVector> inheritedVelocity = std::nullopt);

        WinStatus computeWinStatus() const;

        bool addResourceDelta(const UnitId& unitId, const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal);
        bool addResourceDelta(const UnitId& unitId, const Energy& energy, const Metal& metal);

        bool trySetYardOpen(const UnitId& unitId, bool open);

        void emitBuggerOff(const UnitId& unitId);

        void tellToBuggerOff(const UnitId& unitId, const DiscreteRect& rect);

        GameHash computeHash() const;

        void activateUnit(UnitId unitId);

        void deactivateUnit(UnitId unitId);

        void quietlyKillUnit(UnitId unitId);

        Matrix4x<SimScalar> getUnitPieceLocalTransform(UnitId unitId, const std::string& pieceName) const;

        Matrix4x<SimScalar> getUnitPieceTransform(UnitId unitId, const std::string& pieceName) const;

        SimVector getUnitPiecePosition(UnitId unitId, const std::string& pieceName) const;

        void setBuildStance(UnitId unitId, bool value);

        void setYardOpen(UnitId unitId, bool value);

        void setBuggerOff(UnitId unitId, bool value);

        MovementClassDefinition getAdHocMovementClass(const UnitDefinition::MovementCollisionInfo& info) const;

        std::pair<unsigned int, unsigned int> getFootprintXZ(const UnitDefinition::MovementCollisionInfo& info) const;

        BoundingBox3x<SimScalar> createBoundingBox(const UnitState& unit) const;

        void killUnit(UnitId unitId);

        /**
         * Kill a unit and credit the kill to the supplied attacker, if any.
         * Self-damage / friendly-fire kills are credited (matches TA behavior).
         * The attacker must be a living unit; dead/missing attackers are not credited.
         */
        void killUnit(UnitId unitId, std::optional<UnitId> attacker);

        void applyDamage(UnitId unitId, unsigned int damagePoints);

        /**
         * Apply damage and, if the unit is killed, credit the attacker.
         * Same crediting rules as killUnit(UnitId, std::optional<UnitId>).
         */
        void applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker);

        void applyDamageInRadius(const SimVector& position, SimScalar radius, const Projectile& projectile);

        void doProjectileImpact(const Projectile& projectile, ImpactType impactType);

        void updateProjectiles();

        void killPlayer(PlayerId playerId);

        void processVictoryCondition();

        void updateWind();

        void updateResources();

        void trySpawnFeature(const std::string& featureType, const SimVector& position, SimAngle rotation);

        void deleteDeadUnits();

        void updateSelfDestructs();

        void updateVisibility();

        void deleteDeadProjectiles();

        void spawnNewUnits();

        void tick();

        std::optional<FeatureDefinitionId> tryGetFeatureDefinitionId(const std::string& featureName) const;

        const FeatureDefinition& getFeatureDefinition(FeatureDefinitionId featureDefinitionId) const;

        // Register an AI controller for `playerId`. The simulation takes
        // ownership. Multiple registrations for the same playerId replace
        // the previous controller. Must be called only at game setup time;
        // there is no thread-safety on this map.
        void addAiController(PlayerId playerId, std::unique_ptr<AiPlayerController> controller);

        // Drain and return the commands the AI controller for `playerId`
        // has accumulated. Returns an empty vector if no commands are
        // pending or no AI is registered. After draining, the per-player
        // queue is empty.
        std::vector<PlayerCommand> takeAiCommandsForPlayer(PlayerId playerId);

        // Run all AI controllers in deterministic key order. Each
        // controller appends its decided commands into
        // `aiPendingCommands[playerId]`.
        // Called once per sim tick at the top of `tick()`.
        void runAiControllers();
    };
}
