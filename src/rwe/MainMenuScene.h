#pragma once

#include <memory>
#include <optional>
#include <vector>
#include <rwe/AudioService.h>
#include <rwe/CursorService.h>
#include <rwe/RenderService.h>
#include <rwe/SceneContext.h>
#include <rwe/TextureService.h>
#include <rwe/game/GameParameters.h>
#include <rwe/io/campaign/campaign.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/scene/Scene.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/render/SpriteSeries.h>

namespace rwe
{
    struct MultiplayerFlow;
    struct MenuProbe;

    class MainMenuScene : public Scene
    {
        /**
         * ui_probe reads the real panels, the real model and the flow of a
         * running menu to say what a person would see; it is the one reader
         * of them that is not the scene.
         */
        friend struct MenuProbe;

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
        unsigned int pendingCameraZoom{100};
        unsigned int pendingUiScale{0};
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

        /** The campaign screen's choices: 0 Arm or 1 Core, 0 Easy to 2 Hard, and the campaigns the side can play. */
        unsigned int campaignSide{0};
        unsigned int campaignDifficulty{0};
        std::vector<std::string> campaignNames;
        std::optional<std::string> selectedCampaign;

        /** The mission the briefing on screen is for, and its narration. */
        std::optional<GameParameters> briefedGame;
        std::optional<AudioService::SoundHandle> narration;
        int narrationChannel{-1};

        /** A campaign to carry on with when the menu opens: the briefing of its mission, from the screen after a game. */
        std::optional<CampaignProgress> pendingCampaign;

    public:
        /** Opens on the briefing of a campaign's mission rather than the main menu; for the screen after a campaign game. */
        void resumeCampaign(const CampaignProgress& progress);

        MainMenuScene(
            const SceneContext& sceneContext,
            TdfBlock* audioLookup,
            float width,
            float height);

        /** Out of line: the multiplayer flow is a type only the .cpp that defines it knows. */
        ~MainMenuScene() override;

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

        /** NEWGAME.GUI, the campaign screen: side, difficulty, campaign and mission (§115). */
        void goToCampaignMenu();
        void refreshCampaignMenu();
        void fillCampaignMissions(unsigned int campaignIndex);
        void campaignMenuMessage(const std::string& message);
        void startCampaignMission();
        void openCampaignMission(CampaignProgress progress);
        std::vector<std::string> campaignNamesForSide(unsigned int side);

        /** MSNBRIEF.GUI, the briefing before a mission: its planet, its text and its narration (§115). */
        void goToCampaignBriefing(const GameParameters& params, const OtaRecord& ota);
        void campaignBriefingMessage(const std::string& message);
        void launchBriefedGame();
        void startNarration();
        void stopNarration();

        void goToSkirmishMenu();

        /** The way into a game against other machines; the same as goToSelectConnectionMenu. */
        void goToMultiplayerMenu();

        /**
         * The original's route to a network game, which is where MULTI goes:
         * SELPROV.GUI (the connection), TCP.GUI (an address), SELGAME.GUI
         * (the games, with New and Join), NEWMULTI.GUI (name the game), and
         * then the battleroom, LOUNGE2.GUI.
         */
        void goToSelectConnectionMenu();

        /**
         * Handles a popped panel that belongs to the multiplayer flow
         * (leaving the list goes back to the address dialog, and so on).
         * True if it did, in which case the panel stack has been dealt with.
         */
        bool multiplayerGoBack();

        /**
         * A message from one of the multiplayer screens, the battleroom
         * included. True if it was dealt with.
         */
        bool multiplayerMessage(const std::string& topic, const std::string& message, const ActivateMessage& details);

        /** Once a frame: the list refreshes, the host announces, the guest follows. */
        void updateMultiplayer();

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
         * of it. The battleroom sets it too, although it draws no address or
         * port box: the seats' addresses are on the model and Start's checks
         * are the same.
         */
        bool multiplayerSetup{false};

    private:
        /**
         * The multiplayer screens' state: discovery, the game list, the
         * host's session or the guest's claim. Present from MULTI until the
         * main menu is back or a game starts; defined in
         * MainMenuScene_multiplayer.cpp, where the logic is too.
         */
        std::unique_ptr<MultiplayerFlow> multiplayerFlow;

        /** The name last given on the list or the new-game screen, offered again next time. */
        std::string rememberedNickname;

        /**
         * Where a probe that runs several scenes in one process points this
         * one's network at something of its own: a discovery port that is
         * not the shared 1338, and STUN servers ("host:port") on loopback
         * rather than the internet's. Zero and empty are what a game uses.
         * Set only through MenuProbe.
         */
        unsigned short discoveryPortOverride{0};
        std::vector<std::string> stunServerOverride;

        bool isMultiplayerGuest() const;
        bool isMultiplayerHost() const;

        /** The address dialog over the connection screen. */
        void openTcpAddressDialog();
        void confirmTcpAddress();

        /** SELGAME.GUI, with discovery listening. */
        void openGameList();
        void updateGameList();
        void joinSelectedGame();

        /** NEWMULTI.GUI, and the host's side of its Next button. */
        void openNewGameScreen();
        void confirmNewGame();

        /**
         * Host: advertises the lobby as it now is, seats guests as they ask,
         * and runs the second or two in which the guests are told to start.
         */
        void updateHostedLobby();

        /** Guest: mirrors the host's announcement, and follows it into the game. */
        void updateJoinedLobby();

        /**
         * The internet side of a lobby, every update and for any role: the
         * invite code a host asked for and the reply code a guest owes, and
         * the addresses the game port keeps a way open to.
         */
        void updateNatTraversal();

        /** Starts finding this machine's outside addresses, if that is not under way or done. */
        void startNatTraversal();

        /**
         * A line typed in the battleroom, before it is chat. True if it was
         * the host's `/invite` or a guest's reply code and has been dealt
         * with, so that it is not said to the room.
         */
        bool takeInviteInput(const std::string& text);

        /** LOUNGE2.GUI, the battleroom, for the role the flow is in. */
        void goToBattleroom();

        /** The battleroom's gadgets, set from the model and the room: every update. */
        void refreshBattleroom();

        /** A press on the battleroom. True if it was its own; Previous Menu is not. */
        bool battleroomMessage(const std::string& message, const ActivateMessage& details);

        /** The line typed in the battleroom's message box, said to the room. */
        void sendChatLine();

        /** Pops back to the game list from the battleroom, saying why if it was not by choice. */
        void returnToGameList(const std::string& message);

        /** Ends the multiplayer visit: discovery stopped, the setup screen's state put back. */
        void endMultiplayer();

        /** Host pressed Start: checked now, then the guests are told for a couple of seconds, then it starts. */
        void beginHostedLaunch();

        /** What the name button of a seat says: the person's name, or the kind of seat. */
        std::string captionForSeat(int playerIndex) const;

        /**
         * Everything startGame checks and builds, short of starting: nothing
         * if there is a problem, in which case the screen has said so.
         */
        std::optional<GameParameters> prepareGameParameters();

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
