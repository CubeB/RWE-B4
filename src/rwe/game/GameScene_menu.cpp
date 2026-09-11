// The in-game menu, its options pages, and the save and load dialogs.
//
// Everything the F2 panel reaches: the menu itself and its Resume / Options /
// Restart / Exit flow, the confirmations those exits go through, the PREFS
// composite and the wiring behind every control on it, and the save and load
// dialogs with the list that says what each save is. It comes out of
// GameScene_commands.cpp, which at 26,721 sections was 82% of what a COFF
// object can address -- see CLAUDE.md, and GameScene_debug.cpp for the same
// cut made a commit earlier. Menu code is the densest kind for this purpose:
// every gadget lookup is a template instantiation and every caption a string.
#include "GameScene.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <rwe/GlobalConfig.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/game/save_util.h>
#include <rwe/io/gui/gui.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>

namespace rwe
{
    namespace
    {
        /** H:MM:SS, the way DIFF's neighbours read -- no leading zero on the hour, one always on the rest. */
        std::string formatSaveGameTime(unsigned int totalSeconds)
        {
            auto hours = totalSeconds / 3600u;
            auto minutes = (totalSeconds % 3600u) / 60u;
            auto seconds = totalSeconds % 60u;
            auto pad2 = [](unsigned int v) {
                auto s = std::to_string(v);
                return s.size() < 2 ? std::string("0") + s : s;
            };
            return std::to_string(hours) + ":" + pad2(minutes) + ":" + pad2(seconds);
        }

        void setSaveListLabel(UiPanel& panel, const char* gadget, const std::string& text)
        {
            if (auto label = panel.find<UiLabel>(gadget))
            {
                label->get().setText(text);
            }
        }

        /** Fills the GAMES listbox and mirrors clicks into the name box and the metadata labels. */
        void wireSaveList(UiPanel& panel)
        {
            // The metadata gadgets default to their caption text, doubling
            // the captions the art already paints; they stay empty until a
            // selected save can fill them in.
            for (const auto* name : {"GAMETYPE", "SIDE", "MISSION", "DIFF", "TIME"})
            {
                setSaveListLabel(panel, name, std::string());
            }

            auto games = panel.find<UiListBox>("GAMES");
            if (!games)
            {
                return;
            }
            for (const auto& name : listSaveGames())
            {
                games->get().appendItem(name);
            }
            auto sub = games->get().selectedIndex().subscribe([&panel](const std::optional<unsigned int>& index) {
                if (!index)
                {
                    return;
                }
                auto games = panel.find<UiListBox>("GAMES");
                auto box = panel.find<UiTextBox>("GAMENAME");
                if (!games || !box || *index >= games->get().getItems().size())
                {
                    return;
                }
                auto name = games->get().getItems()[*index];
                box->get().setText(name);

                // One file read per click, not the whole list -- this only
                // has to answer for the entry that was just clicked, and
                // reading every save's header to populate a listbox would
                // mean opening every file on disk on every dialog open.
                auto save = readSaveFile(savePathForName(name));
                if (!save)
                {
                    for (const auto* gadget : {"GAMETYPE", "SIDE", "MISSION", "DIFF", "TIME"})
                    {
                        setSaveListLabel(panel, gadget, std::string());
                    }
                    return;
                }

                setSaveListLabel(panel, "MISSION", save->parameters.mapName);

                auto isNetwork = std::any_of(save->parameters.players.begin(), save->parameters.players.end(), [](const auto& p) {
                    return p && std::holds_alternative<PlayerControllerTypeNetwork>(p->controller);
                });
                setSaveListLabel(panel, "GAMETYPE", isNetwork ? "Network" : "Skirmish");

                // No slot in GameParameters says which player is local, so
                // (per LoadingScene's own reading of a fresh game) the first
                // human controller stands in for it.
                std::string side;
                for (const auto& p : save->parameters.players)
                {
                    if (p && std::visit(IsHumanVisitor(), p->controller))
                    {
                        side = p->side;
                        break;
                    }
                }
                setSaveListLabel(panel, "SIDE", side);

                setSaveListLabel(panel, "DIFF", aiDifficultyDisplayName(save->parameters.aiDifficulty));

                setSaveListLabel(panel, "TIME", save->gameTimeSeconds ? formatSaveGameTime(*save->gameTimeSeconds) : std::string());
            });
            games->get().addSubscription(std::move(sub));
        }
    }

    void GameScene::openSaveDialog()
    {
        // LOADGAME.GUI is the save/load dialog both ways in the original --
        // list, name field, metadata labels, radar frame -- and only the
        // painted background differs: DSavegame2 titles it SAVE GAME.
        // (SAVEGAME.GUI is a smaller, matching nothing that ships; unused.)
        auto guiRaw = sceneContext.vfs->readFile("guis/LOADGAME.GUI");
        auto entries = guiRaw ? parseGuiFromBytes(*guiRaw) : std::nullopt;
        auto panel = entries
            ? uiFactory.panelFromGuiFile("SAVEGAME", "DSavegame2", *entries)
            : uiFactory.panelFromGuiFile("SAVEGAME");
        wireSaveList(*panel);
        if (auto box = panel->find<UiTextBox>("GAMENAME"))
        {
            box->get().setText("savegame");
        }

        // LOADGAME.GUI ships an empty defaultfocus, so the original opens
        // this dialog with no focused gadget and therefore no caret. RWE
        // starts the name field focused instead: its keyDown is broadcast to
        // every child rather than routed to the focused one, so typing works
        // either way, and a field you can type into ought to look like one.
        panel->setFocusByName("GAMENAME");

        setGameMenuPanel(std::move(panel));
    }

    void GameScene::openLoadDialog()
    {
        auto panel = uiFactory.panelFromGuiFile("LOADGAME");
        wireSaveList(*panel);
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::saveCurrentGame(const std::string& name)
    {
        SaveFile save(gameParameters);
        save.cameraPosition = worldCameraState.position;
        save.gameTimeSeconds = simulation.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond);
        save.simulation = saveSimulationToJson(simulation);
        writeSaveFile(savePathForName(name), save);
        printConsole("Game saved: " + name);
    }

    void GameScene::loadSavedGame(const std::string& name)
    {
        auto path = savePathForName(name);
        auto save = readSaveFile(path);
        if (!save)
        {
            printConsole("Could not read save: " + name);
            return;
        }

        // Rerun the whole loading pipeline for the saved game's map and
        // players; the loading scene applies the saved state instead of
        // spawning the starting commanders.
        auto parameters = save->parameters;
        parameters.loadFromSaveFile = path.string();
        leaveFor(std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            parameters));
    }

    void GameScene::applyLoadedGame(const SaveFile& save)
    {
        // The loader adds the players itself from the save -- economy state
        // and all -- so the loading pipeline's freshly added ones step
        // aside. Same parameters, same slots, same ids.
        simulation.clearPlayers();
        loadSimulationFromJson(save.simulation, simulation);
        setCameraPosition(save.cameraPosition);
    }

    void GameScene::setMenuPause(bool wantPaused)
    {
        // The original pauses when the game menu opens in single player and
        // never in multiplayer; RWE routes it through the same command path
        // as the Pause key so peers stay in step either way.
        if (wantPaused && !paused)
        {
            paused = true;
            menuPausedGame = true;
            localPlayerCommandBuffer.push_back(PlayerPauseGameCommand{});
        }
        else if (!wantPaused && menuPausedGame)
        {
            menuPausedGame = false;
            if (paused)
            {
                paused = false;
                localPlayerCommandBuffer.push_back(PlayerUnpauseGameCommand{});
            }
        }
    }

    void GameScene::setGameMenuPanel(std::unique_ptr<UiPanel>&& panel)
    {
        panel->groupMessages().subscribe([this](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                gameMenuMessage(msg.topic, msg.controlName);
            }
        });
        gameMenuPanels.clear();
        gameMenuPanels.push_back(std::move(panel));
    }

    void GameScene::toggleGameMenu()
    {
        if (isGameMenuOpen())
        {
            closeGameMenu();
        }
        else
        {
            openGameMenuRoot();
        }
    }

    void GameScene::openGameMenuRoot()
    {
        // The original's GAME OPTIONS panel, drawn over the left unit panel.
        // Tab opens it in single player (the sliding TABMENU bar it shares a
        // key with is multiplayer-only), F2 opens it anywhere.
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        auto panel = uiFactory.panelFromGuiFile(sidePrefix + "OPT");

        // The briefing and help do not exist in RWE yet; the original greys
        // what does not apply rather than hiding it.
        for (const auto* name : {"MISSION", "HELP"})
        {
            if (auto button = panel->find<UiStagedButton>(name))
            {
                button->get().setEnabled(false);
            }
        }

        setGameMenuPanel(std::move(panel));
        inGameOptionsPage.clear();
        setMenuPause(true);
    }

    void GameScene::openGameExitMenu()
    {
        auto panel = uiFactory.panelFromGuiFile("EXITMENU");
        if (auto button = panel->find<UiStagedButton>("RESTART"))
        {
            // TOTALA-EXE.md S:63: campaign/skirmish gets this enabled with
            // the fixed text "Restart"; only multiplayer keeps it blank and
            // dead, and RWE has no multiplayer game in progress to gate it
            // on here yet.
            button->get().setEnabled(true);
            button->get().setLabel("Restart");
        }
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::openRestartMenu()
    {
        // RESTART.GUI, ui_probe'd against the shipped data: its buttons are
        // named CANCEL and RESTART, same as the panel's own topic.
        setGameMenuPanel(uiFactory.panelFromGuiFile("RESTART"));
    }

    void GameScene::restartGame()
    {
        // Same pipeline loadSavedGame hands a save to, minus the save: a
        // fresh LoadingScene over the game's own parameters spawns the
        // starting commanders exactly as the first load did.
        leaveFor(std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            gameParameters));
    }

    void GameScene::openConfirmDialog(const std::string& title, std::function<void()> onYes, std::function<void()> onNo)
    {
        // YESORNO.GUI is the original's one confirmation dialog, reused for
        // both exit prompts and (below) the save-list overwrite/delete
        // prompts: TOTALA-EXE.md S:63 has it asking under CHOICE1/CHOICE2,
        // and nothing else about it changes between call sites.
        auto panel = uiFactory.panelFromGuiFile("YESORNO");

        // S:63 says the question goes into a TITLE gadget. The shipped file
        // has no such gadget: it is three entries, the panel and the two
        // buttons, and the original writes the question into the panel's own
        // text at 0x4605c0. A UiPanel has no text, so the question gets a
        // label of its own, centred across the panel above the buttons --
        // which sit at y=55, 20 tall, in a box 400x100. Without this the
        // dialog asked nothing at all and offered Yes and No to a blank
        // plate.
        if (auto label = panel->find<UiLabel>("TITLE"))
        {
            label->get().setText(title);
        }
        else
        {
            panel->appendChild(uiFactory.createLabel(0, 20, panel->getWidth(), 20, title, UiLabel::Alignment::Center));
        }
        pendingConfirmAction = std::move(onYes);
        pendingConfirmCancel = std::move(onNo);
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::addGameMenuPanel(std::unique_ptr<UiPanel>&& panel)
    {
        panel->groupMessages().subscribe([this](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                gameMenuMessage(msg.topic, msg.controlName);
            }
        });
        gameMenuPanels.push_back(std::move(panel));
    }

    void GameScene::openInGameOptions(const std::string& page)
    {
        // The original's options screen is two panels side by side, and the
        // gui files say so: PREFS.GUI is the sidebar at (0,126) 128 wide,
        // and each RT page is 150 wide at (128,128) -- it folds out to the
        // right of the sidebar, over the game view. Merging them into one
        // panel put the page's gadgets at sidebar-relative coordinates,
        // which is why the sub-options landed on top of the tab buttons.
        // Each page carries its own background as a picture-box gadget.
        gameMenuPanels.clear();
        inGameOptionsPage = page;

        addGameMenuPanel(uiFactory.panelFromGuiFile("PREFS"));
        if (!page.empty())
        {
            addGameMenuPanel(uiFactory.panelFromGuiFile(page));
        }

        wireInGameOptionControls();
    }

    namespace
    {
        /**
         * How much of the PALETTE.SHD lookup each kind of model gets.
         *
         * The original has no such split. 0x459C70 shades every unit and
         * every feature through one path and never reads a "is a building"
         * flag (TOTALA-EXE-SHADING.md S:11, NOT FOUND), so this is a
         * deliberate divergence and is recorded as one in TOTALA-EXE.md S:88.
         *
         * The reason to have it is that the two read differently on screen
         * even though the arithmetic is identical. A building is a large,
         * still, mostly flat-sided model: its faces hold one shade level
         * across a big area, the wrap puts a whole wall near row 0, and the
         * banding reads as deliberate. A unit is small, in motion, and
         * shaded in world space rather than object space -- so the bands
         * slide across it as it turns, which reads as flicker rather than
         * form. Pulling the unit strength down keeps the shape information
         * and drops most of that movement.
         *
         * Both strengths default to 1.0, the faithful setting: row 0 is
         * genuinely black, as the original's is, on units and buildings
         * alike. The two values are rwe.cfg keys (shading-strength-units,
         * shading-strength-buildings) for anyone who wants it softer, and
         * the VISUALS switch still turns either category off outright.
         */
    }

    float GameScene::shadeStrengthFor(bool isBuilding) const
    {
        const auto& config = *sceneContext.globalConfig;
        if (isBuilding)
        {
            return shadingModeCoversBuildings(shadingMode)
                ? static_cast<float>(config.shadingStrengthBuildings) / 100.0f
                : 0.0f;
        }
        return shadingModeCoversUnits(shadingMode)
            ? static_cast<float>(config.shadingStrengthUnits) / 100.0f
            : 0.0f;
    }

    void GameScene::widenShadingButton()
    {
        // VISUALRT.GUI declares SHADING with two stages, Off|On, because the
        // original has two whole rasterizer chains and one bit to choose
        // between them. Splitting it by category needs four stages, and the
        // GUI files are read-only game data with no override directory, so
        // the gadget is rebuilt here at exactly the geometry the data gave
        // it. openInGameOptions clears and rebuilds every panel each time it
        // runs, so doing this unconditionally from wireInGameOptionControls
        // is idempotent.
        for (auto& panel : gameMenuPanels)
        {
            uiFactory.replaceStagedButton(*panel, "VISUALRT", "SHADING", "SHADINGMODE", shadingModeLabels(), static_cast<unsigned int>(shadingMode));
            addBuildingHaloButton(*panel);
            addAntiAliasUnitsButton(*panel);
        }
    }

    void GameScene::addBuildingHaloButton(UiPanel& panel)
    {
        // VISUALRT has no gadget for the purple building fringe, and the GUI
        // files are read-only game data with no override directory, so the
        // button is built here -- the same reasoning that rebuilds SHADING
        // above, one step further because this one does not exist at all.
        //
        // Where it goes is derived, not guessed: one row below the shadows
        // toggle, with the row step taken from the gap between that and the
        // anti-alias toggle above it. On this panel ui_probe measures SHADING
        // at y=63, ANTI at 108 and BSHADOWS at 152, and the next gadget down
        // is RESTORE at 269 -- so a 44-pixel step lands at 196 with room to
        // spare. It borrows BSHADOWS's artwork so it looks like the toggles
        // either side of it rather than like an addition.
        uiFactory.addStagedButtonBelow(panel, "VISUALRT", "BSHADOWS", "HALO", "BSHADOWS", "ANTI", {"Fringe Off", "Fringe On"}, buildingHaloEnabled ? 1 : 0);
    }

    void GameScene::addAntiAliasUnitsButton(UiPanel& panel)
    {
        // How far the 2x2 box filter reaches, and another gadget VISUALRT has
        // no entry for. The original anti-aliased a building's cached bitmap
        // and nothing else, so RWE stops the filter there too and this switch
        // is what puts it back over the units for anyone who wants it -- a
        // preference, which is why it is a separate control rather than more
        // stages on ANTI.
        //
        // It anchors to HALO, which addBuildingHaloButton added a moment ago,
        // with BSHADOWS above it: the same 44-pixel step one row further down,
        // landing at 240 with RESTORE still clear at 269.
        uiFactory.addStagedButtonBelow(panel, "VISUALRT", "BSHADOWS", "AAUNITS", "HALO", "BSHADOWS", {"Units Sharp", "Units Smooth"}, antiAliasUnitsEnabled ? 1 : 0);
    }

    void GameScene::wireInGameOptionControls()
    {
        widenShadingButton();

        auto state = currentInGameOptions();

        if (auto bar = findInGameMenu<UiScrollBar>("FXVOL"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent(static_cast<float>(state.soundVolume) / 100.0f);
            auto sub = bar->scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setSoundVolume(v);
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto bar = findInGameMenu<UiScrollBar>("MUSICVOL"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent(static_cast<float>(state.musicVolume) / 100.0f);
            auto sub = bar->scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setMusicVolume(v);
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("NOTRAK"))
        {
            toggle->setStage(state.musicEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("MODE"))
        {
            toggle->setStage(static_cast<unsigned int>(state.soundMode));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("SPEECH"))
        {
            toggle->setStage(static_cast<unsigned int>(state.unitSpeech));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("BSHADOWS"))
        {
            toggle->setStage(state.shadows ? 1 : 0);
        }

        if (auto bar = findInGameMenu<UiScrollBar>("GAMMA"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent((static_cast<float>(state.gamma) - 50.0f) / 83.0f);
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                // The original's twenty steps of 0.5 + v/24: 0.5x to 1.333x.
                gammaSetting = 50u + static_cast<unsigned int>(v * 83.0f);
                applyGamma();
            });
            bar->addSubscription(std::move(sub));
        }

        // Screen scroll: how fast the view moves when the cursor is held at
        // the edge (and on the arrow keys), 25 to 200 percent.
        if (auto bar = findInGameMenu<UiScrollBar>("SCREEN"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent((static_cast<float>(state.scrollSpeed) - 25.0f) / 175.0f);
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                scrollSpeedSetting = 25u + static_cast<unsigned int>(v * 175.0f);
            });
            bar->addSubscription(std::move(sub));
        }

        // Game speed across the whole -10..+10 range, through the same
        // lockstep command the +/- keys use.
        if (auto bar = findInGameMenu<UiScrollBar>("GAME"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent(static_cast<float>(gameSpeed.index()) / static_cast<float>(GameSpeed::MaxIndex));
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                auto index = static_cast<int>((v * static_cast<float>(GameSpeed::MaxIndex)) + 0.5f);
                if (index != gameSpeed.index())
                {
                    localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{GameSpeed(index).index()});
                }
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto bar = findInGameMenu<UiScrollBar>("VIDSLDR"))
        {
            bar->setScrollBarPercent(0.34f);
            auto modeToPercent = pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f);
            bar->setScrollPercent(modeToPercent);
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                pendingWindowMode = v < 0.33f ? "windowed" : (v < 0.67f ? "borderless" : "fullscreen");
                if (auto label = findInGameMenu<UiLabel>("VIDVAL"))
                {
                    label->setText(windowModeDisplayName(pendingWindowMode));
                }
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto label = findInGameMenu<UiLabel>("VIDVAL"))
        {
            label->setText(windowModeDisplayName(pendingWindowMode));
        }

        // Still no machinery behind these; the original greys what does not
        // apply rather than letting it lie.
        if (auto toggle = findInGameMenu<UiStagedButton>("SHADING"))
        {
            toggle->setStage(static_cast<unsigned int>(shadingMode));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("ANTI"))
        {
            toggle->setStage(antiAliasEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("HALO"))
        {
            toggle->setStage(buildingHaloEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("AAUNITS"))
        {
            toggle->setStage(antiAliasUnitsEnabled ? 1 : 0);
        }

        for (const auto* name : {"LEFTCLICK", "UNITCHAT", "TXTSCROL", "MAXLINES"})
        {
            if (auto button = findInGameMenu<UiStagedButton>(name))
            {
                button->setEnabled(false);
            }
        }
    }

    void GameScene::closeGameMenu()
    {
        gameMenuPanels.clear();
        inGameOptionsPage.clear();
        setMenuPause(false);
    }

    GameOptions GameScene::currentInGameOptions() const
    {
        return GameOptions{
            static_cast<unsigned int>(sceneContext.audioService->getSoundVolume() * 100.0f),
            static_cast<unsigned int>(sceneContext.audioService->getMusicVolume() * 100.0f),
            sceneContext.audioService->isMusicEnabled(),
            pendingWindowMode,
            shadowsEnabled,
            scrollSpeedSetting,
            soundModeSetting,
            unitSpeechSetting,
            gammaSetting,
            shadingMode,
            antiAliasEnabled,
            buildingHaloEnabled,
            antiAliasUnitsEnabled};
    }

    void GameScene::applyInGameOptions(const GameOptions& state)
    {
        auto* audio = sceneContext.audioService;
        audio->setSoundVolume(static_cast<float>(state.soundVolume) / 100.0f);
        audio->setMusicVolume(static_cast<float>(state.musicVolume) / 100.0f);
        audio->setMusicEnabled(state.musicEnabled);
        pendingWindowMode = state.windowMode;
        shadowsEnabled = state.shadows;
        scrollSpeedSetting = state.scrollSpeed;
        soundModeSetting = state.soundMode;
        unitSpeechSetting = state.unitSpeech;
        audio->setSoundEnabled(state.soundMode != SoundMode::Off);
        gammaSetting = state.gamma;
        applyGamma();
        shadingMode = state.shading;
        buildingHaloEnabled = state.buildingHalo;
        antiAliasUnitsEnabled = state.antiAliasUnits;
        if (antiAliasEnabled != state.antiAlias)
        {
            antiAliasEnabled = state.antiAlias;
            recreateWorldRenderTextures();
        }
    }

    void GameScene::applyGamma()
    {
        // Nothing to push: the world's post-process blit reads gammaSetting
        // every frame, so moving the slider is visible at once. The hook is
        // kept so the callers read as intent rather than as an assignment.
    }

    void GameScene::saveInGameOptions()
    {
        auto localDataPath = getLocalDataPath();
        if (!localDataPath)
        {
            return;
        }
        writeGameOptions(*localDataPath / "rwe.cfg", currentInGameOptions());
    }

    void GameScene::exitToMainMenu()
    {
        leaveFor(std::make_shared<MainMenuScene>(
            sceneContext,
            audioLookup,
            sceneContext.viewport->width(),
            sceneContext.viewport->height()));
    }

    void GameScene::leaveFor(std::shared_ptr<Scene> scene)
    {
        sceneContext.audioService->stopMusic();
        leavingScene = true;
        sceneContext.sceneManager->setNextScene(std::move(scene));
    }

    void GameScene::gameMenuMessage(const std::string& topic, const std::string& control)
    {
        // Defer: this is called from inside the panel's own event dispatch,
        // and most handlers replace the panel, which would destroy the object
        // whose callback we are standing in.
        pendingMenuActions.push_back([this, topic, control]() { gameMenuMessageNow(topic, control); });
    }

    void GameScene::gameMenuMessageNow(const std::string& topic, const std::string& control)
    {
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        if (topic == sidePrefix + "OPT")
        {
            if (control == "OK")
            {
                closeGameMenu();
            }
            else if (control == "SAVEGAME")
            {
                openSaveDialog();
            }
            else if (control == "LOADGAME")
            {
                openLoadDialog();
            }
            else if (control == "PREFS")
            {
                if (pendingWindowMode.empty())
                {
                    pendingWindowMode = sceneContext.globalConfig->windowMode;
                }
                gameOptionsUndo = currentInGameOptions();
                openInGameOptions(std::string());
            }
            else if (control == "EXIT")
            {
                openGameExitMenu();
            }
        }
        else if (topic == "SAVEGAME")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
            }
            else if (control == "DELETE")
            {
                std::string name;
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                if (!name.empty())
                {
                    openConfirmDialog(
                        "Delete the saved game?",
                        [this, name]() {
                            std::error_code ec;
                            std::filesystem::remove(savePathForName(name), ec);
                            openSaveDialog();
                        },
                        [this]() { openSaveDialog(); });
                }
                else
                {
                    openSaveDialog();
                }
            }
            // The gadget is named LOAD in the shared dialog gui; its label
            // is what says OK.
            else if (control == "SAVE" || control == "LOAD")
            {
                std::string name = "savegame";
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                if (std::filesystem::exists(savePathForName(name)))
                {
                    openConfirmDialog(
                        "Overwrite the saved game?",
                        [this, name]() {
                            saveCurrentGame(name);
                            openGameMenuRoot();
                        },
                        [this]() { openSaveDialog(); });
                }
                else
                {
                    saveCurrentGame(name);
                    openGameMenuRoot();
                }
            }
        }
        else if (topic == "LOADGAME")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
            }
            else if (control == "DELETE")
            {
                std::string name;
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                if (!name.empty())
                {
                    openConfirmDialog(
                        "Delete the saved game?",
                        [this, name]() {
                            std::error_code ec;
                            std::filesystem::remove(savePathForName(name), ec);
                            openLoadDialog();
                        },
                        [this]() { openLoadDialog(); });
                }
                else
                {
                    openLoadDialog();
                }
            }
            else if (control == "LOAD")
            {
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        loadSavedGame(box->get().getText());
                    }
                }
            }
        }
        else if (topic == "EXITMENU")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
            }
            else if (control == "RESTART")
            {
                openRestartMenu();
            }
            else if (control == "EXITGAME")
            {
                // TOTALA-EXE.md S:63, mode 2: "Surrender this battle and exit to Windows?"
                openConfirmDialog(
                    "Surrender this battle and exit to Windows?",
                    [this]() { sceneContext.sceneManager->requestExit(); },
                    [this]() { openGameExitMenu(); });
            }
            else if (control == "MAINMENU")
            {
                // TOTALA-EXE.md S:63, mode 0: "Surrender this battle and return to main menu?"
                openConfirmDialog(
                    "Surrender this battle and return to main menu?",
                    [this]() { exitToMainMenu(); },
                    [this]() { openGameExitMenu(); });
            }
        }
        else if (topic == "RESTART")
        {
            if (control == "CANCEL")
            {
                openGameExitMenu();
            }
            else if (control == "RESTART")
            {
                restartGame();
            }
        }
        else if (topic == "YESORNO")
        {
            if (control == "CHOICE1")
            {
                auto action = pendingConfirmAction;
                pendingConfirmAction = nullptr;
                pendingConfirmCancel = nullptr;
                if (action)
                {
                    action();
                }
            }
            else if (control == "CHOICE2")
            {
                auto cancel = pendingConfirmCancel;
                pendingConfirmAction = nullptr;
                pendingConfirmCancel = nullptr;
                if (cancel)
                {
                    cancel();
                }
                else
                {
                    openGameMenuRoot();
                }
            }
        }
        else if (topic == "PREFS" || topic == "SOUNDSRT" || topic == "MUSICRT" || topic == "VISUALRT" || topic == "SPEEDSRT")
        {
            // The fold-out is two panels, and each emits under its own name:
            // the sidebar's tabs come in as PREFS, but every control on a
            // page arrives under the page's topic.
            if (control == "SOUND")
            {
                openInGameOptions("SOUNDSRT");
            }
            else if (control == "MUSIC")
            {
                openInGameOptions("MUSICRT");
            }
            else if (control == "VISUALS")
            {
                openInGameOptions("VISUALRT");
            }
            else if (control == "SPEEDS")
            {
                openInGameOptions("SPEEDSRT");
            }
            else if (control == "PREV")
            {
                // The button says OK: keep the settings, apply the ones
                // that are not already live, and go back.
                saveInGameOptions();
                sceneContext.sceneManager->setWindowMode(pendingWindowMode);
                openGameMenuRoot();
            }
            else if (control == "CANCEL")
            {
                applyInGameOptions(gameOptionsUndo);
                openGameMenuRoot();
            }
            else if (control == "RESTORE")
            {
                applyInGameOptions(GameOptions{});
                openInGameOptions(inGameOptionsPage);
            }
            else if (control == "UNDO")
            {
                applyInGameOptions(gameOptionsUndo);
                openInGameOptions(inGameOptionsPage);
            }
            else if (control == "NOTRAK")
            {
                auto* audio = sceneContext.audioService;
                audio->setMusicEnabled(!audio->isMusicEnabled());
            }
            else if (control == "BSHADOWS")
            {
                shadowsEnabled = !shadowsEnabled;
            }
            else if (control == "SHADING")
            {
                shadingMode = nextStage(shadingMode);
            }
            else if (control == "ANTI")
            {
                antiAliasEnabled = !antiAliasEnabled;
                recreateWorldRenderTextures();
            }
            else if (control == "HALO")
            {
                buildingHaloEnabled = !buildingHaloEnabled;
            }
            else if (control == "AAUNITS")
            {
                antiAliasUnitsEnabled = !antiAliasUnitsEnabled;
            }
            else if (control == "MODE")
            {
                // Off | Mono | 3D, cycled by the button itself.
                soundModeSetting = nextStage(soundModeSetting);
                sceneContext.audioService->setSoundEnabled(soundModeSetting != SoundMode::Off);
            }
            else if (control == "SPEECH")
            {
                // Off | Medium | Full: how much of the unit chatter plays.
                unitSpeechSetting = nextStage(unitSpeechSetting);
            }
            else if (control == "CDPLAY")
            {
                sceneContext.audioService->setMusicEnabled(true);
            }
            else if (control == "CDSTOP")
            {
                sceneContext.audioService->stopMusic();
            }
            else if (control == "CDNEXT" || control == "CDPREV")
            {
                // The in-game rotation picks its own next track; stopping the
                // current one is what asks it for another.
                sceneContext.audioService->stopMusic();
            }
            else if (control == "TEST")
            {
                if (auto sound = sceneContext.audioService->loadSound("BUTTON10"))
                {
                    sceneContext.audioService->playSound(*sound);
                }
            }
        }

        // Whatever just happened, the widgets show the state as it now is.
        refreshInGameOptionControls();
    }

    void GameScene::refreshInGameOptionControls()
    {
        auto* audio = sceneContext.audioService;
        if (auto toggle = findInGameMenu<UiStagedButton>("NOTRAK"))
        {
            toggle->setStage(audio->isMusicEnabled() ? 1 : 0);
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("MODE"))
        {
            toggle->setStage(static_cast<unsigned int>(soundModeSetting));
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("SPEECH"))
        {
            toggle->setStage(static_cast<unsigned int>(unitSpeechSetting));
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("BSHADOWS"))
        {
            toggle->setStage(shadowsEnabled ? 1 : 0);
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("SHADING"))
        {
            toggle->setStage(static_cast<unsigned int>(shadingMode));
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("ANTI"))
        {
            toggle->setStage(antiAliasEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("HALO"))
        {
            toggle->setStage(buildingHaloEnabled ? 1 : 0);
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("AAUNITS"))
        {
            toggle->setStage(antiAliasUnitsEnabled ? 1 : 0);
        }
        if (auto bar = findInGameMenu<UiScrollBar>("FXVOL"))
        {
            bar->setScrollPercent(audio->getSoundVolume());
        }
        if (auto bar = findInGameMenu<UiScrollBar>("MUSICVOL"))
        {
            bar->setScrollPercent(audio->getMusicVolume());
        }
        if (auto bar = findInGameMenu<UiScrollBar>("GAMMA"))
        {
            bar->setScrollPercent((static_cast<float>(gammaSetting) - 50.0f) / 83.0f);
        }
        if (auto bar = findInGameMenu<UiScrollBar>("SCREEN"))
        {
            bar->setScrollPercent((static_cast<float>(scrollSpeedSetting) - 25.0f) / 175.0f);
        }
        if (auto bar = findInGameMenu<UiScrollBar>("GAME"))
        {
            bar->setScrollPercent(static_cast<float>(gameSpeed.index()) / static_cast<float>(GameSpeed::MaxIndex));
        }
        if (auto bar = findInGameMenu<UiScrollBar>("VIDSLDR"))
        {
            bar->setScrollPercent(pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f));
        }
        if (auto label = findInGameMenu<UiLabel>("VIDVAL"))
        {
            label->setText(windowModeDisplayName(pendingWindowMode));
        }
    }

    void GameScene::handleEscapeDown()
    {
        // Escape first closes anything drawn over the game.
        if (showDebugWindow)
        {
            showDebugWindow = false;
            return;
        }
        if (helpVisible)
        {
            helpVisible = false;
            return;
        }

        if (gameOver)
        {
            returnToMainMenu();
            return;
        }

        match(
            cursorMode.getValue(),
            [this](const NormalCursorMode&) {
                clearUnitSelection();
            },
            [this](const auto&) {
                cursorMode.next(NormalCursorMode());
            });
    }

    void GameScene::returnToMainMenu()
    {
        LOG_INFO << "Returning to the main menu";
        leaveFor(std::make_shared<MainMenuScene>(
            sceneContext,
            audioLookup,
            static_cast<float>(sceneContext.viewport->width()),
            static_cast<float>(sceneContext.viewport->height())));
    }
}
