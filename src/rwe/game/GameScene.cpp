#include "GameScene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/io/gui/gui.h>
#include <rwe/game/save_util.h>
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
          gameParameters(gameParameters),
          audioLookup(audioLookup),
          stateLogStream(std::move(stateLogStream))
    {
        if (this->gameParameters.aiArenaSeconds)
        {
            // One row every ten seconds of game time: the interesting thing
            // is the shape of the curve, and a row is cheap.
            const unsigned int sampleIntervalTicks = 10u * static_cast<unsigned int>(SimTicksPerSecond);
            arenaReport.emplace(sampleIntervalTicks);
            arenaEndTick = *this->gameParameters.aiArenaSeconds * static_cast<unsigned int>(SimTicksPerSecond);
            LOG_INFO << "AI arena: running for " << *this->gameParameters.aiArenaSeconds
                     << " seconds of game time (" << *arenaEndTick << " ticks)";
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

    void GameScene::init()
    {
        setCrashScene("GameScene");
        setCrashMap(gameParameters.mapName.c_str());

        // A dialog raised over the battlefield that ships no art of its own --
        // YESORNO and EXITMENU are the two -- is filled with the in-game
        // patch rather than the front end's tile. See UiFactory::plateTileName.
        uiFactory.setPlateTileName("igpatch");

        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        currentPanel = uiFactory.panelFromGuiFile(sidePrefix + "MAIN2");

        sceneContext.audioService->reserveChannels(reservedChannelsCount);
        gameNetworkService->start();

        recreateWorldRenderTextures();
    }

    float computeSoundCeiling(int soundCount)
    {
        assert(soundCount > 0);
        if (soundCount <= 4)
        {
            return soundCount;
        }

        if (soundCount <= 8)
        {
            return 4 + ((soundCount - 4) * 0.5f);
        }

        if (soundCount <= 16)
        {
            return (6 + ((soundCount - 8) * 0.25f));
        }

        return 8;
    }

    int computeSoundVolume(int soundCount)
    {
        soundCount = std::max(soundCount, 1);
        auto headRoom = computeSoundCeiling(soundCount);
        return std::clamp(static_cast<int>(headRoom * 128) / soundCount, 1, 128);
    }

    void GameScene::update(int millisecondsElapsed)
    {
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
            millisecondsBuffer += (millisecondsElapsed * gameSpeed.perMille()) / 1000;
        }

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
        {
            auto mousePosition = getMousePosition();
            auto directionX = mousePosition.x == sceneContext.viewport->left()
                ? -1
                : mousePosition.x == sceneContext.viewport->right() - 1
                ? 1
                : 0;
            auto directionZ = mousePosition.y == sceneContext.viewport->top()
                ? -1
                : mousePosition.y == sceneContext.viewport->bottom() - 1
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
                // ok, the cursor is dragging the minimap.
                // work out where the cursor is on the minimap,
                // convert that to the world, then set the camera's position to there
                // (clamped to map bounds)

                auto minimapToWorld = minimapToWorldMatrix(simulation.terrain, minimapRect);
                auto mousePos = getMousePosition();
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
                auto isValid = simulation.canBeBuiltAt(mc, unitDefinition.yardMap, unitDefinition.yardMapContainsGeo, footprintRect.x, footprintRect.y);
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

        auto maxRtt = std::clamp(gameNetworkService->getMaxAverageRttMillis(), 16.0f, 2000.0f);
        auto highCommandLatencyMillis = maxRtt + (maxRtt / 4.0f) + 200.0f;
        auto commandLatencyFrames = static_cast<unsigned int>(highCommandLatencyMillis / 16.0f) + 1;
        auto targetCommandBufferSize = commandLatencyFrames;

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
        if (simulation.getPlayer(localPlayerId).type == GamePlayerType::Human)
        {
            // If we have too many commands buffered,
            // defer submitting commands this frame
            // so that we drop back down to the threshold.
            if (bufferedCommandCount <= targetCommandBufferSize)
            {
                // Queue up commands collected from the local player
                playerCommandService->pushCommands(localPlayerId, localPlayerCommandBuffer);
                gameNetworkService->submitCommands(sceneTime, localPlayerCommandBuffer);
                localPlayerCommandBuffer.clear();
                ++bufferedCommandCount;
            }

            // fill up to the required threshold
            for (; bufferedCommandCount < targetCommandBufferSize; ++bufferedCommandCount)
            {
                playerCommandService->pushCommands(localPlayerId, std::vector<PlayerCommand>());
                gameNetworkService->submitCommands(sceneTime, std::vector<PlayerCommand>());
            }
        }

        // Queue up commands from the computer players. The AI runs inside
        // the simulation (one tick ahead of this drain) and writes its
        // PlayerCommands into `simulation.aiPendingCommands`. We pull them
        // here and push them through the same PlayerCommandService channel
        // human input uses, so MP/replay/desync detection treats AI
        // identically to a remote human.
        //
        // The AI buffer is kept topped up to the same threshold as the
        // local human buffer. A single frame may dispatch several sim
        // ticks (catch-up after a slow frame, or any game speed above 1x),
        // and each tick pops one entry from every player's buffer. If the
        // AI only had one entry queued, the second tick in a frame would
        // find its buffer empty and be skipped ("Blocked waiting for
        // player commands").
        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            PlayerId id(i);
            const auto& player = simulation.players[i];
            if (player.type != GamePlayerType::Computer)
            {
                continue;
            }

            auto aiBufferedCount = playerCommandService->bufferedCommandCount(id);
            if (aiBufferedCount <= targetCommandBufferSize)
            {
                auto aiCommands = simulation.takeAiCommandsForPlayer(id);
                playerCommandService->pushCommands(id, aiCommands);
                ++aiBufferedCount;
            }

            for (; aiBufferedCount < targetCommandBufferSize; ++aiBufferedCount)
            {
                playerCommandService->pushCommands(id, std::vector<PlayerCommand>());
            }
        }
        }

        // If we are waiting to swap in a new unit GUI panel, do that now
        if (nextPanel)
        {
            currentPanel = std::move(*nextPanel);
            nextPanel = std::nullopt;
            attachOrdersMenuEventHandlers();
        }

        // The drift gate below asks the network thread what time everyone
        // else is at, and skips ticks to stay level with them. There is
        // nobody else in a replay, and letting it skip would end the playback
        // at a different game time than the recording did.
        auto averageSceneTime = replayPlayback ? sceneTime : gameNetworkService->estimateAvergeSceneTime(sceneTime);

        // allow skipping sim frames every so often to get back down to average.
        // We tolerate X frames of drift in either direction to cope with noisiness in the estimation.
        const SceneTime frameTolerance(3);
        const SceneTime frameCheckInterval(5);
        auto highSceneTime = averageSceneTime + frameTolerance;
        auto lowSceneTime = averageSceneTime <= frameTolerance ? SceneTime{0} : averageSceneTime - frameTolerance;
        // Cap the number of sim ticks we dispatch per frame to prevent
        // a runaway "spiral of death" if frame times spike at high speeds.
        //
        // Watching a recording raises it instead of raising the game speed.
        // Speed scales the accumulator, and whatever the cap then refuses to
        // dispatch is thrown away below -- so a fast-forward driven that way
        // silently drops ticks and finishes the replay early, which is the
        // one thing a replay must not do. Seeking runs flat out in blocks,
        // large enough to cross ten minutes in a couple of seconds and small
        // enough that the window still answers between them.
        const int maxTicksPerFrame = replaySeekTarget
            ? 2000
            : (replayPlayback ? 10 * std::max(replaySpeed, 1) : 10);
        int ticksThisFrame = 0;
        for (; millisecondsBuffer >= SimMillisecondsPerTick && ticksThisFrame < maxTicksPerFrame; millisecondsBuffer -= SimMillisecondsPerTick)
        {
            if (sceneTime % frameCheckInterval != SceneTime(0) || sceneTime <= highSceneTime)
            {
                tryTickGame();
                ++ticksThisFrame;

                // simulate an extra frame to catch up every so often
                if (sceneTime % frameCheckInterval == SceneTime(0) && sceneTime < lowSceneTime && ticksThisFrame < maxTicksPerFrame)
                {
                    tryTickGame();
                    ++ticksThisFrame;
                }
            }
        }
        // If we hit the cap, drain the buffer so we don't carry over
        // unbounded backlog into the next frame.
        if (ticksThisFrame >= maxTicksPerFrame)
        {
            millisecondsBuffer = 0;
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
        if (replayPlayback && !replaySeekTarget && !replayReachedEnd
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
