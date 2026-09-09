#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <rwe/AudioService.h>
#include <rwe/game/AiArenaReport.h>
#include <rwe/game/ReplayFile.h>
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
#include <rwe/game/GameScene_util.h>
#include <rwe/game/GameSpeed.h>
#include <rwe/game/SaveFile.h>
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
#include <rwe/sim/PlayerVisibility.h>
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

    /** Picking what to fire a commandfire weapon at -- the BLAST button. */
    struct DgunCursorMode
    {
        bool operator==(const DgunCursorMode& /*rhs*/) const { return true; }
        bool operator!=(const DgunCursorMode& /*rhs*/) const { return false; }
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

    using CursorMode = std::variant<AttackCursorMode, DgunCursorMode, MoveCursorMode, GuardCursorMode, ReclaimCursorMode, RepairCursorMode, PatrolCursorMode, CaptureCursorMode, LoadCursorMode, UnloadCursorMode, BuildCursorMode, NormalCursorMode>;

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

        /**
          * The audio service outlives every scene, so this subscription has
          * to be given back by hand in the destructor: see Subscription.
          */
        std::unique_ptr<Subscription> audioSub = sceneContext.audioService->getChannelFinished().subscribe([this](int channel) { onChannelFinished(channel); });

        CroppedViewport worldViewport;

        std::unique_ptr<PlayerCommandService> playerCommandService;

        GameCameraState worldCameraState;

        GameMediaDatabase gameMediaDatabase;
        SharedTextureHandle unitTextureAtlas;
        std::vector<SharedTextureHandle> unitTeamTextureAtlases;
        SharedTextureHandle unitPaletteIndexAtlas;
        std::vector<SharedTextureHandle> unitTeamPaletteIndexAtlases;
        SharedTextureHandle shadeTableTexture;
        /** palettes/PALETTE.ALP; the building halo in worldPost.frag reads it. */
        SharedTextureHandle alphaTableTexture;

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

        /** COMIX.FNT, the original's world-text font; the speech console prints in it. */
        std::shared_ptr<SpriteSeries> speechFont;

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

        // Which units were cloaked as of last tick. The original announces
        // cloaking and decloaking to the unit's owner, and nothing about that
        // reaches its simulation, so the edge is watched from out here rather
        // than reported out of the sim.
        std::unordered_set<UnitId> cloakedUnits;

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

        /**
         * Stands in for the local player's vision while the fog is switched
         * off: every cell explored and in sight. Switching the fog off means
         * the map is permanently seen and mapped, not that the fog machinery
         * is stepped around -- everything that draws or picks still asks the
         * same questions of the same kind of grid, and simply gets "yes" for
         * an answer. Built the first time it is wanted, and rebuilt if the
         * simulation's grids are ever a different size.
         */
        mutable std::optional<PlayerVisibility> revealedVisibility;

        /** F1: the hotkey reference overlay. */
        bool helpVisible{false};
        /** The minimap's fog: one texel per vision cell, alpha encodes unexplored / explored / visible. */
        std::optional<Sprite> fogSprite;
        /**
         * The minimap's fog texture and the buffer it is filled from, both
         * kept for the life of the game: the size never changes, so there is
         * nothing to gain by asking the driver for a new one every time a
         * unit moves and a cell lights up.
         */
        SharedTextureHandle minimapFogTexture;
        std::vector<Color> minimapFogPixels;
        int minimapFogWidth{0};
        int minimapFogHeight{0};
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

        /**
         * Whether the selected unit is asking for its cloak. It is what the
         * button is lit by, and it is deliberately the request rather than the
         * cloak itself: a unit that has been decloaked by an enemy walking past
         * still has the order standing.
         */
        BehaviorSubject<bool> cloak{false};

        UiFactory uiFactory;

        /**
         * The in-game GAME OPTIONS menu (the original's ARMOPT/COROPT panel,
         * opened by Tab or F2) and whatever screen hangs off it -- the exit
         * menu, or the options composite. While anything is here, it owns
         * the input and the game is paused.
         */
        std::vector<std::unique_ptr<UiPanel>> gameMenuPanels;
        bool menuPausedGame{false};
        std::string inGameOptionsPage;


        GameOptions gameOptionsUndo;

        SoundMode soundModeSetting{SoundMode::Stereo};
        UnitSpeechLevel unitSpeechSetting{UnitSpeechLevel::Full};
        unsigned int gammaSetting{100};
        ShadingMode shadingMode{ShadingMode::Both};
        bool antiAliasEnabled{true};
        /** The purple building fringe; see GlobalConfig and TOTALA-EXE.md S:101. */
        bool buildingHaloEnabled{true};

        void applyGamma();

        void addGameMenuPanel(std::unique_ptr<UiPanel>&& panel);

        void wireInGameOptionControls();

        /** Pushes the current settings back into the menu widgets: a staged button does not advance its own display. */
        void refreshInGameOptionControls();

        /**
         * How much of the measured PALETTE.SHD ramp a model of each kind
         * gets: 0 for one the switch excludes, otherwise the category's own
         * strength. See the constants in GameScene.cpp for why the two
         * differ.
         */
        float shadeStrengthFor(bool isBuilding) const;

        /**
         * Rebuilds VISUALRT's two-stage SHADING gadget as a four-stage one.
         * The GUI files are read-only game data, so a control the original
         * does not have has to be made in code.
         */
        void widenShadingButton();
        void addBuildingHaloButton(UiPanel& panel);

        /** Finds a control by name across every open menu panel. */
        template <typename T>
        T* findInGameMenu(const std::string& name)
        {
            for (auto& panel : gameMenuPanels)
            {
                if (auto found = panel->find<T>(name))
                {
                    return &found->get();
                }
            }
            return nullptr;
        }
        std::string pendingWindowMode;

        /** Live copies of the display settings the options pages edit. */
        bool shadowsEnabled{true};
        /**
         * The original's second shadow bit, under the master above: with it
         * clear, buildings and scenery go on casting and mobile units do not.
         * rwe.cfg only -- see GlobalConfig for why it is not a button.
         */
        bool vehicleShadowsEnabled{true};
        /** The purple building halo: strength as a percentage, width in output pixels. */
        unsigned int buildingHaloStrength{100};
        unsigned int buildingHaloSaturation{65};
        unsigned int buildingHaloRedShift{50};
        unsigned int scrollSpeedSetting{100};

        /** What this game was started with, kept for the save-game header. */
        GameParameters gameParameters;

        /**
         * Set only for a computer-versus-computer measurement run. Samples
         * the simulation as it goes and, at the time limit, writes a CSV and
         * quits. See AiArenaReport for why this exists.
         */
        std::optional<AiArenaReport> arenaReport;
        std::optional<unsigned int> arenaEndTick;

        // --- Replays ---
        /** Set while recording: every command popped for a tick is written here. */
        std::optional<ReplayWriter> replayWriter;
        /**
         * Set while watching one. The commands are pushed into the command
         * service a tick at a time instead of coming from a player or an AI,
         * and the computer players are idled so they add nothing of their own.
         */
        std::optional<Replay> replayPlayback;
        /** Playing or paused; separate from the game's own pause. */
        bool replayPlaying{true};
        /**
         * Simulation ticks to run per frame while watching. One is real time.
         * This is raised rather than the game speed because the speed control
         * scales an accumulator that the per-frame tick cap then truncates,
         * which silently drops ticks and ends the replay early.
         */
        int replaySpeed{1};
        /** While seeking, run flat out until this tick is reached. */
        std::optional<unsigned int> replaySeekTarget;

        /**
         * Where the scrub handle is, in seconds. Held here rather than made
         * fresh from the playback position each frame: rebuilding it every
         * frame means the game drags the handle out from under the mouse,
         * which is what made scrubbing unusable.
         */
        int replayScrubSeconds{0};
        /** True while the handle is being dragged, so the position stops following the game. */
        bool replayScrubbing{false};
        /** Set once the end has been reached, so it is only paused there once. */
        bool replayReachedEnd{false};

        /**
         * Which player's resources and panel art the interface shows. A
         * recording has no local player in the sense the interface means, and
         * whichever slot stood in for one is not necessarily the side worth
         * watching.
         */
        std::optional<PlayerId> hudPlayerOverride;

        /**
         * Both sides' fog at once, rebuilt when the tick moves on. A spectator
         * with the fog on wants to see what each side could see, which is
         * neither player's own grid nor a fully lit map.
         */
        mutable std::optional<PlayerVisibility> combinedVisibility;
        mutable unsigned int combinedVisibilityTick{0};

        /**
         * The other recordings on disk, so one can be picked without going
         * back to a command line. Read when playback starts and when the
         * refresh button is pressed, rather than every frame: it is a
         * directory scan and a header parse per file.
         */
        std::vector<ReplaySummary> availableReplays;

        /**
         * Snapshots of the simulation taken as a recording plays, keyed by
         * the scene time each was taken at, so that a scrub backwards can
         * start from the nearest one instead of from the beginning. Each is
         * the save-game document in CBOR, which is a fraction of the json
         * object's size in memory; a thirty-minute game holds sixty of them.
         *
         * A keyframe at scene time S is the state before tick S's commands
         * are pushed, which is the state after tick S-1 has run in full.
         * Restoring it sets sceneTime to S, and the next tryTickGame feeds
         * tick S exactly as the first pass did.
         */
        std::map<unsigned int, std::vector<std::uint8_t>> replayKeyframes;
        std::size_t replayKeyframeBytes{0};
        /** Thirty seconds of game time between keyframes. */
        static constexpr unsigned int ReplayKeyframeInterval = 900;
        /**
         * RWE_REPLAY_NO_KEYFRAMES=1 in the environment: take none, so every
         * scrub backwards rebuilds the scene from the start as it did before
         * there were any. For timing one path against the other, and for
         * telling a keyframe bug from a playback one.
         */
        bool replayKeyframesDisabled{false};

        void pushReplayCommandsForTick(unsigned int tick);
        void renderReplayWindow();
        void restartReplayAt(unsigned int tick);
        void openReplay(const std::filesystem::path& path);
        void takeReplayKeyframe();
        /** Puts the simulation and the scene back to the keyframe at the given scene time. */
        void restoreReplayKeyframe(unsigned int tick, const std::vector<std::uint8_t>& keyframe);
        /**
         * Moves the playback to the given tick: winds forward from where it
         * is, or from the latest keyframe at or before the target, or if
         * there is none rebuilds the scene from the start.
         */
        void seekReplayTo(unsigned int tick);

        /**
         * Shows what no player can see: cloaked enemies, and every unit
         * whoever owns it. Distinct from fogOfWarEnabled, which only decides
         * whether the map is lit -- a cloaked unit stays hidden with the fog
         * off, because hiding it is not a fog rule.
         */
        bool spectatorMode{false};

        /** Sound lookup table, kept so a main menu scene can be built on the way out. */
        TdfBlock* audioLookup;

        std::unordered_map<UnitId, UnitGuiInfo> unitGuiInfos;

        std::unordered_map<UnitId, std::unordered_map<std::string, int>> unconfirmedBuildQueueDelta;

        /** The same thing for rounds ordered on a launcher, which has only the one queue. */
        std::unordered_map<UnitId, int> unconfirmedStockpileDelta;

        /**
         * A timed line of text in the top-left of the world view -- TA's
         * "Speech Text": the countdown, cloak reports, defeat announcements.
         */
        struct ConsoleMessage
        {
            std::string text;
            Color color;
            SceneTime expires;
        };
        std::deque<ConsoleMessage> consoleMessages;

        /** The last whole second each counting-down unit announced, so each number is said once. */
        std::unordered_map<UnitId, unsigned int> selfDestructAnnounced;

        /** Players whose defeat has already been announced. */
        std::unordered_set<unsigned int> defeatAnnounced;

        std::vector<std::pair<GameTime, GameHash>> gameHashes;

        std::optional<std::ofstream> stateLogStream;

        bool showDebugWindow{false};
        char unitSpawnText[20]{""};
        int unitSpawnPlayer{0};

        /** Debug unit placer: the type picked from the list, and whether clicks place it. */
        std::string unitSpawnType;
        char unitSpawnFilter[32]{""};
        bool unitSpawnOnClick{false};

        /** The spawner in its own window, rather than inside the debug panel. */
        bool showUnitSpawnerWindow{false};

        /**
         * What fraction of its hit points a spawned unit arrives on.
         *
         * Most of what is worth testing needs a damaged unit -- repair pads,
         * nanoframe decay, the wreck level a death picks, the info panel's
         * damage bar -- and the alternative is shooting one carefully.
         */
        int unitSpawnHealthPercent{100};

        /**
         * Unit types grouped by the FBI's own `TEDClass`, built once.
         *
         * Each entry is (category, [(code, display name)]), both sorted. The
         * display name is the FBI `Name`, which is what a player would call
         * the thing.
         */
        std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> unitTypesByCategory;

        /**
         * The hit points a debug-spawned unit arrives on: the chosen
         * percentage of its maximum, never below one, since a unit spawning
         * dead is not a thing anyone wants to test.
         */
        unsigned int debugSpawnHitPoints(const UnitDefinition& unitDefinition) const;

        void renderUnitSpawnerWindow();
        void buildUnitTypeCategories();
        bool unitSpawnComplete{true};
        /** Every unit type in the loaded data, sorted, built once on first use. */
        std::vector<std::string> allUnitTypes;

        std::mutex playingUnitChannelsLock;
        std::unordered_set<int> playingUnitChannels;

        std::vector<Particle> particles;

        /**
         * Where the wake dots' geometry is built, kept between frames for its
         * capacity alone. A wake dot is six vertices and a busy water map has
         * tens of thousands of them on screen, so a vector that starts empty
         * every frame spends the frame growing back to a couple of hundred
         * thousand entries and copying what it already had each time.
         * Cleared, not destroyed, at the top of every frame.
         */
        ColoredMeshBatch wakeBatch;

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

        /**
         * The original's situational music, decoded from the exe: tracks are
         * typed Building or Battle, a ring of thirty one-second slots scores
         * combat involving the local player, and the game switches type on
         * thresholds over that ring. Victory and Defeat types exist in the
         * original but nothing in gameplay ever triggers them, so neither do
         * we.
         */
        enum class MusicSituation
        {
            Building,
            Battle,
        };

        std::vector<std::string> buildingTracks;
        std::vector<std::string> battleTracks;
        std::vector<std::string> musicBag;
        std::string lastMusicTrack;
        bool musicPlaylistBuilt{false};

        MusicSituation musicSituation{MusicSituation::Building};
        std::array<int, 30> battlePointsRing{};
        unsigned int battleRingCursor{0};
        unsigned int lastMusicSecond{0};
        GameTime musicLockoutUntil{0};
        GameTime battleEnteredTime{0};
        GameTime musicHoldOffUntil{0};

        /** Set when a type switch is fading the current track out. */
        std::optional<MusicSituation> musicFadeTarget;
        float musicFade{1.0f};

        void addBattlePoints(int points);
        void updateMusic();

        /** What size the world render textures were made at, so a window resize remakes them. */
        std::pair<unsigned int, unsigned int> worldRenderTextureSize{0, 0};

        /** 2 while anti-aliasing supersamples the world buffer, 1 otherwise. */
        unsigned int worldRenderTextureScale{1};

        ScreenShakeState screenShake;

        /**
         * How far the shake has the camera pushed at the moment. Held
         * separately so it can be taken back off before the next frame's
         * offset goes on: the original simply adds to the scroll position
         * every frame and lets the camera random-walk away from where the
         * player left it, which is the one thing about its shake worth not
         * copying.
         */
        Vector3f appliedShakeOffset{0.0f, 0.0f, 0.0f};

        int millisecondsBuffer{0};

        GameSpeed gameSpeed;
        bool paused{false};

        std::vector<FlashEffect> flashes;
        bool guiVisible{true};

        FrameBufferInfo worldFrameBuffer;

        TextureHandle dodgeMask;
        /** Coverage of the finished buildings, for the purple halo the post pass draws. */
        TextureHandle buildingMask;


    public:
        GameScene(
            const SceneContext& sceneContext,
            std::unique_ptr<PlayerCommandService>&& playerCommandService,
            GameMediaDatabase&& meshDatabase,
            const GameCameraState& cameraState,
            SharedTextureHandle unitTextureAtlas,
            std::vector<SharedTextureHandle>&& unitTeamTextureAtlases,
            SharedTextureHandle unitPaletteIndexAtlas,
            std::vector<SharedTextureHandle>&& unitTeamPaletteIndexAtlases,
            SharedTextureHandle shadeTableTexture,
            SharedTextureHandle alphaTableTexture,
            GameSimulation&& simulation,
            MapTerrainGraphics&& terrainGraphics,
            BuilderGuisDatabase&& builderGuisDatabase,
            std::unique_ptr<GameNetworkService>&& gameNetworkService,
            const std::shared_ptr<Sprite>& minimap,
            const std::shared_ptr<SpriteSeries>& minimapDots,
            const std::shared_ptr<Sprite>& minimapDotHighlight,
            InGameSoundsInfo sounds,
            const std::shared_ptr<SpriteSeries>& guiFont,
            const std::shared_ptr<SpriteSeries>& speechFont,
            const GameParameters& gameParameters,
            PlayerId localPlayerId,
            TdfBlock* audioLookup,
            std::optional<std::ofstream>&& stateLogStream);

        ~GameScene() override;

        void init() override;

        /**
         * Turns the scene into a battle harness: `unitsPerSide` units for
         * each player named, spawned at that player's own start position and
         * ordered at the next player's, replaced as they die.
         */
        void enableBattleTest(unsigned int unitsPerSide, const std::vector<std::string>& unitTypes, const std::vector<PlayerId>& players, const std::vector<SimVector>& spawns);

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

        /** Applies a saved game's state onto the freshly built simulation. */
        void applyLoadedGame(const SaveFile& save);

        /** Watch a recorded game instead of playing one. */
        void enableReplayPlayback(Replay&& replay);

        /** Write every command issued in this game to a replay file. */
        void enableReplayRecording(const std::filesystem::path& path, const ReplayHeader& header);

        bool isReplayPlayback() const { return replayPlayback.has_value(); }

        /** Whose resources the top bar reads out; the local player unless a replay says otherwise. */
        PlayerId hudPlayerId() const { return hudPlayerOverride.value_or(localPlayerId); }

        void setCameraPosition(const Vector3f& newPosition);

        const MapTerrain& getTerrain() const;

    private:
        GameTime getGameTime() const;

        void playUiSound(const AudioService::SoundHandle& sound);

        void playNotificationSound(const PlayerId& playerId, const AudioService::SoundHandle& sound);

        void playUnitNotificationSound(const PlayerId& playerId, const std::string& unitType, UnitSoundType soundType);

        /** Speaks the unit's cloak and uncloak lines as its cloak comes and goes. */
        void updateCloakNotifications();

        void printConsole(const std::string& text, const Color& color = Color(255, 255, 255));

        void updateSelfDestructNotifications();

        void updateDefeatNotifications();

        void renderConsole();

        void playSoundAt(const Vector3f& position, const AudioService::SoundHandle& sound);

        void playWeaponStartSound(const Vector3f& position, const std::string& weaponType);

        void playWeaponImpactSound(const Vector3f& position, const std::string& weaponType, ImpactType impactType);

        void spawnWeaponImpactExplosion(const Vector3f& position, const std::string& weaponType, ImpactType impactType, bool positionVisible);

        /**
         * The art, the smoke and the sound of a detonation. `visible` says
         * whether the local player is entitled to see it: everything but the
         * sound is withheld when they are not.
         */
        void doProjectileImpact(const SimVector& position, const std::string& weaponType, ImpactType impactType, bool visible);

        void createLightSmoke(const Vector3f& position);

        void createWeaponSmoke(const Vector3f& position);

        void emitLightSmokeFromPiece(UnitId unitId, const std::string& pieceName);

        void emitBlackSmokeFromPiece(UnitId unitId, const std::string& pieceName);

        void emitWakeFromPiece(UnitId unitId, const std::string& pieceName, bool reverse, unsigned int rampPeriod);

        /** An aircraft's exhaust: small warm sparks dropped under a thruster piece, left behind as a trail. */
        void emitVtolFromPiece(UnitId unitId, const std::string& pieceName, unsigned int divisor);

        void modifyBuildQueue(UnitId unitId, const std::string& unitType, int count);

        void modifyStockpileQueue(UnitId unitId, int count);

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
        std::unique_ptr<UiPanel> createOrdersPanel();

        /** Greys or hides the order-strip buttons the selection cannot use, on any panel carrying the strip. */
        void applyOrderButtonGating(UiPanel& panel);

        void localPlayerSetFireOrders(UnitId unitId, UnitFireOrders orders);

        void localPlayerSetMovementOrders(UnitId unitId, UnitMovementOrders orders);

        /** Select every owned live unit the predicate admits; false leaves the selection alone if nothing matched. */
        bool selectAllWhere(const std::function<bool(const UnitState&, const UnitDefinition&)>& predicate);

        /** Ctrl+letter category selection, matched against the FBI Category tokens (CTRL_V and friends). */
        void selectAllByCategoryToken(const std::string& token);

        /** F5-F8 camera bookmarks; Ctrl+Fn stores, Fn recalls. */
        std::array<std::optional<Vector3f>, 4> cameraBookmarks;

        /** Where the local player last took a hit, for F3. */
        std::optional<SimVector> lastAttackPosition;

        /** The cursor for the 'n' key's walk through the player's own units. */
        std::optional<UnitId> nextUnitCursor;

        /**
         * The battle test. Empty unless the harness asked for it: each
         * player's start position, so the top-up knows where to put a
         * replacement and where to send it.
         */
        std::vector<SimVector> battleTestSpawns;
        std::vector<PlayerId> battleTestPlayers;
        int battleTestUnitsPerSide{0};

        /** Ever-climbing per-player slot counter, so a blocked cell moves the next spawn along. */
        std::vector<unsigned int> battleTestSpawnCounter;
        std::vector<std::string> battleTestUnitTypes;

        /**
         * How many tiles across each player's unit is. The spacing of the
         * spawn block is worked out from it: laid out any tighter than the
         * unit's own width and the block is one nothing can walk out of.
         */
        std::vector<int> battleTestFootprint;

        /** What each side had standing on the last pass, for the debug panel. */
        std::vector<int> battleTestAlive;

        /** Running totals for the log, so a killed run still leaves evidence. */
        unsigned int battleTestSpawned{0};
        unsigned int battleTestSpawnsBlocked{0};
        unsigned int battleTestCulled{0};
        unsigned int battleTestReordered{0};
        unsigned int battleTestFramesSinceLog{0};
        unsigned int battleTestLastLogTime{0};

        /** Tops every player back up to the wanted count and points the new arrivals at the enemy. */
        void runBattleTest();

        void localPlayerSetOnOff(UnitId unitId, bool on);

        /** Ask a unit for its cloak, or drop it. */
        void localPlayerSetCloak(UnitId unitId, bool cloaked);

        void localPlayerModifyBuildQueue(UnitId unitId, const std::string& unitType, int count);

        /** Order another round for the unit's stockpiled weapon, or take one off the queue. */
        void localPlayerModifyStockpile(UnitId unitId, int count);

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

        /**
         * What the local player can see, which with the fog switched off is
         * the whole map. Everything that consults the fog goes through here,
         * so there is only one render path rather than one per setting.
         */
        const PlayerVisibility& localPlayerVisibility() const;

        bool unitIsVisibleToLocalPlayer(UnitId unitId, const UnitState& unit) const;

        bool unitIsDetectableByLocalPlayer(UnitId unitId, const UnitState& unit) const;

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

        void addScreenShakeFromWeapon(const WeaponMediaInfo& weaponMediaInfo);

        void updateScreenShake();

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

        /** The radar, sonar and jammer rings the original draws on the minimap around a selected unit. */
        void renderMinimapDetectionRings(const Matrix4f& worldToMinimap);

        bool isGameMenuOpen() const { return !gameMenuPanels.empty(); }
        void toggleGameMenu();
        void openGameMenuRoot();
        void openGameExitMenu();

        /**
         * TotalA's YESORNO.GUI, built fresh for whichever question is being
         * asked and dropped in as the whole menu stack so CHOICE2/Cancel has
         * a clean panel to land back on. onYes and onNo are copy-then-cleared
         * before being called, because either one is free to replace the
         * whole scene (exiting to Windows, tearing down to the main menu) and
         * a member read after that point would be reading freed state.
         */
        void openConfirmDialog(const std::string& title, std::function<void()> onYes, std::function<void()> onNo);
        void openInGameOptions(const std::string& page);
        void closeGameMenu();
        void setGameMenuPanel(std::unique_ptr<UiPanel>&& panel);
        void gameMenuMessage(const std::string& topic, const std::string& control);

        void gameMenuMessageNow(const std::string& topic, const std::string& control);

        /**
         * Menu actions queued out of the emitting panel's own dispatch: a
         * button handler that destroys the panel it lives on frees the object
         * whose callback is still on the stack. Everything a menu button does
         * lands here and runs at the top of the next update.
         */
        std::vector<std::function<void()>> pendingMenuActions;

        /** What CHOICE1/CHOICE2 on the open YESORNO panel run; see openConfirmDialog. */
        std::function<void()> pendingConfirmAction;
        /** What CHOICE2 runs instead, or nothing to fall back to openGameMenuRoot(). */
        std::function<void()> pendingConfirmCancel;

        void exitToMainMenu();
        GameOptions currentInGameOptions() const;
        void applyInGameOptions(const GameOptions& state);
        void saveInGameOptions();

        void openSaveDialog();
        void openLoadDialog();
        void saveCurrentGame(const std::string& name);
        void loadSavedGame(const std::string& name);
        /** RESTART.GUI, opened from the exit menu in skirmish; see TOTALA-EXE.md S:63. */
        void openRestartMenu();
        /** Reloads the current map and players from scratch, the way a fresh game starts. */
        void restartGame();
        /** Pause the sim for the menu, through the same command path the Pause key uses. */
        void setMenuPause(bool wantPaused);

        void renderUnitOrders(UnitId unitId, bool drawLines);

        /**
         * When each build order the local player can see first appeared, so
         * the placement sweep can be played once. The original stamps the tick
         * onto the order itself; this is kept scene-side instead, because the
         * effect is decoration and putting it in the simulation would mean
         * carrying it through the hash, the dump and the network protocol.
         * Keyed on the footprint's grid origin.
         */
        std::unordered_map<uint64_t, GameTime> buildBoxAppearedAt;

        void updateBuildBoxAppearances();

        /** The ten-tick sweep the original plays over a newly placed building. */
        void renderBuildBoxSweep(const Matrix4f& worldToUi, const DiscreteRect& footprintRect, unsigned int age, bool ownerSelected);

        void renderPlacementSweeps();

        /** The marching string of stars the original draws between queued waypoints. */
        void drawWaypointTrail(const Matrix4f& worldToUi, const SimVector& from, const SimVector& to);

        void renderBuildBoxes(const UnitState& unit, const Color& outerColor, const Color& innerColor);

        /**
         * The white ring the v3.1 patch draws around a cloaked unit while
         * SHIFT is held: its MinCloakDistance, the radius inside which an
         * enemy unit forces it back into view.
         */
        void renderCloakRadius(const UnitState& unit);

        void renderMinimapCoverageRings(const UnitState& unit, const UnitDefinition& unitDefinition, const Vector3f& centre, float worldUnitsToMinimapPixels);

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

        /**
         * Rewrite the "N +M" readout on a launcher's MAKENUKE/MAKEANTI button.
         * Unlike a build queue this has to be done every frame rather than only
         * when a command lands: the magazine also goes up on its own when the
         * simulation finishes a round, with no command to hang the refresh on.
         */
        void refreshBuildGuiTotals();

        void refreshStockpileGuiTotal();

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

        /**
         * One puff of smoke out of anims/FX.GAF, played the way the original's
         * puffs play. riseRate is how far it climbs each tick: a damaged unit's
         * smoke and a thermal vent's steam share this emitter class in the
         * original and differ only in that.
         */
        void spawnSmokePuff(const Vector3f& position, const std::string& anim, float riseRate);

        /** This tick's steam from every thermal vent on the map. */
        void spawnGeoVentSteam();

        void spawnWake(const Vector3f& position, const Vector3f& velocity, GameTime duration, unsigned int rampPeriod, GameTime startTime);

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
