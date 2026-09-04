#include "MainMenuScene.h"
#include <rwe/MovieScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/util/SimpleLogger.h>
#include <algorithm>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuModel.h>
#include <rwe/camera_util.h>
#include <rwe/config.h>
#include <rwe/io/gui/gui.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/resource_io.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiSurface.h>
#include <rwe/util/Index.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    const Viewport MainMenuViewport(0, 0, 640, 480);

    MainMenuScene::MainMenuScene(
        const SceneContext& sceneContext,
        TdfBlock* audioLookup,
        float width,
        float height)
        : sceneContext(sceneContext),
          soundLookup(audioLookup),
          scaledUiRenderService(sceneContext.graphics, sceneContext.shaders, &MainMenuViewport),
          nativeUiRenderService(sceneContext.graphics, sceneContext.shaders, sceneContext.viewport),
          model(),
          uiFactory(sceneContext.textureService, sceneContext.audioService, soundLookup, sceneContext.vfs, sceneContext.pathMapping, 640, 480),
          panelStack(),
          dialogStack(),
          bgm()
    {
    }

    void MainMenuScene::init()
    {
        // The menu has no music: the original plays only the [BGM] ambience
        // (the drone from ALLSOUND.TDF) over its front end, and so does RWE.
        // The playlist is still gathered so the options music page's CD
        // controls can preview tracks.
        menuPlaylist = sceneContext.audioService->getMusicPlaylist();

        bgm = startBgm();
        goToMainMenu();
    }

    void MainMenuScene::render()
    {
        panelStack.back()->render(scaledUiRenderService);

        for (auto& e : dialogStack)
        {
            scaledUiRenderService.fillScreen(Color(0, 0, 0, 63));
            e->render(scaledUiRenderService);
        }
    }

    void MainMenuScene::onMouseDown(MouseButtonEvent event)
    {
        auto p = toScaledCoordinates(event.x, event.y);
        event.x = p.x;
        event.y = p.y;
        topPanel().mouseDown(event);
    }

    void MainMenuScene::onMouseUp(MouseButtonEvent event)
    {
        auto p = toScaledCoordinates(event.x, event.y);
        event.x = p.x;
        event.y = p.y;
        topPanel().mouseUp(event);
    }

    void MainMenuScene::onMouseMove(MouseMoveEvent event)
    {
        auto p = toScaledCoordinates(event.x, event.y);
        event.x = p.x;
        event.y = p.y;
        topPanel().mouseMove(event);
    }

    AudioService::LoopToken MainMenuScene::startBgm()
    {
        auto bgmBlock = soundLookup->findBlock("BGM");
        if (!bgmBlock)
        {
            return AudioService::LoopToken();
        }

        auto bgmName = bgmBlock->get().findValue("sound");
        if (!bgmName)
        {
            return AudioService::LoopToken();
        }

        auto bgm = sceneContext.audioService->loadSound(*bgmName);
        if (!bgm)
        {
            return AudioService::LoopToken();
        }

        return sceneContext.audioService->loopSound(*bgm);
    }

    void MainMenuScene::playMenuMusic()
    {
        if (menuPlaylist.empty())
        {
            return;
        }
        const auto& path = menuPlaylist[menuPlaylistIndex % menuPlaylist.size()];
        if (!sceneContext.audioService->playMusic(path, true))
        {
            LOG_ERROR << "Failed to start menu music: " << path;
        }
    }

    GameOptions MainMenuScene::currentOptions() const
    {
        return GameOptions{
            static_cast<unsigned int>(sceneContext.audioService->getSoundVolume() * 100.0f),
            static_cast<unsigned int>(sceneContext.audioService->getMusicVolume() * 100.0f),
            sceneContext.audioService->isMusicEnabled(),
            pendingWindowMode,
            pendingShadows,
            pendingScrollSpeed,
            pendingSoundMode,
            pendingUnitSpeech,
            pendingGamma,
            pendingShading,
            pendingAntiAlias};
    }

    void MainMenuScene::applyOptions(const GameOptions& state)
    {
        auto* audio = sceneContext.audioService;
        audio->setSoundVolume(static_cast<float>(state.soundVolume) / 100.0f);
        audio->setMusicVolume(static_cast<float>(state.musicVolume) / 100.0f);
        audio->setMusicEnabled(state.musicEnabled);
        pendingWindowMode = state.windowMode;
        pendingShadows = state.shadows;
        pendingScrollSpeed = state.scrollSpeed;
        pendingSoundMode = state.soundMode;
        pendingUnitSpeech = state.unitSpeech;
        pendingGamma = state.gamma;
        pendingShading = state.shading;
        pendingAntiAlias = state.antiAlias;
        audio->setSoundEnabled(state.soundMode != SoundMode::Off);
    }

    void MainMenuScene::saveOptions()
    {
        auto localDataPath = getLocalDataPath();
        if (!localDataPath)
        {
            return;
        }
        writeGameOptions(*localDataPath / "rwe.cfg", currentOptions());
    }

    void MainMenuScene::goToOptionsMenu()
    {
        if (pendingWindowMode.empty())
        {
            pendingWindowMode = sceneContext.globalConfig->windowMode;
            pendingShadows = sceneContext.globalConfig->shadows;
            pendingScrollSpeed = sceneContext.globalConfig->scrollSpeed;
            pendingSoundMode = static_cast<SoundMode>(sceneContext.globalConfig->soundMode);
            pendingUnitSpeech = static_cast<UnitSpeechLevel>(sceneContext.globalConfig->unitSpeech);
            pendingGamma = sceneContext.globalConfig->gamma;
            pendingShading = sceneContext.globalConfig->shading;
            pendingAntiAlias = sceneContext.globalConfig->antiAlias;
        }
        optionsUndo = currentOptions();
        currentOptionsPage.clear();
        goToOptionsPage(std::string());
    }

    void MainMenuScene::goToOptionsPage(const std::string& page)
    {
        // The original's options screen is a composite: STARTOPT.GUI holds
        // the page tabs and OK/Cancel, and the chosen page's own gadgets are
        // laid over it, each page bringing its own background bitmap. The
        // same effect here: one panel built from both files' gadgets.
        auto startOptRaw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/STARTOPT.GUI");
        if (!startOptRaw)
        {
            return;
        }
        auto entries = parseGuiFromBytes(*startOptRaw);
        if (!entries)
        {
            return;
        }

        std::string background = "Options4x";
        if (!page.empty())
        {
            auto pageRaw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/" + page + ".GUI");
            if (pageRaw)
            {
                if (auto pageEntries = parseGuiFromBytes(*pageRaw))
                {
                    entries->insert(entries->end(), pageEntries->begin() + 1, pageEntries->end());
                }
            }
            if (page == "SOUNDS")
            {
                background = "OptSound4x";
            }
            else if (page == "MUSIC")
            {
                background = "Optmusic4x";
            }
            else if (page == "VISUALS")
            {
                background = "OptVisual4x";
            }
            else if (page == "SPEEDS")
            {
                background = "OptInterface4x";
            }
        }

        auto panel = uiFactory.panelFromGuiFile("STARTOPT", background, *entries);

        // Switching pages swaps the panel in place; only the first entry
        // into the options screen pushes a new one.
        if (lastPanelWasOptions)
        {
            panelStack.pop_back();
        }
        currentOptionsPage = page;
        lastPanelWasOptions = true;
        goToMenu(std::move(panel));

        auto& active = *panelStack.back();
        auto state = currentOptions();

        if (auto bar = active.find<UiScrollBar>("FXVOL"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent(static_cast<float>(state.soundVolume) / 100.0f);
            auto sub = bar->get().scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setSoundVolume(v);
            });
            bar->get().addSubscription(std::move(sub));
        }

        if (auto bar = active.find<UiScrollBar>("MUSICVOL"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent(static_cast<float>(state.musicVolume) / 100.0f);
            auto sub = bar->get().scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setMusicVolume(v);
            });
            bar->get().addSubscription(std::move(sub));
        }

        if (auto toggle = active.find<UiStagedButton>("NOTRAK"))
        {
            toggle->get().setStage(state.musicEnabled ? 1 : 0);
        }

        if (auto bar = active.find<UiScrollBar>("VIDSLDR"))
        {
            bar->get().setScrollBarPercent(0.34f);
            auto modeToPercent = pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f);
            bar->get().setScrollPercent(modeToPercent);
            auto sub = bar->get().scrollChanged().subscribe([this](float v) {
                pendingWindowMode = v < 0.33f ? "windowed" : (v < 0.67f ? "borderless" : "fullscreen");
                if (auto label = panelStack.back()->find<UiLabel>("VIDVAL"))
                {
                    label->get().setText(windowModeDisplayName(pendingWindowMode));
                }
            });
            bar->get().addSubscription(std::move(sub));
        }

        if (auto label = active.find<UiLabel>("VIDVAL"))
        {
            label->get().setText(windowModeDisplayName(pendingWindowMode));
        }

        if (auto toggle = active.find<UiStagedButton>("BSHADOWS"))
        {
            toggle->get().setStage(pendingShadows ? 1 : 0);
        }

        if (auto toggle = active.find<UiStagedButton>("MODE"))
        {
            toggle->get().setStage(static_cast<unsigned int>(pendingSoundMode));
        }

        if (auto toggle = active.find<UiStagedButton>("SPEECH"))
        {
            toggle->get().setStage(static_cast<unsigned int>(pendingUnitSpeech));
        }

        if (auto bar = active.find<UiScrollBar>("GAMMA"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent((static_cast<float>(pendingGamma) - 50.0f) / 83.0f);
            auto sub = bar->get().scrollChanged().subscribe([this](float v) {
                // The original's twenty steps of 0.5 + v/24: 0.5x to 1.333x.
                pendingGamma = 50u + static_cast<unsigned int>(v * 83.0f);
            });
            bar->get().addSubscription(std::move(sub));
        }

        // Screen scroll: 25 to 200 percent across the slider's travel; takes
        // effect in the next game.
        if (auto bar = active.find<UiScrollBar>("SCREEN"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent((static_cast<float>(pendingScrollSpeed) - 25.0f) / 175.0f);
            auto sub = bar->get().scrollChanged().subscribe([this](float v) {
                pendingScrollSpeed = 25u + static_cast<unsigned int>(v * 175.0f);
            });
            bar->get().addSubscription(std::move(sub));
        }

        // What RWE has no machinery behind stays visible but grey. GAME is
        // in-game speed, meaningless from the front end.
        if (auto toggle = active.find<UiStagedButton>("SHADING"))
        {
            toggle->get().setStage(pendingShading ? 1 : 0);
        }
        if (auto toggle = active.find<UiStagedButton>("ANTI"))
        {
            toggle->get().setStage(pendingAntiAlias ? 1 : 0);
        }

        for (const auto* name : {"LEFTCLICK", "UNITCHAT", "TXTSCROL", "MAXLINES", "GAME"})
        {
            if (auto button = active.find<UiStagedButton>(name))
            {
                button->get().setEnabled(false);
            }
        }
    }

    void MainMenuScene::playMovie(const std::string& vfsPath)
    {
        auto movieBytes = sceneContext.vfs->readFile(vfsPath);
        if (!movieBytes)
        {
            return;
        }

        // The menu's own music has to stop -- the movie's soundtrack plays
        // through the same track.
        sceneContext.audioService->stopMusic();
        bgm = AudioService::LoopToken();

        auto context = sceneContext;
        auto lookup = soundLookup;
        auto scene = std::make_shared<MovieScene>(
            sceneContext,
            std::move(*movieBytes),
            [context, lookup]() {
                auto menu = std::make_shared<MainMenuScene>(
                    context,
                    lookup,
                    context.viewport->width(),
                    context.viewport->height());
                context.sceneManager->setNextScene(menu);
            });
        sceneContext.sceneManager->setNextScene(scene);
    }

    void MainMenuScene::openMessageBox(const std::string& message)
    {
        // MSGBOX.GUI is the original's one-line message box: a 372x272 plate
        // at (116,82) with an OK button. No art ships for it, so it wears the
        // panel's own plate.
        auto panel = uiFactory.panelFromGuiFile("MSGBOX");
        panel->setDrawSolidPlate(true);

        auto font = sceneContext.textureService->getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)");
        auto label = std::make_unique<UiLabel>(
            0,
            (static_cast<int>(panel->getHeight()) / 2) - 40,
            panel->getWidth(),
            20,
            message,
            font);
        label->setAlignment(UiLabel::Alignment::Center);
        panel->appendChild(std::move(label));

        openDialog(std::move(panel));
    }

    void MainMenuScene::goToLoadGameMenu()
    {
        // Nothing to load is worth saying out loud rather than opening an
        // empty list and leaving the player to work it out.
        if (listSaveGames().empty())
        {
            openMessageBox("There are no saved games to load from");
            return;
        }

        auto panel = uiFactory.panelFromGuiFile("LOADGAME");
        for (const auto* labelName : {"GAMETYPE", "SIDE", "MISSION", "DIFF", "TIME"})
        {
            if (auto label = panel->find<UiLabel>(labelName))
            {
                label->get().setText(std::string());
            }
        }
        if (auto games = panel->find<UiListBox>("GAMES"))
        {
            for (const auto& name : listSaveGames())
            {
                games->get().appendItem(name);
            }
            auto* rawPanel = panel.get();
            auto sub = games->get().selectedIndex().subscribe([rawPanel](const std::optional<unsigned int>& index) {
                if (!index)
                {
                    return;
                }
                auto games = rawPanel->find<UiListBox>("GAMES");
                auto box = rawPanel->find<UiTextBox>("GAMENAME");
                if (games && box && *index < games->get().getItems().size())
                {
                    box->get().setText(games->get().getItems()[*index]);
                }
            });
            games->get().addSubscription(std::move(sub));
        }
        goToMenu(std::move(panel));
    }

    void MainMenuScene::startLoadedGame(const std::string& name)
    {
        auto path = savePathForName(name);
        auto save = readSaveFile(path);
        if (!save)
        {
            return;
        }

        auto parameters = save->parameters;
        parameters.loadFromSaveFile = path.string();
        sceneContext.audioService->stopMusic();
        auto scene = std::make_unique<LoadingScene>(
            sceneContext,
            soundLookup,
            std::move(bgm),
            parameters);
        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(std::move(scene)));
    }

    void MainMenuScene::goToPreviousMenu()
    {
        if (!dialogStack.empty())
        {
            dialogStack.pop_back();
            return;
        }

        if (panelStack.size() > 1)
        {
            panelStack.pop_back();
            return;
        }

        exit();
    }

    void MainMenuScene::goToMenu(std::unique_ptr<UiPanel>&& panel)
    {
        auto& p = panelStack.emplace_back(std::move(panel));
        p->groupMessages().subscribe([this](const auto& msg) {
            if (auto activate = std::get_if<ActivateMessage>(&msg.message); activate != nullptr)
            {
                message(msg.topic, msg.controlName, *activate);
            }
        });
    }

    void MainMenuScene::openDialog(std::unique_ptr<UiPanel>&& panel)
    {
        auto& p = dialogStack.emplace_back(std::move(panel));
        p->groupMessages().subscribe([this](const auto& msg) {
            if (auto activate = std::get_if<ActivateMessage>(&msg.message); activate != nullptr)
            {
                message(msg.topic, msg.controlName, *activate);
            }
        });
    }

    UiPanel& MainMenuScene::topPanel()
    {
        if (!dialogStack.empty())
        {
            return *(dialogStack.back());
        }

        return *(panelStack.back());
    }

    void MainMenuScene::update(int millisecondsElapsed)
    {
        for (auto& action : std::exchange(pendingMenuActions, {}))
        {
            action();
        }

        topPanel().update(static_cast<float>(millisecondsElapsed) / 1000.0f);
    }

    void MainMenuScene::onMouseWheel(MouseWheelEvent event)
    {
        topPanel().mouseWheel(event);
    }

    void MainMenuScene::onKeyDown(const SDL_KeyboardEvent& keysym)
    {
        topPanel().keyDown(KeyEvent(keysym.key));
    }

    void MainMenuScene::goToMainMenu()
    {
        auto mainMenuGuiRaw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/MAINMENU.GUI");
        if (!mainMenuGuiRaw)
        {
            throw std::runtime_error("Couldn't read MAINMENU.GUI");
        }

        auto parsedGui = parseGuiFromBytes(*mainMenuGuiRaw);
        if (!parsedGui)
        {
            throw std::runtime_error("Failed to parse GUI file");
        }

        auto panel = uiFactory.panelFromGuiFile("MAINMENU", "FrontendX", *parsedGui);
        if (auto debugStrLabel = panel->find<UiLabel>("DebugString"))
        {
            debugStrLabel->get().setText(RevivalTitle);
            debugStrLabel->get().setAlignment(UiLabel::Alignment::Center);
        }
        goToMenu(std::move(panel));
    }

    void MainMenuScene::exit()
    {
        sceneContext.sceneManager->requestExit();
    }

    std::optional<int> matchesPlayer(const std::string& pattern, const std::string& input)
    {
        // FIXME: this should probably be a regex match instead of this crude brute-force search
        for (int i = 0; i < 10; ++i)
        {
            std::string candidate = pattern;
            auto pos = candidate.find("{0}");
            if (pos != std::string::npos)
            {
                candidate.replace(pos, 3, std::to_string(i));
            }
            if (input == candidate)
            {
                return i;
            }
        }

        return std::nullopt;
    }

    void MainMenuScene::message(const std::string& topic, const std::string& message, const ActivateMessage& details)
    {
        // Defer: handlers that pop or replace the panel would otherwise
        // destroy the object whose event dispatch we are standing in.
        pendingMenuActions.push_back([this, topic, message, details]() { messageNow(topic, message, details); });
    }

    void MainMenuScene::messageNow(const std::string& topic, const std::string& message, const ActivateMessage& details)
    {
        if (message == "PrevMenu" || message == "PREVMENU")
        {
            goToPreviousMenu();
        }
        else if (topic == "MAINMENU")
        {
            if (message == "EXIT")
            {
                exit();
            }
            else if (message == "SINGLE")
            {
                goToSingleMenu();
            }
            else if (message == "INTRO")
            {
                playMovie("movies/2.zrb");
            }
        }
        else if (topic == "SINGLE")
        {
            if (message == "Skirmish")
            {
                goToSkirmishMenu();
            }
            else if (message == "Options")
            {
                goToOptionsMenu();
            }
            else if (message == "LoadGame")
            {
                goToLoadGameMenu();
            }
        }
        else if (topic == "LOADGAME")
        {
            if (message == "CANCEL")
            {
                goToPreviousMenu();
            }
            else if (message == "DELETE")
            {
                if (auto box = panelStack.back()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                {
                    std::error_code ec;
                    std::filesystem::remove(savePathForName(box->get().getText()), ec);
                }
                goToPreviousMenu();
                goToLoadGameMenu();
            }
            else if (message == "LOAD")
            {
                if (auto box = panelStack.back()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                {
                    startLoadedGame(box->get().getText());
                }
            }
        }
        else if (topic == "MSGBOX")
        {
            if (message == "OK")
            {
                goToPreviousMenu();
            }
        }
        else if (topic == "STARTOPT")
        {
            if (message == "SOUND")
            {
                goToOptionsPage("SOUNDS");
            }
            else if (message == "MUSIC")
            {
                goToOptionsPage("MUSIC");
            }
            else if (message == "VISUALS")
            {
                goToOptionsPage("VISUALS");
            }
            else if (message == "SPEEDS")
            {
                goToOptionsPage("SPEEDS");
            }
            else if (message == "PREV")
            {
                // The button says OK. Keep the settings and leave.
                saveOptions();
                lastPanelWasOptions = false;
                currentOptionsPage.clear();
                goToPreviousMenu();
            }
            else if (message == "CANCEL")
            {
                applyOptions(optionsUndo);
                lastPanelWasOptions = false;
                currentOptionsPage.clear();
                goToPreviousMenu();
            }
            else if (message == "RESTORE")
            {
                applyOptions(GameOptions{});
                goToOptionsPage(currentOptionsPage);
            }
            else if (message == "BSHADOWS")
            {
                pendingShadows = !pendingShadows;
            }
            else if (message == "MODE")
            {
                pendingSoundMode = nextStage(pendingSoundMode);
                sceneContext.audioService->setSoundEnabled(pendingSoundMode != SoundMode::Off);
            }
            else if (message == "SPEECH")
            {
                pendingUnitSpeech = nextStage(pendingUnitSpeech);
            }
            else if (message == "SHADING")
            {
                pendingShading = !pendingShading;
            }
            else if (message == "ANTI")
            {
                pendingAntiAlias = !pendingAntiAlias;
            }
            else if (message == "UNDO")
            {
                applyOptions(optionsUndo);
                goToOptionsPage(currentOptionsPage);
            }
            else if (message == "NOTRAK")
            {
                // The button has already cycled its own Off|On label; the
                // setting takes effect in game, the menu stays quiet.
                auto* audio = sceneContext.audioService;
                audio->setMusicEnabled(!audio->isMusicEnabled());
            }
            else if (message == "CDPLAY")
            {
                playMenuMusic();
            }
            else if (message == "CDSTOP")
            {
                sceneContext.audioService->stopMusic();
            }
            else if (message == "CDNEXT" || message == "CDPREV")
            {
                if (!menuPlaylist.empty())
                {
                    auto count = menuPlaylist.size();
                    menuPlaylistIndex = (menuPlaylistIndex + (message == "CDNEXT" ? 1 : count - 1)) % count;
                    playMenuMusic();
                }
            }
            else if (message == "TEST")
            {
                if (auto sound = sceneContext.audioService->loadSound("BUTTON10"))
                {
                    sceneContext.audioService->playSound(*sound);
                }
            }
        }
        else if (topic == "SKIRMISH")
        {
            if (message == "SelectMap")
            {
                resetCandidateSelectedMap();
                openMapSelectionDialog();
            }
            else if (message == "Start")
            {
                startGame();
            }
            else if (message == "CommanderDeath" || message == "StartLocation" || message == "Mapping" || message == "LineOfSight" || message == "Difficulty")
            {
                cycleSkirmishOption(message);
            }
            else if (auto num = matchesPlayer("PLAYER{0}", message))
            {
                togglePlayer(*num);
            }
            else if (auto num = matchesPlayer("PLAYER{0}_side", message))
            {
                togglePlayerSide(*num);
            }
            else if (auto num = matchesPlayer("PLAYER{0}_color", message))
            {
                switch (details.type)
                {
                    case ActivateMessage::Type::Primary:
                        cyclePlayerColor(*num);
                        break;
                    case ActivateMessage::Type::Secondary:
                        reverseCyclePlayerColor(*num);
                        break;
                    default:
                        throw std::logic_error("Invalid activation type");
                }
            }
            else if (auto num = matchesPlayer("PLAYER{0}_team", message))
            {
                cyclePlayerTeam(*num);
            }
            else if (auto num = matchesPlayer("PLAYER{0}_metal", message))
            {
                switch (details.type)
                {
                    case ActivateMessage::Type::Primary:
                        incrementPlayerMetal(*num);
                        break;
                    case ActivateMessage::Type::Secondary:
                        decrementPlayerMetal(*num);
                        break;
                    default:
                        throw std::logic_error("Invalid activation type");
                }
            }
            else if (auto num = matchesPlayer("PLAYER{0}_energy", message))
            {
                switch (details.type)
                {
                    case ActivateMessage::Type::Primary:
                        incrementPlayerEnergy(*num);
                        break;
                    case ActivateMessage::Type::Secondary:
                        decrementPlayerEnergy(*num);
                        break;
                    default:
                        throw std::logic_error("Invalid activation type");
                }
            }
        }
        else if (topic == "SELMAP")
        {
            if (message == "LOAD")
            {
                commitSelectedMap();
                goToPreviousMenu();
            }
        }

        // Whatever just happened, the widgets show the state as it now is.
        refreshOptionControls();
    }

    void MainMenuScene::refreshOptionControls()
    {
        if (panelStack.empty())
        {
            return;
        }
        auto& active = *panelStack.back();
        auto* audio = sceneContext.audioService;
        if (auto toggle = active.find<UiStagedButton>("NOTRAK"))
        {
            toggle->get().setStage(audio->isMusicEnabled() ? 1 : 0);
        }
        if (auto toggle = active.find<UiStagedButton>("MODE"))
        {
            toggle->get().setStage(static_cast<unsigned int>(pendingSoundMode));
        }
        if (auto toggle = active.find<UiStagedButton>("SPEECH"))
        {
            toggle->get().setStage(static_cast<unsigned int>(pendingUnitSpeech));
        }
        if (auto toggle = active.find<UiStagedButton>("BSHADOWS"))
        {
            toggle->get().setStage(pendingShadows ? 1 : 0);
        }
        if (auto toggle = active.find<UiStagedButton>("SHADING"))
        {
            toggle->get().setStage(pendingShading ? 1 : 0);
        }
        if (auto toggle = active.find<UiStagedButton>("ANTI"))
        {
            toggle->get().setStage(pendingAntiAlias ? 1 : 0);
        }
        if (auto bar = active.find<UiScrollBar>("FXVOL"))
        {
            bar->get().setScrollPercent(audio->getSoundVolume());
        }
        if (auto bar = active.find<UiScrollBar>("MUSICVOL"))
        {
            bar->get().setScrollPercent(audio->getMusicVolume());
        }
        if (auto bar = active.find<UiScrollBar>("GAMMA"))
        {
            bar->get().setScrollPercent((static_cast<float>(pendingGamma) - 50.0f) / 83.0f);
        }
        if (auto bar = active.find<UiScrollBar>("SCREEN"))
        {
            bar->get().setScrollPercent((static_cast<float>(pendingScrollSpeed) - 25.0f) / 175.0f);
        }
        if (auto bar = active.find<UiScrollBar>("VIDSLDR"))
        {
            bar->get().setScrollPercent(pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f));
        }
        if (auto label = active.find<UiLabel>("VIDVAL"))
        {
            label->get().setText(windowModeDisplayName(pendingWindowMode));
        }
    }

    void MainMenuScene::goToSingleMenu()
    {
        auto mainMenuGuiRaw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/SINGLE.GUI");
        if (!mainMenuGuiRaw)
        {
            throw std::runtime_error("Couldn't read SINGLE.GUI");
        }

        auto parsedGui = parseGuiFromBytes(*mainMenuGuiRaw);
        if (!parsedGui)
        {
            throw std::runtime_error("Failed to parse GUI file");
        }

        auto panel = uiFactory.panelFromGuiFile("SINGLE", "SINGLEBG", *parsedGui);
        goToMenu(std::move(panel));
    }

    void MainMenuScene::goToSkirmishMenu()
    {
        auto mainMenuGuiRaw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/SKIRMISH.GUI");
        if (!mainMenuGuiRaw)
        {
            throw std::runtime_error("Couldn't read SKIRMISH.GUI");
        }

        auto parsedGui = parseGuiFromBytes(*mainMenuGuiRaw);
        if (!parsedGui)
        {
            throw std::runtime_error("Failed to parse GUI file");
        }

        // Pick the default map before the panel subscribes to the selection,
        // so the map name shows as soon as the screen opens.
        selectDefaultMap();

        auto panel = uiFactory.panelFromGuiFile("SKIRMISH", "Skirmsetup4x", *parsedGui);
        if (auto mapLabel = panel->find<UiLabel>("MapName"))
        {
            auto sub = model.selectedMap.subscribe([&l = mapLabel->get()](const auto& selectedMap) {
                if (selectedMap)
                {
                    l.setText(selectedMap->name);
                }
                else
                {
                    l.setText("");
                }
            });
            mapLabel->get().addSubscription(std::move(sub));
        }

        attachSkirmishOptionComponents(*panel);
        attachPlayerSelectionComponents("SKIRMISH", *panel);

        goToMenu(std::move(panel));
    }

    void MainMenuScene::selectDefaultMap()
    {
        if (model.selectedMap.getValue())
        {
            return;
        }

        auto mapNames = getMapNames();
        if (mapNames.empty())
        {
            return;
        }

        const std::string preferredMap = "Coast To Coast";
        auto it = std::find_if(mapNames.begin(), mapNames.end(), [&preferredMap](const auto& name) { return toUpper(name) == toUpper(preferredMap); });
        const auto& mapName = it != mapNames.end() ? *it : mapNames.front();

        setCandidateSelectedMap(mapName);
        commitSelectedMap();
    }

    std::string describeSkirmishOption(const std::string& optionName, unsigned int stage)
    {
        // These are the original's own sentences, not a paraphrase of them:
        // they sit in one run in TotalA.exe from 0x1067bc, in the same part
        // of the string table as the skirmish screen's other help lines, and
        // are listed for translation in gamedata/Translate.tdf. Difficulty is
        // the exception -- it has no per-stage line, only the help= field
        // SKIRMISH.GUI hangs on the button itself, so that is what it gets.
        if (optionName == "CommanderDeath")
        {
            return stage == 0
                ? "Game ends when commander is destroyed."
                : "Game continues after Commander is destroyed.";
        }
        if (optionName == "StartLocation")
        {
            return stage == 0
                ? "Commanders are placed at pre-determined locations."
                : "Commanders are randomly placed on the battle field.";
        }
        if (optionName == "Mapping")
        {
            return stage == 0
                ? "Terrain is blacked out until explored."
                : "Terrain is visible.";
        }
        if (optionName == "LineOfSight")
        {
            switch (stage)
            {
                case 0:
                    return "All mapped terrain is visible.";
                case 1:
                    return "Terrain elevations affect a unit's view.";
                default:
                    return "Terrain elevations do not affect a unit's view.";
            }
        }
        if (optionName == "Difficulty")
        {
            return "Adjust skirmish difficulty.";
        }

        return "";
    }

    void MainMenuScene::attachSkirmishOptionComponents(UiPanel& panel)
    {
        // SKIRMISH.GUI's HELPTEXT field, the wide empty strip under the setup
        // box. Nothing else writes to it, so the option buttons can have it.
        auto helpText = panel.find<UiLabel>("HELPTEXT");
        auto* helpLabel = helpText ? &helpText->get() : nullptr;

        auto attach = [&panel, helpLabel](const std::string& name, BehaviorSubject<unsigned int>& option) {
            auto button = panel.find<UiStagedButton>(name);
            if (!button)
            {
                return;
            }

            auto sub = option.subscribe([b = &button->get(), helpLabel, name](unsigned int stage) {
                b->setStage(stage);
                if (helpLabel != nullptr)
                {
                    helpLabel->setText(describeSkirmishOption(name, stage));
                }
            });
            button->get().addSubscription(std::move(sub));
        };

        attach("CommanderDeath", model.skirmishOptions.commanderDeath);
        attach("StartLocation", model.skirmishOptions.startLocation);
        attach("Mapping", model.skirmishOptions.mapping);
        attach("LineOfSight", model.skirmishOptions.lineOfSight);
        attach("Difficulty", model.skirmishOptions.difficulty);

        // Subscribing fires each callback once, so the field would otherwise
        // open showing whichever option was attached last. Start it on the
        // button at the top of the column instead.
        if (helpLabel != nullptr)
        {
            helpLabel->setText(describeSkirmishOption("CommanderDeath", model.skirmishOptions.commanderDeath.getValue()));
        }
    }

    void MainMenuScene::cycleSkirmishOption(const std::string& optionName)
    {
        // Each option cycles through the stages listed for it in SKIRMISH.GUI.
        auto cycle = [](BehaviorSubject<unsigned int>& option, unsigned int stageCount) {
            option.next((option.getValue() + 1) % stageCount);
        };

        if (optionName == "CommanderDeath")
        {
            cycle(model.skirmishOptions.commanderDeath, 2);
        }
        else if (optionName == "StartLocation")
        {
            cycle(model.skirmishOptions.startLocation, 2);
        }
        else if (optionName == "Mapping")
        {
            cycle(model.skirmishOptions.mapping, 2);
        }
        else if (optionName == "LineOfSight")
        {
            cycle(model.skirmishOptions.lineOfSight, 3);
        }
        else if (optionName == "Difficulty")
        {
            cycle(model.skirmishOptions.difficulty, 3);
        }
    }

    void MainMenuScene::openMapSelectionDialog()
    {
        auto guiRaw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/SELMAP.GUI");
        if (!guiRaw)
        {
            throw std::runtime_error("Couldn't read SELMAP.GUI");
        }

        auto parsedGui = parseGuiFromBytes(*guiRaw);
        if (!parsedGui)
        {
            throw std::runtime_error("Failed to parse GUI file");
        }

        auto panel = uiFactory.panelFromGuiFile("SELMAP", "DSelectmap2", *parsedGui);

        if (auto descriptionLabel = panel->find<UiLabel>("DESCRIPTION"))
        {
            auto sub = model.candidateSelectedMap.subscribe([&l = descriptionLabel->get()](const auto& selectedMap) {
                if (selectedMap)
                {
                    l.setText(selectedMap->description);
                }
                else
                {
                    l.setText("");
                }
            });
            descriptionLabel->get().addSubscription(std::move(sub));
        }
        if (auto sizeLabel = panel->find<UiLabel>("SIZE"))
        {
            auto sub = model.candidateSelectedMap.subscribe([&l = sizeLabel->get()](const auto& selectedMap) {
                if (selectedMap)
                {
                    l.setText(selectedMap->size);
                }
                else
                {
                    l.setText("");
                }
            });
            sizeLabel->get().addSubscription(std::move(sub));
        }

        if (auto listBox = panel->find<UiListBox>("MAPNAMES"))
        {
            auto mapNames = getMapNames();
            for (const auto& e : mapNames)
            {
                listBox->get().appendItem(e);
            }

            auto sub = model.selectedMap.subscribe([&l = listBox->get()](const auto& selectedMap) {
                if (selectedMap)
                {
                    l.setSelectedItem(selectedMap->name);
                }
                else
                {
                    l.clearSelectedItem();
                }
            });
            listBox->get().addSubscription(std::move(sub));

            listBox->get().selectedIndex().subscribe([&l = listBox->get(), &c = *this](const auto& selectedMap) {
                if (selectedMap)
                {
                    c.setCandidateSelectedMap(l.getItems()[*selectedMap]);
                }
                else
                {
                    c.clearCandidateSelectedMap();
                }
            });
        }

        if (auto surface = panel->find<UiSurface>("MAPPIC"))
        {
            auto sub = model.candidateSelectedMap.subscribe([&s = surface->get()](const auto& info) {
                if (info)
                {
                    s.setBackground(info->minimap);
                }
                else
                {
                    s.clearBackground();
                }
            });
            surface->get().addSubscription(std::move(sub));
        }

        openDialog(std::move(panel));
    }

    void MainMenuScene::setCandidateSelectedMap(const std::string& mapName)
    {
        auto otaRaw = sceneContext.vfs->readFile(std::string("maps/").append(mapName).append(".ota"));
        if (!otaRaw)
        {
            return;
        }

        std::string otaStr(otaRaw->begin(), otaRaw->end());

        auto ota = parseOta(parseTdfFromString(otaStr));

        auto minimap = sceneContext.textureService->getMinimap(mapName);

        // this is what TA shows in its map selection dialog
        auto sizeInfo = std::string().append(ota.memory).append("  Players: ").append(ota.numPlayers);

        MainMenuModel::SelectedMapInfo info(
            mapName,
            ota.missionDescription,
            sizeInfo,
            minimap);

        model.candidateSelectedMap.next(std::move(info));
    }

    void MainMenuScene::resetCandidateSelectedMap()
    {
        model.candidateSelectedMap.next(model.selectedMap.getValue());
    }

    void MainMenuScene::commitSelectedMap()
    {
        model.selectedMap.next(model.candidateSelectedMap.getValue());
    }

    void MainMenuScene::clearCandidateSelectedMap()
    {
        model.candidateSelectedMap.next(std::nullopt);
    }

    void MainMenuScene::incrementPlayerMetal(int playerIndex)
    {
        auto& player = model.players[playerIndex];

        if (player.metal.getValue() == Metal(200))
        {
            player.metal.next(Metal(500));
        }
        else if (player.metal.getValue() < Metal(10000))
        {
            player.metal.next(player.metal.getValue() + Metal(500));
        }
    }

    void MainMenuScene::decrementPlayerMetal(int playerIndex)
    {
        auto& player = model.players[playerIndex];

        if (player.metal.getValue() > Metal(500))
        {
            player.metal.next(player.metal.getValue() - Metal(500));
        }
        else if (player.metal.getValue() == Metal(500))
        {
            player.metal.next(Metal(200));
        }
    }

    void MainMenuScene::incrementPlayerEnergy(int playerIndex)
    {
        auto& player = model.players[playerIndex];

        if (player.energy.getValue() == Energy(200))
        {
            player.energy.next(Energy(500));
        }
        else if (player.energy.getValue() < Energy(10000))
        {
            player.energy.next(player.energy.getValue() + Energy(500));
        }
    }

    void MainMenuScene::decrementPlayerEnergy(int playerIndex)
    {
        auto& player = model.players[playerIndex];

        if (player.energy.getValue() > Energy(500))
        {
            player.energy.next(player.energy.getValue() - Energy(500));
        }
        else if (player.energy.getValue() == Energy(500))
        {
            player.energy.next(Energy(200));
        }
    }

    void MainMenuScene::togglePlayer(int playerIndex)
    {
        auto& player = model.players[playerIndex];

        switch (player.type.getValue())
        {
            case MainMenuModel::PlayerSettings::Type::Open:
            {
                auto color = player.colorIndex.getValue();
                if (model.isColorInUse(color))
                {
                    auto newColor = model.getFirstFreeColor();
                    if (!newColor)
                    {
                        throw std::logic_error("No free colors for the player");
                    }
                    color = *newColor;
                }

                bool humanAllowed = std::find_if(model.players.begin(), model.players.end(), [](const auto& p) {
                    return p.type.getValue() == MainMenuModel::PlayerSettings::Type::Human;
                }) == model.players.end();
                if (humanAllowed)
                {
                    player.type.next(MainMenuModel::PlayerSettings::Type::Human);
                }
                else
                {
                    player.type.next(MainMenuModel::PlayerSettings::Type::Computer);
                }
                player.colorIndex.next(color);
                break;
            }
            case MainMenuModel::PlayerSettings::Type::Human:
                player.type.next(MainMenuModel::PlayerSettings::Type::Computer);
                break;
            case MainMenuModel::PlayerSettings::Type::Computer:
                player.type.next(MainMenuModel::PlayerSettings::Type::Open);
                break;
        }
    }

    void MainMenuScene::togglePlayerSide(int playerIndex)
    {
        auto& player = model.players[playerIndex];
        switch (player.side.getValue())
        {
            case MainMenuModel::PlayerSettings::Side::Arm:
                player.side.next(MainMenuModel::PlayerSettings::Side::Core);
                break;
            case MainMenuModel::PlayerSettings::Side::Core:
                player.side.next(MainMenuModel::PlayerSettings::Side::Arm);
                break;
        }
    }

    void MainMenuScene::cyclePlayerColor(int playerIndex)
    {
        auto& player = model.players[playerIndex];
        auto currentColor = player.colorIndex.getValue();
        for (unsigned int i = 1; i < 10; ++i)
        {
            auto newColor = PlayerColorIndex((currentColor.value + i) % 10);
            if (!model.isColorInUse(newColor))
            {
                player.colorIndex.next(newColor);
                return;
            }
        }
    }

    void MainMenuScene::reverseCyclePlayerColor(int playerIndex)
    {
        auto& player = model.players[playerIndex];
        auto currentColor = player.colorIndex.getValue();
        for (unsigned int i = 9; i >= 1; --i)
        {
            auto newColor = PlayerColorIndex((currentColor.value + i) % 10);
            if (!model.isColorInUse(newColor))
            {
                player.colorIndex.next(newColor);
                return;
            }
        }
    }

    void MainMenuScene::cyclePlayerTeam(int playerIndex)
    {
        auto& player = model.players[playerIndex];
        if (!player.teamIndex.getValue())
        {
            player.teamIndex.next(0);
            model.teamChanges.next(0);
        }
        else
        {
            auto val = *(player.teamIndex.getValue());
            if (val == 4)
            {
                player.teamIndex.next(std::nullopt);
                model.teamChanges.next(val);
            }
            else
            {
                player.teamIndex.next(val + 1);
                model.teamChanges.next(val);
                model.teamChanges.next(val + 1);
            }
        }
    }

    std::string getSideName(const MainMenuModel::PlayerSettings::Side side)
    {
        switch (side)
        {
            case MainMenuModel::PlayerSettings::Side::Arm:
                return "ARM";
            case MainMenuModel::PlayerSettings::Side::Core:
                return "CORE";
        }

        throw std::logic_error("Invalid side");
    }

    PlayerControllerType playerSettingsTypeToPlayerControllerType(MainMenuModel::PlayerSettings::Type t)
    {
        switch (t)
        {
            case MainMenuModel::PlayerSettings::Type::Human:
                return PlayerControllerTypeHuman();
            case MainMenuModel::PlayerSettings::Type::Computer:
                return PlayerControllerTypeComputer();
            default:
                throw std::logic_error("Invalid player settings type");
        }
    }

    AiDifficulty skirmishDifficultyToAiDifficulty(unsigned int stage)
    {
        // The skirmish screen offers Easy | Medium | Hard.
        switch (stage)
        {
            case 0:
                return AiDifficulty::Easy;
            case 1:
                return AiDifficulty::Standard;
            default:
                return AiDifficulty::Hard;
        }
    }

    // The remaining option buttons, turned from the stage the button is
    // showing into the mode the game runs under. The stage order is
    // SKIRMISH.GUI's own, read off the text= field of each gadget, and the
    // last stage is the default arm so that a button that somehow ran past
    // its stage count still lands on a real mode.

    /** SKIRMISH.GUI: text=Permanent|True|Circular. */
    LineOfSightMode skirmishStageToLineOfSightMode(unsigned int stage)
    {
        switch (stage)
        {
            case 0:
                return LineOfSightMode::Permanent;
            case 1:
                return LineOfSightMode::True;
            default:
                return LineOfSightMode::Circular;
        }
    }

    /** SKIRMISH.GUI: text=Unmapped|Mapped. */
    MappingMode skirmishStageToMappingMode(unsigned int stage)
    {
        return stage == 0 ? MappingMode::Unmapped : MappingMode::Mapped;
    }

    /** SKIRMISH.GUI: text=Fixed|Random. */
    StartLocationMode skirmishStageToStartLocationMode(unsigned int stage)
    {
        return stage == 0 ? StartLocationMode::Fixed : StartLocationMode::Random;
    }

    /** SKIRMISH.GUI: text=Game ends|Continues. */
    CommanderDeathMode skirmishStageToCommanderDeathMode(unsigned int stage)
    {
        return stage == 0 ? CommanderDeathMode::GameEnds : CommanderDeathMode::GameContinues;
    }

    void MainMenuScene::startGame()
    {
        if (!model.selectedMap.getValue())
        {
            return;
        }

        GameParameters params{model.selectedMap.getValue()->name, 0};
        params.aiDifficulty = skirmishDifficultyToAiDifficulty(model.skirmishOptions.difficulty.getValue());
        params.lineOfSight = skirmishStageToLineOfSightMode(model.skirmishOptions.lineOfSight.getValue());
        params.mapping = skirmishStageToMappingMode(model.skirmishOptions.mapping.getValue());
        params.startLocation = skirmishStageToStartLocationMode(model.skirmishOptions.startLocation.getValue());
        params.commanderDeath = skirmishStageToCommanderDeathMode(model.skirmishOptions.commanderDeath.getValue());

        for (Index i = 0; i < getSize(model.players); ++i)
        {
            const auto& playerSlot = model.players[i];
            if (playerSlot.type.getValue() == MainMenuModel::PlayerSettings::Type::Open)
            {
                params.players[i] = std::nullopt;
                continue;
            }

            auto controller = playerSettingsTypeToPlayerControllerType(playerSlot.type.getValue());

            PlayerInfo playerInfo{std::nullopt, controller, getSideName(playerSlot.side.getValue()), playerSlot.colorIndex.getValue(), playerSlot.metal.getValue(), playerSlot.energy.getValue(), playerSlot.teamIndex.getValue()};
            params.players[i] = std::move(playerInfo);
        }

        auto scene = std::make_unique<LoadingScene>(
            sceneContext,
            soundLookup,
            std::move(bgm),
            params);

        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(std::move(scene)));
    }

    Point MainMenuScene::toScaledCoordinates(int x, int y) const
    {
        auto clip = screenToWorldRayUtil(scaledUiRenderService.getInverseViewProjectionMatrix(), sceneContext.viewport->toClipSpace(x, y));
        return Point(static_cast<int>(clip.origin.x), static_cast<int>(clip.origin.y));
    }

    std::vector<std::string> MainMenuScene::getMapNames()
    {
        auto mapNames = sceneContext.vfs->getFileNames("maps", ".ota");

        // Keep only maps that have a multiplayer schema
        mapNames.erase(std::remove_if(mapNames.begin(), mapNames.end(), [this](const auto& e) { return !hasMultiplayerSchema(e); }), mapNames.end());

        for (auto& e : mapNames)
        {
            // chop off the extension
            e.resize(e.size() - 4);
        }

        return mapNames;
    }

    void MainMenuScene::attachPlayerSelectionComponents(const std::string& guiName, UiPanel& panel)
    {
        unsigned int tableStart = 78;
        unsigned int rowHeight = 20;

        for (int i = 0; i < 10; ++i)
        {
            unsigned int rowStart = tableStart + (i * rowHeight);

            {
                // player name button
                unsigned int width = 112;
                unsigned int height = 20;

                auto b = uiFactory.createBasicButton(45, rowStart, width, height, guiName, "skirmname", "Player");
                b->setName("PLAYER" + std::to_string(i));
                b->setTextAlign(UiStagedButton::TextAlign::Center);

                auto sub = model.players[i].type.subscribe([b = b.get(), &panel, this, guiName, i](MainMenuModel::PlayerSettings::Type type) {
                    switch (type)
                    {
                        case MainMenuModel::PlayerSettings::Type::Open:
                            panel.removeChildrenWithPrefix("PLAYER" + std::to_string(i) + "_");
                            b->setLabel("Open");
                            break;
                        case MainMenuModel::PlayerSettings::Type::Human:
                            b->setLabel("Player");
                            panel.removeChildrenWithPrefix("PLAYER" + std::to_string(i) + "_");
                            attachDetailedPlayerSelectionComponents(guiName, panel, i);
                            break;
                        case MainMenuModel::PlayerSettings::Type::Computer:
                            b->setLabel("Computer");
                            panel.removeChildrenWithPrefix("PLAYER" + std::to_string(i) + "_");
                            attachDetailedPlayerSelectionComponents(guiName, panel, i);
                            break;
                    }
                });
                b->addSubscription(std::move(sub));

                panel.appendChild(std::move(b));
            }
        }
    }

    void MainMenuScene::attachDetailedPlayerSelectionComponents(const std::string& guiName, UiPanel& panel, int i)
    {
        unsigned int tableStart = 78;
        unsigned int rowHeight = 20;

        unsigned int rowStart = tableStart + (i * rowHeight);

        {
            // side button
            unsigned int width = 44;
            unsigned int height = 20;

            auto b = uiFactory.createStagedButton(163, rowStart, width, height, guiName, "SIDEx", std::vector<std::string>(2), 2);
            b->setName("PLAYER" + std::to_string(i) + "_side");

            auto sub = model.players[i].side.subscribe([b = b.get()](MainMenuModel::PlayerSettings::Side side) {
                switch (side)
                {
                    case MainMenuModel::PlayerSettings::Side::Arm:
                        b->setStage(0);
                        break;
                    case MainMenuModel::PlayerSettings::Side::Core:
                        b->setStage(1);
                        break;
                }
            });
            b->addSubscription(std::move(sub));

            panel.appendChild(std::move(b));
        }

        {
            // color
            unsigned int width = 19;
            unsigned int height = 19;

            auto graphics = sceneContext.textureService->getGafEntry("anims/LOGOS.GAF", "32xlogos");
            auto newSprites = std::make_shared<SpriteSeries>();
            newSprites->sprites.reserve(graphics->sprites.size());

            std::transform(graphics->sprites.begin(), graphics->sprites.end(), std::back_inserter(newSprites->sprites), [width, height](const auto& sprite) {
                auto bounds = Rectangle2f::fromTopLeft(0.0f, 0.0f, width, height);
                return std::make_shared<Sprite>(bounds, sprite->texture, sprite->mesh);
            });

            auto b = uiFactory.createButton(214, rowStart, width, height, guiName, "logo", "");
            b->setName("PLAYER" + std::to_string(i) + "_color");

            auto sub = model.players[i].colorIndex.subscribe([b = b.get(), newSprites](const auto& index) {
                b->setNormalSprite(newSprites->sprites[index.value]);
                b->setPressedSprite(newSprites->sprites[index.value]);
            });
            b->addSubscription(std::move(sub));

            panel.appendChild(std::move(b));
        }

        {
            // ally
            unsigned int width = 38;
            unsigned int height = 20;

            auto graphics = sceneContext.textureService->getGuiTexture(guiName, "TEAMICONSx");
            if (!graphics)
            {
                graphics = sceneContext.textureService->getGuiTexture(guiName, "ally icons");
            }
            if (!graphics)
            {
                throw std::runtime_error("Failed to load TEAMICONSx");
            }

            auto b = uiFactory.createButton(241, rowStart, width, height, guiName, "team", "");
            b->setName("PLAYER" + std::to_string(i) + "_team");

            auto sub = model.players[i].teamIndex.subscribe([b = b.get(), &m = model, g = *graphics](auto index) {
                if (!index)
                {
                    b->setNormalSprite(g->sprites[10]);
                    b->setPressedSprite(g->sprites[10]);
                    return;
                }

                auto stage = (*index) * 2;
                if (!m.isTeamShared(*index))
                {
                    ++stage;
                }

                b->setNormalSprite(g->sprites[stage]);
                b->setPressedSprite(g->sprites[stage]);
            });
            b->addSubscription(std::move(sub));

            auto teamSub = model.teamChanges.subscribe([b = b.get(), &m = model, g = *graphics, i](auto index) {
                if (index == m.players[i].teamIndex.getValue())
                {
                    auto stage = index * 2;
                    if (m.isTeamShared(index))
                    {
                        b->setNormalSprite(g->sprites[stage]);
                        b->setPressedSprite(g->sprites[stage]);
                    }
                    else
                    {
                        b->setNormalSprite(g->sprites[stage + 1]);
                        b->setPressedSprite(g->sprites[stage + 1]);
                    }
                }
            });
            b->addSubscription(std::move(teamSub));

            panel.appendChild(std::move(b));
        }

        {
            // metal
            unsigned int width = 46;
            unsigned int height = 20;

            auto b = uiFactory.createButton(286, rowStart, width, height, guiName, "skirmmet", "");
            b->setName("PLAYER" + std::to_string(i) + "_metal");
            b->setTextAlign(UiStagedButton::TextAlign::Center);

            auto sub = model.players[i].metal.subscribe([b = b.get()](const auto& newMetal) {
                b->setLabel(formatResource(newMetal));
            });
            b->addSubscription(std::move(sub));

            panel.appendChild(std::move(b));
        }

        {
            // energy
            unsigned int width = 46;
            unsigned int height = 20;

            auto b = uiFactory.createButton(337, rowStart, width, height, guiName, "skirmmet", "");
            b->setName("PLAYER" + std::to_string(i) + "_energy");
            b->setTextAlign(UiStagedButton::TextAlign::Center);

            auto sub = model.players[i].energy.subscribe([b = b.get()](const auto& newEnergy) {
                b->setLabel(formatResource(newEnergy));
            });
            b->addSubscription(std::move(sub));

            panel.appendChild(std::move(b));
        }
    }

    bool MainMenuScene::hasMultiplayerSchema(const std::string& mapName)
    {
        auto otaRaw = sceneContext.vfs->readFile(std::string("maps/").append(mapName));
        if (!otaRaw)
        {
            throw std::runtime_error("Failed to read OTA file");
        }

        return parseTdfHasNetworkSchema(*otaRaw);
    }
}
