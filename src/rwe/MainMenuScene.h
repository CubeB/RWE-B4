#pragma once

#include <memory>
#include <rwe/AudioService.h>
#include <rwe/CursorService.h>
#include <rwe/RenderService.h>
#include <rwe/SceneContext.h>
#include <rwe/TextureService.h>
#include <rwe/game/GameParameters.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/scene/Scene.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiPanel.h>

namespace rwe
{
    class MainMenuScene : public Scene
    {
    private:
        SceneContext sceneContext;
        TdfBlock* soundLookup;

        GameOptions optionsUndo;
        bool pendingShadows{true};
        unsigned int pendingScrollSpeed{100};
        SoundMode pendingSoundMode{SoundMode::Stereo};
        UnitSpeechLevel pendingUnitSpeech{UnitSpeechLevel::Full};
        MusicTrackMode pendingMusicTrackMode{MusicTrackMode::Custom};
        std::vector<unsigned int> pendingMusicTrackTypes;
        unsigned int pendingGamma{100};
        ShadingMode pendingShading{ShadingMode::BuildingsOnly};
        bool pendingAntiAlias{true};
        bool pendingBuildingHalo{true};
        bool pendingAntiAliasUnits{false};

        /** Pushes the current settings back into the menu widgets: a staged button does not advance its own display. */
        void refreshOptionControls();
        std::string pendingWindowMode;
        std::string currentOptionsPage;
        std::vector<std::string> menuPlaylist;
        std::size_t menuPlaylistIndex{0};
        bool lastPanelWasOptions{false};

        /** Starts the title music, or whatever the CD controls have picked. */
        void playMenuMusic();

        /** TRACKNUM and TRACKTYPE for the track the CD controls are on. */
        void refreshTrackControls(UiPanel& active);

        UiRenderService scaledUiRenderService;
        UiRenderService nativeUiRenderService;

        MainMenuModel model;
        UiFactory uiFactory;

        std::vector<std::unique_ptr<UiPanel>> panelStack;
        std::vector<std::unique_ptr<UiPanel>> dialogStack;

        AudioService::LoopToken bgm;

    public:
        MainMenuScene(
            const SceneContext& sceneContext,
            TdfBlock* audioLookup,
            float width,
            float height);

        MainMenuScene(const MainMenuScene&) = delete;
        MainMenuScene& operator=(const MainMenuScene&) = delete;
        MainMenuScene(MainMenuScene&&) = delete;
        MainMenuScene& operator=(MainMenuScene&&) = delete;

        void init() override;

        void render() override;

        void onMouseDown(MouseButtonEvent event) override;

        void onMouseUp(MouseButtonEvent event) override;

        void onMouseMove(MouseMoveEvent event) override;

        void onMouseWheel(MouseWheelEvent event) override;

        void onKeyDown(const SDL_KeyboardEvent& keysym) override;

        void onTextInput(const std::string& text) override;

        void update(int millisecondsElapsed) override;

        void goToPreviousMenu();

        /**
         * Plays one of the game's Smacker movies and comes back to a fresh
         * main menu afterwards. Falls back to doing nothing when the file is
         * not in any data path.
         */
        void playMovie(const std::string& vfsPath);

        /** Menu actions deferred out of the emitting panel's dispatch; see GameScene::pendingMenuActions. */
        std::vector<std::function<void()>> pendingMenuActions;

        void goToOptionsMenu();

        void goToLoadGameMenu();

        /** The original's MSGBOX, with one line of text and an OK button. */
        void openMessageBox(const std::string& message);

        void startLoadedGame(const std::string& name);

        /** Swaps which options page shares the screen with the hub buttons; empty means the hub alone. */
        void goToOptionsPage(const std::string& page);

        void applyOptions(const GameOptions& state);

        GameOptions currentOptions() const;

        void saveOptions();

        void goToMenu(std::unique_ptr<UiPanel>&& panel);

        void openDialog(std::unique_ptr<UiPanel>&& panel);

        void goToMainMenu();

        void goToSingleMenu();

        void goToSkirmishMenu();

        /**
         * The same screen as the skirmish one, for a game played against other
         * machines rather than against the computer. What it adds is a slot
         * type -- Network, with an address -- and the port this machine
         * listens on; what it has no way to add is agreement, there being no
         * lobby here to agree anything in. See MultiplayerSetup.h.
         */
        void goToMultiplayerMenu();

        /** The screen shared by both, given which one it is being built for. */
        void goToPlayerSetupMenu(bool multiplayer);

        void openMapSelectionDialog();

        void exit();

        void message(const std::string& topic, const std::string& message, const ActivateMessage& details);

        void messageNow(const std::string& topic, const std::string& message, const ActivateMessage& details);

        void setCandidateSelectedMap(const std::string& mapName);

        void clearCandidateSelectedMap();

        void commitSelectedMap();

        void resetCandidateSelectedMap();

        void togglePlayer(int playerIndex);

        void incrementPlayerMetal(int playerIndex);

        void decrementPlayerMetal(int playerIndex);

        void incrementPlayerEnergy(int playerIndex);

        void decrementPlayerEnergy(int playerIndex);

        void togglePlayerSide(int playerIndex);

        void cyclePlayerColor(int playerIndex);

        void reverseCyclePlayerColor(int playerIndex);

        void cyclePlayerTeam(int playerIndex);

        /** Steps a computer player through the AI personalities, forwards or back. */
        void cyclePlayerPersonality(int playerIndex, int step);

        void cycleSkirmishOption(const std::string& optionName);

        void startGame();

        /**
         * Takes what has been typed into the address and port boxes onto the
         * model. A text box has no change notification, so this is called
         * before anything reads those values -- at Start, and whenever the
         * screen is about to throw its boxes away.
         */
        void readNetworkAddressesFromPanel();

        /**
         * Whether this screen is setting up a game against other machines. It
         * decides three things: whether a slot can be Network at all, whether
         * the address and port boxes are drawn, and whether Start checks any
         * of it.
         */
        bool multiplayerSetup{false};

    private:
        AudioService::LoopToken startBgm();

        /**
         * Why the selected map cannot seat the filled slots, or nothing if
         * it can. Slot n needs the map's StartPos n; see startGame.
         */
        std::optional<std::string> startPositionProblem(const GameParameters& params);

        UiPanel& topPanel();

        Point toScaledCoordinates(int x, int y) const;

        std::vector<std::string> getMapNames();

        /**
         * Picks the default map ("Coast To Coast", or the first installed
         * map) if no map has been chosen yet.
         */
        void selectDefaultMap();

        /**
         * Which skirmish option the pointer is over, and so whose description
         * the help line under the setup box is currently showing. Empty when
         * it is over none of them, which is when the line is blank.
         */
        std::string hoveredSkirmishOption;

        /**
         * The AI personalities a computer player can be given, name and
         * description, built-ins first: read again each time the skirmish
         * screen opens, so a file dropped into the `ai` folder shows up
         * without a restart.
         */
        std::vector<std::pair<std::string, std::string>> aiPersonalities;

        /** "Rush: Raids early and ..." for the help line, or just the name if it is not known. */
        std::string describeAiPersonality(const std::string& name) const;

        /**
         * Hooks the staged option buttons on the right of the skirmish
         * screen up to the model so that clicking them cycles the stage,
         * and to the help line so that hovering one describes it.
         */
        void attachSkirmishOptionComponents(UiPanel& panel);

        void attachPlayerSelectionComponents(const std::string& guiName, UiPanel& panel);

        void attachDetailedPlayerSelectionComponents(const std::string& guiName, UiPanel& panel, int i);

        bool hasMultiplayerSchema(const std::string& mapName);
    };
}
