#pragma once

#include <memory>
#include <rwe/AudioService.h>
#include <rwe/CursorService.h>
#include <rwe/RenderService.h>
#include <rwe/SceneContext.h>
#include <rwe/TextureService.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/scene/Scene.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiPanel.h>

namespace rwe
{
    class MainMenuScene : public Scene
    {
    public:
        /** What the options screen edits, for undo, cancel and restore. */
        struct OptionsState
        {
            unsigned int soundVolume;
            unsigned int musicVolume;
            bool musicEnabled;
            std::string windowMode;
        };

    private:
        SceneContext sceneContext;
        TdfBlock* soundLookup;

        OptionsState optionsUndo;
        std::string pendingWindowMode;
        std::string currentOptionsPage;
        std::vector<std::string> menuPlaylist;
        std::size_t menuPlaylistIndex{0};

        /** Set by the music page's stop button so the retry leaves silence alone. */
        bool menuMusicStopped{false};
        bool lastPanelWasOptions{false};

        /** Starts the title music, or whatever the CD controls have picked. */
        void playMenuMusic();

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

        void update(int millisecondsElapsed) override;

        void goToPreviousMenu();

        /**
         * Plays one of the game's Smacker movies and comes back to a fresh
         * main menu afterwards. Falls back to doing nothing when the file is
         * not in any data path.
         */
        void playMovie(const std::string& vfsPath);

        void goToOptionsMenu();

        /** Swaps which options page shares the screen with the hub buttons; empty means the hub alone. */
        void goToOptionsPage(const std::string& page);

        void applyOptions(const OptionsState& state);

        OptionsState currentOptions() const;

        void saveOptions();

        void goToMenu(std::unique_ptr<UiPanel>&& panel);

        void openDialog(std::unique_ptr<UiPanel>&& panel);

        void goToMainMenu();

        void goToSingleMenu();

        void goToSkirmishMenu();

        void openMapSelectionDialog();

        void exit();

        void message(const std::string& topic, const std::string& message, const ActivateMessage& details);

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

        void cycleSkirmishOption(const std::string& optionName);

        void startGame();

    private:
        AudioService::LoopToken startBgm();

        UiPanel& topPanel();

        Point toScaledCoordinates(int x, int y) const;

        std::vector<std::string> getMapNames();

        /**
         * Picks the default map ("Coast To Coast", or the first installed
         * map) if no map has been chosen yet.
         */
        void selectDefaultMap();

        /**
         * Hooks the staged option buttons on the right of the skirmish
         * screen up to the model so that clicking them cycles the stage.
         */
        void attachSkirmishOptionComponents(UiPanel& panel);

        void attachPlayerSelectionComponents(const std::string& guiName, UiPanel& panel);

        void attachDetailedPlayerSelectionComponents(const std::string& guiName, UiPanel& panel, int i);

        bool hasMultiplayerSchema(const std::string& mapName);
    };
}
