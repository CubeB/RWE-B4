#pragma once

#include <array>
#include <deque>
#include <fstream>
#include <functional>
#include <optional>
#include <queue>
#include <rwe/AudioService.h>
#include <rwe/CroppedViewport.h>
#include <rwe/CursorService.h>
#include <rwe/RenderService.h>
#include <rwe/SceneContext.h>
#include <rwe/TextureService.h>
#include <rwe/UiRenderService.h>
#include <rwe/Viewport.h>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/game/GameCameraState.h>
#include <rwe/game/GameMediaDatabase.h>
#include <rwe/game/GameNetworkService.h>
#include <rwe/game/GameSpeed.h>
#include <rwe/game/InGameSoundsInfo.h>
#include <random>
#include <rwe/game/Particle.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/game/SceneTime.h>
#include <rwe/game/UnitSoundType.h>
#include <rwe/game/WeaponMediaInfo.h>
#include <rwe/grid/DiscreteRect.h>
#include <rwe/io/featuretdf/FeatureTdf.h>
#include <rwe/observable/BehaviorSubject.h>
#include <rwe/render/FogTiles.h>
#include <rwe/scene/Scene.h>
#include <rwe/scene/util.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/OccupiedGrid.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitState.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiPanel.h>
#include <unordered_set>
#include <variant>

namespace rwe
{
    struct GameSceneTimeAction
    {
        using Time = SceneTime;
        Time triggerTime;
        std::function<void()> callback;

        GameSceneTimeAction(Time triggerTime, const std::function<void()>& callback)
            : triggerTime(triggerTime), callback(callback)
        {
        }

        GameSceneTimeAction(Time triggerTime, std::function<void()>&& callback)
            : triggerTime(triggerTime), callback(std::move(callback))
        {
        }
    };

    struct AttackCursorMode
    {
        bool operator==(const AttackCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const AttackCursorMode& /*rhs*/) const { return false; }
    };

    struct MoveCursorMode
    {
        bool operator==(const MoveCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const MoveCursorMode& /*rhs*/) const { return false; }
    };

    struct GuardCursorMode
    {
        bool operator==(const GuardCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const GuardCursorMode& /*rhs*/) const { return false; }
    };

    struct ReclaimCursorMode
    {
        bool operator==(const ReclaimCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const ReclaimCursorMode& /*rhs*/) const { return false; }
    };

    struct RepairCursorMode
    {
        bool operator==(const RepairCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const RepairCursorMode& /*rhs*/) const { return false; }
    };

    struct PatrolCursorMode
    {
        bool operator==(const PatrolCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const PatrolCursorMode& /*rhs*/) const { return false; }
    };

    struct CaptureCursorMode
    {
        bool operator==(const CaptureCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const CaptureCursorMode& /*rhs*/) const { return false; }
    };

    struct LoadCursorMode
    {
        bool operator==(const LoadCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const LoadCursorMode& /*rhs*/) const { return false; }
    };

    struct UnloadCursorMode
    {
        bool operator==(const UnloadCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const UnloadCursorMode& /*rhs*/) const { return false; }
    };

    struct NormalCursorMode
    {
        struct SelectingState
        {
            SceneTime startTime;
            Point startPosition;
            explicit SelectingState(SceneTime startTime, const Point& startPosition) : startTime(startTime), startPosition(startPosition) {}
            SelectingState(int x, int y) : startPosition(x, y) {}
            bool operator==(const SelectingState& rhs) const
            {
                return startTime == rhs.startTime && startPosition == rhs.startPosition;
            }
            bool operator!=(const SelectingState& rhs) const
            {
                return !(rhs == *this);
            }
        };
        struct DraggingMinimapState
        {
            bool operator==(const DraggingMinimapState& /*rhs*/) const { return true; }
            bool operator!=(const DraggingMinimapState& /*rhs*/) const { return false; }
        };
        struct UpState
        {
            bool operator==(const UpState& /*rhs*/) const { return true; }
            bool operator!=(const UpState& /*rhs*/) const { return false; }
        };
        using State = std::variant<SelectingState, DraggingMinimapState, UpState>;

        State state{UpState()};

        bool operator==(const NormalCursorMode& rhs) const
        {
            return state == rhs.state;
        }

        bool operator!=(const NormalCursorMode& rhs) const
        {
            return !(rhs == *this);
        }
    };

    struct BuildCursorMode
    {
        std::string unitType;

        bool operator==(const BuildCursorMode& rhs) const
        {
            return unitType == rhs.unitType;
        }

        bool operator!=(const BuildCursorMode& rhs) const
        {
            return !(rhs == *this);
        }
    };

    using CursorMode = std::variant<AttackCursorMode, MoveCursorMode, GuardCursorMode, ReclaimCursorMode, RepairCursorMode, PatrolCursorMode, CaptureCursorMode, LoadCursorMode, UnloadCursorMode, BuildCursorMode, NormalCursorMode>;

    struct UnitGuiInfo
    {
        enum class Section
        {
            Build,
            Orders,
        };

        Section section;
        unsigned int currentBuildPage;
    };

    struct HoverBuildInfo
    {
        DiscreteRect rect;
        bool isValid;
    };

    class GameScene : public Scene
    {
    public:
        static inline const SimScalar SecondsPerTick = SimScalar(SceneTickInterval) / 1000_ss;

        static constexpr int GuiSizeLeft = 128;
        static constexpr int GuiSizeRight = 0;
        static constexpr int GuiSizeTop = 32;
        static constexpr int GuiSizeBottom = 32;

    private:
        static const unsigned int UnitSelectChannel = 0;

        static const unsigned int reservedChannelsCount = 1;

        /**
         * Speed the camera pans via the arrow keys
         * in world units/second.
         */
        static constexpr float CameraPanSpeed = 1000.0f;

        static const Rectangle2f minimapViewport;

        SceneContext sceneContext;

        std::unique_ptr<Subscription> audioSub = sceneContext.audioService->getChannelFinished().subscribe([this](int channel) { onChannelFinished(channel); });

        CroppedViewport worldViewport;

        std::unique_ptr<PlayerCommandService> playerCommandService;

        GameCameraState worldCameraState;

        GameMediaDatabase gameMediaDatabase;
        SharedTextureHandle unitTextureAtlas;
        std::vector<SharedTextureHandle> unitTeamTextureAtlases;

        UiRenderService worldUiRenderService;
        UiRenderService chromeUiRenderService;

        GameSimulation simulation;

        MapTerrainGraphics terrainGraphics;

        BuilderGuisDatabase builderGuisDatabase;

        std::unique_ptr<GameNetworkService> gameNetworkService;

        std::shared_ptr<Sprite> minimap;
        std::shared_ptr<SpriteSeries> minimapDots;
        std::shared_ptr<Sprite> minimapDotHighlight;
        Rectangle2f minimapRect;

        std::unique_ptr<UiPanel> currentPanel;
        std::optional<std::unique_ptr<UiPanel>> nextPanel;

        InGameSoundsInfo sounds;

        std::shared_ptr<SpriteSeries> guiFont;

        PlayerId localPlayerId;

        SceneTime sceneTime{0};

        bool left{false};
        bool right{false};
        bool up{false};
        bool down{false};

        bool leftCtrlDown{false};
        bool rightCtrlDown{false};
        bool leftShiftDown{false};
        bool rightShiftDown{false};

        struct CameraControlStateFree
        {
        };
        struct CameraControlStateTrackingUnit
        {
        };
        struct CameraControlStateMiddleMousePan
        {
            Point previousCursorPosition;
        };

        using CameraControlState = std::variant<CameraControlStateFree, CameraControlStateTrackingUnit, CameraControlStateMiddleMousePan>;

        CameraControlState cameraControlState{CameraControlStateFree()};
        // We hold onto trackedUnitId outside of CameraControlStateTrackingUnit
        // because if you exit and re-enter tracking while having
        // a group of units selected, the original game will resume
        // tracking the next unit in the group.
        std::optional<UnitId> trackedUnitId;

        std::optional<UnitId> hoveredUnit;
        std::optional<FeatureId> hoveredFeature;
        std::unordered_set<UnitId> selectedUnits;

        // Control groups are scene/UI state, not part of the deterministic sim.
        // Index 0 = group bound to key '1', index 9 = group bound to key '0'.
        static constexpr int ControlGroupCount = 10;
        std::array<std::unordered_set<UnitId>, ControlGroupCount> controlGroups;

        std::optional<HoverBuildInfo> hoverBuildInfo;

        bool occupiedGridVisible{false};
        bool pathfindingVisualisationVisible{false};
        bool movementClassGridVisible{false};
        bool cursorTerrainDotVisible{false};

        bool healthBarsVisible{false};

        /** Set when the game has been decided; the result is shown until the player leaves. */
        std::optional<WinStatus> gameOver;
        GameTime gameOverTime{0};

        /** Fog of war: hide what the local player cannot see. Off reveals the whole map. */
        bool fogOfWarEnabled{true};

        /** F1: the hotkey reference overlay. */
        bool helpVisible{false};
        /** The minimap's fog: one texel per vision cell, alpha encodes unexplored / explored / visible. */
        std::optional<Sprite> fogSprite;
        GameTime fogSpriteTime{0};
        std::vector<unsigned char> fogVisibleSnapshot;
        std::vector<unsigned char> fogExploredSnapshot;
        /** TA's fog artwork, read from anims/fog.gaf the first time the fog is drawn. */
        std::optional<FogTileSet> fogTiles;
        /** The world's fog, drawn from those tiles at one texel per world unit. */
        FogRasterizer fogRasterizer;
        SharedTextureHandle fogOverlayTexture;
        unsigned int fogOverlayWidth{0};
        unsigned int fogOverlayHeight{0};
        /** World rectangle the fog overlay texture covers, for the terrain shader. */
        Rectangle2f fogOverlayBounds{Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f)};

        BehaviorSubject<CursorMode> cursorMode{NormalCursorMode()};

        std::deque<std::optional<GameSceneTimeAction>> actions;

        std::vector<PlayerCommand> localPlayerCommandBuffer;
        bool commandWasQueued{false};

        BehaviorSubject<UnitFireOrders> fireOrders{UnitFireOrders::HoldFire};
        BehaviorSubject<bool> onOff{false};

        UiFactory uiFactory;

        /** Sound lookup table, kept so a main menu scene can be built on the way out. */
        TdfBlock* audioLookup;

        std::unordered_map<UnitId, UnitGuiInfo> unitGuiInfos;

        std::unordered_map<UnitId, std::unordered_map<std::string, int>> unconfirmedBuildQueueDelta;

        std::vector<std::pair<GameTime, GameHash>> gameHashes;

        std::optional<std::ofstream> stateLogStream;

        bool showDebugWindow{false};
        char unitSpawnText[20]{""};
        int unitSpawnPlayer{0};

        /** Debug unit placer: the type picked from the list, and whether clicks place it. */
        std::string unitSpawnType;
        char unitSpawnFilter[32]{""};
        bool unitSpawnOnClick{false};
        bool unitSpawnComplete{true};
        /** Every unit type in the loaded data, sorted, built once on first use. */
        std::vector<std::string> allUnitTypes;

        std::mutex playingUnitChannelsLock;
        std::unordered_set<int> playingUnitChannels;

        std::vector<Particle> particles;

        /** A piece blown off a unit by its script, tumbling under gravity. Purely visual. */
        struct Debris
        {
            std::string objectName;
            std::string pieceName;
            PlayerColorIndex color{0u};
            Vector3f position;
            Vector3f velocity;
            Vector3f rotation;
            Vector3f angularVelocity;
            GameTime endTime;
            GameTime nextTrail;
            unsigned int flags{0};
            /** A fragment of a shattered piece, drawn as a small dark square instead of a mesh. */
            bool shard{false};
        };
        std::vector<Debris> debris;

        /** Scatter for purely visual effects; never feeds the simulation. */
        std::minstd_rand effectsRng{20260828u};

        int millisecondsBuffer{0};

        GameSpeed gameSpeed;
        bool paused{false};

        std::vector<FlashEffect> flashes;
        bool guiVisible{true};

        FrameBufferInfo worldFrameBuffer;

        TextureHandle dodgeMask;

        struct ProjectileRenderInfo
        {
            GameTime lastSmoke{GameTime(0)};
        };

        std::unordered_map<ProjectileId, ProjectileRenderInfo> projectileRenderInfos;

    public:
        GameScene(
            const SceneContext& sceneContext,
            std::unique_ptr<PlayerCommandService>&& playerCommandService,
            GameMediaDatabase&& meshDatabase,
            const GameCameraState& cameraState,
            SharedTextureHandle unitTextureAtlas,
            std::vector<SharedTextureHandle>&& unitTeamTextureAtlases,
            GameSimulation&& simulation,
            MapTerrainGraphics&& terrainGraphics,
            BuilderGuisDatabase&& builderGuisDatabase,
            std::unique_ptr<GameNetworkService>&& gameNetworkService,
            const std::shared_ptr<Sprite>& minimap,
            const std::shared_ptr<SpriteSeries>& minimapDots,
            const std::shared_ptr<Sprite>& minimapDotHighlight,
            InGameSoundsInfo sounds,
            const std::shared_ptr<SpriteSeries>& guiFont,
            PlayerId localPlayerId,
            TdfBlock* audioLookup,
            std::optional<std::ofstream>&& stateLogStream);

        void init() override;

        void render() override;

        void onKeyDown(const SDL_KeyboardEvent& keysym) override;

        void onKeyUp(const SDL_KeyboardEvent& keysym) override;

        void onMouseDown(MouseButtonEvent event) override;

        void onMouseUp(MouseButtonEvent event) override;

        void onMouseMove(MouseMoveEvent event) override;

        void onMouseWheel(MouseWheelEvent event) override;

        void update(int millisecondsElapsed) override;

        std::optional<UnitId> spawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<const std::reference_wrapper<SimAngle>> rotation);

        std::optional<std::reference_wrapper<UnitState>> spawnCompletedUnit(const std::string& unitType, PlayerId owner, const SimVector& position);

        void setCameraPosition(const Vector3f& newPosition);

        const MapTerrain& getTerrain() const;

    private:
        GameTime getGameTime() const;

        void playUiSound(const AudioService::SoundHandle& sound);

        void playNotificationSound(const PlayerId& playerId, const AudioService::SoundHandle& sound);

        void playUnitNotificationSound(const PlayerId& playerId, const std::string& unitType, UnitSoundType soundType);

        void playSoundAt(const Vector3f& position, const AudioService::SoundHandle& sound);

        void playWeaponStartSound(const Vector3f& position, const std::string& weaponType);

        void playWeaponImpactSound(const Vector3f& position, const std::string& weaponType, ImpactType impactType);

        void spawnWeaponImpactExplosion(const Vector3f& position, const std::string& weaponType, ImpactType impactType);

        void doProjectileImpact(const SimVector& position, const std::string& weaponType, ImpactType impactType);

        void createLightSmoke(const Vector3f& position);

        void createWeaponSmoke(const Vector3f& position);

        void emitLightSmokeFromPiece(UnitId unitId, const std::string& pieceName);

        void emitBlackSmokeFromPiece(UnitId unitId, const std::string& pieceName);

        void emitWake1FromPiece(UnitId unitId, const std::string& pieceName);

        /** An aircraft's exhaust: small warm sparks dropped under a thruster piece, left behind as a trail. */
        void emitVtolFromPiece(UnitId unitId, const std::string& pieceName);

        void modifyBuildQueue(UnitId unitId, const std::string& unitType, int count);

        void onChannelFinished(int channel);

        static Matrix4f worldToMinimapMatrix(const MapTerrain& terrain, const Rectangle2f& minimapRect);

        static Matrix4f minimapToWorldMatrix(const MapTerrain& terrain, const Rectangle2f& minimapRect);

        void tryTickGame();

        std::optional<UnitId> getUnitUnderCursor() const;
        std::optional<FeatureId> getFeatureUnderCursor() const;

        Vector2f screenToWorldClipSpace(Point p) const;

        bool isCursorOverMinimap() const;

        bool isCursorOverWorld() const;

        Point getMousePosition() const;

        std::optional<UnitId> getFirstCollidingUnit(const Ray3f& ray) const;
        std::optional<FeatureId> getFirstCollidingFeature(const Ray3f& ray) const;

        /**
         * Returns a value if the given ray intersects this unit
         * for the purposes of unit selection.
         * The value returned is the distance along the ray
         * where the intersection occurred.
         */
        std::optional<float> selectionIntersect(const UnitState& unit, const CollisionMesh& mesh, const Ray3f& ray) const;

        std::optional<SimVector> getMouseTerrainCoordinate() const;

        void localPlayerIssueUnitOrder(UnitId unitId, const UnitOrder& order);

        void localPlayerEnqueueUnitOrder(UnitId unitId, const UnitOrder& order);

        void localPlayerStopUnit(UnitId unitId);

        void localPlayerSelfDestructUnit(UnitId unitId);

        /** Drops the queued build order whose footprint covers position (a shift-click on a planned building). */
        void localPlayerCancelBuildOrder(UnitId unitId, const SimVector& position);

        void cancelBuildOrderAt(UnitId unitId, const SimVector& position);

        /** The position of the unit's queued build order whose footprint covers position, if any. */
        std::optional<SimVector> plannedBuildOrderAt(UnitId unitId, const SimVector& position) const;

        /** The ORDERS panel with the buttons a unit cannot use taken out. */
        std::unique_ptr<UiPanel> createOrdersPanel(std::optional<UnitId> unitId);

        void localPlayerSetFireOrders(UnitId unitId, UnitFireOrders orders);

        void localPlayerSetOnOff(UnitId unitId, bool on);

        void localPlayerModifyBuildQueue(UnitId unitId, const std::string& unitType, int count);

        void issueUnitOrder(UnitId unitId, const UnitOrder& order);

        void enqueueUnitOrder(UnitId unitId, const UnitOrder& order);

        void stopUnit(UnitId unitId);

        void setFireOrders(UnitId unitId, UnitFireOrders orders);

        void startTrack();

        void startTrackInternal(const std::vector<UnitId>& unitIds);

        bool isCtrlDown() const;

        bool isShiftDown() const;

        void handleEscapeDown();

        void returnToMainMenu();

        void renderGameOverOverlay();

        void renderHelpOverlay();

        void updateFogSprite();

        bool unitIsVisibleToLocalPlayer(const UnitState& unit) const;

        bool unitIsDetectableByLocalPlayer(const UnitState& unit) const;

        bool positionIsExploredByLocalPlayer(const SimVector& position) const;

        UnitState& getUnit(UnitId id);

        const UnitState& getUnit(UnitId id) const;

        std::optional<std::reference_wrapper<UnitState>> tryGetUnit(UnitId id);

        std::optional<std::reference_wrapper<const UnitState>> tryGetUnit(UnitId id) const;

        GamePlayerInfo& getPlayer(PlayerId player);

        const GamePlayerInfo& getPlayer(PlayerId player) const;

        bool isEnemy(UnitId id) const;

        bool isFriendly(UnitId id) const;

        void updateProjectiles();

        void processSimEvents();

        void updateFlashes();

        void processActions();

        void processPlayerCommands(const std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>& commands);

        void processPlayerCommand(PlayerId issuingPlayer, const PlayerCommand& playerCommand);

        void processUnitCommand(const PlayerUnitCommand& unitCommand);

        template <typename T>
        void delay(SceneTime interval, T&& f)
        {
            actions.emplace_back(GameSceneTimeAction(sceneTime + interval, std::forward<T>(f)));
        }

        void renderUi();

        void renderOverlay();

        void renderMinimap();

        /** The debug window's unit placer: pick a type and an owner, then drop units on the map. */
        void renderUnitPlacer();

        /** Places one unit of the placer's current type and owner, finished and ready to act. */
        void placeDebugUnit(const SimVector& position);

        void renderWorld();

        void renderDebugWindow();

        void renderUnitOrders(UnitId unitId, bool drawLines);

        void renderBuildBoxes(const UnitState& unit, const Color& color);

        void attachOrdersMenuEventHandlers();

        void onMessage(const std::string& message, ActivateMessage::Type mode);

        bool matchesWithSidePrefix(const std::string& suffix, const std::string& value) const;

        /**
         * If there is exactly one unit selected, returns the unit ID.
         * Otherwise, returns nothing.
         */
        std::optional<UnitId> getSingleSelectedUnit() const;

        void selectUnitsInBandbox(const DiscreteRect& box);

        void selectAllOnScreen();

        void toggleUnitSelection(const UnitId& unitId);

        void toggleUnitSelection(const std::unordered_set<UnitId>& units);

        void selectAdditionalUnit(const UnitId& unitId);

        void replaceUnitSelection(const UnitId& unitId);

        void replaceUnitSelection(const std::unordered_set<UnitId>& units);

        void deselectUnit(const UnitId& unitId);

        void clearUnitSelection();

        void onSelectedUnitsChanged();

        /**
         * The unit's panel state, created on demand. Never fails: a unit can
         * be selected before the spawn event that would normally set this up
         * has been processed.
         */
        UnitGuiInfo& getGuiInfo(const UnitId& unitId);

        void setNextPanel(std::unique_ptr<UiPanel>&& panel);

        void refreshBuildGuiTotal(UnitId unitId, const std::string& unitType);

        void updateUnconfirmedBuildQueueDelta(UnitId unitId, const std::string& unitType, int count);

        int getUnconfirmedBuildQueueCount(UnitId unitId, const std::string& unitType) const;

        std::unique_ptr<UiPanel> createBuildPanel(const std::string& guiname, const std::vector<GuiEntry>& panelDefinition, const std::unordered_map<std::string, int>& totals);

        template <typename T>
        std::optional<std::reference_wrapper<T>> findWithSidePrefix(UiPanel& p, const std::string& name)
        {
            for (const auto& [_, side] : *sceneContext.sideData)
            {
                auto control = p.find<T>(side.namePrefix + name);
                if (control)
                {
                    return control;
                }
            }

            return std::nullopt;
        }

        bool leftClickMode() const;

        void spawnExplosion(const Vector3f& position, const AnimLocation& anim);

        void spawnFlash(const Vector3f& position);

        void spawnSmoke(const Vector3f& position, const std::string& gaf, const std::string& anim, ParticleFinishTime duration, GameTime frameDuration);

        void spawnWake(const Vector3f& position, const Vector3f& velocity, GameTime duration);

        /** Emits this tick's nanolathe spray for every builder the local player can see. */
        void spawnNanoParticles();

        /** Turns a COB piece explosion into flying debris, shards or an explosion sprite. */
        void spawnDebris(const PieceExplodedEvent& e);

        void updateDebris();

        bool positionIsVisibleToLocalPlayer(const SimVector& position) const;

        /** One square of nano spray travelling from one point to another. */
        void spawnNanoParticle(const Vector3f& from, const Vector3f& to, unsigned char colorPhase);

        void recreateWorldRenderTextures();

        /**
         * Handler for when the player nudges the camera
         * e.g. via arrow keys or by moving the mouse cursor
         * to the edge of the screen.
         */
        void nudgeCamera(int millisecondsElapsed, const Rectangle2f& cameraConstraint, int directionX, int directionZ);

        /**
         * Handler for when the player relocates the camera
         * e.g. by clicking a location on the minimap.
         */
        void relocateCamera(const Rectangle2f& cameraConstraint, float x, float z);

        std::optional<std::string> getUnitBuildButtonUnderCursor() const;
    };
}
