#pragma once

#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <memory>
#include <random>
#include <rwe/cob/CobUnitId.h>
#include <rwe/collections/SimpleVectorMap.h>
#include <rwe/collections/VectorMap.h>
#include <rwe/game/GameParameters.h>
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
#include <rwe/sim/UnitSpatialIndex.h>
#include <rwe/sim/UnitState.h>
#include <set>
#include <unordered_map>
#include <vector>

namespace rwe
{
    class AiPlayerController;

    constexpr int MaxUtilizableWindSpeed = 5000;

    /** A hit of at least this much goes through armour untouched (TotalA.exe 0x489BD1). */
    constexpr int ArmourBypassDamage = 30000;

    /**
     * How far Circular sight can reach, in vision cells. The original's
     * circular masks are ten hand-drawn frames of radius 5 to 14, so 14 cells
     * -- 448 world units -- is where its sight saturates. The ray tables' cap
     * of 8 does not apply, because in this mode no ray is walked.
     *
     * The original's *lower* bound of 5 is deliberately not reproduced: a unit
     * with no SightDistance at all should see nothing but the ground it is
     * standing on, whichever mode is chosen.
     */
    constexpr int MaxCircularSightRadiusInCells = 14;

    /**
     * Deals the map's own start positions out to the player slots that are
     * filled, given in ascending slot order and one-based (slot 0 is the map's
     * StartPos1). Returns one start position number per slot, in the same
     * order.
     *
     * Fixed hands each slot its own number back. Random permutes the same set,
     * so every position is used exactly once and no coordinate is invented:
     * the map's StartPos entries are all there ever is.
     *
     * The permutation is drawn from the simulation's RNG, which every peer
     * seeds identically, so every peer deals the same hand. The draw is a
     * modulo of the generator's raw output on purpose --
     * std::uniform_int_distribution's output is not fixed by the standard and
     * differs between implementations, which would desync a network game.
     */
    std::vector<int> dealStartPositions(const std::vector<int>& startPositions, StartLocationMode mode, std::minstd_rand& rng);

    /** What one second's settle decided for one resource. */
    struct ResourceSettlement
    {
        /** The share of this second's requests that could be paid, 0 to 1. */
        float requestFraction;
        /** The share of the debt carried in from earlier seconds that could be paid, 0 to 1. */
        float debtFraction;
        /** What is left in the stockpile once both have been paid. */
        float remaining;
        /** True when either share fell short of the whole. */
        bool stalled;
    };

    /**
     * Divides a second's supply between what is already owed and what has been
     * asked for since. Debt is paid first and in preference: if it cannot be
     * paid in full then nothing new gets anything at all this second. Whatever
     * fraction comes back is the same for every consumer of that resource, so a
     * player who can afford two thirds of its outgoings has every builder,
     * every metal maker and every cloak working at two thirds rather than a
     * lucky two thirds of them working and the rest stopped.
     *
     * This is TotalA.exe 0x401A4D, run once for energy and once for metal.
     */
    ResourceSettlement settleResourcePool(float supply, float debt, float requested);

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
     * Total worker-time needed to fully reclaim a feature, given how much of it
     * is still standing. Scales with both the feature's value and its remaining
     * hit points, so a boulder takes far longer than a bush and a wreck that has
     * been shelled is quicker to clear than a pristine one. See the definition
     * for the weighting and the TA data behind it.
     * Mirrors build costs: a builder contributes workerTimePerTick per tick.
     * Never zero, so valueless features can still be cleared.
     */
    unsigned int computeFeatureReclaimWork(const FeatureDefinition& definition, unsigned int currentHitPoints);

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

        /**
         * Team from the lobby; players sharing one share sight and radar.
         * Nothing means the player has no allies but itself. Last of the
         * positional members so the existing aggregate initialisers, which
         * stop at startingEnergy, keep working.
         */
        std::optional<int> teamId;

        bool metalStalled{false};
        bool energyStalled{false};

        /** Enemy (or, with friendly fire, any) units this player's units have destroyed. */
        unsigned int unitsKilled{0};
        /** Units this player has lost, by any cause. */
        unsigned int unitsLost{0};

        /**
         * Everything the player has ever earned, and everything it earned with
         * nowhere to put it. The end-of-game chart's four middle columns --
         * Energy Produced, Metal Produced, Excess Energy, Excess Metal -- are
         * these four, read out of the player record at `player+0xAC`, `+0xB4`,
         * `+0xCC` and `+0xD4` in the original (0x41DD86-0x41DDBB). Excess is
         * measured where the original measures it: what the storage cap threw
         * away at the end of a second, not what a full bar refused to take
         * during one.
         */
        Metal metalProduced{0};
        Energy energyProduced{0};
        Metal metalExcess{0};
        Energy energyExcess{0};

        Metal desiredMetalConsumptionBuffer{0};
        Energy desiredEnergyConsumptionBuffer{0};

        Metal previousDesiredMetalConsumptionBuffer{0};
        Energy previousDesiredEnergyConsumptionBuffer{0};

        /** Everything the player's units and the player itself earned this second. */
        Metal metalProductionBuffer{0};
        Energy energyProductionBuffer{0};

        Metal previousMetalProductionBuffer{0};
        Energy previousEnergyProductionBuffer{0};

        /**
         * The player's own slice of the economy, for income and spending that
         * belongs to nobody in particular rather than to one of its units. The
         * original keeps an identical block hanging off the player at
         * `player+0xEC` and folds it into the same totals as the per-unit ones.
         */
        Metal metalRequestBuffer{0};
        Energy energyRequestBuffer{0};
        Metal metalDebt{0};
        Energy energyDebt{0};

        /**
         * Books a resource change against the player directly. Income is always
         * taken. Spending is refused while the player-level block still owes for
         * earlier work, on the same rule a unit follows; what is granted is only
         * a claim on the second's income, settled at the end of it.
         */
        bool addResourceDelta(const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal);
        void recordDesire(const Energy& energy);
        void recordDesire(const Metal& metal);
        void acceptResource(const Energy& energy);
        void acceptResource(const Metal& metal);
        bool inResourceDebt() const;
    };

    /**
     * The direction a missile with this attitude is pointing. TA builds its
     * velocity this way round every tick -- pitch first, then heading -- so a
     * missile always flies exactly where its nose points (0x49BA74). That is
     * why the renderer can take a missile's heading straight off its velocity,
     * and why it has to come back here on the one frame the velocity is zero.
     */
    SimVector toMissileDirection(SimAngle heading, SimAngle pitch);

    /**
     * The map's wind, as a per-tick displacement in world units.
     *
     * Ballistic and dropped projectiles have this added to their position
     * every tick, on top of gravity (TotalA.exe 0x49BD10). `speed` is the raw
     * OTA figure -- minwindspeed/maxwindspeed, which run into the thousands --
     * and not a world-unit speed, so the conversion lives in here. Strictly
     * horizontal: the original never writes the vector's Y word.
     */
    SimVector computeWindVector(SimAngle direction, int speed);

    struct PathRequest
    {
        UnitId unitId;

        bool operator==(const PathRequest& rhs) const;

        bool operator!=(const PathRequest& rhs) const;
    };

    /** Where a carried unit would be set down: the cells it takes, and the point at their centre. */
    struct UnloadSpot
    {
        DiscreteRect footprint;
        SimVector position;
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

        /** Who lost the unit and who killed it, where the sim knows. Scene-facing; not hashed. */
        std::optional<PlayerId> owner;
        std::optional<PlayerId> killerOwner;
    };

    /**
     * A unit took damage. Emitted for the scene -- the original's music
     * evaluator scores every weapon hit involving the local player -- and
     * never hashed.
     */
    struct UnitDamagedEvent
    {
        UnitId unitId;
        PlayerId victimOwner;
        std::optional<PlayerId> attackerOwner;

        /**
         * The original's damage cause byte (unit+0xF5): 1 is a weapon hit
         * and 2 a paralyser. The under-attack voice, sound slot 2, plays
         * for damage from another player whatever its cause, and for damage
         * from the unit's own side only when it is a weapon hit
         * (0x4071BA-0x4071D1). See TOTALA-EXE.md §97.
         */
        bool paralyzer{false};
    };

    /**
     * A repair job has finished and its unit says "Unit repaired": sound
     * slot 10, `repair`. The original plays it from both ends of the job --
     * the repairer when RepairUnit or VTOL_RepairUnit completes, and the
     * aircraft itself when SELFREPAIR (0x402491) or VTOL_GetRepaired
     * (0x415298) sees its hit points reach the maximum on a pad. See
     * TOTALA-EXE.md §97.
     *
     * Emitted for the scene, and never hashed.
     */
    struct UnitRepairedEvent
    {
        UnitId unitId;
    };

    /**
     * An order the unit cannot carry out, with the caption the original
     * prints for it: sound slot 7, `cant`, whose table caption is "Cannot
     * Comply" but which every one of its forty-five call sites overrides
     * with a message of its own ("That unit is a cloud of vapor and cannot
     * be captured", "Landing aborted: no pads available", ...). See
     * TOTALA-EXE.md §97 for the full list.
     *
     * Emitted for the scene, and never hashed.
     */
    struct UnitCannotComplyEvent
    {
        UnitId unitId;
        std::string message;
    };

    struct UnitStartedBuildingEvent
    {
        UnitId unitId;
    };

    /**
     * A builder's nanolathe has just begun a reclaim or a capture job: the
     * unit is in reach, the arm is out, and this is the first tick on which
     * work is actually done. Sound slot 11, `working` -- `reclaim1` for every
     * construction category in the shipped data -- is played once here, by
     * all three of the original's Reclaim (0x404C69), ReclaimUnit (0x4048B5)
     * and Capture (0x404568) handlers alike. See TOTALA-EXE.md §97.
     *
     * Emitted for the scene, and never hashed.
     */
    struct UnitStartedReclaimingEvent
    {
        UnitId unitId;
    };

    struct EmitParticleFromPieceEvent
    {
        enum class SfxType
        {
            LightSmoke,
            BlackSmoke,
            /**
             * Water foam behind a ship. Wake2 is the same thing at double
             * speed and half the life, and the Reverse pair are the same
             * again with the emitting piece's two vertices swapped, which
             * turns the drift round. The original really does distinguish
             * them by nothing more than that.
             */
            Wake1,
            Wake2,
            ReverseWake1,
            ReverseWake2,
            /** An aircraft thruster: the exhaust an Atlas trails from under its arms. */
            Vtol,
            /** The same emitter as Vtol, one step longer and slower. */
            Thrust,
        };

        SfxType sfxType;
        UnitId unitId;
        std::string pieceName;
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

    /**
     * A projectile went off and carried on flying: a `noexplode` round, which
     * detonates without being consumed (0x499EDE). The projectile is still
     * alive, so this is not a ProjectileDiedEvent -- the scene wants the
     * explosion art and the screen shake, and must not treat the round as
     * finished.
     */
    struct ProjectileDetonatedEvent
    {
        std::string weaponType;
        SimVector position;
        bool inWater;
    };

    struct UnitCapturedEvent
    {
        UnitId unitId;
        PlayerId previousOwner;
        PlayerId newOwner;

        /**
         * The unit that took it, where a unit did. Sound slot 16, `capture`,
         * is played by the captor when the job lands (0x4046cc, the Capture
         * mission's state 5) -- no shipped category sets the slot, so on the
         * shipped data it is silent. See TOTALA-EXE.md §97. Empty when the
         * change of hands came from somewhere other than a capture mission.
         */
        std::optional<UnitId> captorUnitId;
    };

    /** A feature has just been fully reclaimed and removed. */
    struct FeatureReclaimedEvent
    {
        FeatureDefinitionId featureType;
        SimVector position;
    };

    /** A unit script exploded one of its pieces (the COB `explode` command). */
    struct PieceExplodedEvent
    {
        UnitId unitId;
        std::string unitType;
        PlayerId owner;
        std::string pieceName;
        /** World position of the piece when it went. */
        SimVector position;
        SimAngle rotation;
        /** TA's explode flags: SHATTER 1, EXPLODE_ON_HIT 2, FALL 4, SMOKE 8, FIRE 16, BITMAPONLY 32, BITMAP1..5 64..1024. */
        unsigned int flags;
    };

    using GameEvent = std::variant<
        FeatureReclaimedEvent,
        PieceExplodedEvent,
        FireWeaponEvent,
        UnitArrivedEvent,
        UnitActivatedEvent,
        UnitDeactivatedEvent,
        UnitCompleteEvent,
        EmitParticleFromPieceEvent,
        UnitSpawnedEvent,
        UnitDiedEvent,
        UnitDamagedEvent,
        UnitStartedBuildingEvent,
        UnitStartedReclaimingEvent,
        ProjectileDiedEvent,
        ProjectileDetonatedEvent,
        UnitCapturedEvent,
        UnitRepairedEvent,
        UnitCannotComplyEvent>;


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

    /**
     * What the COB `Killed` script is told about how hard the unit was hit.
     *
     * `clamp(1, 100, (100 * overkill / maxdamage + X) / 2)`, where `overkill`
     * is the damage the killing blow had left over once the unit's remaining
     * hit points were paid for, and `X` is `unit+0xF7` -- the one term in the
     * formula with no known writer anywhere in the binary, taken as zero here
     * and recorded as a gap in TOTALA-EXE.md section 88.
     *
     * A shipped `Killed` reads it as a three-band ladder: a gentle kill picks
     * the intact wreck, a heavy one picks rubble.
     */
    int computeKilledSeverity(unsigned int overkill, unsigned int maxHitPoints);

    /**
     * The corpse level a `Killed` script wrote into its second parameter, if
     * it wrote one. Masked to four bits as the original masks it.
     */
    std::optional<unsigned int> readCorpseLevel(const CobThread& thread);

    struct GameSimulation
    {
        std::minstd_rand rng;

        WinStatus gameStatus{WinStatusUndecided()};

        /**
         * The skirmish screen's rules, copied out of GameParameters before the
         * first player is added. They are fixed for the life of a game and
         * identical on every peer, which is what lets them change simulation
         * behaviour -- visibility feeds target selection, so a client-side
         * option like GameScene's fog-of-war toggle could never live here.
         *
         * Set them before addPlayer: Mapped hands a player its explored grid
         * at the moment that grid is created.
         */
        LineOfSightMode lineOfSightMode{LineOfSightMode::True};
        MappingMode mappingMode{MappingMode::Unmapped};
        CommanderDeathMode commanderDeathMode{CommanderDeathMode::GameEnds};

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

        /** Ground height per vision cell, for line-of-sight checks. */
        VisionHeightGrid visionHeights;

        /**
         * The authored line-of-sight ray fans, read from gamedata/los.tdf.
         * Defaults to generated fans so a simulation built without game data
         * still has sight.
         */
        LosTables losTables;

        /**
         * The one explored grid, a bit per line-of-sight group: every cell
         * the original remembers, with a bit set for each group that has seen
         * it (TOTALA-EXE.md section 2). Set-only and never cleared.
         *
         * It is shared rather than kept per player, which is what makes an
         * ally's ground known to the ally: players on one team own the same
         * bit, so a cell either of them has looked at is explored for both
         * without anything being copied between grids. The bit a player reads
         * and writes is losGroupBitFor.
         */
        Grid<ExploredMask> explored;

        Grid<bool> geoGrid;

        std::vector<GamePlayerInfo> players;

        /** One entry per player, indexed by PlayerId. Derived state: rebuilt every tick, not hashed. */
        std::vector<PlayerVisibility> playerVisibility;

        /**
         * One explored bit per player, indexed by PlayerId: a small pure
         * function of `players`, handed out in player order.
         *
         * Derived, and never saved or hashed: on a load the players are added
         * in the saved order and the bits come out identical, so there is
         * nothing here a save would have to restore. See losGroupBitFor.
         */
        std::vector<ExploredMask> playerLosGroupBits;

        VectorMap<MapFeature, FeatureIdTag> features;

        VectorMap<UnitState, UnitIdTag> units;

        /**
         * Where the units are, for the target scan. Derived state: rebuilt
         * from `units` once a tick, not saved and not hashed. See
         * getUnitSpatialIndex.
         */
        UnitSpatialIndex unitSpatialIndex;
        std::optional<GameTime> unitSpatialIndexStamp;

        /** The fastest thing the loaded data defines, in world units a tick. Worked out once. */
        std::optional<float> maxUnitSpeedPerTick;

        VectorMap<Projectile, ProjectileIdTag> projectiles;

        std::deque<PathRequest> pathRequests;

        std::deque<UnitId> unitCreationRequests;

        GameTime gameTime{0};

        std::vector<GameEvent> events;

        SimScalar currentWindGenerationFactor{0_ss};

        /**
         * The wind as a per-tick displacement for ballistic and dropped
         * projectiles. Rebuilt only when the wind changes; the speed and
         * direction it came from are not retained anywhere, so this is the
         * only form of the wind the simulation keeps -- which is why it is
         * saved and hashed rather than treated as derived state.
         *
         * Explicitly zeroed: Vector3x default-constructs its components, so
         * leaving this bare would put three uninitialised floats into the
         * simulation and desync the first shot of the game.
         */
        SimVector currentWindVector{0_ss, 0_ss, 0_ss};

        const int minWindSpeed;

        const int maxWindSpeed;

        /**
         * The map's `tidalstrength`, which is what a tidal generator's
         * `TidalGenerator` is multiplied by. Set from the OTA once the map is
         * loaded; unlike the wind it never changes during a game.
         */
        int tidalStrength{0};

        /**
         * The map's `killmul` and `timemul`, the two numbers the end-of-game
         * chart's Score column is made of: `score = kills * killmul +
         * seconds * timemul`, truncated one term at a time and floored at zero
         * (0x41DDBE-0x41DE11). Map constants like the tide, read out of the OTA
         * at load. The original defaults killmul to 50 and timemul to nothing,
         * so by default a game is scored on kills alone.
         */
        int killMul{50};
        int timeMul{0};

        GameTime nextWindSpeedChange;

        /**
         * Where the feature regrowth sweep has got to. The original examines one
         * map square a tick and walks the whole grid backwards, wrapping round at
         * the bottom, so a feature gets one chance to seed per full sweep however
         * big the map is. -1 means the sweep just wrapped and this tick is idle.
         */
        int featureRegrowthCursor{0};

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

        /**
         * Puts a saved feature back into the slot it was saved from, after
         * `features.restoreLayout`. Only a load has any business calling
         * this: it skips the occupancy check and keeps the hit points the
         * feature arrives with. See VectorMap::Layout for why a load wants
         * the slot back rather than just the feature.
         */
        FeatureId addFeatureInSlot(unsigned int slot, MapFeature&& newFeature);

        /** Removes a feature and frees the grid cells it occupied. No-op if the id is stale. */
        void deleteFeature(FeatureId id);

        /**
         * Removes a feature and, if a replacement definition is supplied, places a
         * feature of that type at the same position and rotation. Used wherever a
         * feature turns into a lesser one: burning out into its featureBurnt form.
         * No-op if the id is stale; the replacement is dropped if it does not fit.
         */
        /** Removes the feature and stands its replacement, if any, in its place; returns the replacement's id. */
        std::optional<FeatureId> replaceFeature(FeatureId id, const std::optional<FeatureDefinitionId>& replacement);

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
         * How many ticks it takes to capture this unit, as the original works
         * it out in the Capture mission's state 0 (0x404313-0x404407):
         *
         *     t  = trunc(BuildCostEnergy * 0.015 + BuildCostMetal * 3/14 + 150)
         *     t  = min(t, 1800)
         *     t  = t * (hitPoints + maxHitPoints) / (2 * maxHitPoints)
         *     t  = t * (kills/5 + 10) / 10
         *
         * so a damaged unit changes hands faster (down to half the time at
         * death's door) and a veteran one slower (+10% per five kills, with no
         * ceiling -- unlike the damage tiers, which stop at five). Note what is
         * *not* in it: the captor's `workertime`. Capture runs at one tick of
         * progress per tick for everybody. See TOTALA-EXE.md §96.
         */
        unsigned int computeCaptureTime(const UnitState& target) const;

        /**
         * Hands an enemy unit to a new owner: it drops its orders and weapon
         * targets and a UnitCapturedEvent is emitted. Returns true when the
         * unit changed hands, was already the captor's, or no longer exists.
         *
         * The work of getting here is counted on the capture order, not here
         * and not on the target -- see CaptureOrder.
         *
         * `captorUnitId` names the unit that did it, where a unit did; it
         * rides along on the event so the scene can play the captor's slot 16
         * `capture` sound (TOTALA-EXE.md §97) and is otherwise unused.
         */
        bool captureUnit(UnitId targetId, PlayerId captor, std::optional<UnitId> captorUnitId = std::nullopt);

        /** Length of the self-destruct countdown, as in TA. */
        static constexpr unsigned int SelfDestructCountdownTicks = 5 * SimTicksPerSecond;

        /**
         * The longest a capture can take, however expensive the target. The
         * original clamps the raw build-cost figure to 0x708 at 0x40438A,
         * before the damage and veterancy scaling, so a healthy veteran can
         * still run past a minute.
         */
        static constexpr unsigned int MaxCaptureTicks = 1800;

        /**
         * The longest a unit can be stunned for, however many EMP hits land on
         * it. The original clamps the paralyse order's duration to 0x708
         * (0x402D33) -- which is also exactly what the EMP missile does to a
         * CORE unit in one hit, so the shipped nuke buys the whole minute.
         */
        static constexpr unsigned int MaxParalysisTicks = 1800;

        /**
         * How long a freshly placed nanoframe is left alone before its
         * `GetBuilt` mission first looks for a builder: state 0 schedules
         * +300, state 1 schedules +30 without testing anything (0x402DA0).
         * A frame placed and never touched therefore decays first at 330.
         */
        static constexpr unsigned int NanoframeDecayGraceTicks = 330;

        /** How often the frame looks again while a builder is still on it. */
        static constexpr unsigned int NanoframeDecayCheckTicks = 30;

        /**
         * How often it decays once nobody is, and the `n` in TA's
         * `buildtime * n / buildCostEnergy` (0x41BCD0). The build time cancels
         * out of that division against the one the build routine does, which
         * is why the rate is a flat one energy-point of the frame's cost per
         * tick whatever else the unit is: 760 ticks for an ARMSOLAR nanoframe,
         * twenty minutes for a full ARMFUS one.
         */
        static constexpr unsigned int NanoframeDecayTicks = 11;

        /** Starts a unit's self-destruct countdown, or cancels it if one is already running. */
        void toggleSelfDestruct(UnitId unitId);

        /** Income multiplier for a player: 1 for everyone except cheating computer players. */
        float resourceBonusFor(PlayerId playerId) const;

        /**
         * The vision grid cell containing a world position. The returned cell
         * may lie outside the grid; callers must bounds check, which
         * PlayerVisibility::contains does.
         *
         * The grid is indexed in *projected* space, not in plan view, exactly
         * as Total Annihilation indexes it. RWE draws the world with
         * cabinetProjection(0, 0.5) applied after the view rotation, so a
         * world point (x, y, z) lands on screen at (x, z - y/2), and the
         * vision grid is aligned to that screen space. Writing
         *
         *   H = the terrain heightmap sample under the position (0 off the map),
         *   L = terrain.leftInWorldUnits(),
         *   T = terrain.topInWorldUnits(),
         *   C = MapTerrain::HeightTileWidthInWorldUnits
         *         * PlayerVisibility::VisionCellSizeInTiles   (= 32 world units),
         *
         * the cell is
         *
         *   cellX = floor((position.x - L) / C)
         *   cellY = floor((position.z - T - (H / 2)) / C)
         *
         * The "- H / 2" is the cabinet skew: high ground moves its vision cell
         * towards the top of the map by half its height in world units, so
         * that a cell's fog sits where that ground is actually drawn. The
         * terrain height under the position is used, not the position's own
         * y, so the cell is a function of (x, z) alone and a flying unit
         * shares a cell with the ground beneath it.
         *
         * Everything that samples the vision grids - the fog renderer
         * included - must apply this identical transform.
         */
        Point visionCellAt(const SimVector& position) const;

        /** The heightmap sample under a world position; 0 off the map. */
        int terrainSampleHeightAt(const SimVector& position) const;

        bool isExploredBy(PlayerId player, const SimVector& position) const;
        bool isVisibleTo(PlayerId player, const SimVector& position) const;

        /**
         * True when any line-of-sight group has explored the position.
         *
         * The one shared grid holds every group's bit, so this asks whether the
         * cell has any bit set at all. A spectator watching a recording with
         * the fog on sees every side, and this is the union of what all of them
         * have walked -- the same union the old per-player explored grids had
         * to build by hand.
         */
        bool isExploredByAnyGroup(const SimVector& position) const;

        /**
         * The bit a player reads and writes in the shared explored grid.
         *
         * The original keeps one grid and a bit per line-of-sight group, and a
         * lobby team is one such group: every player on it hands back the same
         * bit, so ground one of them has seen is explored for all of them. A
         * player with no team is a group of its own.
         */
        ExploredMask losGroupBitFor(PlayerId player) const;

        /**
         * True when the two players are on the same lobby team, or are the
         * same player. A player with no team has no allies but itself.
         */
        bool arePlayersAllied(PlayerId a, PlayerId b) const;

        /**
         * Forgets every player and the visibility that belongs to them. The
         * two lists are index-parallel and must be emptied together: clearing
         * players alone leaves a longer visibility list behind, and the next
         * pass that walks one while indexing the other runs off the end.
         */
        void clearPlayers();

        /**
         * True when a world position lies inside the reach of one of the
         * player's active radar or sonar units.
         *
         * There is no radar grid: radar reveals no ground at all, it only
         * makes units detectable, so this is the position-level form of the
         * same range test canDetectUnit applies. Distance is measured in the
         * map plane.
         */
        bool isOnRadarOf(PlayerId player, const SimVector& position) const;

        /**
         * The original's one can-see predicate, 0x465AC0: true for the
         * viewer's own units, and for another player's unit that is not
         * cloaked, is either breaking the surface or held on the viewer's
         * sonar, and stands in the viewer's line of sight.
         *
         * This is the predicate every simulation decision about who can be
         * engaged goes through, because in the original both the weapon scan
         * and the computer player pick out of a list built with it.
         */
        bool canSeeUnit(PlayerId viewer, UnitId unitId) const;

        /**
         * True when the unit can be seen, or is a bare radar or sonar contact.
         *
         * This is the player's picture -- what the minimap draws -- and not
         * something the simulation may act on: a radar contact is a blip, not
         * a target. Use canSeeUnit for anything that decides an outcome.
         */
        bool canDetectUnit(PlayerId viewer, UnitId unitId) const;

        /**
         * Whether the weapon is allowed to engage that unit at all, before
         * range comes into it: a torpedo cannot reach something standing on
         * dry land, an ordinary gun cannot reach a submerged submarine, and a
         * flak gun will not point itself at the ground. The attacker matters
         * because a weapon that is not a water weapon also needs the shooter
         * itself out of the water.
         */
        bool weaponCanHitUnit(const WeaponDefinition& weaponDefinition, const UnitState& attacker, const UnitState& target) const;

        /**
         * The height a building stands at on a given footprint (0x47D820).
         *
         * A building whose yardmap has any land cell in it stands on the
         * lowest terrain corner under those cells. One whose yardmap is all
         * water -- every shipyard, tidal generator, sonar station, torpedo
         * launcher and floating radar in the game -- stands at
         * `seaLevel - waterline` instead, which is what puts it in the water
         * rather than on top of it.
         *
         * The placement box is drawn at this same height, because in the
         * original it is the same number: the two cannot disagree.
         */
        SimScalar computeBuildHeight(const UnitDefinition& unitDefinition, const DiscreteRect& footprint) const;

        /**
         * The spatial index over the units, rebuilt if what is there was not
         * built this tick. Callers use it to narrow a search and must still
         * make the real test against live unit state: see UnitSpatialIndex.
         *
         * Rebuilding is keyed on gameTime rather than done from tick() so
         * that anything driving UnitBehaviorService directly -- the test
         * suite does -- gets a good index without having to know it exists.
         * The unit list changing within a tick invalidates it: see
         * invalidateUnitSpatialIndex. Loading a save fills the unit list
         * without going through tryAddUnit, but it fills a simulation that
         * has never built an index and the first tick after it rebuilds
         * anyway, so nothing there needs to know about this.
         */
        const UnitSpatialIndex& getUnitSpatialIndex();

        /**
         * Marks the index as needing a rebuild before the next query. Called
         * wherever a unit joins or leaves the unit list, since an index that
         * has never heard of a unit would hide it from every search.
         */
        void invalidateUnitSpatialIndex();

        /**
         * Everything a unit does about having just been shot: it points any
         * weapon that has nothing better to do at whoever hit it. Units on
         * hold fire do nothing, and a unit with no weapons has nothing to
         * do either.
         */
        void returnFire(UnitId victimId, UnitId attackerId);

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

        /**
         * As canBeBuiltAt, but blind to occupants the given player has not
         * discovered, so that refusing a placement cannot tell them something
         * is there. The original allows the placement in that case and reports
         * the failure when the builder arrives -- see TOTALA-EXE.md §27's
         * correction.
         *
         * For the interface only. Nothing in the tick may call this: what it
         * answers depends on one player's fog, where canBeBuiltAt answers the
         * same for every peer, and only the latter may decide anything the
         * simulation does.
         */
        bool canBeBuiltAtAsSeenBy(const MovementClassDefinition& mc, const std::optional<Grid<YardMapCell>>& yardMap, bool yardMapContainsGeo, unsigned int x, unsigned int y, PlayerId player) const;

        DiscreteRect computeFootprintRegion(const SimVector& position, unsigned int footprintX, unsigned int footprintZ) const;

        DiscreteRect computeFootprintRegion(const SimVector& position, const UnitDefinition::MovementCollisionInfo& collisionInfo) const;

        bool anyFeatureOccupies(const DiscreteRect& rect) const;

        /** The grid side of placing a feature, shared by addFeature and addFeatureInSlot. */
        void writeFeatureToGrids(FeatureId featureId, const FeatureDefinition& featureDefinition, const DiscreteRect& footprintRegion);

        bool containsAnyGeoMatch(const Grid<YardMapCell>& yardMap, unsigned int x, unsigned int y) const;

        bool isCollisionAt(const DiscreteRect& rect) const;

        bool isCollisionAt(const GridRegion& region) const;

        /**
         * The collision test, but with one building's own cells treated as
         * clear. An aircraft coming down on a repair pad is not blocked by the
         * pad: ARMASP's yardmap is sixteen `o` cells and `o` is
         * `YardMapCell::Ground`, which is impassable, so the ordinary test
         * refused every touchdown on a pad in the game. The original never
         * asks, because a landed aircraft is *attached* to the pad (0x48AAC0
         * links it, 0x47E570 walks the links) rather than standing on cells.
         * See TOTALA-EXE.md §94.
         */
        bool isCollisionAtIgnoringBuilding(const GridRegion& region, UnitId building) const;

        bool isCollisionAt(const DiscreteRect& rect, UnitId self) const;

        bool isYardmapBlocked(unsigned int x, unsigned int y, const Grid<YardMapCell>& yardMap, bool open, UnitId self) const;

        bool isAdjacentToObstacle(const DiscreteRect& rect) const;

        void showObject(UnitId unitId, const std::string& name);

        void hideObject(UnitId unitId, const std::string& name);

        void enableShading(UnitId unitId, const std::string& name);

        void disableShading(UnitId unitId, const std::string& name);

        /** The COB cache / dont-cache state of a piece; see UnitMesh::cached. */
        void enableCaching(UnitId unitId, const std::string& name);

        void disableCaching(UnitId unitId, const std::string& name);

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

        /**
         * Sets a flammable feature alight. It burns for burnMin..burnMax
         * seconds, tries to spread every sparkTime seconds, and is replaced by
         * its burnt form (or removed) when it burns out.
         */
        void igniteFeature(FeatureId id);

        /** Gives every flammable feature within radius a chancePercent chance to catch fire. */
        void tryIgniteFeaturesInRadius(const SimVector& position, SimScalar radius, unsigned int chancePercent);

        void updateBurningFeatures();

        /**
         * Lifts a unit off the ground into a transport: it leaves the occupancy
         * grid, drops its orders and follows the transport (hanging from piece
         * if the transport's model has it) until unloaded. Returns false if
         * either unit is missing, dead, or the unit is already carried.
         */
        bool loadUnitIntoTransport(UnitId transportId, UnitId unitId, const std::string& piece);

        /**
         * Where a carried unit would be set down if let go at position: the
         * nearest footprint its movement class may stand on, searching
         * outwards ring by ring up to twelve cells, with the point on the
         * ground at its centre (on the surface, for a floater or a
         * hovercraft). Empty if there is nowhere. An air transport asks
         * this before it descends; unloadUnitFromTransport lets go there.
         */
        std::optional<UnloadSpot> findUnloadSpot(UnitId unitId, const SimVector& position) const;

        /**
         * Sets a carried unit down on the nearest clear ground to position.
         * Returns false (and keeps carrying) if there is no room nearby.
         */
        bool unloadUnitFromTransport(UnitId transportId, UnitId unitId, const SimVector& position);

        /**
         * A transport script's attach-unit: takes the unit aboard if it is not
         * yet carried, or moves it to another of the transport's pieces if it is.
         */
        void attachUnitToTransportPiece(UnitId transportId, UnitId unitId, const std::string& piece);

        /** A transport script's drop-unit: sets the unit down where it hangs right now. */
        void dropUnitFromTransport(UnitId transportId, UnitId unitId);

        /** Moves carried units along with their transports; run after unit behaviour each tick. */
        void updateCarriedUnits();

        /** On death: leaves the transport it was in, and kills whatever it was carrying. */
        /**
         * The original's order-time load predicate (0x489A90), in full: may
         * this transport take this unit aboard? Capacity is a flat headcount
         * and an air transport carries exactly one whatever its FBI says.
         */
        bool canLoadUnitIntoTransport(UnitId transportId, UnitId unitId) const;

        void releaseTransportLinks(UnitId unitId);

        void releaseTransportLinks(UnitId unitId, std::optional<UnitId> attacker);

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

        Projectile createProjectileFromWeapon(PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker = std::nullopt, std::optional<SimVector> inheritedVelocity = std::nullopt, std::optional<SimVector> targetPosition = std::nullopt, std::optional<ProjectileId> targetProjectile = std::nullopt);

        Projectile createProjectileFromWeapon(PlayerId owner, const std::string& weaponType, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker = std::nullopt, std::optional<SimVector> inheritedVelocity = std::nullopt, std::optional<SimVector> targetPosition = std::nullopt, std::optional<ProjectileId> targetProjectile = std::nullopt);

        void spawnProjectile(PlayerId owner, const UnitWeapon& weapon, const SimVector& position, const SimVector& direction, SimScalar distanceToTarget, std::optional<UnitId> targetUnit, std::optional<UnitId> attacker = std::nullopt, std::optional<SimVector> inheritedVelocity = std::nullopt, std::optional<SimVector> targetPosition = std::nullopt, std::optional<ProjectileId> targetProjectile = std::nullopt);

        WinStatus computeWinStatus() const;

        bool addResourceDelta(const UnitId& unitId, const Energy& apparentEnergy, const Metal& apparentMetal, const Energy& actualEnergy, const Metal& actualMetal);
        bool addResourceDelta(const UnitId& unitId, const Energy& energy, const Metal& metal);

        /**
         * The single-resource request, `0x401180`. A repair asks through this
         * rather than through `0x4011C0`, the two-resource one the build path
         * uses, and the difference is which debt is consulted: this one looks
         * at the unit's **energy** debt alone, so a builder whose owner owes
         * metal can still mend something. The demand is booked for the display
         * either way, as it is there. See TOTALA-EXE.md §94.
         */
        bool addEnergyRequest(const UnitId& unitId, const Energy& amount);

        bool trySetYardOpen(const UnitId& unitId, bool open);

        void emitBuggerOff(const UnitId& unitId);

        void tellToBuggerOff(const UnitId& unitId, const DiscreteRect& rect);

        GameHash computeHash() const;

        void activateUnit(UnitId unitId);

        void deactivateUnit(UnitId unitId);

        /**
         * Add `count` rounds to the queue of the unit's stockpiled weapon, or
         * take them off again with a negative count. The original's fire button
         * only ever looks at weapon one and both launchers that ship put their
         * missile there, so the first stockpiled weapon is the one meant.
         */
        void modifyStockpileQueue(UnitId unitId, int count);

        /**
         * The unit's stockpiled weapon, for anything that wants to read the
         * magazine without knowing which slot it is in. Nothing if the unit has
         * none, which is every unit but a launcher.
         */
        std::optional<std::reference_wrapper<const UnitWeapon>> tryGetStockpileWeapon(UnitId unitId) const;

        /**
         * Whether a point falls inside an interceptor's coverage. The original
         * makes this a square rather than a circle and ignores Y outright
         * (0x49D18D): `|dx|` and `|dz|` are each compared against twice the
         * coverage after the coverage has been added, which is the usual
         * unsigned-wrap way of writing `-c <= d <= c` in one branch.
         */
        static bool isWithinCoverage(const SimVector& launcher, const SimVector& point, SimScalar coverage);

        /**
         * What an interceptor weapon should shoot at, or nothing. The original
         * refuses outright with an empty magazine, then takes the first live
         * projectile that is not the launcher's own side, whose weapon carries
         * `targetable`, whose aim point is inside the coverage, and that no
         * other projectile has already claimed (0x49D120).
         */
        std::optional<ProjectileId> findInterceptTarget(UnitId launcherId, unsigned int weaponIndex) const;

        /**
         * Detonate every live projectile within `radius` of a blast, which is
         * what an interceptor's explosion does to everything around it
         * (0x49A664). The test is a sphere and strictly inside -- `jge` skips
         * at exactly the radius -- and unlike the acquisition it re-checks
         * nothing: not `targetable`, not the owner. Anything in the blast dies,
         * which is how one anti-nuke can clear a salvo.
         */
        void detonateProjectilesInBlast(std::optional<ProjectileId> source, const SimVector& position, SimScalar radius);

        /**
         * Take a unit off the board with no wreck, no explosion and no
         * `Killed` script, counting it as a loss for its owner. This is a
         * nanoframe shot to pieces, which the original records as an ordinary
         * weapon death (cause 1) -- the owner's Losses go up, but the
         * attacker's Kills do not, the build-progress test at 0x4869A7
         * standing in front of that one.
         */
        void quietlyKillUnit(UnitId unitId);

        /**
         * The same, for death cause 9: a nanoframe the build tick gave up on
         * (0x41BC49) or one its builder took back (0x402701). Nobody's
         * counters move at all -- the dispatch at 0x48688C accepts only causes
         * 1 to 6 and rejects this one before reaching the table -- because a
         * frame that was never finished was never a unit.
         */
        void removeUnfinishedUnit(UnitId unitId);

        /** What the two above share; the flag is the only thing between them. */
        void quietlyKillUnit(UnitId unitId, bool countAsLoss);

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

        /**
         * As above, but for a death caused by a blow whose size is known.
         *
         * `overkill` is the damage left over once the unit's remaining hit
         * points were paid for, and it decides the severity the COB `Killed`
         * script is given -- see the note at the call site.
         */
        void killUnit(UnitId unitId, std::optional<UnitId> attacker, unsigned int overkill);

        void applyDamage(UnitId unitId, unsigned int damagePoints);

        /**
         * Apply damage and, if the unit is killed, credit the attacker.
         * Same crediting rules as killUnit(UnitId, std::optional<UnitId>).
         */
        void applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker);

        /**
         * As above, but `paralyzer` selects TA's damage type 2 (0x499E20): the
         * number is spent on stun time instead of hit points.
         */
        void applyDamage(UnitId unitId, unsigned int damagePoints, std::optional<UnitId> attacker, bool paralyzer);

        void applyDamageInRadius(const SimVector& position, SimScalar radius, const Projectile& projectile);

        void doProjectileImpact(const Projectile& projectile, ImpactType impactType, std::optional<ProjectileId> projectileId = std::nullopt);

        std::optional<SimVector> getSelfPropelledAimPoint(const Projectile& projectile, const ProjectilePhysicsTypeSelfPropelled& p);

        /** Runs one tick of a missile's motor and guidance. False means it detonated where it is. */
        bool updateSelfPropelledProjectile(Projectile& projectile, const ProjectilePhysicsTypeSelfPropelled& p);

        void updateProjectiles();

        void killPlayer(PlayerId playerId);

        void processVictoryCondition();

        void updateWind();

        void updateResources();

        void trySpawnFeature(const std::string& featureType, const SimVector& position, SimAngle rotation, bool isFeature = false);

        /** Sinks wreckage that went into the water; see MapFeature::velocity. */
        void updateFallingFeatures();

        void deleteDeadUnits();

        void updateSelfRepair();

        void updateSelfDestructs();

        /**
         * Runs the `GetBuilt` timer on every nanoframe: a frame nobody has
         * worked on for a period loses build progress, and with it hit points,
         * until there is nothing left of it and it is removed.
         */
        void updateNanoframeDecay();

        /**
         * Gives one map square a chance to seed a copy of whatever feature stands
         * on it into a nearby empty square. See §NN of docs/TOTALA-EXE.md; nothing
         * in the shipped data sets `reproduce`, so on stock content this never
         * fires and only a mod will see it.
         */
        void updateFeatureRegrowth();

        void updateVisibility();

        /**
         * The explored bit for the player just appended to `players`: shared
         * with the earliest earlier player on its team, or the lowest bit no
         * group holds yet. Called from addPlayer, so the assignment follows
         * player order and is a pure function of the player list.
         */
        ExploredMask nextLosGroupBit() const;

        /**
         * Holds off the cloak of any cloakable unit with a live enemy standing
         * within its MinCloakDistance, for three seconds counted from the last
         * tick one was that close.
         */
        void updateCloakSuppression();

        /** The height of a definition's model, or zero if it has none loaded. */
        SimScalar modelHeightOf(const UnitDefinition& unitDefinition) const;

        void deleteDeadProjectiles();

        void spawnNewUnits();

        /**
         * What to do with a creation request whose site was occupied: count
         * the attempt, tell the player the first time and again when it gives
         * up, and say whether to keep waiting or abandon it.
         */
        UnitCreationStatus retryBlockedSite(UnitId unitId, const UnitCreationStatusPending& pending);

        /**
         * The unit a corpse raises into, if any.
         *
         * The corpse's name up to its first underscore, upper-cased and
         * looked up: ARMCK_DEAD gives ARMCK. A name with no underscore, or
         * one whose stem names nothing, raises nothing. See
         * TOTALA-EXE.md S:98.
         */
        std::optional<std::string> resurrectedUnitType(const std::string& featureName) const;

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
