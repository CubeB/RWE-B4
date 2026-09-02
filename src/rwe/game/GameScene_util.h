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
}
