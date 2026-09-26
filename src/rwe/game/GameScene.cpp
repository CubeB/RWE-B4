#include "GameScene.h"
#include <algorithm>
#include <cmath>
#include <rwe/game/panel_slide.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/game/ScenarioDriver.h>
#include <rwe/io/gui/gui.h>
#include <rwe/game/save_util.h>
#include <rwe/network_util.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/MainMenuScene.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/Mesh.h>
#include <rwe/camera_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/game/OrderButtons.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/matrix_util.h>
#include <rwe/render/render_prof.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/resource_io.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    /** True for particles drawn among the world's geometry rather than over the finished frame. */
    bool particleDrawsInWorld(const Particle& particle)
    {
        auto sprite = std::get_if<ParticleRenderTypeSprite>(&particle.renderType);
        return sprite != nullptr && sprite->inWorld;
    }

    bool isValidUnitType(const GameSimulation& simulation, const std::string& unitType)
    {
        return simulation.unitDefinitions.find(unitType) != simulation.unitDefinitions.end();
    }

    std::optional<std::reference_wrapper<const std::vector<GuiEntry>>> getBuilderGui(const BuilderGuisDatabase& db, const std::string& unitType, unsigned int page)
    {
        const auto& pages = db.tryGetBuilderGui(unitType);
        if (!pages)
        {
            return std::nullopt;
        }

        const auto& unwrappedPages = pages->get();

        if (page >= unwrappedPages.size())
        {
            return std::nullopt;
        }

        return unwrappedPages[page];
    }

    /** If the unit has no build gui, this will be zero. */
    unsigned int getBuildPageCount(const BuilderGuisDatabase& db, const std::string& unitType)
    {
        const auto& pages = db.tryGetBuilderGui(unitType);
        if (!pages)
        {
            return 0;
        }

        return pages->get().size();
    }

    bool unitIsBuilder(const GameSimulation& sim, std::optional<UnitId> singleSelectedUnit)
    {
        if (!singleSelectedUnit)
        {
            return false;
        }
        const auto& unit = sim.getUnitState(*singleSelectedUnit);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.builder;
    }

    bool unitIsSelectableBy(const GameSimulation& sim, UnitId unitId, PlayerId playerId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unit.isSelectableBy(unitDefinition, playerId);
    }

    bool unitIsOwnedByPlayerAndIsBuilder(const GameSimulation& sim, PlayerId playerId, std::optional<UnitId> singleSelectedUnit)
    {
        if (!singleSelectedUnit)
        {
            return false;
        }
        const auto& unit = sim.getUnitState(*singleSelectedUnit);
        if (!unit.isOwnedBy(playerId))
        {
            return false;
        }
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.builder;
    }

    bool shouldShowAllBuildBoxes(const GameSimulation& sim, PlayerId localPlayerId, std::optional<UnitId> singleSelectedUnit, std::optional<UnitId> hoveredUnit)
    {
        return unitIsBuilder(sim, singleSelectedUnit) || unitIsOwnedByPlayerAndIsBuilder(sim, localPlayerId, hoveredUnit);
    }

    Line3x<SimScalar> floatToSimLine(const Line3f& line)
    {
        return Line3x<SimScalar>(floatToSimVector(line.start), floatToSimVector(line.end));
    }

    Matrix4f computeView(const Vector3f& cameraPosition)
    {
        auto translation = Matrix4f::translation(-cameraPosition);
        auto rotation = Matrix4f::rotationToAxes(Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, -1.0f), Vector3f(0.0f, 1.0f, 0.0f));
        return rotation * translation;
    }

    Matrix4f computeInverseView(const Vector3f& cameraPosition)
    {
        auto translation = Matrix4f::translation(cameraPosition);
        auto rotation = Matrix4f::rotationToAxes(Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, -1.0f), Vector3f(0.0f, 1.0f, 0.0f)).transposed();
        return translation * rotation;
    }

    Matrix4f computeProjection(float width, float height)
    {
        float halfWidth = width / 2.0f;
        float halfHeight = height / 2.0f;

        auto cabinet = Matrix4f::cabinetProjection(0.0f, 0.5f);

        auto ortho = Matrix4f::orthographicProjection(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            -1000.0f,
            1000.0f);

        return ortho * cabinet;
    }

    Matrix4f computeInverseProjection(float width, float height)
    {
        float halfWidth = width / 2.0f;
        float halfHeight = height / 2.0f;

        auto inverseCabinet = Matrix4f::cabinetProjection(0.0f, -0.5f);

        auto inverseOrtho = Matrix4f::inverseOrthographicProjection(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            -1000.0f,
            1000.0f);

        return inverseCabinet * inverseOrtho;
    }

    Matrix4f computeViewProjectionMatrix(const GameCameraState& cameraState, int screenWidth, int screenHeight)
    {
        auto view = computeView(cameraState.getRoundedPosition());
        auto projection = computeProjection(cameraState.scaleDimension(screenWidth), cameraState.scaleDimension(screenHeight));
        return projection * view;
    }

    Matrix4f computeInverseViewProjectionMatrix(const GameCameraState& cameraState, int screenWidth, int screenHeight)
    {
        auto inverseView = computeInverseView(cameraState.getRoundedPosition());
        auto inverseProjection = computeInverseProjection(cameraState.scaleDimension(screenWidth), cameraState.scaleDimension(screenHeight));
        return inverseView * inverseProjection;
    }

    const Rectangle2f GameScene::minimapViewport = Rectangle2f::fromTopLeft(0.0f, 0.0f, GuiSizeLeft, GuiSizeLeft);

    GameScene::GameScene(
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
        std::optional<std::ofstream>&& stateLogStream)
        : sceneContext(sceneContext),
          worldViewport(CroppedViewport(this->sceneContext.viewport, GuiSizeLeft, GuiSizeTop, GuiSizeRight, GuiSizeBottom)),
          playerCommandService(std::move(playerCommandService)),
          worldCameraState(cameraState),
          gameMediaDatabase(std::move(meshDatabase)),
          unitTextureAtlas(unitTextureAtlas),
          unitTeamTextureAtlases(std::move(unitTeamTextureAtlases)),
          unitPaletteIndexAtlas(unitPaletteIndexAtlas),
          unitTeamPaletteIndexAtlases(std::move(unitTeamPaletteIndexAtlases)),
          shadeTableTexture(shadeTableTexture),
          alphaTableTexture(alphaTableTexture),
          worldUiRenderService(this->sceneContext.graphics, this->sceneContext.shaders, &this->worldViewport),
          chromeUiRenderService(this->sceneContext.graphics, this->sceneContext.shaders, this->sceneContext.viewport),
          simulation(std::move(simulation)),
          terrainGraphics(std::move(terrainGraphics)),
          builderGuisDatabase(std::move(builderGuisDatabase)),
          gameNetworkService(std::move(gameNetworkService)),
          minimap(minimap),
          minimapDots(minimapDots),
          minimapDotHighlight(minimapDotHighlight),
          minimapRect(minimapViewport.scaleToFit(this->minimap->bounds)),
          minimapRectBase(minimapRect),
          sounds(std::move(sounds)),
          guiFont(guiFont),
          speechFont(speechFont),
          localPlayerId(localPlayerId),
          uiFactory(sceneContext.textureService, sceneContext.audioService, audioLookup, sceneContext.vfs, sceneContext.pathMapping, sceneContext.viewport->width(), sceneContext.viewport->height()),
          soundModeSetting(static_cast<SoundMode>(sceneContext.globalConfig->soundMode)),
          unitSpeechSetting(static_cast<UnitSpeechLevel>(sceneContext.globalConfig->unitSpeech)),
          musicTrackModeSetting(static_cast<MusicTrackMode>(sceneContext.globalConfig->musicTrackMode)),
          gammaSetting(sceneContext.globalConfig->gamma),
          shadingMode(static_cast<ShadingMode>(sceneContext.globalConfig->shadingMode)),
          antiAliasEnabled(sceneContext.globalConfig->antiAlias),
          buildingHaloEnabled(sceneContext.globalConfig->buildingHalo),
          antiAliasUnitsEnabled(sceneContext.globalConfig->antiAliasUnits),
          shadowsEnabled(sceneContext.globalConfig->shadows),
          vehicleShadowsEnabled(sceneContext.globalConfig->vehicleShadows),
          buildingHaloStrength(sceneContext.globalConfig->buildingHaloStrength),
          buildingHaloSaturation(sceneContext.globalConfig->buildingHaloSaturation),
          buildingHaloRedShift(sceneContext.globalConfig->buildingHaloRedShift),
          scrollSpeedSetting(sceneContext.globalConfig->scrollSpeed),
          cameraZoomSetting(sceneContext.globalConfig->cameraZoom),
          uiScaleSetting(sceneContext.globalConfig->uiScale),
          gameParameters(gameParameters),
          audioLookup(audioLookup),
          stateLogStream(std::move(stateLogStream))
    {
        worldCameraState.zoom = static_cast<float>(cameraZoomSetting) / 100.0f;

        if (this->gameParameters.aiArenaSeconds)
        {
            // One row every ten seconds of game time: the interesting thing
            // is the shape of the curve, and a row is cheap.
            const unsigned int sampleIntervalTicks = 10u * static_cast<unsigned int>(SimTicksPerSecond);
            arenaReport.emplace(sampleIntervalTicks);

            // The one thing that will write the event log, so the one thing
            // that turns it on.
            this->simulation.eventLog.setRecording(true);
            arenaEndTick = *this->gameParameters.aiArenaSeconds * static_cast<unsigned int>(SimTicksPerSecond);
            LOG_INFO << "AI arena: running for " << *this->gameParameters.aiArenaSeconds
                     << " seconds of game time (" << *arenaEndTick << " ticks)";
        }

        // A loaded mission has had its sounds already.
        missionCelebrationsHeard = missionCelebrations();

        if (this->gameParameters.scenarioName)
        {
            scenarioDriver = std::make_unique<ScenarioDriver>(*this, *this->gameParameters.scenarioName);
        }
    }

    GameScene::~GameScene()
    {
        // The audio service outlives us and holds a callback into this
        // object; without handing it back, the mixer reports a finished
        // channel into freed memory the moment the NEXT game renders its
        // first frame. Destroying the handle is not enough -- see
        // Subscription, whose destructor deliberately does nothing.
        audioSub->unsubscribe();
    }

    void GameScene::syncUiScale()
    {
        auto scale = effectiveUiScale();
        chromeUiRenderService.setUiScale(scale);
        uiFactory.setScreenSize(
            static_cast<int>(static_cast<float>(sceneContext.viewport->width()) / scale),
            static_cast<int>(static_cast<float>(sceneContext.viewport->height()) / scale));
    }

    void GameScene::reportUiScaleFit()
    {
        auto width = sceneContext.viewport->width();
        auto height = sceneContext.viewport->height();
        auto requested = requestedUiScale(uiScaleSetting, sceneContext.sceneManager->contentScale());
        auto fits = largestFittingUiScale(width, height);
        if (requested <= fits)
        {
            return;
        }

        printConsole(
            "UI " + std::to_string(requested) + "x needs " + std::to_string(640 * requested) + "x" + std::to_string(480 * requested)
                + " pixels; the window has " + std::to_string(width) + "x" + std::to_string(height)
                + ", so the UI stays at " + std::to_string(fits) + "x",
            Color(252, 252, 0));
    }

    void GameScene::init()
    {
        syncUiScale();
        reportUiScaleFit();
        setCrashScene("GameScene");
        setCrashMap(gameParameters.mapName.c_str());

        // A dialog raised over the battlefield that ships no art of its own --
        // YESORNO and EXITMENU are the two -- is filled with the in-game
        // patch rather than the front end's tile. See UiFactory::plateTileName.
        uiFactory.setPlateTileName("igpatch");

        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        currentPanel = uiFactory.panelFromGuiFile(sidePrefix + "MAIN2");
        panelBaseX = currentPanel->getX();

        sceneContext.audioService->reserveChannels(reservedChannelsCount);

        gameNetworkService->start();

        // A peer rejoining a game in progress listens from the start but takes
        // nothing until it has wound itself forward: its command buffers are
        // being filled from the recording, and a set arriving live would be
        // appended to those same buffers mid-wind. The service has to be
        // running all the same, because the scene asks it for the round trip
        // time every frame and waits for the answer.
        if (rejoiningAtTick)
        {
            gameNetworkService->setAcceptingCommands(false);
        }

        recreateWorldRenderTextures();
    }

    float computeSoundCeiling(int soundCount)
    {
        assert(soundCount > 0);

        // How loud the mix as a whole may get, counted in units of one sound
        // at the base gain. Per-sound gain is this over soundCount, so the
        // worst case -- every sound peaking in the same instant -- is the
        // ceiling times the base gain, and that is the number the mixer has
        // to fit.
        //
        // Four, against a base gain of a half, puts that worst case at 2.0.
        // The curve this replaces climbed to eight, which was the same 2.0
        // while the base gain was a quarter and multiplied in, and 8.0 once
        // the SDL3 migration dropped that multiplication -- which is the sum
        // the mixer was clamping when a loud battle crackled (issue #58).
        //
        // Doubling the base and halving the ceiling together leave 2/N for
        // any N at or above the cap, exactly what the old curve gave, so a
        // dense battle sounds as it did. What comes up is the sparse end.
        //
        // The graduated middle goes with it, which is less of a loss than it
        // looks: past a summed 1.0 the extra was clamped rather than heard,
        // so most of that climb was the mixer running out of room rather
        // than the battle getting bigger.
        //
        // Neither number is the original's. TA's own mixing of weapon and
        // explosion voices is not decoded -- only the eight-slot
        // notification queue at 0x47FAD0 is, and that is a different
        // mechanism for a different class of sound -- so this is RWE's own
        // balance, tuned by ear.
        return static_cast<float>(std::min(soundCount, 4));
    }

    int computeSoundVolume(int soundCount)
    {
        soundCount = std::max(soundCount, 1);
        auto headRoom = computeSoundCeiling(soundCount);
        return std::clamp(static_cast<int>(headRoom * 128) / soundCount, 1, 128);
    }

    bool GameScene::isCursorOverPanel()
    {
        // The panel's live bounds, not its resting ones: once it has started
        // moving the cursor is no longer on it, which is what keeps a held
        // Space from arguing with itself part way through the slide. The
        // panel is laid out in raw UI coordinates; the mouse arrives in frame
        // pixels.
        auto frame = getMousePosition();
        auto p = toUiCoordinates(frame.x, frame.y);
        auto x = currentPanel->getX();
        auto y = currentPanel->getY();
        return p.x >= x
            && p.x < x + static_cast<int>(currentPanel->getWidth())
            && p.y >= y
            && p.y < y + static_cast<int>(currentPanel->getHeight());
    }

    void GameScene::updateStatsBarSlide(int millisecondsElapsed)
    {
        // 0x4689c0. Each poll moves the strip a third of the way it has left
        // to go, never less than a pixel, so it leaves quickly and settles
        // gently. The original polls every fifteen of its timer units; the
        // unit is not decoded, and 10ms matches it in play better than 15ms.
        // The key is read live, not as a press.
        statsBarMillisecondsOwed += millisecondsElapsed;
        auto wanted = (spaceDown && !isGameMenuOpen()) ? StatsBarTravel : 0;
        while (statsBarMillisecondsOwed >= 10)
        {
            statsBarMillisecondsOwed -= 10;
            if (statsBarSlide == wanted)
            {
                continue;
            }
            auto remaining = std::abs(wanted - statsBarSlide);
            auto step = std::max(1, remaining / 3);
            statsBarSlide += wanted > statsBarSlide ? step : -step;
        }
    }

    void GameScene::updatePanelSlide(int millisecondsElapsed)
    {
        // TOTALA-EXE.md 76. The original keeps a display word bit (game+0x37f06
        // bit 7) that only F4 toggles -- it has no registry name, so it does not
        // outlive the session -- and a slide position at 0x51f2d8 animated
        // between 0 and 0x7d, its side panel's width. With the bit set the
        // position runs to 0x7d; with it clear it runs back to 0, except while
        // Space is held and the cursor is off the panel's own gadget. A second
        // updater at 0x4689c0 runs the same Space logic.
        //
        // Which endpoint is the hidden one is inference, not decoding: the
        // arithmetic and the two sounds are as the finding states, but nothing
        // in the trace says 0x7d means "out of the way". That Space is a
        // momentary peek is what makes this the sensible reading of it.
        if (!guiVisible)
        {
            // The debug panel's GUI checkbox owns the inset while the HUD is
            // off, and there is no panel on screen to slide anyway.
            return;
        }

        // Space brings the players' list out and leaves the side panel where
        // it is, as the original does in play; F4 moves them together.
        auto wantList = playerListWantsOut(
            panelHiddenLatch,
            spaceDown && !isGameMenuOpen(),
            isCursorOverPanel());
        auto listTarget = wantList ? static_cast<float>(PanelSlideTravel) : 0.0f;
        auto panelTarget = panelHiddenLatch ? static_cast<float>(PanelSlideTravel) : 0.0f;

        panelSlide = advancePanelSlide(panelSlide, panelTarget, PanelSlidePixelsPerSecond, millisecondsElapsed);

        auto previous = playerListSlide;
        playerListSlide = advancePanelSlide(playerListSlide, listTarget, PanelSlidePixelsPerSecond, millisecondsElapsed);

        if (playerListSlide != previous && playerListSlide == listTarget)
        {
            // Both endpoints play a UI sound. 76 names them and ALLSOUND.TDF
            // has both -- PANEL is servsml6, OPTIONS is butoptn -- but which
            // sound belongs to which end is not decoded, so this pairing is
            // RWE's: the servo as the panel leaves, the button as it returns.
            const auto& arrival = wantList ? sounds.panel : sounds.options;
            // On a free channel rather than the UI one, which drops a sound
            // while another plays: a quick tap of Space would lose the second.
            if (arrival)
            {
                sceneContext.audioService->playSound(*arrival);
            }
        }

        auto offset = static_cast<int>(std::lround(panelSlide));
        currentPanel->setX(panelBaseX - offset);
        minimapRect = Rectangle2f(
            Vector2f(minimapRectBase.position.x - static_cast<float>(offset), minimapRectBase.position.y),
            minimapRectBase.extents);

        // The inset is snapped, not animated. It sizes the world framebuffer
        // and two full screen textures, and anti-aliasing doubles all three,
        // so moving it every frame would reallocate them every frame. Opening
        // it the moment the slide leaves rest, and closing it only once the
        // panel is home again, costs two reallocations per round trip and
        // means the panel always slides across a world that is already drawn
        // underneath it.
        // The HUD is laid out in raw UI coordinates, which the chrome
        // projection scales up by uiScale; the world inset is in frame
        // pixels, so the GuiSize constants are scaled with it.
        auto scale = effectiveUiScale();
        auto desiredLeft = panelSlide > 0.0f ? 0 : static_cast<int>(std::lround(GuiSizeLeft * scale));
        if (desiredLeft != appliedLeftInset || scale != appliedUiScale)
        {
            // The wider viewport re-centres on the camera, which would drag
            // every world pixel sideways with it. Moving the camera by half
            // the change keeps the ground still and only adds the strip the
            // panel has left.
            worldCameraState.position.x -= panelSlideCameraShift(appliedLeftInset, desiredLeft, worldCameraState.zoom);
            appliedLeftInset = desiredLeft;
            appliedUiScale = scale;
            worldViewport.setInset(
                desiredLeft,
                static_cast<int>(std::lround(GuiSizeTop * scale)),
                static_cast<int>(std::lround(GuiSizeRight * scale)),
                static_cast<int>(std::lround(GuiSizeBottom * scale)));
            recreateWorldRenderTextures();
        }
    }

    void GameScene::update(int millisecondsElapsed)
    {
        // The scenario harness, if one is driving. Keyed on the tick about to
        // run and ahead of everything else, so input delivered at tick T is
        // in the command set flushed for T below. afterTick at the foot of
        // this function sees the tick and any panel rebuild it caused.
        const auto scenarioTick = sceneTime.value;
        if (scenarioDriver)
        {
            scenarioDriver->beforeTick(scenarioTick);
        }

        // The world inset follows in updatePanelSlide, once the panel's own
        // slide has been worked out.
        syncUiScale();

        // The battle harness, if one was asked for: keep both sides at
        // strength and send every replacement at the enemy. Gated on the
        // harness being enabled rather than on the count, since a count of
        // zero is the slider being dragged to the bottom and still has work
        // to do -- clearing the field.
        if (!battleTestPlayers.empty())
        {
            RWE_RENDERPROF("u.battletest");
            runBattleTest();
        }

        for (auto& action : std::exchange(pendingMenuActions, {}))
        {
            action();
        }

        // Once a frame, not once a tick: chat arrives on its own and is meant
        // to keep arriving while the simulation is stalled waiting for a peer.
        receiveChatMessages();
        updateChatTest();

        // Before the accumulator below, so the speed this frame runs at is
        // the one every peer's machine can sustain (#356).
        updateEffectiveSpeed();

        updateMusic();

        // Pause halts simulation tick dispatch by not advancing the
        // scaled-time accumulator. Speed scales the accumulator using
        // integer arithmetic to keep determinism friendly: at perMille
        // == 1000 we accumulate 1ms per real ms; at 100 we accumulate
        // 0.1ms per real ms; at 5000 we accumulate 5ms per real ms.
        // The sim tick threshold (SimMillisecondsPerTick) is unchanged.
        if (replaySeekTarget)
        {
            // Seeking: fill the accumulator past anything the cap will
            // dispatch, so the block runs at whatever rate the machine
            // manages rather than at the rate the clock ticks.
            millisecondsBuffer = static_cast<unsigned int>(SimMillisecondsPerTick) * 2001u;
        }
        else if (replayPlayback)
        {
            // Speed is a whole multiple of real time and multiplies the
            // elapsed time rather than the game speed, so that one second of
            // watching is one second of the recorded game at 1x whatever the
            // frame rate is doing.
            if (replayPlaying)
            {
                millisecondsBuffer += millisecondsElapsed * static_cast<unsigned int>(std::max(replaySpeed, 1));
            }
        }
        else if (!paused)
        {
            millisecondsBuffer += (millisecondsElapsed * effectiveSpeedPermille) / 1000;
        }

        // Ease the live zoom toward the setting rather than jumping, and do it
        // before the constraint so it is computed against the current extent.
        worldCameraState.zoom = advanceCameraZoom(worldCameraState.zoom, static_cast<float>(cameraZoomSetting) / 100.0f, millisecondsElapsed);

        // A monitor move can change the density mid-game, so re-read it each
        // frame and let the constraint below follow it live.
        worldCameraState.density = sceneContext.sceneManager->contentScale();

        auto cameraConstraint = computeCameraConstraint(simulation.terrain, worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()));

        // update camera position from keyboard arrows
        {
            int directionX = (right ? 1 : 0) - (left ? 1 : 0);
            int directionZ = (down ? 1 : 0) - (up ? 1 : 0);

            if (directionX || directionZ)
            {
                nudgeCamera(millisecondsElapsed, cameraConstraint, directionX, directionZ);
            }
        }

        // update camera position from edge scroll
        //
        // Only while this window is the one taking input. SDL keeps reporting
        // the last position it saw once the pointer has left, and that
        // position is usually against an edge -- so a game left running
        // behind another window scrolls itself into the corner of the map and
        // sits there. It is also what stopped the off-screen visual tests
        // working: every run photographed an unexplored corner.
        //
        // The gate is input focus rather than the pointer being inside the
        // window, which is what it used to be and was wrong in the ordinary
        // case: shoving the pointer at an edge pushes it clean out of a
        // windowed game, whereupon mouse focus goes and the scroll stops
        // exactly when it is wanted. A window nobody is working in has no
        // input focus either way, so the background case stays fixed.
        //
        // The position is clamped into the viewport for the same reason: once
        // the pointer is outside, its coordinates are past the edge rather
        // than on it, and the tests below want the edge itself.
        if (sceneContext.sdl->getKeyboardFocus() != nullptr)
        {
            auto mousePosition = getMousePosition();
            auto clampedX = std::clamp(mousePosition.x, sceneContext.viewport->left(), sceneContext.viewport->right() - 1);
            auto clampedY = std::clamp(mousePosition.y, sceneContext.viewport->top(), sceneContext.viewport->bottom() - 1);
            auto directionX = clampedX == sceneContext.viewport->left()
                ? -1
                : clampedX == sceneContext.viewport->right() - 1
                ? 1
                : 0;
            auto directionZ = clampedY == sceneContext.viewport->top()
                ? -1
                : clampedY == sceneContext.viewport->bottom() - 1
                ? 1
                : 0;

            if (directionX || directionZ)
            {
                nudgeCamera(millisecondsElapsed, cameraConstraint, directionX, directionZ);
            }
        }

        // handle minimap dragging
        if (auto cursor = std::get_if<NormalCursorMode>(&cursorMode.getValue()); cursor != nullptr)
        {
            if (std::holds_alternative<NormalCursorMode::DraggingMinimapState>(cursor->state))
            {
                auto minimapToWorld = minimapToWorldMatrix(simulation.terrain, minimapRect);
                auto frame = getMousePosition();
                auto mousePos = toUiCoordinates(frame.x, frame.y);
                auto worldPos = minimapToWorld * Vector3f(static_cast<float>(mousePos.x) + 0.5f, static_cast<float>(mousePos.y) + 0.5, 0.0f);

                relocateCamera(cameraConstraint, worldPos.x, worldPos.z);
            }
        }

        // handle tracking
        {
            // TODO (kwh) - tracking of projectiles not yet implemented. E.g. while tracking Bertha or Nuke Silo,
            // screen should follow a projectile until it hits, then return to the tracking group

            if (std::holds_alternative<CameraControlStateTrackingUnit>(cameraControlState) && trackedUnitId)
            {
                // get tracked unit position, or stop tracking if it's gone
                auto unit = tryGetUnit(*trackedUnitId);
                if (unit && !unit->get().isDead())
                {
                    // Move camera... OTA behavior:
                    // For each x and z component (not euclidean distance), halve the distance from camera to unit each frame,
                    //  but limit to a max of 320 pixels per frame, at 30fps for normal speed (Scroll speed followed game speed, eg +10 scrolls faster).
                    // Presumably 320 to make scrolling look continuous on the lowest res setting of 640x480

                    // We will use millisecondsElapsed to interpolate for smoother scrolling at high fps in rwe,
                    // while maintaining similar scroll speed on the map; Speed is linear and clamped at 320 pixels/(1/30)s = 9600 pix/s,
                    const float maxScroll = 9.6f * millisecondsElapsed;
                    // To interpolate halving distance every 1/30s, we'll use the definition of geometric progression: a_n = a*r^(n); where:
                    //  a_n = distance (pixels) from the unit we should be after n 1/30s frames, a = current distance from the unit
                    //  r = common ratio i.e. 1/2, the ratio the distance should decrease every 1/30 seconds
                    //  n = # of OTA frames = seconds elapsed / (1/30 s per OTA frame) = (time_ms / 1000) * 30 = 3 * time_ms / 100

                    const auto& cameraPos = worldCameraState.position;
                    const auto unitPos = simVectorToFloat(unit->get().position);
                    const auto cameraPosDelta = unitPos - cameraPos;

                    float decayFactor = 1.0f - std::pow(.5f, .03f * millisecondsElapsed);

                    float newDelta_x = cameraPosDelta.x * decayFactor;
                    if (std::abs(newDelta_x) > maxScroll)
                    {
                        newDelta_x = newDelta_x < 0 ? -maxScroll : maxScroll;
                    }

                    float newDelta_z = cameraPosDelta.z * decayFactor;
                    if (std::abs(newDelta_z) > maxScroll)
                    {
                        newDelta_z = newDelta_z < 0 ? -maxScroll : maxScroll;
                    }

                    auto newPos = cameraConstraint.clamp(Vector2f(newDelta_x + cameraPos.x, newDelta_z + cameraPos.z));
                    worldCameraState.position = Vector3f(newPos.x, worldCameraState.position.y, newPos.y);
                }
            }
        }

        // reset cursor mode if shift is released and at least 1 order was queued since shift was held
        if (commandWasQueued && !isShiftDown())
        {
            cursorMode.next(NormalCursorMode());
            commandWasQueued = false;
        }

        hoveredUnit = getUnitUnderCursor();
        hoveredFeature = getFeatureUnderCursor();

        if (auto buildCursor = std::get_if<BuildCursorMode>(&cursorMode.getValue()); buildCursor != nullptr && isCursorOverWorld())
        {
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            auto intersect = simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));

            if (intersect)
            {
                const auto& unitType = buildCursor->unitType;
                const auto& pos = *intersect;
                const auto& unitDefinition = simulation.unitDefinitions.at(unitType);
                auto mc = simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo);
                auto footprintRect = simulation.computeFootprintRegion(pos, unitDefinition.movementCollisionInfo);
                // Asked as the local player sees the world, not as the
                // simulation knows it: an enemy building nobody here has
                // discovered must not turn the box red, or the box has told
                // them it is there. The build fails on arrival instead, with
                // the message that failure now carries.
                auto isValid = simulation.canBeBuiltAtAsSeenBy(mc, unitDefinition.yardMap, unitDefinition.yardMapContainsGeo, footprintRect.x, footprintRect.y, localPlayerId);
                hoverBuildInfo = HoverBuildInfo{footprintRect, isValid};
            }
            else
            {
                hoverBuildInfo = std::nullopt;
            }
        }
        else
        {
            hoverBuildInfo = std::nullopt;
        }

        if (!isCursorOverMinimap() && !isCursorOverWorld())
        {
            // The cursor is outside the world, so over UI elements.
            sceneContext.cursor->useCursor(CursorType::Normal);
        }
        else
        {
            match(
                cursorMode.getValue(),
                [&](const AttackCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Attack);
                },
                [&](const DgunCursorMode&) {
                    // CURSORS.GAF has no D-gun cursor of its own.
                    sceneContext.cursor->useCursor(CursorType::Attack);
                },
                [&](const MoveCursorMode&) {
                    // The MOVE button is not a plain move. Command 2's cursor
                    // arm (0x43E8BB) is the whole context ladder, and it is
                    // the only armed command that can show a pickup without
                    // the LOAD button. See TOTALA-EXE.md S:103.
                    sceneContext.cursor->useCursor(selectionDefaultCursor(DefaultActionScheme::MoveButton));
                },
                [&](const GuardCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Guard);
                },
                [&](const ReclaimCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Reclaim);
                },
                [&](const RepairCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Repair);
                },
                [&](const PatrolCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Patrol);
                },
                [&](const CaptureCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Capture);
                },
                [&](const LoadCursorMode&) {
                    // Command 6's cursor arm, 0x43E7D3: the plain arrow
                    // unless the unit under the cursor could actually be
                    // taken aboard, and then the air/crane split -- an
                    // aircraft shows cursorpickup, a crane cursorload.
                    auto cursor = CursorType::Normal;
                    if (hoveredUnit && isFriendly(*hoveredUnit))
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (!simulation.canLoadUnitIntoTransport(selectedUnit, *hoveredUnit))
                            {
                                continue;
                            }
                            const auto& selected = getUnit(selectedUnit);
                            auto canFly = simulation.unitDefinitions.at(selected.unitType).canFly;
                            cursor = preferredCursor(cursor, canFly ? CursorType::Pickup : CursorType::Load);
                        }
                    }
                    sceneContext.cursor->useCursor(cursor);
                },
                [&](const UnloadCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Unload);
                },
                [&](const BuildCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Normal);
                },
                [&](const NormalCursorMode&) {
                    // One ladder, the same one the click handlers run, so the
                    // cursor cannot promise something the click will not do.
                    // The two schemes differ in kind as well as in arms: in
                    // "Left Click" the cursor *is* the decision (0x498F70
                    // dispatches on the displayed id), while in "Right Click"
                    // it is feedback only -- 0x43EB02 answers select, red or
                    // green, and the right button issues without consulting
                    // it at all. See TOTALA-EXE.md S:103.
                    auto scheme = leftClickMode()
                        ? DefaultActionScheme::LeftClickDefault
                        : DefaultActionScheme::RightClickDefault;
                    auto cursor = selectionDefaultCursor(scheme);

                    if (selectedUnits.empty() && hoveredUnit && unitIsSelectableBy(simulation, *hoveredUnit, localPlayerId))
                    {
                        // An empty selection contributes no cursor at all --
                        // the original's chooser loops over selected units
                        // and returns its sentinel -- but hovering one of
                        // your own units still has to say it can be picked
                        // up, so RWE keeps the select cursor here.
                        cursor = CursorType::Select;
                    }

                    sceneContext.cursor->useCursor(cursor);
                });
        }

        // Before anything is queued, so that a drop decided this frame goes
        // out this frame: the local buffer is at its limit while the game is
        // stalled, and the push below would otherwise hold it back until a
        // tick that cannot run without it.
        updatePeerLiveness();
        updateControlRequests();
        updateRejoinRequest();
        writeRejoinBundleIfDue();

        // Here as well as at the top of tryTickGame, because a peer winding
        // itself forward arrives at the rejoin tick on the same frame the
        // recording runs out -- and a recording that has run out dispatches no
        // more ticks, so the check that lives in the tick would never be
        // reached. Whichever of the two fires first, it fires on the frame the
        // last wound tick was run and the block below then fills the command
        // buffer the same frame.
        finishRejoinIfCaughtUp();

        auto targetCommandBufferSize = commandBufferTargetForRttMillis(gameNetworkService->getMaxAverageRttMillis());

        auto bufferedCommandCount = playerCommandService->bufferedCommandCount(localPlayerId);

        LOG_DEBUG << "Buffer levels (real/target) " << bufferedCommandCount << "/" << targetCommandBufferSize;

        // Watching a recording: every command for every player comes out of
        // the file, one set per player per tick, pushed in tryTickGame where
        // the tick number is known. Nothing below here may push as well.
        if (!replayPlayback)
        {

        // In an AI arena there is no human at all and the local player is
        // itself a computer, so its commands arrive through the drain below
        // like everybody else's. Pushing here as well would put two entries a
        // tick into one player's queue and take it out of step with the rest.
        //
        // And in a game with no peers at all the human is fed a tick at a time
        // in tryTickGame instead -- see localHumanCommandsAreFedPerTick -- so
        // pushing here as well would put two entries a tick into their queue.
        if (simulation.getPlayer(localPlayerId).type == GamePlayerType::Human && !localHumanCommandsAreFedPerTick())
        {
            // If we have too many commands buffered,
            // defer submitting commands this frame
            // so that we drop back down to the threshold.
            // The second arm is for a stalled game. The buffer is not being
            // drained, so it sits at its limit and the first arm stops
            // firing -- which would leave a drop command waiting for a tick
            // that is waiting for the drop. Queueing an extra set is safe:
            // every peer sees this peer's stream exactly as it is sent, and
            // the depth only decides how long an order waits.
            auto setsToPush = planCommandSets(
                bufferedCommandCount,
                targetCommandBufferSize,
                !waitingForPlayers.empty(),
                !localPlayerCommandBuffer.empty());

            if (setsToPush.pushLocalSet)
            {
                // Queue up commands collected from the local player, as many
                // as make one set a packet can carry. An order to a large
                // selection is more than that -- a hundred units is three
                // kilobytes against a 1500-byte datagram -- and sending it as
                // one set used to stop the game. The rest waits for the next
                // tick, still in order, which puts a tick between the first
                // thirty or so units moving and the next. Issue #75.
                auto count = GameNetworkService::commandsFittingOneSet(localPlayerCommandBuffer);
                std::vector<PlayerCommand> set(localPlayerCommandBuffer.begin(), localPlayerCommandBuffer.begin() + static_cast<std::ptrdiff_t>(count));
                playerCommandService->pushCommands(localPlayerId, set);
                gameNetworkService->submitCommands(sceneTime, set);
                ++localSetsSubmitted;
                localPlayerCommandBuffer.erase(localPlayerCommandBuffer.begin(), localPlayerCommandBuffer.begin() + static_cast<std::ptrdiff_t>(count));
            }

            // fill up to the required threshold
            for (unsigned int i = 0; i < setsToPush.emptySetsToPush; ++i)
            {
                playerCommandService->pushCommands(localPlayerId, std::vector<PlayerCommand>());
                gameNetworkService->submitCommands(sceneTime, std::vector<PlayerCommand>());
                ++localSetsSubmitted;
            }
        }

        // The computer players' commands are NOT queued here. They are taken
        // in tryTickGame, a tick at a time: the buffer is drained a set per
        // player per tick, so feeding it per frame made the delay on an AI
        // order a function of how many ticks the last frame dispatched, which
        // is not the same number on two peers of a network game. See
        // feedAiCommands.
        }

        // If we are waiting to swap in a new unit GUI panel, do that now
        if (nextPanel)
        {
            currentPanel = std::move(*nextPanel);
            nextPanel = std::nullopt;
            attachOrdersMenuEventHandlers();
            // The incoming panel carries its own x from its gui file, and the
            // slide is taken off that -- so it has to be read before the slide
            // is applied, or a panel swapped in mid-slide would have the
            // offset subtracted from an already offset position.
            panelBaseX = currentPanel->getX();
        }

        // Straight after the swap, so a panel that arrived this frame is put
        // in the right place before anything draws it.
        updatePanelSlide(millisecondsElapsed);
        updateStatsBarSlide(millisecondsElapsed);

        // The drift gate below asks the network thread what time everyone
        // else is at, and skips ticks to stay level with them. There is
        // nobody else in a replay, and letting it skip would end the playback
        // at a different game time than the recording did.
        auto averageSceneTime = replayPlayback ? sceneTime : gameNetworkService->estimateAvergeSceneTime(sceneTime);

        // Cap the number of sim ticks we dispatch per frame to prevent
        // a runaway "spiral of death" if frame times spike at high speeds.
        //
        // Watching a recording raises it instead of raising the game speed.
        // Speed scales the accumulator, and whatever the cap then refuses to
        // dispatch is thrown away -- so a fast-forward driven that way
        // silently drops ticks and finishes the replay early, which is the
        // one thing a replay must not do. Seeking runs flat out in blocks,
        // large enough to cross ten minutes in a couple of seconds and small
        // enough that the window still answers between them.
        const int maxTicksPerFrame = replaySeekTarget
            ? 2000
            : (replayPlayback ? 10 * std::max(replaySpeed, 1) : 10);
        FrameScheduler scheduler(static_cast<unsigned int>(millisecondsBuffer), averageSceneTime, sceneTime, maxTicksPerFrame);
        // Fast playback is bounded by the clock as well as by the count. At
        // 64x a frame asks for thirty-odd ticks, and if those take longer
        // than the frame the next one asks for more, and the one after for
        // more again, until the viewer is drawing a frame every two seconds.
        // Past the budget the rest of the backlog is dropped: the replay
        // plays as fast as the machine can run it and the window stays live.
        const auto frameTickingStarted = std::chrono::steady_clock::now();
        const bool clockBounded = replayPlayback && !replaySeekTarget;
        while (scheduler.hasWork())
        {
            if (clockBounded && scheduler.ticksThisFrame() > 0
                && std::chrono::steady_clock::now() - frameTickingStarted > std::chrono::milliseconds(40))
            {
                scheduler.discardBuffer();
                break;
            }

            scheduler.next();
            auto tickStarted = std::chrono::steady_clock::now();
            tryTickGame();

            if (!lastTickAttemptBlocked)
            {
                tickCostThisFrameMillis += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStarted).count();
                ++ticksTimedThisFrame;
            }
        }
        auto frameOutcome = scheduler.finish();
        millisecondsBuffer = static_cast<int>(frameOutcome.millisecondsLeft);
        ticksLostToCap += frameOutcome.ticksLostToCap;

        // What this machine can sustain, for the peers that will cap the game
        // speed to it and for its own governor next frame. The moving average
        // keeps one busy frame from being read as a slow machine.
        if (ticksTimedThisFrame > 0)
        {
            auto tickCostSample = static_cast<float>(tickCostThisFrameMillis / static_cast<double>(ticksTimedThisFrame));
            averageTickCostMillis = ema(tickCostSample, averageTickCostMillis, 0.1f);
        }
        ownSustainableSpeedPermille = estimateSustainableSpeedPermille(
            static_cast<unsigned int>(gameSpeed.perMille()),
            frameOutcome.ticksDispatched,
            frameOutcome.ticksLostToCap,
            averageTickCostMillis);
        tickCostThisFrameMillis = 0.0;
        ticksTimedThisFrame = 0;

        // Once a frame, so a peer's projection of this one knows the speed it
        // is advancing at, whether it has stopped, and what its machine can
        // sustain. A replay has no peers.
        if (!replayPlayback)
        {
            gameNetworkService->submitRunState(
                effectiveSpeedPermille,
                paused,
                lastTickAttemptBlocked,
                ownSustainableSpeedPermille);
        }

        if (replaySeekTarget && sceneTime.value >= *replaySeekTarget)
        {
            LOG_INFO << "Replay: seek reached tick " << sceneTime.value;
            replaySeekTarget.reset();
            millisecondsBuffer = 0;
        }

        // The recording has run out. Pause rather than carry on ticking a
        // game with no more commands coming, which looks like the viewer has
        // frozen when in fact it has finished.
        //
        // Not for a peer winding itself forward: its recording running out is
        // the moment it goes live, not the end of anything, and a bundle is
        // cut to end exactly there. Pausing would be a game that never starts.
        if (replayPlayback && !replaySeekTarget && !replayReachedEnd && !rejoiningAtTick
            && sceneTime.value >= replayPlayback->lastTick)
        {
            replayReachedEnd = true;
            replayPlaying = false;
            millisecondsBuffer = 0;
            LOG_INFO << "Replay: reached the end at tick " << sceneTime.value;
        }

        // A launcher's magazine fills without anybody ordering anything, so its
        // readout cannot be refreshed off a command the way a build queue's is.
        refreshStockpileGuiTotal();

        // Nor can a factory's queue, once it starts working through it: the
        // count drops when a unit begins, and no command passes through the
        // UI to hang a refresh on. The original does not try -- 0x4199B0
        // rebuilds the caption of every gadget on the page on every refresh,
        // counting the outstanding orders on demand through 0x439D80 -- so
        // this does the same.
        refreshBuildGuiTotals();

        renderReplayWindow();
        renderDebugWindow();
        recordNetworkHistory();
        renderNetworkOverlay();

        if (scenarioDriver)
        {
            scenarioDriver->afterTick(scenarioTick);
        }
    }

    void GameScene::setCameraPosition(const Vector3f& newPosition)
    {
        auto cameraConstraint = computeCameraConstraint(simulation.terrain, worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()));
        auto constrainedPosition = cameraConstraint.clamp(Vector2f(newPosition.x, newPosition.z));
        worldCameraState.position = Vector3f(constrainedPosition.x, newPosition.y, constrainedPosition.y);
    }

    const MapTerrain& GameScene::getTerrain() const
    {
        return simulation.terrain;
    }

    GameTime
    GameScene::getGameTime() const
    {
        return simulation.gameTime;
    }

    GameTime GameScene::renderTime() const
    {
        return renderTimeFor(simulation.gameTime);
    }

    Matrix4f GameScene::worldToMinimapMatrix(const MapTerrain& terrain, const Rectangle2f& minimapRect)
    {
        auto view = Matrix4f::rotationToAxes(
            Vector3f(1.0f, 0.0f, 0.0f),
            Vector3f(0.0f, 0.0f, 1.0f),
            Vector3f(0.0f, -1.0f, 0.0f));
        auto cabinet = Matrix4f::cabinetProjection(0.0f, 0.5f);
        auto orthographic = Matrix4f::orthographicProjection(
            simScalarToFloat(terrain.leftInWorldUnits()),
            simScalarToFloat(terrain.rightCutoffInWorldUnits()),
            simScalarToFloat(terrain.bottomCutoffInWorldUnits()),
            simScalarToFloat(terrain.topInWorldUnits()),
            -1000.0f,
            1000.0f);
        auto worldProjection = orthographic * cabinet;
        auto minimapInverseProjection = Matrix4f::inverseOrthographicProjection(
            minimapRect.left(),
            minimapRect.right(),
            minimapRect.bottom(),
            minimapRect.top(),
            -1.0f,
            1.0f);
        return minimapInverseProjection * worldProjection * view;
    }

    Matrix4f GameScene::minimapToWorldMatrix(const MapTerrain& terrain, const Rectangle2f& minimapRect)
    {
        auto view = Matrix4f::rotationToAxes(
            Vector3f(1.0f, 0.0f, 0.0f),
            Vector3f(0.0f, 0.0f, 1.0f),
            Vector3f(0.0f, -1.0f, 0.0f));
        auto inverseView = view.transposed();
        auto inverseCabinet = Matrix4f::cabinetProjection(0.0f, -0.5f);
        auto inverseOrthographic = Matrix4f::inverseOrthographicProjection(
            simScalarToFloat(terrain.leftInWorldUnits()),
            simScalarToFloat(terrain.rightCutoffInWorldUnits()),
            simScalarToFloat(terrain.bottomCutoffInWorldUnits()),
            simScalarToFloat(terrain.topInWorldUnits()),
            -1000.0f,
            1000.0f);
        auto worldInverseProjection = inverseCabinet * inverseOrthographic;
        auto minimapProjection = Matrix4f::orthographicProjection(
            minimapRect.left(),
            minimapRect.right(),
            minimapRect.bottom(),
            minimapRect.top(),
            -1.0f,
            1.0f);
        return inverseView * worldInverseProjection * minimapProjection;
    }

}
