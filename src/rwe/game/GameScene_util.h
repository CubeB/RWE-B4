#pragma once

#include <rwe/RenderService.h>
#include <rwe/collections/VectorMap.h>
#include <rwe/game/GameMediaDatabase.h>
#include <rwe/game/Particle.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/math/Matrix4x.h>
#include <rwe/pathfinding/AStarPathFinder.h>
#include <rwe/pathfinding/PathCost.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/OccupiedGrid.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/ProjectileId.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitMesh.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/UnitPieceDefinition.h>
#include <rwe/sim/UnitState.h>
#include <vector>

namespace rwe
{
    /**
     * Whether a model standing at a world position could reach the view.
     *
     * The renderer walked every unit on the map, worked out a transform for
     * every piece of it and put the result in a batch whether or not the
     * camera was pointing anywhere near it. On a battlefield of eight hundred
     * units that is ten thousand piece transforms and ten thousand draw calls
     * for the models, and as many again for their shadows, when a screenful
     * is a few hundred.
     *
     * The world projection is orthographic, so a point goes straight to clip
     * space with no divide, and a world length scales into clip space by a
     * constant. That makes the test a transform and four comparisons, with
     * the model's own size added to the box so nothing pops in at the edge.
     */
    struct ViewCullTest
    {
        Matrix4f viewProjectionMatrix;

        /** How much clip space one world unit of model radius is worth. */
        float clipPerWorldUnit;

        bool couldBeVisible(const Vector3f& position, float worldRadius) const
        {
            auto clip = viewProjectionMatrix * position;
            auto bound = 1.0f + (worldRadius * clipPerWorldUnit);
            return clip.x >= -bound && clip.x <= bound && clip.y >= -bound && clip.y <= bound;
        }
    };

    ViewCullTest makeViewCullTest(const Matrix4f& viewProjectionMatrix);

    /**
     * Radius, in world units, used to grow the cull box around a unit or a
     * feature. Comfortably larger than the tallest model TA ships and than
     * the widest footprint, so a model whose origin is off screen but whose
     * top or side is not is still drawn.
     */
    constexpr float ViewCullModelRadius = 256.0f;

    /** What one viewer is shown of one unit. */
    enum class UnitDrawStyle
    {
        /** Nothing at all. */
        Hidden,

        /** The ordinary opaque draw. */
        Solid,

        /** Averaged with whatever is behind it. */
        Cloaked,
    };

    /**
     * The original decides whether to draw a unit at all in one predicate,
     * `0x465AE8`: a unit of your own passes at `0x465AD7` before anything else
     * is looked at, and a cloaked unit that is not yours returns zero before
     * line of sight is even consulted. Having decided to draw it, the same
     * cloak flag picks the blit at `0x459779` -- and there is no ownership test
     * there, so your own cloaked Commander is a ghost to you as well. Cloak is
     * therefore not "invisible": it is invisible to everyone else and
     * see-through to you.
     */
    UnitDrawStyle computeUnitDrawStyle(bool ownedByViewer, bool cloaked, bool positionVisible);

    /**
     * What a pixel of a cloaked unit comes out as over a given background.
     * This is the CPU statement of what CloakBlendFactor asks the blend
     * hardware for, and of what the original's ALPHA TABLE holds: the midpoint
     * of the two, with neither colour privileged over the other.
     */
    Vector3f blendCloakedColor(const Vector3f& unitColor, const Vector3f& backgroundColor);

    void
    drawPathfindingVisualisation(const MapTerrain& terrain, const AStarPathInfo<Point, PathCost>& pathInfo, ColoredMeshBatch& batch);

    void
    drawTerrainArrow(const MapTerrain& terrain, const Point& start, const Point& end, const Color& color, ColoredMeshBatch& batch);

    void drawOccupiedGrid(const Vector3f& cameraPosition, float viewportWidth, float viewportHeight, const MapTerrain& terrain, const OccupiedGrid& occupiedGrid, ColoredMeshBatch& batch);

    void drawMovementClassCollisionGrid(const MapTerrain& terrain, const Grid<char>& movementClassGrid, const Vector3f& cameraPosition, float viewportWidth, float viewportHeight, ColoredMeshBatch& batch);

    void drawUnit(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        PlayerColorIndex playerColorIndex,
        unsigned int unitIndex,
        unsigned int gameTime,
        float frac,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch);

    void drawMeshFeature(
        const std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const MapFeature& feature,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch);

    /**
     * Whether this unit gets a shadow at all.
     *
     * The original's two shadow passes both start by testing bit 25 of the
     * definition's flag word and skipping the draw when it is set
     * (0x4592AC, 0x4594C0). Both are also under a global shadows-on option,
     * `WORD [0x511DE8+0x37F06]` bit 2, which RWE has no equivalent of and
     * which is why the caller is only ever asked about the unit.
     */
    bool unitCastsShadow(const UnitDefinition& unitDefinition);

    void drawUnitShadow(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        float groundHeight,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitShadowMeshBatch& batch);

    void drawFeatureMeshShadow(
        const std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const MapFeature& feature,
        float groundHeight,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitShadowMeshBatch& batch);

    /** fogged draws the sprite in fog-of-war grey; currentTime drives the burning animation. */
    void drawFeature(
        const GameMediaDatabase& gameMediaDatabase,
        const MapFeature& feature,
        const FeatureDefinition& featureDefinition,
        const Matrix4f& viewProjectionMatrix,
        GameTime currentTime,
        bool fogged,
        SpriteBatch& batch);
    void drawFeatureShadow(
        const GameMediaDatabase& gameMediaDatabase,
        const MapFeature& feature,
        const FeatureDefinition& featureDefinition,
        const Matrix4f& viewProjectionMatrix,
        bool fogged,
        SpriteBatch& batch);

    /** One piece of a unit model at an arbitrary transform, for debris. */
    void drawDebrisPiece(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const std::string& objectName,
        const std::string& pieceName,
        const Matrix4f& matrix,
        PlayerColorIndex playerColorIndex,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        UnitMeshBatch& batch);

    /** A small dark square: a fragment of a shattered piece. */
    void drawDebrisShard(const Vector3f& position, ColoredMeshBatch& batch);

    /**
     * How the original draws a nanoframe at one point in its construction:
     * a height threshold and what to do above it, on it and below it.
     */
    struct BuildPhase
    {
        float ratio{0.0f};
        BuildFillMode aboveMode{BuildFillMode::Erase};
        BuildFillMode bandMode{BuildFillMode::Erase};
        BuildFillMode belowMode{BuildFillMode::Erase};
        Vector3f colorA;
        Vector3f colorB;
    };

    BuildPhase computeBuildPhase(float percentComplete, unsigned int unitIndex, unsigned int gameTime);

    /**
     * The two colours the original animates its construction display with,
     * both triangle waves over palette entries 160..175 but running at
     * different rates: A takes about 0.97s to come round, B about 0.56s.
     * unitIndex offsets the phase so neighbouring nanoframes are not in step.
     */
    Vector3f buildCycleColorA(unsigned int unitIndex, unsigned int gameTime);
    Vector3f buildCycleColorB(unsigned int unitIndex, unsigned int gameTime);

    /**
     * Outlines the polygons of a nanoframe that face the camera, in one colour.
     * Edges resting on the ground are left out, and the lines are nudged towards
     * the camera so the depth buffer hides those behind other parts of the model.
     */
    void drawUnitWireframe(
        const GameMediaDatabase& gameMediaDatabase,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        const Vector3f& toCamera,
        const Vector3f& color,
        ColoredMeshBatch& batch);

    /** The unit's model as the camera sees it, for stencil cut-outs. */
    void drawUnitSilhouette(
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const UnitModelDefinition& modelDefinition,
        float frac,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        std::vector<UnitTextureMeshRenderInfo>& out);

    void drawSpriteParticle(const GameMediaDatabase& gameMediaDatabase, GameTime currentTime, const Matrix4f& viewProjectionMatrix, const Particle& particle, SpriteBatch& batch);

    void drawWakeParticle(const GameMediaDatabase& gameMediaDatabase, GameTime currentTime, const Matrix4f& viewProjectionMatrix, const Particle& particle, ColoredMeshBatch& batch);

    /** frac is the fraction of the current tick that has elapsed, for smooth motion between ticks. */
    void drawNanoParticle(GameTime currentTime, float frac, const Particle& particle, ColoredMeshBatch& batch);

    void updateParticles(const GameMediaDatabase& gameMediaDatabase, const MapTerrain& terrain, GameTime currentTime, std::vector<Particle>& particles);

    /**
     * A thermal vent puffs once every this many ticks, for as long as the map
     * lasts.
     *
     * The original gives every geothermal feature its own emitter object the
     * moment the feature is placed (0x423F73 reads the geothermal bit,
     * 0x423FE3 makes the emitter). Init at 0x475150 sets the interval to five
     * ticks, and the emitter's is-it-finished at 0x475330 is `return 0`, so it
     * never stops. Every vent on a map is placed on the same tick, so they all
     * puff together; what stops that reading as a metronome is that each puff
     * rolls its own frame schedule.
     */
    const unsigned int geoVentSteamIntervalTicks = 5;

    /**
     * How fast a vent's steam rises, in world units per tick.
     *
     * The vent emitter's per-tick update at 0x475640 lifts a puff by sixteen
     * times the map's gravity, where the damage smoke of the same class
     * (0x475380) lifts it by only four. On the 112 that nearly every shipped
     * map uses, four works out at the half a unit the rest of the smoke here
     * uses, so sixteen is two.
     */
    const float geoVentSteamRiseRate = 2.0f;

    /**
     * Where on the map steam is coming out of the ground.
     *
     * One point per geothermal feature, at the centre of its footprint on the
     * ground, which is exactly where the original puts the emitter: when no
     * position is handed to it, 0x423F8D computes `(2*cell + footprint) << 19`
     * for each of x and z and takes the ground height between them, which is
     * the same point computeFeaturePosition already stores on the feature.
     * The puffs carry no spread -- 0x4752A5 copies the emitter's position into
     * the puff unchanged -- so the plume leaves from one spot.
     */
    std::vector<Vector3f> findGeoVentSteamPoints(const GameSimulation& simulation);

    void drawProjectiles(
        const GameSimulation& sim,
        const GameMediaDatabase& gameMediaDatabase,
        const Matrix4f& viewProjectionMatrix,
        const VectorMap<Projectile, ProjectileIdTag>& projectiles,
        GameTime currentTime,
        float frac,
        TextureIdentifier unitTextureAtlas,
        std::vector<SharedTextureHandle>& unitTeamTextureAtlases,
        ColoredMeshBatch& coloredMeshbatch,
        SpriteBatch& spriteBatch,
        UnitMeshBatch& unitMeshBatch);

    void drawSelectionRect(const GameMediaDatabase& gameMediaDatabase, const Matrix4f& viewProjectionMatrix, const UnitState& unit, const UnitDefinition& unitDefinition, float frac, ColoredMeshesBatch& batch);

    /**
     * Where a wake dot starts, which way it drifts and how long it lasts.
     * All four wake types come out of this one function; see
     * GameScene::emitWakeFromPiece.
     */
    struct WakeEmission
    {
        Vector3f spawnPosition;
        Vector3f velocity;
        GameTime duration;
    };

    WakeEmission computeWakeEmission(const Vector3f& firstVertex, const Vector3f& secondVertex, bool reverse, unsigned int rampPeriod);

    /**
     * Which of the seven water blues a wake dot is showing at a given age.
     * The original steps one entry every rampPeriod ticks over a life of
     * exactly six steps, so this never needs to wrap.
     */
    std::size_t wakeColorIndex(unsigned int age, unsigned int rampPeriod);

    /**
     * The camera shake the original runs from `shakemagnitude` and
     * `shakeduration` (TotalA.exe 0x41C5E0-0x41C7B7). Render-side only: the
     * original keeps it in globals next to the camera scroll position and
     * nothing in the simulation ever reads it back.
     */
    struct ScreenShakeState
    {
        /** Ticks left to run, counted down once a frame. */
        int ticksRemaining{0};

        /** What ticksRemaining started at, which is what the ramp divides by. */
        int totalTicks{0};

        int magnitudeX{0};
        int magnitudeY{0};

        bool running{false};
    };

    /**
     * Folds another explosion into whatever shake is already running, the way
     * 0x41C640 does it: magnitudes add, but durations are *averaged* rather
     * than taking the longer of the two.
     */
    void accumulateScreenShake(ScreenShakeState& state, int magnitude, int durationTicks);

    /**
     * This frame's amplitudes, which ramp linearly to nothing over the
     * shake's life (0x41C721).
     */
    std::pair<int, int> screenShakeAmplitudes(const ScreenShakeState& state);

    /** Counts the shake down by a frame and retires it when it runs out. */
    void advanceScreenShake(ScreenShakeState& state);

    /**
     * What the footer's MISSIONTEXT line says the unit is doing.
     *
     * The original does not compose this string: every mission record carries
     * a display name at its +0x00 (the mission tables at 0x4FC490 for ground
     * and 0x4FCA18 for air), and 0x439DF0 hands the footer the name belonging
     * to the unit's current mission. RWE has orders rather than missions, so
     * this enumeration is the join between the two -- one entry per distinct
     * string in those tables that an RWE order can reach.
     */
    enum class UnitActivity
    {
        Standby,
        Moving,
        Attacking,
        Annihilating,
        Nanolathing,
        Guarding,
        Reclaiming,
        Repairing,
        Patrolling,
        Capturing,
        Resurrecting,
        Loading,
        Unloading,
        Landing,
        UnderRepair,
        UnderConstruction,
        BeingTransported,
        Paralyzed,
        SelfDestructing,
    };

    /**
     * The original's own wording, transcribed from the two mission tables.
     * These are the strings the shipped executable draws, so they are not
     * open to improvement.
     */
    const char* missionDisplayName(UnitActivity activity);

    /** Which of those the hovered unit is currently doing. */
    UnitActivity unitActivity(const UnitState& unit, bool underConstruction, bool weaponQueued);

    /**
     * The kills line under the damage bar (0x46B2B8-0x46B3B2).
     *
     * Empty at zero kills, because the original tests the count against zero
     * and skips the whole block. Singular at one. From five upward the string
     * gains a Veteran suffix -- the comparison is jbe against 4, and there is
     * no other veterancy display anywhere in the binary.
     */
    std::string killsCaption(unsigned int kills);

    /**
     * What the unit's current order is pointed at, for the footer's second
     * name-and-bar slot.
     *
     * The original reads this straight off the mission (0x439DD0 returns
     * mission+0x16, the mission's target unit), so it is not a build-only
     * readout: a guard shows what it is guarding and an attacker what it is
     * shooting at. A build order names a type rather than a unit until the
     * nanoframe exists, which is what buildOrderUnitId holds.
     */
    std::optional<UnitId> unitOrderTargetUnit(const UnitState& unit);
}
