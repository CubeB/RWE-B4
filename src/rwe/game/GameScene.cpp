#include "GameScene.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
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
#include <rwe/resource_io.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/ui/UiStagedButton.h>
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

    bool featureCanBeReclaimed(const GameSimulation& sim, FeatureId featureId)
    {
        const auto& featureState = sim.getFeature(featureId);
        const auto& def = sim.getFeatureDefinition(featureState.featureName);
        return def.reclaimable;
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

    bool unitCanAttack(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.canAttack;
    }

    bool unitCanMove(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.canMove;
    }

    bool unitCanGuard(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.canGuard;
    }

    bool unitIsBuilder(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.builder;
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

    bool unitIsBeingBuilt(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unit.isBeingBuilt(unitDefinition);
    }

    bool unitIsDamaged(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unit.isAlive() && !unit.isBeingBuilt(unitDefinition) && unit.hitPoints < unitDefinition.maxHitPoints;
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
          shadowsEnabled(sceneContext.globalConfig->shadows),
          scrollSpeedSetting(sceneContext.globalConfig->scrollSpeed),
          gameParameters(gameParameters),
          localPlayerId(localPlayerId),
          uiFactory(sceneContext.textureService, sceneContext.audioService, audioLookup, sceneContext.vfs, sceneContext.pathMapping, sceneContext.viewport->width(), sceneContext.viewport->height()),
          audioLookup(audioLookup),
          stateLogStream(std::move(stateLogStream))
    {
    }

    void GameScene::init()
    {
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

    void GameScene::render()
    {
        if (guiVisible)
        {
            renderUi();
        }

        sceneContext.graphics->enableDepthBuffer();

        renderWorld();
        sceneContext.graphics->disableDepthBuffer();

        if (guiVisible)
        {
            renderOverlay();
        }

        // oh yeah also regulate sound.
        // Never call into the mixer while holding playingUnitChannelsLock:
        // the mixer's audio thread holds its track lock while it reports a
        // finished track, and that report wants our lock in turn. Finished
        // tracks are therefore announced here, on this thread, and the
        // volume pass works from a copy of the set.
        sceneContext.audioService->dispatchFinishedChannels();
        std::vector<int> channels;
        int volume;
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            channels.assign(playingUnitChannels.begin(), playingUnitChannels.end());
            volume = computeSoundVolume(playingUnitChannels.size());
        }
        for (auto channel : channels)
        {
            sceneContext.audioService->setVolume(channel, volume);
        }
    }

    void GameScene::renderUi()
    {
        renderMinimap();

        const auto& localSideData = sceneContext.sideData->at(getPlayer(localPlayerId).side);

        // render top bar
        const auto& intGafName = localSideData.intGaf;
        auto topPanelBackground = sceneContext.textureService->tryGetGafEntry("anims/" + intGafName + ".GAF", "PANELTOP");
        auto bottomPanelBackground = sceneContext.textureService->tryGetGafEntry("anims/" + intGafName + ".GAF", "PANELBOT");
        float topXBuffer = GuiSizeLeft;
        if (topPanelBackground)
        {
            const auto& sprite = *(*topPanelBackground)->sprites.at(0);
            chromeUiRenderService.drawSpriteAbs(topXBuffer, 0, sprite);
            topXBuffer += sprite.bounds.width();
        }
        if (bottomPanelBackground)
        {
            while (topXBuffer < sceneContext.viewport->width())
            {
                const auto& sprite = *(*bottomPanelBackground)->sprites.at(0);
                chromeUiRenderService.drawSpriteAbs(topXBuffer, 0.0f, sprite);
                topXBuffer += sprite.bounds.width();
            }
        }

        auto logos = sceneContext.textureService->tryGetGafEntry("textures/LOGOS.GAF", "32xlogos");
        if (logos)
        {
            auto playerColorIndex = getPlayer(localPlayerId).color;
            const auto& rect = localSideData.logo.toDiscreteRect();
            chromeUiRenderService.drawSpriteAbs(rect.x, rect.y, rect.width, rect.height, *(*logos)->sprites.at(playerColorIndex.value));
        }

        // A stalled resource flashes its bar red, as in TA.
        const bool stallFlashOn = ((sceneContext.timeService->getTicks() / 250) % 2) == 0;
        const Color stallColor(255, 40, 40);

        // draw energy bar
        {
            const auto& rect = localSideData.energyBar.toDiscreteRect();
            const auto& localPlayer = getPlayer(localPlayerId);
            auto rectWidth = localPlayer.maxEnergy == Energy(0) ? 0 : (rect.width * std::max(Energy(0), localPlayer.energy).value) / localPlayer.maxEnergy.value;
            const auto& colorIndex = localSideData.energyColor;
            const auto& color = sceneContext.palette->at(colorIndex);
            if (localPlayer.energyStalled && stallFlashOn)
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rect.width, rect.height, stallColor);
            }
            else
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rectWidth, rect.height, color);
            }
        }
        {
            const auto& rect = localSideData.energy0;
            chromeUiRenderService.drawText(rect.x1, rect.y1, formatResource(Energy(0)), *guiFont);
        }
        {
            const auto& rect = localSideData.energyMax;
            auto text = formatResource(getPlayer(localPlayerId).maxEnergy);
            chromeUiRenderService.drawTextAlignRight(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.energyNum;
            auto text = formatResource(std::max(Energy(0), getPlayer(localPlayerId).energy));
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.energyProduced;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousEnergyProductionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(83, 223, 79));
        }
        {
            const auto& rect = localSideData.energyConsumed;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousDesiredEnergyConsumptionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(255, 71, 0));
        }

        // draw metal bar
        {
            const auto& rect = localSideData.metalBar.toDiscreteRect();
            const auto& localPlayer = getPlayer(localPlayerId);
            auto rectWidth = localPlayer.maxMetal == Metal(0) ? 0 : (rect.width * std::max(Metal(0), localPlayer.metal).value) / localPlayer.maxMetal.value;
            const auto& colorIndex = localSideData.metalColor;
            const auto& color = sceneContext.palette->at(colorIndex);
            if (localPlayer.metalStalled && stallFlashOn)
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rect.width, rect.height, stallColor);
            }
            else
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rectWidth, rect.height, color);
            }
        }
        {
            const auto& rect = localSideData.metal0;
            chromeUiRenderService.drawText(rect.x1, rect.y1, "0", *guiFont);
        }
        {
            const auto& rect = localSideData.metalMax;
            auto text = formatResource(getPlayer(localPlayerId).maxMetal);
            chromeUiRenderService.drawTextAlignRight(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.metalNum;
            auto text = formatResource(std::max(Metal(0), getPlayer(localPlayerId).metal));
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.metalProduced;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousMetalProductionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(83, 223, 79));
        }
        {
            const auto& rect = localSideData.metalConsumed;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousDesiredMetalConsumptionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(255, 71, 0));
        }

        renderHelpOverlay();

        renderGameOverOverlay();

        // render bottom bar
        float bottomXBuffer = GuiSizeLeft;
        if (bottomPanelBackground)
        {
            while (bottomXBuffer < sceneContext.viewport->width())
            {
                const auto& sprite = *(*bottomPanelBackground)->sprites.at(0);
                chromeUiRenderService.drawSpriteAbs(bottomXBuffer, worldViewport.bottom(), sprite);
                bottomXBuffer += sprite.bounds.width();
            }
        }

        auto extraBottom = sceneContext.viewport->height() - 480;
        if (hoveredUnit)
        {
            const auto& unit = getUnit(*hoveredUnit);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (logos)
            {
                const auto& rect = localSideData.logo2.toDiscreteRect();
                const auto& color = *(*logos)->sprites.at(getPlayer(unit.owner).color.value);
                chromeUiRenderService.drawSpriteAbs(rect.x, extraBottom + rect.y, rect.width, rect.height, color);
            }

            {
                const auto& rect = localSideData.unitName;
                const auto& playerName = getPlayer(unit.owner).name;
                const auto& text = unitDefinition.showPlayerName && playerName ? *playerName : unitDefinition.unitName;
                chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, text, *guiFont);
            }

            if (unit.isOwnedBy(localPlayerId) || !unitDefinition.hideDamage)
            {
                const auto& rect = localSideData.damageBar.toDiscreteRect();
                chromeUiRenderService.drawHealthBar2(rect.x, extraBottom + rect.y, rect.width, rect.height, static_cast<float>(unit.hitPoints) / static_cast<float>(unitDefinition.maxHitPoints));
            }

            if (unit.isOwnedBy(localPlayerId))
            {
                {
                    const auto& rect = localSideData.unitMetalMake;
                    auto text = "+" + formatResourceDelta(unit.getMetalMake());
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(83, 223, 79));
                }
                {
                    const auto& rect = localSideData.unitMetalUse;
                    auto text = "-" + formatResourceDelta(unit.getMetalUse());
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(255, 71, 0));
                }
                {
                    const auto& rect = localSideData.unitEnergyMake;
                    auto text = "+" + formatResourceDelta(unit.getEnergyMake());
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(83, 223, 79));
                }
                {
                    const auto& rect = localSideData.unitEnergyUse;
                    auto text = "-" + formatResourceDelta(unit.getEnergyUse());
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(255, 71, 0));
                }
                {
                    const auto& rect = localSideData.missionText;
                    auto text = "Standby";
                    chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, text, *guiFont);
                }

                // A launcher says how full it is and how far through the next
                // round it has got, in the footer's spare name-and-bar slot.
                // This is an addition, not a restoration: the original shows
                // the count only as the caption on the MAKENUKE button, and
                // shows the missile under construction nowhere at all -- no
                // bar, no percentage, no format string in the binary. The
                // UNITNAME2/DAMAGEBAR2 rectangles are SIDEDATA.TDF's own, and
                // the bar is drawn exactly as the damage bar beside it so the
                // footer keeps one visual language.
                if (auto stockpileWeapon = simulation.tryGetStockpileWeapon(*hoveredUnit); stockpileWeapon)
                {
                    const auto& weapon = stockpileWeapon->get();
                    const auto& weaponDefinition = simulation.weaponDefinitions.at(weapon.weaponType);

                    {
                        const auto& rect = localSideData.unitName2;
                        auto text = stockpileButtonLabel(weapon.stockedRounds, weapon.queuedRounds);
                        if (text.empty())
                        {
                            text = "0";
                        }
                        chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, "Stockpile " + text, *guiFont);
                    }

                    // Progress is the ticks already paid for over the whole
                    // build, which is the weapon's own reloadtime -- 180
                    // seconds, 5400 ticks, for a nuclear missile. An idle
                    // silo with nothing on order shows an empty bar rather
                    // than a full red one.
                    auto totalTicks = std::max(1, static_cast<int>(deltaSecondsToTicks(weaponDefinition.reloadTime).value));
                    auto fraction = std::clamp(static_cast<float>(weapon.stockpileProgress) / static_cast<float>(totalTicks), 0.0f, 1.0f);

                    const auto& bar = localSideData.damageBar2.toDiscreteRect();
                    if (weapon.queuedRounds > 0 || weapon.stockpileProgress > 0)
                    {
                        chromeUiRenderService.drawHealthBar2(static_cast<float>(bar.x), static_cast<float>(extraBottom + bar.y), static_cast<float>(bar.width), static_cast<float>(bar.height), fraction);
                    }
                }
            }
        }
        else if (hoveredFeature)
        {
            const auto& feature = simulation.getFeature(*hoveredFeature);
            const auto& featureDefinition = simulation.getFeatureDefinition(feature.featureName);
            const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

            {
                const auto& rect = localSideData._name;
                auto text = featureMediaInfo.description;
                if (featureDefinition.reclaimable)
                {
                    text += " ";
                    if (featureDefinition.metal > 0)
                    {
                        text += " M:" + formatResource(Metal(featureDefinition.metal));
                    }

                    if (featureDefinition.energy > 0)
                    {
                        text += " E:" + formatResource(Energy(featureDefinition.energy));
                    }
                }
                chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont);
            }
        }
        else if (auto hoveredBuildButtonUnitType = getUnitBuildButtonUnderCursor(); hoveredBuildButtonUnitType)
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(*hoveredBuildButtonUnitType);

            {
                const auto& rect = localSideData._name;
                auto text = unitDefinition.unitName + "  M:" + formatResource(unitDefinition.buildCostMetal) + " E:" + formatResource(unitDefinition.buildCostEnergy);
                chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont);
            }

            {
                const auto& rect = localSideData.description;
                chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, unitDefinition.unitDescription, *guiFont);
            }
        }

        currentPanel->render(chromeUiRenderService);

        for (auto& panel : gameMenuPanels)
        {
            panel->render(chromeUiRenderService);
        }
    }

    void GameScene::renderOverlay()
    {
        // These overlays must render AFTER renderWorld, otherwise the world
        // pass overwrites the center of the screen where they sit.

        // Speed indicator: TA shows "+N" / "-N" relative to default 1.0x speed.
        if (!gameSpeed.isDefault())
        {
            int offset = gameSpeed.displayOffset();
            std::string speedText = (offset > 0 ? "+" : "") + std::to_string(offset);
            float centerX = static_cast<float>(sceneContext.viewport->width()) / 2.0f;
            chromeUiRenderService.drawTextCenteredX(centerX, GuiSizeTop + 8, speedText, *guiFont);
        }

        renderConsole();

        if (paused)
        {
            float centerX = static_cast<float>(sceneContext.viewport->width()) / 2.0f;
            float centerY = static_cast<float>(sceneContext.viewport->height()) / 2.0f;
            // TA's own title from anims/IGTITLES.GAF, centred on the screen.
            auto title = gameMediaDatabase.getSpriteSeries("IGTITLES", "igpaused");
            if (title && !(*title)->sprites.empty())
            {
                const auto& sprite = *(*title)->sprites.front();
                chromeUiRenderService.drawSpriteAbs(std::floor(centerX - (sprite.bounds.width() / 2.0f)), std::floor(centerY - (sprite.bounds.height() / 2.0f)), sprite);
            }
            else
            {
                chromeUiRenderService.drawTextCenteredX(centerX, centerY, "PAUSED", *guiFont);
            }
        }
    }

    void GameScene::renderMinimap()
    {
        // draw minimap
        chromeUiRenderService.drawSpriteAbs(minimapRect, *minimap);
        if (fogOfWarEnabled && fogSprite)
        {
            chromeUiRenderService.drawSpriteAbs(minimapRect, *fogSprite);
        }

        auto cameraInverse = computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        auto worldToMinimap = worldToMinimapMatrix(simulation.terrain, minimapRect);

        // draw minimap dots
        for (const auto& [_, unit] : simulation.units)
        {
            if (!unitIsDetectableByLocalPlayer(unit) || unit.carriedBy)
            {
                // Units riding in a transport are inside it: only the
                // transport shows on the minimap.
                continue;
            }
            auto minimapPos = worldToMinimap * simVectorToFloat(unit.position);
            minimapPos.x = std::floor(minimapPos.x);
            minimapPos.y = std::floor(minimapPos.y);
            auto ownerId = unit.owner;
            auto colorIndex = getPlayer(ownerId).color;
            chromeUiRenderService.drawSprite(minimapPos.x, minimapPos.y, *minimapDots->sprites[colorIndex.value]);
        }
        // highlight the minimap dot for the hovered unit
        if (hoveredUnit)
        {
            const auto& unit = getUnit(*hoveredUnit);
            auto minimapPos = worldToMinimap * simVectorToFloat(unit.position);
            minimapPos.x = std::floor(minimapPos.x);
            minimapPos.y = std::floor(minimapPos.y);
            chromeUiRenderService.drawSprite(minimapPos.x, minimapPos.y, *minimapDotHighlight);
        }

        // draw the detection rings of every selected unit
        renderMinimapDetectionRings(worldToMinimap);

        // draw minimap viewport rectangle
        {
            auto transform = worldToMinimap * cameraInverse;
            auto bottomLeft = transform * Vector3f(-1.0f, -1.0f, 0.0f);
            auto topRight = transform * Vector3f(1.0f, 1.0f, 0.0f);

            chromeUiRenderService.drawBoxOutline(
                std::round(bottomLeft.x),
                std::round(topRight.y),
                std::round(topRight.x - bottomLeft.x),
                std::round(bottomLeft.y - topRight.y),
                Color(247, 227, 103));
        }
    }

    void GameScene::renderMinimapDetectionRings(const Matrix4f& worldToMinimap)
    {
        // 0x466DC0, the minimap render, per unit and gated on the selection
        // bit -- so every selected unit draws its rings, not just one.
        //
        // Four ranges get a ring: RadarDistance, SonarDistance and the two
        // jammer radii. SightDistance does not. It sits one slot away in the
        // definition (+0x202 against +0x204) and the routine steps over it
        // deliberately, which is easy to disbelieve until you read it -- the
        // rings are about what the unit tells you, not about what it can see.
        //
        // An onoffable unit that is switched off draws nothing: turning a
        // radar off takes its ring away with it.
        //
        // Honest gap: the colour is a byte out of a runtime table at
        // cfg+0xDCB, and nothing in .text ever writes that table -- it is a
        // logical-colour to palette remap installed for the blitter. The
        // indices could not be recovered without running the game. TA's green
        // ramp is palette 232-239; these are RWE's own greens, with the
        // jammers dimmer than the detectors so the pair can be told apart.
        const int segments = 32;
        auto mapWidth = simScalarToFloat(simulation.terrain.rightCutoffInWorldUnits() - simulation.terrain.leftInWorldUnits());
        if (mapWidth <= 0.0f)
        {
            return;
        }
        auto worldUnitsToMinimapPixels = static_cast<float>(minimapRect.width()) / mapWidth;

        for (const auto& unitId : selectedUnits)
        {
            const auto& unit = getUnit(unitId);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

            if (unitDefinition.onOffable && !unit.activated)
            {
                continue;
            }

            auto centre = worldToMinimap * simVectorToFloat(unit.position);

            // The colours are the interface map's slots 0x0A and 0x0C,
            // resolved through the runtime GUIPAL nearest-match (0x4AC7D0):
            // detectors draw bright green, jammers orange-red.
            const std::pair<unsigned int, Color> rings[] = {
                {unitDefinition.radarDistance, Color(83, 223, 79)},
                {unitDefinition.sonarDistance, Color(83, 223, 79)},
                {unitDefinition.radarDistanceJam, Color(255, 71, 0)},
                {unitDefinition.sonarDistanceJam, Color(255, 71, 0)},
            };

            for (const auto& [range, color] : rings)
            {
                if (range == 0)
                {
                    continue;
                }

                // The horizontal scale is used for both axes, even though the
                // minimap is not square on every map.
                auto radius = static_cast<float>(range) * worldUnitsToMinimapPixels;
                if (radius < 1.0f)
                {
                    continue;
                }

                std::vector<Vector2f> points;
                points.reserve(segments);
                for (int i = 0; i < segments; ++i)
                {
                    auto angle = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(segments);
                    points.emplace_back(centre.x + (std::cos(angle) * radius), centre.y + (std::sin(angle) * radius));
                }

                chromeUiRenderService.drawLineLoop(points, color);
            }
        }
    }

    void GameScene::renderBuildBoxes(const UnitState& unit, const Color& outerColor, const Color& innerColor)
    {
        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        for (const auto& order : unit.orders)
        {
            if (const auto buildOrder = std::get_if<BuildOrder>(&order))
            {
                const auto& unitType = buildOrder->unitType;
                const auto& unitDefinition = simulation.unitDefinitions.at(unitType);
                auto mc = simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo);
                auto footprintRect = simulation.computeFootprintRegion(buildOrder->position, unitDefinition.movementCollisionInfo);

                auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(footprintRect.x, footprintRect.y);
                topLeftWorld.y = simulation.terrain.getHeightAt(
                    topLeftWorld.x + ((SimScalar(footprintRect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
                    topLeftWorld.z + ((SimScalar(footprintRect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));

                auto topLeftUi = worldToUi * simVectorToFloat(topLeftWorld);
                auto boxWidth = footprintRect.width * simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
                auto boxHeight = footprintRect.height * simScalarToFloat(MapTerrain::HeightTileHeightInWorldUnits);

                // Two nested one-pixel outlines with the darker line INSIDE.
                // The exact colours came out of the binary at last: the
                // "unwritable" interface colour table is written by 0x4AC7D0
                // addressing it from a different base, a runtime nearest-match
                // of GUIPAL.PAL into the screen palette. A selected owner's
                // queue draws bright green over dark cyan; anyone else's
                // draws bright blue over navy -- the teal tint in the
                // screenshot was cyan, not a green.
                worldUiRenderService.drawBoxOutline(topLeftUi.x, topLeftUi.y, boxWidth, boxHeight, outerColor, 1.0f);
                if (boxWidth > 2.0f && boxHeight > 2.0f)
                {
                    worldUiRenderService.drawBoxOutline(topLeftUi.x + 1.0f, topLeftUi.y + 1.0f, boxWidth - 2.0f, boxHeight - 2.0f, innerColor, 1.0f);
                }
            }
        }
    }

    namespace
    {
        uint64_t buildBoxKey(const DiscreteRect& rect)
        {
            // Grid coordinates, so two build orders can only collide here if
            // they are for the same square, in which case one sweep is right.
            return (static_cast<uint64_t>(static_cast<uint32_t>(rect.x)) << 32)
                | static_cast<uint64_t>(static_cast<uint32_t>(rect.y));
        }

        /** Ten ticks, a third of a second (0x438C00). */
        const unsigned int BuildBoxSweepTicks = 10;
    }

    void GameScene::updateBuildBoxAppearances()
    {
        // Note when each build order turns up, whatever put it there: the
        // local player's click, an ally's, or the AI's. This runs every frame
        // rather than only while the boxes are being drawn, because the sweep
        // has to start when the building was placed and not when somebody
        // happened to hold shift down.
        std::unordered_map<uint64_t, GameTime> stillThere;
        for (const auto& [_, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            for (const auto& order : unit.orders)
            {
                const auto buildOrder = std::get_if<BuildOrder>(&order);
                if (buildOrder == nullptr)
                {
                    continue;
                }

                const auto& unitDefinition = simulation.unitDefinitions.at(buildOrder->unitType);
                auto key = buildBoxKey(simulation.computeFootprintRegion(buildOrder->position, unitDefinition.movementCollisionInfo));
                auto existing = buildBoxAppearedAt.find(key);
                stillThere[key] = existing == buildBoxAppearedAt.end() ? simulation.gameTime : existing->second;
            }
        }

        // Anything no longer queued is dropped, so the same spot built on
        // twice gets its sweep twice.
        buildBoxAppearedAt = std::move(stillThere);
    }

    void GameScene::renderBuildBoxSweep(const Matrix4f& worldToUi, const DiscreteRect& footprintRect, unsigned int age, bool ownerSelected)
    {
        // 0x438C00. Four full-length lines -- two vertical, two horizontal --
        // sweeping inwards across the footprint: dx = width * t / 10 and
        // dy = height * t / 10, so at t=0 they lie on the box, at t=5 all four
        // meet in the middle, and at t=10 they have crossed and come to rest
        // back on the footprint the other way round. It is linear, there is no
        // overshoot, and it plays once.
        //
        // Each line is drawn twice: colour A overhangs the corners by a pixel,
        // colour B does not, which is where the little nubs come from. The
        // colours are the queued box's own, now exact: guicolours[3]/[10]
        // for a selected owner, [1]/[9] otherwise, through the runtime
        // GUIPAL nearest-match (0x4AC7D0).
        auto colorA = ownerSelected ? Color(0, 128, 128) : Color(0, 0, 128);
        auto colorB = ownerSelected ? Color(83, 223, 79) : Color(84, 84, 252);

        auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(footprintRect.x, footprintRect.y);
        topLeftWorld.y = simulation.terrain.getHeightAt(
            topLeftWorld.x + ((SimScalar(footprintRect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
            topLeftWorld.z + ((SimScalar(footprintRect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));

        auto topLeftUi = worldToUi * simVectorToFloat(topLeftWorld);
        auto width = footprintRect.width * simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
        auto height = footprintRect.height * simScalarToFloat(MapTerrain::HeightTileHeightInWorldUnits);

        auto t = static_cast<float>(std::min(age, BuildBoxSweepTicks)) / static_cast<float>(BuildBoxSweepTicks);
        auto dx = width * t;
        auto dy = height * t;

        const float thickness = 2.0f;
        auto x0 = topLeftUi.x;
        auto y0 = topLeftUi.y;

        for (auto pass = 0; pass < 2; ++pass)
        {
            auto color = pass == 0 ? colorA : colorB;
            auto overhang = pass == 0 ? 1.0f : 0.0f;

            worldUiRenderService.fillColor(x0 + dx, y0 - overhang, thickness, height + (overhang * 2.0f), color);
            worldUiRenderService.fillColor(x0 + width - dx - thickness, y0 - overhang, thickness, height + (overhang * 2.0f), color);
            worldUiRenderService.fillColor(x0 - overhang, y0 + dy, width + (overhang * 2.0f), thickness, color);
            worldUiRenderService.fillColor(x0 - overhang, y0 + height - dy - thickness, width + (overhang * 2.0f), thickness, color);
        }
    }

    void GameScene::renderPlacementSweeps()
    {
        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());

        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            auto ownerSelected = selectedUnits.find(unitId) != selectedUnits.end();

            for (const auto& order : unit.orders)
            {
                const auto buildOrder = std::get_if<BuildOrder>(&order);
                if (buildOrder == nullptr)
                {
                    continue;
                }

                const auto& unitDefinition = simulation.unitDefinitions.at(buildOrder->unitType);
                auto footprintRect = simulation.computeFootprintRegion(buildOrder->position, unitDefinition.movementCollisionInfo);
                auto it = buildBoxAppearedAt.find(buildBoxKey(footprintRect));
                if (it == buildBoxAppearedAt.end())
                {
                    continue;
                }

                auto age = simulation.gameTime.value - it->second.value;
                if (age > BuildBoxSweepTicks)
                {
                    continue;
                }

                renderBuildBoxSweep(worldToUi, footprintRect, age, ownerSelected);
            }
        }
    }

    void GameScene::drawWaypointTrail(const Matrix4f& worldToUi, const SimVector& from, const SimVector& to)
    {
        // The original does not draw a line between waypoints at all. It walks
        // the segment planting a small four-armed star -- `pathicon` in
        // anims/CURSORS.GAF, eleven pixels square, hotspot dead centre, and
        // green with the bright end of the ramp at the tips -- every 48 world
        // units, and slides the whole string of them along by 1.6 world units
        // a tick. 1.6 x 30 is 48, so after a second every star has arrived
        // where its neighbour was and the march is seamless.
        //
        // Where we differ: the original takes the phase from the age of the
        // order being drawn, so two orders queued a few ticks apart march very
        // slightly out of step with one another. RWE's orders do not record
        // when they were issued, and giving them one would mean carrying it
        // through the hash, the dump and the network protocol for an effect
        // nobody can see, so the phase comes off the global clock and every
        // segment marches together.
        const float spacing = 48.0f;
        const float speedPerTick = spacing / 30.0f;

        auto fromF = simVectorToFloat(from);
        auto toF = simVectorToFloat(to);
        auto along = toF - fromF;
        auto length = along.length();
        if (length < 0.001f)
        {
            return;
        }
        auto direction = along / length;

        const auto& frames = sceneContext.cursor->getCursor(CursorType::PathIcon)->sprites;
        if (frames.empty())
        {
            return;
        }
        const auto& icon = *frames.front();

        // The march wraps every 30 ticks, which is what keeps this in step
        // with the simulation rather than with the frame rate.
        auto phase = static_cast<float>(simulation.gameTime.value % 30u) * speedPerTick;

        // The straight line between the two order positions, height included:
        // the trail does not follow the ground, so on a slope it cuts through
        // the hill rather than draping over it. That is the original's
        // behaviour, not an omission.
        for (auto travelled = phase; travelled < length; travelled += spacing)
        {
            auto point = worldToUi * (fromF + (direction * travelled));
            worldUiRenderService.drawSprite(point.x, point.y, icon);
        }
    }

    void GameScene::renderUnitOrders(UnitId unitId, bool drawLines)
    {
        const auto& unit = getUnit(unitId);
        auto pos = unit.position;
        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        for (const auto& order : unit.orders)
        {
            auto nextPos = match(
                order,
                [&](const BuildOrder& o) { return o.position; },
                [&](const MoveOrder& o) { return o.destination; },
                [&](const AttackOrder& o) { return match(
                                                o.target,
                                                [&](const SimVector& v) { return v; },
                                                [&](const UnitId& u) {
                                                    auto unitOption = tryGetUnit(u);
                                                    if (!unitOption)
                                                    {
                                                        return pos;
                                                    }
                                                    return unitOption->get().position;
                                                }); },
                [&](const BuggerOffOrder&) { return pos; },
                [&](const CompleteBuildOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const GuardOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const RepairOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const PatrolOrder& o) { return o.destination; },
                [&](const CaptureOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const LoadOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const UnloadOrder& o) { return o.destination; },
                [&](const ReclaimOrder& o) {
                    return match(
                        o.target,
                        [&](const UnitId& u) {
                            auto unitOption = tryGetUnit(u);
                            if (!unitOption)
                            {
                                return pos;
                            }
                            return unitOption->get().position;
                        },
                        [&](const FeatureId& f) {
                            auto featureOption = simulation.tryGetFeature(f);
                            if (!featureOption)
                            {
                                return pos;
                            }
                            return featureOption->get().position;
                        });
                });

            auto waypointIcon = match(
                order,
                [&](const BuildOrder&) { return std::optional<CursorType>(); },
                [&](const MoveOrder&) { return std::optional<CursorType>(CursorType::Move); },
                [&](const AttackOrder&) { return std::optional<CursorType>(CursorType::Attack); },
                [&](const BuggerOffOrder&) { return std::optional<CursorType>(); },
                [&](const CompleteBuildOrder&) { return std::optional<CursorType>(CursorType::Repair); },
                [&](const GuardOrder&) { return std::optional<CursorType>(CursorType::Guard); },
                [&](const ReclaimOrder&) { return std::optional<CursorType>(CursorType::Reclaim); },
                [&](const RepairOrder&) { return std::optional<CursorType>(CursorType::Repair); },
                [&](const PatrolOrder&) { return std::optional<CursorType>(CursorType::Patrol); },
                [&](const CaptureOrder&) { return std::optional<CursorType>(CursorType::Capture); },
                [&](const LoadOrder&) { return std::optional<CursorType>(CursorType::Load); },
                [&](const UnloadOrder&) { return std::optional<CursorType>(CursorType::Unload); });

            // draw waypoint icons
            if (waypointIcon)
            {
                auto timeInMillis = sceneContext.timeService->getTicks();
                unsigned int frameRateInSeconds = 2;
                unsigned int millisPerFrame = 1000 / frameRateInSeconds;

                const auto& frames = sceneContext.cursor->getCursor(*waypointIcon)->sprites;
                auto frameIndex = (timeInMillis / millisPerFrame) % frames.size();

                auto uiDest = worldToUi * simVectorToFloat(nextPos);
                worldUiRenderService.drawSprite(uiDest.x, uiDest.y, *(frames[frameIndex]), Color(255, 255, 255, 100));
            }

            if (drawLines)
            {
                auto drawLine = match(
                    order,
                    [&](const BuildOrder&) { return true; },
                    [&](const MoveOrder&) { return true; },
                    [&](const AttackOrder&) { return false; },
                    [&](const BuggerOffOrder&) { return false; },
                    [&](const CompleteBuildOrder&) { return true; },
                    [&](const GuardOrder&) { return true; },
                    [&](const ReclaimOrder&) { return true; },
                    [&](const RepairOrder&) { return true; },
                    [&](const PatrolOrder&) { return true; },
                    [&](const CaptureOrder&) { return true; },
                    [&](const LoadOrder&) { return true; },
                    [&](const UnloadOrder&) { return true; });

                if (drawLine)
                {
                    drawWaypointTrail(worldToUi, pos, nextPos);
                }
            }

            pos = nextPos;
        }
    }

    void GameScene::renderWorld()
    {
        // The window can be resized (or dragged between monitors of
        // different scale) at any moment; the world is drawn through an
        // offscreen buffer, which has to follow the viewport or the view
        // ends up squeezed into a corner of the old size.
        if (worldRenderTextureSize != std::pair<unsigned int, unsigned int>{worldViewport.width(), worldViewport.height()})
        {
            recreateWorldRenderTextures();
        }

        updateFogSprite();

        sceneContext.graphics->bindFrameBuffer(worldFrameBuffer.frameBuffer.get());
        sceneContext.graphics->setViewport(
            0,
            0,
            worldViewport.width(),
            worldViewport.height());

        sceneContext.graphics->clear();

        const auto& viewProjectionMatrix = computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        RenderService worldRenderService(sceneContext.graphics, sceneContext.shaders, &viewProjectionMatrix);

        sceneContext.graphics->disableDepthBuffer();

        // Fog of war is applied by the terrain shader: remembered ground goes
        // grey, unknown ground black, along the ragged boundary that TA's own
        // fog tiles have been rasterised into fogOverlayTexture. That texture
        // covers a window around the camera, not the whole map.
        std::optional<FogOverlay> fogOverlay;
        if (fogOfWarEnabled && fogOverlayTexture.isValid())
        {
            fogOverlay = FogOverlay{fogOverlayTexture.get(), fogOverlayBounds.left(), fogOverlayBounds.top(), fogOverlayBounds.width(), fogOverlayBounds.height()};
        }
        worldRenderService.drawMapTerrain(terrainGraphics, worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), fogOverlay);

        SpriteBatch flatFeatureBatch;
        SpriteBatch flatFeatureShadowBatch;
        for (const auto& f : simulation.features)
        {
            if (!positionIsExploredByLocalPlayer(f.second.position))
            {
                continue;
            }
            const auto& featureDefinition = simulation.getFeatureDefinition(f.second.featureName);
            if (!featureDefinition.isStanding())
            {
                auto fogged = !positionIsVisibleToLocalPlayer(f.second.position);
                drawFeature(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, simulation.gameTime, fogged, flatFeatureBatch);
                drawFeatureShadow(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, fogged, flatFeatureShadowBatch);
            }
        }
        worldRenderService.drawSpriteBatch(flatFeatureShadowBatch);
        worldRenderService.drawSpriteBatch(flatFeatureBatch);

        ColoredMeshBatch squareParticlesBatch;
        for (const auto& particle : particles)
        {
            drawWakeParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, squareParticlesBatch);
        }
        worldRenderService.drawBatch(squareParticlesBatch, viewProjectionMatrix);

        ColoredMeshBatch terrainOverlayBatch;

        if (occupiedGridVisible)
        {
            drawOccupiedGrid(worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), simulation.terrain, simulation.occupiedGrid, terrainOverlayBatch);
        }
        if (pathfindingVisualisationVisible)
        {
            drawPathfindingVisualisation(simulation.terrain, simulation.pathFindingService.lastPathDebugInfo, terrainOverlayBatch);
        }

        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && movementClassGridVisible)
        {
            const auto& unit = simulation.getUnitState(*selectedUnit);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            match(
                unitDefinition.movementCollisionInfo,
                [&](const UnitDefinition::NamedMovementClass& c) {
                    const auto& grid = simulation.movementClassCollisionService.getGrid(c.movementClassId);
                    drawMovementClassCollisionGrid(simulation.terrain, grid, worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), terrainOverlayBatch);
                },
                [&](const auto&) {

                });
        }

        worldRenderService.drawBatch(terrainOverlayBatch, viewProjectionMatrix);

        auto interpolationFraction = static_cast<float>(millisecondsBuffer) / static_cast<float>(SimMillisecondsPerTick);
        ColoredMeshesBatch selectionRectBatch;
        for (const auto& selectedUnitId : selectedUnits)
        {
            const auto& unit = getUnit(selectedUnitId);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            drawSelectionRect(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, interpolationFraction, selectionRectBatch);
        }
        worldRenderService.drawLineLoopsBatch(selectionRectBatch);

        auto seaLevel = simulation.terrain.getSeaLevel();

        UnitShadowMeshBatch unitShadowMeshBatch;
        for (const auto& [_, unit] : simulation.units)
        {
            if (!unitIsVisibleToLocalPlayer(unit))
            {
                continue;
            }
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (!shadowsEnabled || !unitCastsShadow(unitDefinition))
            {
                continue;
            }
            const auto& modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);

            auto groundHeight = simulation.terrain.getHeightAt(unit.position.x, unit.position.z);
            if (unitDefinition.floater || unitDefinition.canHover)
            {
                groundHeight = rweMax(groundHeight, seaLevel);
            }
            drawUnitShadow(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, modelDefinition, interpolationFraction, simScalarToFloat(groundHeight), unitTextureAtlas.get(), unitTeamTextureAtlases, unitShadowMeshBatch);

            if (unit.isBeingBuilt(unitDefinition))
            {
                // The frame is see-through while it is built, so the shadow
                // would show through it. Keep only the part cast outside the
                // model's own outline.
                drawUnitSilhouette(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, modelDefinition, interpolationFraction, unitTextureAtlas.get(), unitTeamTextureAtlases, unitShadowMeshBatch.cutouts);
            }
        }
        for (const auto& [_, feature] : simulation.features)
        {
            const auto& position = feature.position;
            if (!positionIsExploredByLocalPlayer(position))
            {
                continue;
            }
            auto groundHeight = simulation.terrain.getHeightAt(position.x, position.z);
            if (position.y >= seaLevel && groundHeight < seaLevel)
            {
                groundHeight = seaLevel;
            }

            drawFeatureMeshShadow(simulation.unitModelDefinitions, gameMediaDatabase, viewProjectionMatrix, feature, simScalarToFloat(groundHeight), unitTextureAtlas.get(), unitTeamTextureAtlases, unitShadowMeshBatch);
        }
        worldRenderService.drawUnitShadowMeshBatch(unitShadowMeshBatch);

        sceneContext.graphics->enableDepthBuffer();

        UnitMeshBatch unitMeshBatch;
        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unitIsVisibleToLocalPlayer(unit))
            {
                continue;
            }
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            const auto& unitModelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);
            drawUnit(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, unitModelDefinition, getPlayer(unit.owner).color, unitId.value, simulation.gameTime.value, interpolationFraction, unitTextureAtlas.get(), unitTeamTextureAtlases, unitMeshBatch);
        }
        for (const auto& [_, feature] : simulation.features)
        {
            if (!positionIsExploredByLocalPlayer(feature.position))
            {
                continue;
            }
            drawMeshFeature(simulation.unitModelDefinitions, gameMediaDatabase, viewProjectionMatrix, feature, unitTextureAtlas.get(), unitTeamTextureAtlases, unitMeshBatch);
        }
        for (const auto& d : debris)
        {
            if (d.shard)
            {
                continue;
            }
            auto position = d.position + (d.velocity * interpolationFraction);
            auto rotation = d.rotation + (d.angularVelocity * interpolationFraction);
            auto matrix = Matrix4f::translation(position) * Matrix4f::rotationZXY(rotation);
            drawDebrisPiece(gameMediaDatabase, viewProjectionMatrix, d.objectName, d.pieceName, matrix, d.color, unitTextureAtlas.get(), unitTeamTextureAtlases, unitMeshBatch);
        }
        worldRenderService.drawUnitMeshBatch(unitMeshBatch, simScalarToFloat(seaLevel));

        // Construction wireframe: the visible polygon edges of each nanoframe,
        // drawn with the depth test on so the model hides its own back. The
        // original outlines every primitive of every piece in its second build
        // colour, a triangle wave down palette entries 160..175 and back that
        // comes round about every half second, offset per unit.
        {
            // The direction from the scene towards the camera, in world space.
            auto inverseViewProjection = computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
            auto toCamera = ((inverseViewProjection * Vector3f(0.0f, 0.0f, -1.0f)) - (inverseViewProjection * Vector3f(0.0f, 0.0f, 0.0f))).normalized();

            ColoredMeshBatch wireframeBatch;
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unit))
                {
                    continue;
                }
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                if (!unit.isBeingBuilt(unitDefinition))
                {
                    continue;
                }
                const auto& modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);
                auto wireframeColor = buildCycleColorB(unitId.value, simulation.gameTime.value);
                drawUnitWireframe(gameMediaDatabase, unit, unitDefinition, modelDefinition, interpolationFraction, toCamera, wireframeColor, wireframeBatch);
            }
            // Lines cannot go below one pixel, so a lighter blend reads as a finer wire.
            worldRenderService.drawBatch(wireframeBatch, viewProjectionMatrix, 0.65f);
        }

        ColoredMeshBatch lineProjectilesBatch;
        SpriteBatch spriteProjectilesBatch;
        UnitMeshBatch meshProjectilesBatch;
        drawProjectiles(simulation, gameMediaDatabase, viewProjectionMatrix, simulation.projectiles, simulation.gameTime, interpolationFraction, unitTextureAtlas.get(), unitTeamTextureAtlases, lineProjectilesBatch, spriteProjectilesBatch, meshProjectilesBatch);
        worldRenderService.drawBatch(lineProjectilesBatch, viewProjectionMatrix);
        worldRenderService.drawUnitMeshBatch(meshProjectilesBatch, simScalarToFloat(seaLevel));
        worldRenderService.drawSpriteBatch(spriteProjectilesBatch);

        sceneContext.graphics->disableDepthWrites();

        SpriteBatch featureBatch;
        SpriteBatch featureShadowBatch;
        for (const auto& f : simulation.features)
        {
            if (!positionIsExploredByLocalPlayer(f.second.position))
            {
                continue;
            }
            const auto& featureDefinition = simulation.getFeatureDefinition(f.second.featureName);
            if (featureDefinition.isStanding())
            {
                auto fogged = !positionIsVisibleToLocalPlayer(f.second.position);
                drawFeature(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, simulation.gameTime, fogged, featureBatch);
                drawFeatureShadow(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, fogged, featureShadowBatch);
            }
        }
        worldRenderService.drawSpriteBatch(featureShadowBatch);
        worldRenderService.drawSpriteBatch(featureBatch);

        // Particles that belong in the world rather than over it: drawn here,
        // while the depth test is still on, so what is in front of them hides
        // them. An aircraft's exhaust comes out from under the hull, and the
        // hull should cover it.
        {
            SpriteBatch worldSpriteParticlesBatch;
            for (const auto& particle : particles)
            {
                if (!particleDrawsInWorld(particle))
                {
                    continue;
                }
                drawSpriteParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, worldSpriteParticlesBatch);
            }
            worldRenderService.drawSpriteBatch(worldSpriteParticlesBatch);
        }

        // Nano spray keeps depth testing (with writes still off) so the unit
        // doing the lathing occludes the part of the stream behind it. Drawn
        // without it, a construction aircraft hovering over its work has the
        // spray painted across the top of the fuselage.
        ColoredMeshBatch nanoParticlesBatch;
        for (const auto& particle : particles)
        {
            drawNanoParticle(simulation.gameTime, interpolationFraction, particle, nanoParticlesBatch);
        }
        for (const auto& d : debris)
        {
            if (d.shard)
            {
                drawDebrisShard(d.position + (d.velocity * interpolationFraction), nanoParticlesBatch);
            }
        }
        worldRenderService.drawBatch(nanoParticlesBatch, viewProjectionMatrix);

        sceneContext.graphics->disableDepthTest();

        sceneContext.graphics->bindFrameBufferColorBuffer(dodgeMask.get());
        sceneContext.graphics->clearColor();
        worldRenderService.drawFlashes(simulation.gameTime, flashes);
        sceneContext.graphics->bindFrameBufferColorBuffer(worldFrameBuffer.texture.get());

        sceneContext.graphics->unbindFrameBuffer();
        auto viewportPos = worldViewport.toOtherViewport(*sceneContext.viewport, 0, worldViewport.height());
        sceneContext.graphics->setViewport(
            viewportPos.x,
            sceneContext.viewport->height() - viewportPos.y,
            worldViewport.width(),
            worldViewport.height());

        sceneContext.graphics->disableDepthBuffer();
        auto quadMesh = sceneContext.graphics->createUnitTexturedQuadFlipped(Rectangle2f::fromTLBR(1.0f, 0.0f, 0.0f, 1.0f));
        sceneContext.graphics->bindShader(sceneContext.shaders->worldPost.handle.get());
        sceneContext.graphics->setUniformInt(sceneContext.shaders->worldPost.dodgeMask, 1);
        sceneContext.graphics->bindTexture(worldFrameBuffer.texture.get());
        sceneContext.graphics->setActiveTextureSlot1();
        sceneContext.graphics->bindTexture(dodgeMask.get());
        sceneContext.graphics->setActiveTextureSlot0();
        sceneContext.graphics->drawTriangles(quadMesh);

        SpriteBatch spriteParticlesBatch;
        for (const auto& particle : particles)
        {
            if (particleDrawsInWorld(particle))
            {
                continue;
            }
            drawSpriteParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, spriteParticlesBatch);
        }
        worldRenderService.drawSpriteBatch(spriteParticlesBatch);
        sceneContext.graphics->enableDepthTest();

        sceneContext.graphics->enableDepthWrites();

        // The sweep over a freshly placed building plays whether or not shift
        // is held: it is the acknowledgement of the click, and the original
        // shows it as soon as the order exists.
        renderPlacementSweeps();

        // in-world UI/overlay rendering
        if (isShiftDown())
        {
            auto singleSelectedUnit = getSingleSelectedUnit();

            // if unit is a builder, show all other buildings being built
            if (shouldShowAllBuildBoxes(simulation, localPlayerId, singleSelectedUnit, hoveredUnit))
            {
                for (const auto& [_, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(localPlayerId))
                    {
                        renderBuildBoxes(unit, Color(84, 84, 252), Color(0, 0, 128));
                    }
                }
            }

            // draw orders + lines for hovered unit
            if (hoveredUnit && getUnit(*hoveredUnit).isOwnedBy(localPlayerId))
            {
                renderUnitOrders(*hoveredUnit, true);
            }

            // draw orders for all selected units
            for (const auto& selectedUnitId : selectedUnits)
            {
                renderBuildBoxes(getUnit(selectedUnitId), Color(83, 223, 79), Color(0, 128, 128));

                if (selectedUnitId != hoveredUnit)
                {
                    // draw lines if only one unit is selected--hovered unit is drawn aleady
                    renderUnitOrders(selectedUnitId, singleSelectedUnit == selectedUnitId);
                }
            }
        }

        if (healthBarsVisible)
        {
            for (const auto& [_, unit] : simulation.units)
            {
                if (!unit.isOwnedBy(localPlayerId) || unit.carriedBy)
                {
                    // only draw healthbars on units we own, and not on cargo
                    continue;
                }

                if (unit.hitPoints == 0)
                {
                    // Do not show health bar when the unit has zero health.
                    // This can happen when the unit is still a freshly created nanoframe.
                    continue;
                }

                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

                auto uiPos = worldUiRenderService.getInverseViewProjectionMatrix()
                    * viewProjectionMatrix
                    * simVectorToFloat(unit.position);
                worldUiRenderService.drawHealthBar(uiPos.x, uiPos.y, static_cast<float>(unit.hitPoints) / static_cast<float>(unitDefinition.maxHitPoints));
            }
        }

        // Radar contacts get nothing here. The original's world render never
        // walks the unit list at all: it consumes a list rebuilt each frame by
        // 0x48BAE0, which admits a unit only if it is the viewer's own or
        // passes the can-see predicate 0x465AC0 -- and that predicate does not
        // look at the radar bits. A contact you only have on radar is a dot on
        // the minimap and nothing whatever in the main view, which is why it
        // cannot be clicked there either.

        // Self-destruct countdowns: seconds remaining, drawn above the unit.
        for (const auto& [_, unit] : simulation.units)
        {
            if (!unit.selfDestructTime || !unit.isAlive())
            {
                continue;
            }

            auto ticksLeft = *unit.selfDestructTime > simulation.gameTime
                ? (*unit.selfDestructTime - simulation.gameTime).value
                : 0u;
            auto secondsLeft = (ticksLeft + SimTicksPerSecond - 1) / SimTicksPerSecond;

            auto uiPos = worldUiRenderService.getInverseViewProjectionMatrix()
                * viewProjectionMatrix
                * simVectorToFloat(unit.position);
            // A red-framed badge so the countdown reads at a glance.
            const float badgeWidth = 22.0f;
            const float badgeHeight = 16.0f;
            auto badgeX = uiPos.x - (badgeWidth / 2.0f);
            auto badgeY = uiPos.y - 34.0f;
            worldUiRenderService.fillColor(badgeX, badgeY, badgeWidth, badgeHeight, Color(0, 0, 0, 200));
            worldUiRenderService.drawBoxOutline(badgeX, badgeY, badgeWidth, badgeHeight, Color(255, 40, 40), 2.0f);
            worldUiRenderService.drawTextCentered(uiPos.x, badgeY + (badgeHeight / 2.0f), std::to_string(secondsLeft), *guiFont);
        }

        // Draw build box outline when a unit is selected to be built.
        // The original's cursor box is two nested one-pixel rectangles in
        // ONE colour -- guicolours[10]/[4], which the runtime nearest-match
        // mapping (0x4AC7D0, GUIPAL.PAL into the screen palette) lands on
        // palette 233 bright green and 213 red.
        if (hoverBuildInfo)
        {
            Color color = hoverBuildInfo->isValid ? Color(83, 223, 79) : Color(171, 23, 0);

            auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(hoverBuildInfo->rect.x, hoverBuildInfo->rect.y);
            topLeftWorld.y = simulation.terrain.getHeightAt(
                topLeftWorld.x + ((SimScalar(hoverBuildInfo->rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
                topLeftWorld.z + ((SimScalar(hoverBuildInfo->rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));

            auto topLeftUi = worldUiRenderService.getInverseViewProjectionMatrix()
                * viewProjectionMatrix
                * simVectorToFloat(topLeftWorld);
            worldUiRenderService.drawBoxOutline(
                topLeftUi.x,
                topLeftUi.y,
                hoverBuildInfo->rect.width * simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits),
                hoverBuildInfo->rect.height * simScalarToFloat(MapTerrain::HeightTileHeightInWorldUnits),
                color,
                2.0f);
        }

        // Draw bandbox selection rectangle
        if (auto normalCursorMode = std::get_if<NormalCursorMode>(&cursorMode.getValue()))
        {
            if (auto selectingState = std::get_if<NormalCursorMode::SelectingState>(&normalCursorMode->state))
            {
                const auto& start = selectingState->startPosition;
                const auto cameraPosition = worldCameraState.getRoundedPosition();
                Point cameraRelativeStart(start.x - cameraPosition.x, start.y - cameraPosition.z);

                auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, getMousePosition());
                auto rect = DiscreteRect::fromPoints(cameraRelativeStart, worldViewportPos);

                worldUiRenderService.drawBoxOutline(rect.x, rect.y, rect.width, rect.height, Color(255, 255, 255));
                if (rect.width > 2 && rect.height > 2)
                {
                    worldUiRenderService.drawBoxOutline(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, Color(0, 0, 0));
                }
            }
        }

        if (cursorTerrainDotVisible)
        {
            // draw a dot where we think the cursor intersects terrain
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            auto intersect = simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
            if (intersect)
            {
                auto cursorTerrainPos = worldUiRenderService.getInverseViewProjectionMatrix()
                    * viewProjectionMatrix
                    * simVectorToFloat(*intersect);
                worldUiRenderService.fillColor(cursorTerrainPos.x - 2, cursorTerrainPos.y - 2, 4, 4, Color(0, 0, 255));

                intersect->y = simulation.terrain.getHeightAt(intersect->x, intersect->z);

                auto heightTestedTerrainPos = worldUiRenderService.getInverseViewProjectionMatrix()
                    * viewProjectionMatrix
                    * simVectorToFloat(*intersect);
                worldUiRenderService.fillColor(heightTestedTerrainPos.x - 2, heightTestedTerrainPos.y - 2, 4, 4, Color(255, 0, 0));
            }
        }

        sceneContext.graphics->enableDepthBuffer();

        sceneContext.graphics->setViewport(0, 0, sceneContext.viewport->width(), sceneContext.viewport->height());
    }

    const char* stateToString(const UnitBehaviorState& state)
    {
        return match(
            state,
            [&](const UnitBehaviorStateIdle&) {
                return "idle";
            },
            [&](const UnitBehaviorStateBuilding&) {
                return "building";
            },
            [&](const UnitBehaviorStateReclaiming&) {
                return "reclaiming";
            },
            [&](const UnitBehaviorStateCreatingUnit&) {
                return "creating unit";
            });
    }

    const char* cobAxisToString(const CobAxis& axis)
    {
        switch (axis)
        {
            case CobAxis::X:
                return "x-axis";
            case CobAxis::Y:
                return "Y-axis";
            case CobAxis::Z:
                return "z-axis";
            default:
                throw std::logic_error("invalid axis");
        }
    }

    std::string blockedStatusToString(const CobEnvironment::BlockedStatus& status)
    {
        return match(
            status.condition,
            [&](const CobEnvironment::BlockedStatus::Move& m) {
                return "wait-for-move piece " + std::to_string(m.object) + " along " + cobAxisToString(m.axis);
            },
            [&](const CobEnvironment::BlockedStatus::Turn& t) {
                return "wait-for-turn piece " + std::to_string(t.object) + " around " + cobAxisToString(t.axis);
            });
    }

    void renderUnitInfoSection(const UnitState& unit)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Unit Info"))
        {
            ImGui::LabelText("State", "%s", stateToString(unit.behaviourState));

            ImGui::LabelText("x", "%f", unit.position.x.value);
            ImGui::LabelText("y", "%f", unit.position.y.value);
            ImGui::LabelText("z", "%f", unit.position.z.value);
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("COB Scripts"))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Static Variables"))
            {
                for (Index i = 0; i < getSize(unit.cobEnvironment->_statics); ++i)
                {
                    ImGui::Text("%lld: %d", i, unit.cobEnvironment->_statics[i]);
                }
                ImGui::TreePop();
            }

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Threads"))
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("All"))
                {

                    for (Index i = 0; i < getSize(unit.cobEnvironment->threads); ++i)
                    {
                        const auto& thread = *unit.cobEnvironment->threads[i];
                        ImGui::Text("%lld: %s (%u)", i, thread.name.c_str(), thread.signalMask);
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Ready"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->readyQueue); ++i)
                    {
                        ImGui::Text("%s", unit.cobEnvironment->readyQueue[i]->name.c_str());
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Blocked"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->blockedQueue); ++i)
                    {
                        const auto& pair = unit.cobEnvironment->blockedQueue[i];
                        ImGui::Text("%s, %s", pair.second->name.c_str(), blockedStatusToString(pair.first).c_str());
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Sleeping"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->sleepingQueue); ++i)
                    {
                        const auto& pair = unit.cobEnvironment->sleepingQueue[i];
                        ImGui::Text("%s, wake time: %d", pair.second->name.c_str(), pair.first.value);
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Finished"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->finishedQueue); ++i)
                    {
                        ImGui::Text("%s", unit.cobEnvironment->finishedQueue[i]->name.c_str());
                    }
                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }
        }
    }

    void GameScene::renderDebugWindow()
    {
        if (!showDebugWindow)
        {
            return;
        }

        ImGui::Begin("Game Debug", &showDebugWindow);
        ImGui::Checkbox("Health bars", &healthBarsVisible);
        ImGui::Checkbox("Fog of war", &fogOfWarEnabled);

        if (!simulation.aiControllers.empty() && ImGui::CollapsingHeader("AI players"))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                auto it = simulation.aiControllers.find(PlayerId(i));
                if (it == simulation.aiControllers.end() || !it->second)
                {
                    continue;
                }
                const auto& ai = *it->second;
                const auto& bb = ai.getBlackboard();
                ImGui::Text("Player %d (%s)", static_cast<int>(i), aiDifficultyName(ai.getProfile().difficulty));
                ImGui::Text("  phase: %s", gamePhaseName(bb.phase));
                ImGui::Text("  metal %.0f/%.0f%s  energy %.0f/%.0f%s",
                    bb.currentMetal.value, bb.metalStorage.value, bb.metalStalled ? " (stalled)" : "",
                    bb.currentEnergy.value, bb.energyStorage.value, bb.energyStalled ? " (stalled)" : "");
                ImGui::Text("  builders idle %d, factories %d, army %d, scout %s", bb.idleBuilderCount, static_cast<int>(bb.factories.size()), bb.armySize, bb.scoutUnitId ? "yes" : "no");
                ImGui::Text("  known enemies %d, near base %d, enemy base %s", static_cast<int>(bb.knownEnemies.size()), static_cast<int>(bb.enemiesNearBase.size()), bb.enemyBasePosition ? "known" : "unknown");
                if (bb.attackTarget)
                {
                    ImGui::Text("  attacking %.0f, %.0f", bb.attackTarget->x.value, bb.attackTarget->z.value);
                }
                for (const auto& [type, count] : bb.ownedTotalCounts)
                {
                    ImGui::Text("    %s x%d", type.c_str(), count);
                }
            }
        }
        if (ImGui::Checkbox("GUI", &guiVisible))
        {
            if (guiVisible)
            {
                worldViewport.setInset(GuiSizeLeft, GuiSizeTop, GuiSizeRight, GuiSizeBottom);
            }
            else
            {
                worldViewport.setInset(0, 0, 0, 0);
            }
            recreateWorldRenderTextures();
        }
        ImGui::Separator();
        ImGui::Checkbox("Cursor terrain dot", &cursorTerrainDotVisible);
        ImGui::Checkbox("Occupied grid", &occupiedGridVisible);
        ImGui::Checkbox("Pathfinding visualisation", &pathfindingVisualisationVisible);
        ImGui::Checkbox("Movement class grid", &movementClassGridVisible);
        ImGui::Separator();
        renderUnitPlacer();
        ImGui::Separator();
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            ImGui::LabelText("Unit sounds", "%lld", getSize(playingUnitChannels));
            ImGui::LabelText("Sound volume", "%d", computeSoundVolume(getSize(playingUnitChannels)));
        }

        if (ImGui::CollapsingHeader("Selected Unit"))
        {
            ImGui::Indent();
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                const auto& unit = getUnit(*selectedUnit);
                renderUnitInfoSection(unit);
            }
            else
            {
                ImGui::Text("None");
            }
            ImGui::Unindent();
        }

        auto mouseTerrainCoordinate = getMouseTerrainCoordinate();

        if (mouseTerrainCoordinate)
        {
            ImGui::LabelText("mouse terrain x", "%f", mouseTerrainCoordinate->x.value);
            ImGui::LabelText("mouse terrain y", "%f", mouseTerrainCoordinate->y.value);
            ImGui::LabelText("mouse terrain z", "%f", mouseTerrainCoordinate->z.value);
        }

        ImGui::End();
    }

    void GameScene::onKeyDown(const SDL_KeyboardEvent& keysym)
    {
        // The game menu owns the keyboard while it is up. Tab and F2 toggle
        // it (the original's keys: Tab opens GAME OPTIONS in single player,
        // F2 anywhere) and Escape closes it.
        if (keysym.key == SDLK_TAB || keysym.key == SDLK_F2)
        {
            toggleGameMenu();
            return;
        }
        if (isGameMenuOpen())
        {
            if (keysym.key == SDLK_ESCAPE)
            {
                closeGameMenu();
                return;
            }
            for (auto& panel : gameMenuPanels)
            {
                panel->keyDown(KeyEvent(keysym.key));
            }
            return;
        }

        // Suppress UI panel key activation when Ctrl is held — Ctrl+letter is
        // hotkey territory (Ctrl+A select-all, Ctrl+S stop, Ctrl+D self-destruct,
        // etc.), and the panel's letter-bound buttons (e.g. ATTACK on plain "A")
        // would otherwise also fire.
        if (!isCtrlDown())
        {
            currentPanel->keyDown(KeyEvent(keysym.key));
        }

        if (keysym.key == SDLK_UP)
        {
            up = true;
        }
        else if (keysym.key == SDLK_DOWN)
        {
            down = true;
        }
        else if (keysym.key == SDLK_LEFT)
        {
            left = true;
        }
        else if (keysym.key == SDLK_RIGHT)
        {
            right = true;
        }
        else if (keysym.key == SDLK_LCTRL)
        {
            leftCtrlDown = true;
        }
        else if (keysym.key == SDLK_RCTRL)
        {
            rightCtrlDown = true;
        }
        else if (keysym.key == SDLK_LSHIFT)
        {
            leftShiftDown = true;
        }
        else if (keysym.key == SDLK_RSHIFT)
        {
            rightShiftDown = true;
        }
        else if (keysym.key == SDLK_ESCAPE)
        {
            handleEscapeDown();
        }
        else if (keysym.key == SDLK_F10)
        {
            showDebugWindow = !showDebugWindow;
        }
        else if (keysym.key == SDLK_F1)
        {
            helpVisible = !helpVisible;
        }
        else if (keysym.scancode == SDL_SCANCODE_GRAVE)
        {
            healthBarsVisible = !healthBarsVisible;
        }
        else if (keysym.key == SDLK_T)
        {
            startTrack();
        }
        else if (keysym.key == SDLK_C)
        {
            if (isCtrlDown())
            {
                const auto& localSideData = sceneContext.sideData->at(getPlayer(localPlayerId).side);
                if (!isShiftDown())
                {
                    clearUnitSelection();
                }

                // Select commanders (edge case: debug mode allows spawning multiple commanders)
                std::optional<UnitId> commanderUnitId;
                for (const auto& [unitId, unit] : simulation.units)
                {
                    const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                    if (unitDefinition.commander && unit.isOwnedBy(localPlayerId))
                    {
                        selectAdditionalUnit(unitId);
                        // For multiple commanders, OTA selects all but always tracks only the last spawned (it won't cycle with repeated ctrl-c)
                        commanderUnitId = unitId;
                    }
                }

                if (commanderUnitId)
                {
                    startTrackInternal({*commanderUnitId});
                }
            }
        }
        else if (keysym.key == SDLK_EQUALS || keysym.key == SDLK_KP_PLUS)
        {
            // Speed up: locally-issued, host-authoritatively applied via lockstep.
            // Any client may emit; processPlayerCommand drops it unless issued by host.
            localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{gameSpeed.increased().index()});
        }
        else if (keysym.key == SDLK_MINUS || keysym.key == SDLK_KP_MINUS)
        {
            // Slow down: see SDLK_EQUALS comment.
            localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{gameSpeed.decreased().index()});
        }
        else if (keysym.key == SDLK_PAUSE)
        {
            // Toggle the local paused flag immediately so the tick loop can
            // resume on unpause; the lockstep-routed command handler is what
            // drives the tick loop and would otherwise never run while paused.
            // Pause/unpause is scene state (not deterministic sim state), so
            // toggling locally is fine; the command still goes through the
            // command stream so peers stay in sync.
            paused = !paused;
            if (paused)
            {
                localPlayerCommandBuffer.push_back(PlayerPauseGameCommand{});
            }
            else
            {
                localPlayerCommandBuffer.push_back(PlayerUnpauseGameCommand{});
            }
        }
        else if (keysym.key == SDLK_A && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+A: select all own units visible on screen.
            selectAllOnScreen();
        }
        else if (keysym.key == SDLK_S && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+S: stop all selected units.
            // Routes through the deterministic command queue so MP peers
            // see the same stop in the same tick.
            cursorMode.next(NormalCursorMode());
            for (const auto& unitId : selectedUnits)
            {
                localPlayerStopUnit(unitId);
            }
        }
        else if (keysym.key == SDLK_D && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+D: self-destruct selected units (TA behaviour).
            // Routes through the deterministic command queue so the
            // explosion happens at the same game tick on all peers.
            unsigned int toggled = 0;
            for (const auto& unitId : selectedUnits)
            {
                const auto& unit = tryGetUnit(unitId);
                if (unit && unit->get().isAlive() && unit->get().isOwnedBy(localPlayerId))
                {
                    localPlayerSelfDestructUnit(unitId);
                    ++toggled;
                }
            }
            LOG_INFO << "Self-destruct toggled for " << toggled << " selected unit(s)";
        }
        else if (keysym.key == SDLK_Z && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+Z: enter attack-ground cursor mode.
            // The next left-click on the terrain issues an AttackOrder targeting
            // the ground coordinate (handled by the AttackCursorMode mouse handler).
            if (sounds.specialOrders)
            {
                playUiSound(*sounds.specialOrders);
            }
            if (std::holds_alternative<AttackCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(AttackCursorMode());
            }
        }
        else if (keysym.key == SDLK_W && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+W: guard/defend cursor mode.
            // TA's "wait" order is not a separate sim order type in RWE;
            // the closest equivalent is the guard/defend mode.
            if (sounds.specialOrders)
            {
                playUiSound(*sounds.specialOrders);
            }
            if (std::holds_alternative<GuardCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(GuardCursorMode());
            }
        }
        else if (keysym.key == SDLK_F && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+F: fight (move-attack) cursor mode.
            // RWE does not have a dedicated FightOrder type yet; the attack
            // cursor mode is the closest available analogue.
            if (sounds.specialOrders)
            {
                playUiSound(*sounds.specialOrders);
            }
            if (std::holds_alternative<AttackCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(AttackCursorMode());
            }
        }
        else if (keysym.key == SDLK_P && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+P: patrol cursor mode.
            // RWE does not have a dedicated PatrolOrder type yet; the move
            // cursor mode is the closest available analogue (issues a MoveOrder
            // when the destination is clicked).
            if (sounds.specialOrders)
            {
                playUiSound(*sounds.specialOrders);
            }
            if (std::holds_alternative<MoveCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(MoveCursorMode());
            }
        }
        else
        {
            // Control groups: keys 1-0 map to groups 0-9.
            // Ctrl+digit  → bind current selection to group (replace).
            // Shift+digit → add current selection to group.
            // Digit alone → recall group (replace current selection).
            // Ctrl+Shift+digit is treated the same as Shift+digit (add).
            std::optional<int> groupIndex;
            if (keysym.key >= SDLK_1 && keysym.key <= SDLK_9)
            {
                groupIndex = keysym.key - SDLK_1; // 0-8
            }
            else if (keysym.key == SDLK_0)
            {
                groupIndex = 9; // '0' maps to group index 9
            }

            if (groupIndex)
            {
                auto idx = *groupIndex;
                if (isCtrlDown() && isShiftDown())
                {
                    // Ctrl+Shift+digit: add selection to control group.
                    for (const auto& unitId : selectedUnits)
                    {
                        controlGroups[idx].insert(unitId);
                    }
                }
                else if (isCtrlDown())
                {
                    // Ctrl+digit: bind (replace) control group with current selection.
                    controlGroups[idx] = selectedUnits;
                }
                else if (isShiftDown())
                {
                    // Shift+digit: add current selection to control group.
                    for (const auto& unitId : selectedUnits)
                    {
                        controlGroups[idx].insert(unitId);
                    }
                }
                else
                {
                    // Digit alone: recall control group.
                    // Filter out any units that are now dead or no longer owned
                    // by the local player so stale IDs do not pollute the set.
                    std::unordered_set<UnitId> liveUnits;
                    for (const auto& unitId : controlGroups[idx])
                    {
                        auto unitRef = tryGetUnit(unitId);
                        if (unitRef && unitRef->get().isAlive() && unitRef->get().isOwnedBy(localPlayerId))
                        {
                            liveUnits.insert(unitId);
                        }
                    }
                    // Prune the stored group to remove dead entries.
                    controlGroups[idx] = liveUnits;
                    replaceUnitSelection(liveUnits);
                }
            }
        }
    }

    void GameScene::onKeyUp(const SDL_KeyboardEvent& keysym)
    {
        currentPanel->keyUp(KeyEvent(keysym.key));

        if (keysym.key == SDLK_UP)
        {
            up = false;
        }
        else if (keysym.key == SDLK_DOWN)
        {
            down = false;
        }
        else if (keysym.key == SDLK_LEFT)
        {
            left = false;
        }
        else if (keysym.key == SDLK_RIGHT)
        {
            right = false;
        }
        else if (keysym.key == SDLK_LCTRL)
        {
            leftCtrlDown = false;
        }
        else if (keysym.key == SDLK_RCTRL)
        {
            rightCtrlDown = false;
        }
        else if (keysym.key == SDLK_LSHIFT)
        {
            leftShiftDown = false;
        }
        else if (keysym.key == SDLK_RSHIFT)
        {
            rightShiftDown = false;
        }
    }

    void GameScene::onMouseDown(MouseButtonEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseDown(event);
            }
            return;
        }

        currentPanel->mouseDown(event);

        // Debug placing mode: clicks drop units on the map instead of
        // selecting and ordering, so a test scenario can be set up quickly.
        if (unitSpawnOnClick && !unitSpawnType.empty() && isValidUnitType(simulation, unitSpawnType))
        {
            if (event.button == MouseButtonEvent::MouseButton::Left)
            {
                if (auto terrainPos = getMouseTerrainCoordinate())
                {
                    placeDebugUnit(*terrainPos);
                }
                return;
            }
            if (event.button == MouseButtonEvent::MouseButton::Right)
            {
                // Right-click leaves placing mode, like cancelling any other cursor mode.
                unitSpawnOnClick = false;
                return;
            }
        }

        if (event.button == MouseButtonEvent::MouseButton::Left)
        {
            match(
                cursorMode.getValue(),
                [&](const AttackCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                        else
                        {
                            auto coord = getMouseTerrainCoordinate();
                            if (coord)
                            {
                                if (isShiftDown())
                                {
                                    localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*coord));
                                }
                                else
                                {
                                    localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*coord));
                                    cursorMode.next(NormalCursorMode());
                                }
                            }
                        }
                    }
                },
                [&](const MoveCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        auto coord = getMouseTerrainCoordinate();
                        if (coord)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const GuardCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isFriendly(*hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, GuardOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, GuardOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const CaptureCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isEnemy(*hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, CaptureOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, CaptureOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const LoadCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isFriendly(*hoveredUnit) && *hoveredUnit != selectedUnit)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, LoadOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, LoadOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const UnloadCursorMode&) {
                    auto coord = getMouseTerrainCoordinate();
                    if (coord)
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, UnloadOrder(*coord));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, UnloadOrder(*coord));
                            }
                        }
                        if (!isShiftDown())
                        {
                            cursorMode.next(NormalCursorMode());
                        }
                    }
                },
                [&](const PatrolCursorMode&) {
                    auto coord = getMouseTerrainCoordinate();
                    if (coord)
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, PatrolOrder(*coord));
                            }
                            else
                            {
                                // A fresh patrol loops between the clicked point
                                // and wherever the unit is standing now.
                                localPlayerIssueUnitOrder(selectedUnit, PatrolOrder(*coord));
                                localPlayerEnqueueUnitOrder(selectedUnit, PatrolOrder(getUnit(selectedUnit).position));
                            }
                        }
                        if (!isShiftDown())
                        {
                            cursorMode.next(NormalCursorMode());
                        }
                    }
                },
                [&](const RepairCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isFriendly(*hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const ReclaimCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                        else if (hoveredFeature)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const BuildCursorMode& buildCursor) {
                    if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
                    {
                        if (hoverBuildInfo)
                        {
                            if (hoverBuildInfo->isValid)
                            {
                                auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(hoverBuildInfo->rect.x,
                                    hoverBuildInfo->rect.y);
                                auto x = topLeftWorld.x + ((SimScalar(hoverBuildInfo->rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss);
                                auto z = topLeftWorld.z + ((SimScalar(hoverBuildInfo->rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss);
                                auto y = simulation.terrain.getHeightAt(x, z);
                                SimVector buildPos(x, y, z);
                                if (isShiftDown())
                                {
                                    // Shift-clicking a building already in the plan takes it out again.
                                    if (auto planned = plannedBuildOrderAt(*selectedUnit, buildPos))
                                    {
                                        localPlayerCancelBuildOrder(*selectedUnit, *planned);
                                    }
                                    else
                                    {
                                        localPlayerEnqueueUnitOrder(*selectedUnit, BuildOrder(buildCursor.unitType, buildPos));
                                    }
                                }
                                else
                                {
                                    localPlayerIssueUnitOrder(*selectedUnit, BuildOrder(buildCursor.unitType, buildPos));
                                    cursorMode.next(NormalCursorMode());
                                }
                            }
                            else if (isShiftDown() && getMouseTerrainCoordinate())
                            {
                                // The spot is blocked by our own plan: shift-click removes that plan.
                                if (auto planned = plannedBuildOrderAt(*selectedUnit, *getMouseTerrainCoordinate()))
                                {
                                    localPlayerCancelBuildOrder(*selectedUnit, *planned);
                                }
                                else if (sounds.notOkToBuild)
                                {
                                    playUiSound(*sounds.notOkToBuild);
                                }
                            }
                            else
                            {
                                if (sounds.notOkToBuild)
                                {
                                    playUiSound(*sounds.notOkToBuild);
                                }
                            }
                        }
                    }
                },
                [&](const NormalCursorMode&) {
                    if (isCursorOverMinimap())
                    {
                        if (leftClickMode())
                        {
                            for (const auto& selectedUnit : selectedUnits)
                            {
                                if (hoveredUnit)
                                {
                                    if (isEnemy(*hoveredUnit))
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                        }
                                    }
                                    else
                                    {
                                        if (const auto& u = getUnit(*hoveredUnit); u.isBeingBuilt(simulation.unitDefinitions.at(u.unitType)))
                                        {
                                            if (isShiftDown())
                                            {
                                                localPlayerEnqueueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                            }
                                            else
                                            {
                                                localPlayerIssueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    auto coord = getMouseTerrainCoordinate();
                                    if (coord)
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::DraggingMinimapState()});
                        }
                    }
                    else if (isCursorOverWorld())
                    {
                        Point p(event.x, event.y);
                        auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, p);
                        const auto cameraPosition = worldCameraState.getRoundedPosition();
                        Point originRelativePos(cameraPosition.x + worldViewportPos.x, cameraPosition.z + worldViewportPos.y);
                        cursorMode.next(NormalCursorMode{NormalCursorMode::SelectingState(sceneTime, originRelativePos)});
                    }
                });
        }
        else if (event.button == MouseButtonEvent::MouseButton::Right)
        {
            match(
                cursorMode.getValue(),
                [&](const AttackCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const MoveCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const GuardCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const ReclaimCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const RepairCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const PatrolCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const CaptureCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const LoadCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const UnloadCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const BuildCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const NormalCursorMode&) {
                    if (leftClickMode())
                    {
                        if (isCursorOverMinimap())
                        {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::DraggingMinimapState()});
                        }
                        else if (isCursorOverWorld())
                        {
                            clearUnitSelection();
                        }
                    }
                    else
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (hoveredUnit)
                            {
                                if (isEnemy(*hoveredUnit))
                                {
                                    if (isShiftDown())
                                    {
                                        localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                    }
                                    else
                                    {
                                        localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                    }
                                }
                                else
                                {
                                    if (const auto& u = getUnit(*hoveredUnit); u.isBeingBuilt(simulation.unitDefinitions.at(u.unitType)))
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                        }
                                    }
                                }
                            }
                            else if (hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                            {
                                if (isShiftDown())
                                {
                                    localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                }
                                else
                                {
                                    localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                }
                            }
                            else
                            {
                                auto coord = getMouseTerrainCoordinate();
                                if (coord)
                                {
                                    if (isShiftDown())
                                    {
                                        localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                                    }
                                    else
                                    {
                                        localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                    }
                                }
                            }
                        }
                    }
                });
        }
        else if (event.button == MouseButtonEvent::MouseButton::Middle)
        {
            cameraControlState = CameraControlStateMiddleMousePan{getMousePosition()};
        }
    }

    void GameScene::onMouseUp(MouseButtonEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseUp(event);
            }
            return;
        }

        currentPanel->mouseUp(event);

        if (event.button == MouseButtonEvent::MouseButton::Left)
        {
            match(
                cursorMode.getValue(),
                [&](const NormalCursorMode& normalCursor) {
                    match(
                        normalCursor.state,
                        [&](const NormalCursorMode::SelectingState& state) {
                            Point p(event.x, event.y);
                            auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, p);
                            const auto cameraPosition = worldCameraState.getRoundedPosition();
                            Point originRelativePos(cameraPosition.x + worldViewportPos.x, cameraPosition.z + worldViewportPos.y);

                            if (sceneTime - state.startTime < SceneTime(30) && state.startPosition.maxSingleDimensionDistance(originRelativePos) < 32)
                            {
                                if (hoveredUnit && getUnit(*hoveredUnit).isSelectableBy(simulation.unitDefinitions.at(getUnit(*hoveredUnit).unitType), localPlayerId))
                                {
                                    if (isShiftDown())
                                    {
                                        toggleUnitSelection(*hoveredUnit);
                                    }
                                    else
                                    {
                                        replaceUnitSelection(*hoveredUnit);
                                    }
                                }
                                else if (leftClickMode() && hoveredUnit)
                                {
                                    if (isEnemy(*hoveredUnit))
                                    {
                                        for (const auto& selectedUnit : selectedUnits)
                                        {
                                            if (isShiftDown())
                                            {
                                                localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                            }
                                            else
                                            {
                                                localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (const auto& u = getUnit(*hoveredUnit); u.isBeingBuilt(simulation.unitDefinitions.at(u.unitType)))
                                        {
                                            for (const auto& selectedUnit : selectedUnits)
                                            {
                                                if (isShiftDown())
                                                {
                                                    localPlayerEnqueueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                                }
                                                else
                                                {
                                                    localPlayerIssueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                                }
                                            }
                                        }
                                        else if (unitIsDamaged(simulation, *hoveredUnit))
                                        {
                                            for (const auto& selectedUnit : selectedUnits)
                                            {
                                                if (!unitIsBuilder(simulation, selectedUnit))
                                                {
                                                    continue;
                                                }
                                                if (isShiftDown())
                                                {
                                                    localPlayerEnqueueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                                                }
                                                else
                                                {
                                                    localPlayerIssueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                                                }
                                            }
                                        }
                                    }
                                }
                                else if (leftClickMode() && hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                                {
                                    for (const auto& selectedUnit : selectedUnits)
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                        }
                                    }
                                }
                                else if (leftClickMode())
                                {
                                    auto coord = getMouseTerrainCoordinate();
                                    if (coord)
                                    {
                                        for (const auto& selectedUnit : selectedUnits)
                                        {
                                            if (isShiftDown())
                                            {
                                                localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                                            }
                                            else
                                            {
                                                localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    if (!isShiftDown())
                                    {
                                        clearUnitSelection();
                                    }
                                }
                            }
                            else
                            {
                                selectUnitsInBandbox(DiscreteRect::fromPoints(state.startPosition, originRelativePos));
                            }

                            cursorMode.next(NormalCursorMode{NormalCursorMode::UpState()});
                        },
                        [&](const NormalCursorMode::DraggingMinimapState&) {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::UpState()});
                        },
                        [&](const NormalCursorMode::UpState&) {
                        });
                },
                [&](const auto&) {
                    // do nothing
                });
        }
        else if (event.button == MouseButtonEvent::MouseButton::Right)
        {
            match(
                cursorMode.getValue(),
                [&](const NormalCursorMode& m) {
                    match(
                        m.state,
                        [&](const NormalCursorMode::DraggingMinimapState&) {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::UpState()});
                        },
                        [](const auto&) {});
                },
                [](const auto&) {});
        }
        else if (event.button == MouseButtonEvent::MouseButton::Middle)
        {
            if (std::holds_alternative<CameraControlStateMiddleMousePan>(cameraControlState))
            {
                cameraControlState = CameraControlStateFree();
            }
        }
    }

    Rectangle2f computeCameraConstraint(const MapTerrain& terrain, float viewportWidth, float viewportHeight)
    {
        auto cameraHalfWidth = viewportWidth / 2.0f;
        auto cameraHalfHeight = viewportHeight / 2.0f;

        auto top = simScalarToFloat(terrain.topInWorldUnits()) + cameraHalfHeight;
        auto left = simScalarToFloat(terrain.leftInWorldUnits()) + cameraHalfWidth;
        auto bottom = simScalarToFloat(terrain.bottomCutoffInWorldUnits()) - cameraHalfHeight;
        auto right = simScalarToFloat(terrain.rightCutoffInWorldUnits()) - cameraHalfWidth;

        if (left > right)
        {
            auto middle = (left + right) / 2.0f;
            left = middle;
            right = middle;
        }

        if (top > bottom)
        {
            auto middle = (top + bottom) / 2.0f;
            top = middle;
            bottom = middle;
        }

        return Rectangle2f::fromTLBR(top, left, bottom, right);
    }

    void GameScene::onMouseMove(MouseMoveEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseMove(event);
            }
            return;
        }

        if (auto middleMousePanningState = std::get_if<CameraControlStateMiddleMousePan>(&cameraControlState); middleMousePanningState)
        {
            auto cameraConstraint = computeCameraConstraint(simulation.terrain, worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()));

            const auto& cameraPos = worldCameraState.position;

            auto currentCursorPosition = Point(event.x, event.y);
            auto delta = currentCursorPosition - middleMousePanningState->previousCursorPosition;

            auto newCameraPos = cameraConstraint.clamp(Vector2f(cameraPos.x - delta.x, cameraPos.z - delta.y));
            worldCameraState.position = Vector3f(newCameraPos.x, cameraPos.y, newCameraPos.y);

            middleMousePanningState->previousCursorPosition = currentCursorPosition;
        }
        currentPanel->mouseMove(event);
    }

    void GameScene::onMouseWheel(MouseWheelEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseWheel(event);
            }
            return;
        }

        currentPanel->mouseWheel(event);
    }

    void GameScene::update(int millisecondsElapsed)
    {
        updateMusic();

        // Pause halts simulation tick dispatch by not advancing the
        // scaled-time accumulator. Speed scales the accumulator using
        // integer arithmetic to keep determinism friendly: at perMille
        // == 1000 we accumulate 1ms per real ms; at 100 we accumulate
        // 0.1ms per real ms; at 5000 we accumulate 5ms per real ms.
        // The sim tick threshold (SimMillisecondsPerTick) is unchanged.
        if (!paused)
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
                [&](const MoveCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Move);
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
                    sceneContext.cursor->useCursor(CursorType::Load);
                },
                [&](const UnloadCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Unload);
                },
                [&](const BuildCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Normal);
                },
                [&](const NormalCursorMode&) {
                    if (leftClickMode())
                    {
                        if (hoveredUnit && unitIsSelectableBy(simulation, *hoveredUnit, localPlayerId))
                        {
                            sceneContext.cursor->useCursor(CursorType::Select);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanAttack(simulation, id); })
                            && hoveredUnit && isEnemy(*hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Attack);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); })
                            && hoveredUnit && isFriendly(*hoveredUnit) && (unitIsBeingBuilt(simulation, *hoveredUnit) || unitIsDamaged(simulation, *hoveredUnit)))
                        {
                            sceneContext.cursor->useCursor(CursorType::Repair);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); }) && hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                        {
                            sceneContext.cursor->useCursor(CursorType::Reclaim);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanMove(simulation, id); }))
                        {
                            sceneContext.cursor->useCursor(CursorType::Move);
                        }
                        else
                        {
                            sceneContext.cursor->useCursor(CursorType::Normal);
                        }
                    }
                    else
                    {
                        if (hoveredUnit && unitIsSelectableBy(simulation, *hoveredUnit, localPlayerId))
                        {
                            sceneContext.cursor->useCursor(CursorType::Select);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanAttack(simulation, id); })
                            && hoveredUnit && isEnemy(*hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Red);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); })
                            && hoveredUnit && isFriendly(*hoveredUnit) && unitIsBeingBuilt(simulation, *hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Green);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanGuard(simulation, id); })
                            && hoveredUnit && isFriendly(*hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Green);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); }) && hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                        {
                            sceneContext.cursor->useCursor(CursorType::Green);
                        }
                        else
                        {
                            sceneContext.cursor->useCursor(CursorType::Normal);
                        }
                    }
                });
        }

        auto maxRtt = std::clamp(gameNetworkService->getMaxAverageRttMillis(), 16.0f, 2000.0f);
        auto highCommandLatencyMillis = maxRtt + (maxRtt / 4.0f) + 200.0f;
        auto commandLatencyFrames = static_cast<unsigned int>(highCommandLatencyMillis / 16.0f) + 1;
        auto targetCommandBufferSize = commandLatencyFrames;

        auto bufferedCommandCount = playerCommandService->bufferedCommandCount(localPlayerId);

        LOG_DEBUG << "Buffer levels (real/target) " << bufferedCommandCount << "/" << targetCommandBufferSize;

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

        // If we are waiting to swap in a new unit GUI panel, do that now
        if (nextPanel)
        {
            currentPanel = std::move(*nextPanel);
            nextPanel = std::nullopt;
            attachOrdersMenuEventHandlers();
        }

        auto averageSceneTime = gameNetworkService->estimateAvergeSceneTime(sceneTime);

        // allow skipping sim frames every so often to get back down to average.
        // We tolerate X frames of drift in either direction to cope with noisiness in the estimation.
        const SceneTime frameTolerance(3);
        const SceneTime frameCheckInterval(5);
        auto highSceneTime = averageSceneTime + frameTolerance;
        auto lowSceneTime = averageSceneTime <= frameTolerance ? SceneTime{0} : averageSceneTime - frameTolerance;
        // Cap the number of sim ticks we dispatch per frame to prevent
        // a runaway "spiral of death" if frame times spike at high speeds.
        const int maxTicksPerFrame = 10;
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

        // A launcher's magazine fills without anybody ordering anything, so its
        // readout cannot be refreshed off a command the way a build queue's is.
        refreshStockpileGuiTotal();

        renderDebugWindow();
    }

    std::optional<UnitId> GameScene::spawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<const std::reference_wrapper<SimAngle>> rotation)
    {
        return simulation.trySpawnUnit(unitType, owner, position, rotation);
    }

    void GameScene::placeDebugUnit(const SimVector& position)
    {
        if (unitSpawnType.empty() || !isValidUnitType(simulation, unitSpawnType))
        {
            return;
        }
        if (unitSpawnPlayer < 0 || unitSpawnPlayer >= getSize(simulation.players))
        {
            return;
        }

        // The list offers every unit type the data defines, and some of those
        // name a model or a script that did not load. Spawning one of those
        // would take the game down, so refuse it here: this is a tool for
        // poking at units, not a way to crash out of a game.
        const auto& unitDefinition = simulation.unitDefinitions.at(unitSpawnType);
        if (simulation.unitModelDefinitions.find(unitDefinition.objectName) == simulation.unitModelDefinitions.end())
        {
            LOG_WARN << "Cannot place " << unitSpawnType << ": its model " << unitDefinition.objectName << " is not loaded";
            return;
        }
        if (simulation.unitScriptDefinitions.find(unitSpawnType) == simulation.unitScriptDefinitions.end())
        {
            LOG_WARN << "Cannot place " << unitSpawnType << ": its script is not loaded";
            return;
        }

        auto owner = PlayerId(unitSpawnPlayer);
        if (unitSpawnComplete)
        {
            spawnCompletedUnit(unitSpawnType, owner, position);
        }
        else
        {
            // Left as a nanoframe, so a builder can be told to finish it.
            spawnUnit(unitSpawnType, owner, position, std::nullopt);
        }
    }

    void GameScene::renderUnitPlacer()
    {
        if (!ImGui::CollapsingHeader("Place units"))
        {
            return;
        }
        ImGui::Indent();

        if (allUnitTypes.empty())
        {
            for (const auto& [unitType, _] : simulation.unitDefinitions)
            {
                allUnitTypes.push_back(unitType);
            }
            std::sort(allUnitTypes.begin(), allUnitTypes.end());
        }

        // Owner: every player in the game, named with its side so it is
        // obvious which team a unit will fight for.
        std::string ownerLabel = "none";
        if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
        {
            const auto& player = simulation.players[unitSpawnPlayer];
            ownerLabel = std::to_string(unitSpawnPlayer) + ": " + player.side
                + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                + (PlayerId(unitSpawnPlayer) == localPlayerId ? " [you]" : "");
        }
        if (ImGui::BeginCombo("Owner", ownerLabel.c_str()))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                const auto& player = simulation.players[i];
                auto label = std::to_string(i) + ": " + player.side
                    + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                    + (PlayerId(i) == localPlayerId ? " [you]" : "");
                if (ImGui::Selectable(label.c_str(), unitSpawnPlayer == static_cast<int>(i)))
                {
                    unitSpawnPlayer = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filter", unitSpawnFilter, IM_ARRAYSIZE(unitSpawnFilter));
        auto filter = toUpper(std::string(unitSpawnFilter));

        ImGui::BeginChild("unit type list", ImVec2(0.0f, 160.0f), true);
        for (const auto& unitType : allUnitTypes)
        {
            if (!filter.empty() && unitType.find(filter) == std::string::npos)
            {
                continue;
            }
            if (ImGui::Selectable(unitType.c_str(), unitType == unitSpawnType))
            {
                unitSpawnType = unitType;
            }
        }
        ImGui::EndChild();

        ImGui::LabelText("Selected", "%s", unitSpawnType.empty() ? "none" : unitSpawnType.c_str());
        ImGui::Checkbox("Place on click (right-click to stop)", &unitSpawnOnClick);
        ImGui::Checkbox("Place finished (off: place a nanoframe)", &unitSpawnComplete);

        if (ImGui::Button("Place one at the cursor"))
        {
            if (auto terrainPos = getMouseTerrainCoordinate())
            {
                placeDebugUnit(*terrainPos);
            }
        }

        // The old typed entry, kept for when the name is already known.
        if (ImGui::InputText("Type a name and press enter", unitSpawnText, IM_ARRAYSIZE(unitSpawnText), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            auto text = toUpper(std::string(unitSpawnText));
            if (!text.empty() && isValidUnitType(simulation, text))
            {
                unitSpawnType = text;
                if (auto terrainPos = getMouseTerrainCoordinate())
                {
                    placeDebugUnit(*terrainPos);
                }
            }
            ImGui::SetKeyboardFocusHere(-1);
        }

        if (ImGui::Button("Kill every unit of the chosen owner"))
        {
            if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
            {
                std::vector<UnitId> doomed;
                for (const auto& [unitId, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(PlayerId(unitSpawnPlayer)) && unit.isAlive())
                    {
                        doomed.push_back(unitId);
                    }
                }
                for (auto unitId : doomed)
                {
                    simulation.killUnit(unitId);
                }
            }
        }

        ImGui::Unindent();
    }

    std::optional<std::reference_wrapper<UnitState>> GameScene::spawnCompletedUnit(const std::string& unitType, PlayerId owner, const SimVector& position)
    {
        auto unitId = spawnUnit(unitType, owner, position, std::nullopt);
        if (unitId)
        {
            auto& unit = getUnit(*unitId);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            // units start as unbuilt nanoframes,
            // we we need to convert it immediately into a completed unit.
            unit.finishBuilding(unitDefinition);

            return unit;
        }

        return std::nullopt;
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

    void GameScene::playUiSound(const AudioService::SoundHandle& handle)
    {
        sceneContext.audioService->playSoundIfFree(handle, UnitSelectChannel);
    }

    void GameScene::playNotificationSound(const PlayerId& playerId, const AudioService::SoundHandle& sound)
    {
        if (playerId == localPlayerId)
        {
            sceneContext.audioService->playSoundIfFree(sound, UnitSelectChannel);
        }
    }


    std::optional<std::string> getSoundName(const SoundClass& c, UnitSoundType sound)
    {
        switch (sound)
        {
            case UnitSoundType::Select1:
                return c.select1;
            case UnitSoundType::UnitComplete:
                return c.unitComplete;
            case UnitSoundType::Activate:
                return c.activate;
            case UnitSoundType::Deactivate:
                return c.deactivate;
            case UnitSoundType::Ok1:
                return c.ok1;
            case UnitSoundType::Arrived1:
                return c.arrived1;
            case UnitSoundType::Cant1:
                return c.cant1;
            case UnitSoundType::UnderAttack:
                return c.underAttack;
            case UnitSoundType::Build:
                return c.build;
            case UnitSoundType::Repair:
                return c.repair;
            case UnitSoundType::Working:
                return c.working;
            case UnitSoundType::Cloak:
                return c.cloak;
            case UnitSoundType::Uncloak:
                return c.uncloak;
            case UnitSoundType::Capture:
                return c.capture;
            case UnitSoundType::Count5:
                return c.count5;
            case UnitSoundType::Count4:
                return c.count4;
            case UnitSoundType::Count3:
                return c.count3;
            case UnitSoundType::Count2:
                return c.count2;
            case UnitSoundType::Count1:
                return c.count1;
            case UnitSoundType::Count0:
                return c.count0;
            case UnitSoundType::CancelDestruct:
                return c.cancelDestruct;
            default:
                throw std::logic_error("Invalid sound type");
        }
    }

    std::optional<AudioService::SoundHandle> getSound(const GameSimulation& sim, const GameMediaDatabase& meshDb, const std::string& unitType, UnitSoundType soundType)
    {
        const auto& unitDefinition = sim.unitDefinitions.at(unitType);
        const auto& soundClass = meshDb.getSoundClassOrDefault(unitDefinition.soundCategory);
        const auto& soundId = getSoundName(soundClass, soundType);
        if (soundId)
        {
            return meshDb.tryGetSoundHandle(*soundId);
        }
        return std::nullopt;
    }

    void GameScene::playUnitNotificationSound(const PlayerId& playerId, const std::string& unitType, UnitSoundType soundType)
    {
        auto sound = getSound(simulation, gameMediaDatabase, unitType, soundType);
        if (sound)
        {
            playNotificationSound(playerId, *sound);
        }
    }

    namespace
    {
        /**
         * TA's ten player colours, read off the shipped palette by eye --
         * close enough for tinting a line of text.
         */
        Color playerColorToRgb(const PlayerColorIndex& index)
        {
            static const Color colors[] = {
                Color(60, 88, 244),   // blue
                Color(228, 32, 32),   // red
                Color(252, 252, 252), // white
                Color(24, 208, 24),   // green
                Color(44, 60, 148),   // navy
                Color(180, 72, 180),  // purple
                Color(252, 252, 0),   // yellow
                Color(96, 96, 96),    // black, lifted so it still reads
                Color(128, 192, 252), // sky
                Color(240, 160, 40),  // orange
            };
            return index.value < 10 ? colors[index.value] : Color(255, 255, 255);
        }
    }

    void GameScene::printConsole(const std::string& text, const Color& color)
    {
        // Five seconds a line, and never more than eight on screen.
        consoleMessages.push_back(ConsoleMessage{text, color, sceneTime + SceneTime(5u * 30u)});
        while (consoleMessages.size() > 8)
        {
            consoleMessages.pop_front();
        }
    }

    void GameScene::renderConsole()
    {
        while (!consoleMessages.empty() && consoleMessages.front().expires <= sceneTime)
        {
            consoleMessages.pop_front();
        }

        // Top-left of the world view, under the resource bar, newest line at
        // the bottom -- where the original prints its speech text, and in the
        // font it prints it in: COMIX, the taller of its two game fonts.
        float y = static_cast<float>(GuiSizeTop) + 16.0f;
        for (const auto& message : consoleMessages)
        {
            chromeUiRenderService.drawText(static_cast<float>(GuiSizeLeft) + 8.0f, y, message.text, *speechFont, message.color);
            y += 14.0f;
        }
    }

    void GameScene::updateSelfDestructNotifications()
    {
        // "Commander: five", printed and spoken a number a second. The words
        // zero..five sit beside the count0-count5 speech keys in the binary
        // with the format "%s: %s" under a "Speech Text" label, and SOUND.TDF
        // maps them backwards -- count5 plays the file COUNT1 -- because the
        // recordings are numbered by their position in the countdown, not by
        // the number they say.
        static const char* const countWords[] = {"zero", "one", "two", "three", "four", "five"};

        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            auto it = selfDestructAnnounced.find(unitId);
            if (!unit.isAlive() || !unit.selfDestructTime)
            {
                if (it != selfDestructAnnounced.end())
                {
                    selfDestructAnnounced.erase(it);
                    if (unit.isAlive())
                    {
                        // Toggled off, not gone off.
                        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                        printConsole(unitDefinition.unitName + ": Self destruct terminated");
                        playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::CancelDestruct);
                    }
                }
                continue;
            }

            auto ticksLeft = *unit.selfDestructTime > simulation.gameTime
                ? (*unit.selfDestructTime - simulation.gameTime).value
                : 0u;
            auto secondsLeft = (ticksLeft + SimTicksPerSecond - 1) / SimTicksPerSecond;
            if (it != selfDestructAnnounced.end() && it->second == secondsLeft)
            {
                continue;
            }
            selfDestructAnnounced[unitId] = secondsLeft;

            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            auto word = secondsLeft < 6 ? std::string(countWords[secondsLeft]) : std::to_string(secondsLeft);
            printConsole(unitDefinition.unitName + ": " + word);

            std::optional<UnitSoundType> countSound;
            switch (secondsLeft)
            {
                case 5: countSound = UnitSoundType::Count5; break;
                case 4: countSound = UnitSoundType::Count4; break;
                case 3: countSound = UnitSoundType::Count3; break;
                case 2: countSound = UnitSoundType::Count2; break;
                case 1: countSound = UnitSoundType::Count1; break;
                default: break;
            }
            if (countSound)
            {
                playUnitNotificationSound(unit.owner, unit.unitType, *countSound);
            }
        }

        // Ids are reused, so entries for units that no longer exist must go.
        for (auto it = selfDestructAnnounced.begin(); it != selfDestructAnnounced.end();)
        {
            if (!simulation.units.tryGet(it->first))
            {
                it = selfDestructAnnounced.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GameScene::updateDefeatNotifications()
    {
        // "Arm forces have been obliterated", in the fallen side and colour.
        // The original keeps a family of these -- "forces have gone to a
        // better place", "vermin have been exterminated" -- but obliterated
        // is the one everybody remembers.
        for (unsigned int i = 0; i < simulation.players.size(); ++i)
        {
            const auto& player = simulation.players[i];
            if (player.status != GamePlayerStatus::Dead || defeatAnnounced.count(i) != 0)
            {
                continue;
            }
            defeatAnnounced.insert(i);

            auto side = player.side;
            std::transform(side.begin() + 1, side.end(), side.begin() + 1, [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            printConsole(side + " forces have been obliterated", playerColorToRgb(player.color));
        }
    }

    namespace
    {
        /**
         * The default track types, decoded from the exe: when the original
         * recognises the game disc it types MCI tracks 1-7 Battle and 8-16
         * Building (0x42F7xx area), and the GOG shim plays music/<n>.mp3 by
         * raw track number. Matching the GOG rips to the tagged soundtrack by
         * duration gives these names. A file the table does not know plays as
         * Building; the title theme -- which the game itself never plays, it
         * belongs to the intro -- is left out entirely.
         */
        bool isBattleTrackName(const std::string& lowerName)
        {
            static const char* const battleNames[] = {
                "brutal battle",
                "fire and ice",
                "attack",
                "warpath",
                "march unto death",
                "ambush in the passage",
            };
            for (const auto* name : battleNames)
            {
                if (lowerName.find(name) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void GameScene::addBattlePoints(int points)
    {
        battlePointsRing[battleRingCursor] += points;
    }

    void GameScene::updateMusic()
    {
        if (!musicPlaylistBuilt)
        {
            musicPlaylistBuilt = true;
            for (const auto& path : sceneContext.audioService->getMusicPlaylist())
            {
                auto lower = path;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("theme") != std::string::npos)
                {
                    continue;
                }
                (isBattleTrackName(lower) ? battleTracks : buildingTracks).push_back(path);
            }
            // A one-sided soundtrack plays whatever it has in both moods.
            if (battleTracks.empty())
            {
                battleTracks = buildingTracks;
            }
            if (buildingTracks.empty())
            {
                buildingTracks = battleTracks;
            }
        }
        if (buildingTracks.empty() || !sceneContext.audioService->isMusicEnabled())
        {
            return;
        }

        // The evaluator runs once a game second, like the original's.
        auto second = simulation.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond);
        if (second != lastMusicSecond)
        {
            lastMusicSecond = second;
            battleRingCursor = (battleRingCursor + 1) % battlePointsRing.size();
            battlePointsRing[battleRingCursor] = 0;

            int sum30 = 0;
            for (auto v : battlePointsRing)
            {
                sum30 += v;
            }
            int sum5 = 0;
            for (unsigned int i = 0; i < 5; ++i)
            {
                sum5 += battlePointsRing[(battleRingCursor + battlePointsRing.size() - i) % battlePointsRing.size()];
            }

            if (!musicFadeTarget && simulation.gameTime >= musicLockoutUntil)
            {
                if (musicSituation == MusicSituation::Building)
                {
                    // The unit-count gate is the original's: with thirty or
                    // fewer units the fight is not big enough for war drums.
                    unsigned int owned = 0;
                    for (const auto& [_, unit] : simulation.units)
                    {
                        if (unit.isAlive() && unit.isOwnedBy(localPlayerId))
                        {
                            ++owned;
                        }
                    }
                    if ((sum30 > 50 || sum5 > 30) && owned > 30)
                    {
                        musicFadeTarget = MusicSituation::Battle;
                    }
                }
                else
                {
                    auto inBattleFor = simulation.gameTime.value - battleEnteredTime.value;
                    if (sum30 < 10 && sum5 == 0 && inBattleFor >= 60u * SimTicksPerSecond)
                    {
                        musicFadeTarget = MusicSituation::Building;
                    }
                }
            }
        }

        // A switch fades the old track out over about 1.2 seconds, the
        // original's -vol/18 every other tick.
        if (musicFadeTarget)
        {
            musicFade -= 1.0f / 36.0f;
            if (musicFade <= 0.0f || !sceneContext.audioService->musicPlaying())
            {
                sceneContext.audioService->stopMusic();
                musicSituation = *musicFadeTarget;
                musicFadeTarget = std::nullopt;
                musicFade = 1.0f;
                sceneContext.audioService->setMusicFadeScale(1.0f);
                musicLockoutUntil = simulation.gameTime + GameTime(10u * SimTicksPerSecond);
                musicBag.clear();
                if (musicSituation == MusicSituation::Battle)
                {
                    battleEnteredTime = simulation.gameTime;
                }
                else
                {
                    // Coming down from battle gets four seconds of quiet.
                    musicHoldOffUntil = simulation.gameTime + GameTime(4u * SimTicksPerSecond);
                }
            }
            else
            {
                sceneContext.audioService->setMusicFadeScale(musicFade);
            }
            return;
        }

        if (sceneContext.audioService->musicPlaying() || simulation.gameTime < musicHoldOffUntil)
        {
            return;
        }

        // Draw the next track of the current mood from a bag, so everything
        // of that type plays before anything repeats.
        const auto& tracks = musicSituation == MusicSituation::Battle ? battleTracks : buildingTracks;
        if (musicBag.empty())
        {
            musicBag = tracks;
            for (auto i = musicBag.size(); i > 1; --i)
            {
                std::swap(musicBag[i - 1], musicBag[effectsRng() % i]);
            }
            if (musicBag.size() > 1 && musicBag.back() == lastMusicTrack)
            {
                std::swap(musicBag.back(), musicBag.front());
            }
        }
        if (musicBag.empty())
        {
            return;
        }

        auto next = musicBag.back();
        musicBag.pop_back();
        if (sceneContext.audioService->playMusic(next, false))
        {
            lastMusicTrack = next;
        }
        else
        {
            buildingTracks.erase(std::remove(buildingTracks.begin(), buildingTracks.end(), next), buildingTracks.end());
            battleTracks.erase(std::remove(battleTracks.begin(), battleTracks.end(), next), battleTracks.end());
        }
    }

    void GameScene::updateCloakNotifications()
    {
        // Cloaking and decloaking are the only thing about a cloak the original
        // tells anyone about. `0x48B173` raises notification 0xE the tick the
        // flag comes on and `0x48B1A5` raises 0xF when it goes off; the table
        // at `0x5086E8` pairs those with the sounds `cloak` and `uncloak` and
        // the captions "Cloaked" and "Visible". None of it is a COB event --
        // no shipped script of a cloakable unit has a function for either --
        // and none of it is drawn on the unit. The captions go to the console
        // in the corner, the same place the original prints them; the sounds
        // were parsed and loaded all along and simply never played.
        for (const auto& [unitId, unit] : simulation.units)
        {
            auto wasCloaked = cloakedUnits.find(unitId) != cloakedUnits.end();
            if (unit.cloaked == wasCloaked)
            {
                continue;
            }

            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (unit.cloaked)
            {
                cloakedUnits.insert(unitId);
                if (unit.isOwnedBy(localPlayerId))
                {
                    printConsole(unitDefinition.unitName + ": Cloaked");
                }
                playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::Cloak);
            }
            else
            {
                cloakedUnits.erase(unitId);
                if (unit.isOwnedBy(localPlayerId))
                {
                    printConsole(unitDefinition.unitName + ": Visible");
                }
                playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::Uncloak);
            }
        }

        // A unit killed while cloaked leaves its id behind, and ids are reused,
        // so a later unit would start out believed to be cloaked already and
        // never announce itself.
        for (auto it = cloakedUnits.begin(); it != cloakedUnits.end();)
        {
            it = simulation.unitExists(*it) ? std::next(it) : cloakedUnits.erase(it);
        }
    }

    void GameScene::playSoundAt(const Vector3f& /*position*/, const AudioService::SoundHandle& sound)
    {
        // FIXME: should play on a position-aware channel
        auto channel = sceneContext.audioService->playSound(sound);
        if (channel < 0)
        {
            return;
        }
        int volume;
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            playingUnitChannels.insert(channel);
            volume = computeSoundVolume(playingUnitChannels.size());
        }
        sceneContext.audioService->setVolume(channel, volume);
    }

    void GameScene::playWeaponStartSound(const Vector3f& position, const std::string& weaponType)
    {

        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        if (weaponMediaInfo.soundStart)
        {
            auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundStart);
            if (sound)
            {
                playSoundAt(position, *sound);
            }
        }
    }

    void GameScene::playWeaponImpactSound(const Vector3f& position, const std::string& weaponType, ImpactType impactType)
    {
        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        switch (impactType)
        {
            case ImpactType::Normal:
            {
                if (weaponMediaInfo.soundHit)
                {
                    auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundHit);
                    if (sound)
                    {
                        playSoundAt(position, *sound);
                    }
                }
                break;
            }
            case ImpactType::Water:
            {
                if (weaponMediaInfo.soundWater)
                {
                    auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundWater);
                    if (sound)
                    {
                        playSoundAt(position, *sound);
                    }
                }
                break;
            }
        }
    }

    void GameScene::spawnWeaponImpactExplosion(const Vector3f& position, const std::string& weaponType, ImpactType impactType)
    {
        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);

        switch (impactType)
        {
            case ImpactType::Normal:
            {
                if (weaponMediaInfo.explosionAnim)
                {
                    spawnExplosion(position, *weaponMediaInfo.explosionAnim);
                }
                if (weaponMediaInfo.endSmoke)
                {
                    createLightSmoke(position);
                }
                break;
            }
            case ImpactType::Water:
            {
                if (weaponMediaInfo.waterExplosionAnim)
                {
                    spawnExplosion(position, *weaponMediaInfo.waterExplosionAnim);
                }
                break;
            }
        }

        spawnFlash(position);
    }

    void GameScene::onChannelFinished(int channel)
    {
        std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
        playingUnitChannels.erase(channel);
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

    void GameScene::tryTickGame()
    {
        if (!playerCommandService->checkHashes())
        {
            std::ofstream dumpFile;
            dumpFile.open("rwe-dump-" + std::to_string(std::rand()) + ".json");
            dumpFile << dumpJson(simulation);
            dumpFile.close();
            throw std::runtime_error("Desync detected");
        }

        auto playerCommands = playerCommandService->tryPopCommands();
        if (!playerCommands)
        {
            LOG_ERROR << "Blocked waiting for player commands";
            return;
        }

        sceneTime += SceneTime(1);

        processActions();

        processPlayerCommands(*playerCommands);

        simulation.tick();

        auto gameHash = simulation.computeHash();
        playerCommandService->pushHash(localPlayerId, gameHash);
        gameNetworkService->submitGameHash(gameHash);

        if (stateLogStream)
        {
            *stateLogStream << dumpJson(simulation) << std::endl;
        }

        processSimEvents();

        updateCloakNotifications();

        updateSelfDestructNotifications();

        updateDefeatNotifications();

        updateProjectiles();

        updateFlashes();

        updateScreenShake();

        updateParticles(gameMediaDatabase, simulation.terrain, simulation.gameTime, particles);

        spawnNanoParticles();

        spawnGeoVentSteam();

        updateDebris();

        updateBuildBoxAppearances();

        // Testing aid: RWE_DEBUG_SPAWN=<unitType>*<count>@<player>:<seconds>
        // drops finished units of that type, owned by that player, in a ring
        // around the local player's first unit at that game time (for example
        // CORAK*6@1:10 to have six AKs attack the commander at ten seconds).
        if (const char* debugSpawn = std::getenv("RWE_DEBUG_SPAWN"))
        {
            std::string spec(debugSpawn);
            auto star = spec.find('*');
            auto at = spec.find('@');
            auto colon = spec.find(':');
            if (star != std::string::npos && at != std::string::npos && colon != std::string::npos)
            {
                auto unitType = spec.substr(0, star);
                auto count = std::atoi(spec.substr(star + 1, at - star - 1).c_str());
                auto player = std::atoi(spec.substr(at + 1, colon - at - 1).c_str());
                auto rest = spec.substr(colon + 1);
                auto nearSep = rest.find(':');
                auto seconds = static_cast<unsigned int>(std::atoi(rest.substr(0, nearSep).c_str()));
                // An optional trailing :<player> puts the ring around that player's first unit instead.
                auto nearPlayer = nearSep == std::string::npos ? PlayerId(localPlayerId) : PlayerId(static_cast<unsigned int>(std::atoi(rest.substr(nearSep + 1).c_str())));
                if (seconds > 0 && simulation.gameTime.value == seconds * static_cast<unsigned int>(SimTicksPerSecond) && isValidUnitType(simulation, unitType) && player >= 0 && player < getSize(simulation.players))
                {
                    std::optional<SimVector> centre;
                    for (const auto& [unitId, unit] : simulation.units)
                    {
                        if (unit.isOwnedBy(nearPlayer) && unit.isAlive())
                        {
                            centre = unit.position;
                            break;
                        }
                    }
                    for (int i = 0; centre && i < count; ++i)
                    {
                        auto angle = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(std::max(1, count));
                        SimVector position(centre->x + SimScalar(std::cos(angle) * 96.0f), centre->y, centre->z + SimScalar(std::sin(angle) * 96.0f));
                        position.y = simulation.terrain.getHeightAt(position.x, position.z);
                        LOG_INFO << "Debug: spawning " << unitType << " for player " << player << " at " << simScalarToFloat(position.x) << "," << simScalarToFloat(position.z);
                        spawnCompletedUnit(unitType, PlayerId(static_cast<unsigned int>(player)), position);
                    }
                }
            }
        }

        // Testing aid: RWE_DEBUG_SELF_DESTRUCT=<seconds> self-destructs a
        // player's first unit (the commander) at that game time, so a crash on
        // commander death can be reproduced under a debugger. The player is
        // the local one unless RWE_DEBUG_SELF_DESTRUCT_PLAYER gives an index.
        if (const char* debugSelfDestruct = std::getenv("RWE_DEBUG_SELF_DESTRUCT"))
        {
            auto seconds = static_cast<unsigned int>(std::atoi(debugSelfDestruct));
            if (seconds > 0 && simulation.gameTime.value == seconds * static_cast<unsigned int>(SimTicksPerSecond))
            {
                auto player = localPlayerId;
                if (const char* debugPlayer = std::getenv("RWE_DEBUG_SELF_DESTRUCT_PLAYER"))
                {
                    player = PlayerId(static_cast<unsigned int>(std::atoi(debugPlayer)));
                }
                for (const auto& [unitId, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(player) && unit.isAlive())
                    {
                        LOG_INFO << "Debug: self-destructing unit " << unitId.value << " of player " << player.value;
                        if (player == localPlayerId)
                        {
                            localPlayerSelfDestructUnit(unitId);
                        }
                        else
                        {
                            simulation.toggleSelfDestruct(unitId);
                        }
                        break;
                    }
                }
            }
        }

        // A game needs an opponent before it can be decided; a lone player
        // is just exploring the map.
        if (!gameOver && simulation.players.size() >= 2)
        {
            auto winStatus = simulation.computeWinStatus();
            match(
                winStatus,
                [&](const WinStatusWon& w) {
                    gameOver = winStatus;
                    gameOverTime = simulation.gameTime;
                    LOG_INFO << "Game over: player " << w.winner.value << " won at tick " << simulation.gameTime.value;
                },
                [&](const WinStatusDraw&) {
                    gameOver = winStatus;
                    gameOverTime = simulation.gameTime;
                    LOG_INFO << "Game over: draw at tick " << simulation.gameTime.value;
                },
                [&](const WinStatusUndecided&) {
                    // do nothing, game still in progress
                });
        }
    }

    std::optional<UnitId> GameScene::getUnitUnderCursor() const
    {
        if (isCursorOverMinimap())
        {
            auto mousePos = getMousePosition();

            auto worldToMinimap = worldToMinimapMatrix(simulation.terrain, minimapRect);

            for (const auto& [unitId, unit] : simulation.units)
            {
                // Only what the minimap actually shows can be picked: your own
                // units and enemies you can see or have on radar.
                if (fogOfWarEnabled && !unit.isOwnedBy(localPlayerId) && !simulation.canDetectUnit(localPlayerId, unitId))
                {
                    continue;
                }

                // convert to minimap rect
                auto minimapPos = worldToMinimap * simVectorToFloat(unit.position);
                minimapPos.x = std::floor(minimapPos.x);
                minimapPos.y = std::floor(minimapPos.y);
                auto ownerId = unit.owner;
                auto colorIndex = getPlayer(ownerId).color;
                const auto& sprite = *minimapDots->sprites[colorIndex.value];
                auto bounds = sprite.bounds;

                // test cursor against the rect
                Vector2f mousePosFloat(static_cast<float>(mousePos.x) + 0.5f, static_cast<float>(mousePos.y) + 0.5f);
                if (bounds.contains(mousePosFloat - minimapPos.xy()))
                {
                    return unitId;
                }
            }

            return std::nullopt;
        }

        if (isCursorOverWorld())
        {
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            return getFirstCollidingUnit(ray);
        }

        return std::nullopt;
    }

    std::optional<FeatureId> GameScene::getFeatureUnderCursor() const
    {
        if (!isCursorOverWorld())
        {
            return std::nullopt;
        }

        auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
        return getFirstCollidingFeature(ray);
    }

    Vector2f GameScene::screenToWorldClipSpace(Point p) const
    {
        return worldViewport.toClipSpace(sceneContext.viewport->toOtherViewport(worldViewport, p));
    }

    bool GameScene::isCursorOverMinimap() const
    {
        auto mousePos = getMousePosition();
        return minimapRect.contains(mousePos.x, mousePos.y);
    }

    bool GameScene::isCursorOverWorld() const
    {
        return worldViewport.contains(getMousePosition());
    }

    Point GameScene::getMousePosition() const
    {
        float fx;
        float fy;
        sceneContext.sdl->getMouseState(&fx, &fy);
        return Point(static_cast<int>(fx), static_cast<int>(fy));
    }

    std::optional<UnitId> GameScene::getFirstCollidingUnit(const Ray3f& ray) const
    {
        auto winnerIsMobile = false;
        auto bestDistance = std::numeric_limits<float>::infinity();
        std::optional<UnitId> it;

        for (const auto& entry : simulation.units)
        {
            if (!unitIsVisibleToLocalPlayer(entry.second))
            {
                // What cannot be seen cannot be clicked.
                continue;
            }
            if (entry.second.carriedBy)
            {
                // Cargo has no hitbox: clicks go to the transport carrying it.
                continue;
            }
            const auto& unitDefinition = simulation.unitDefinitions.at(entry.second.unitType);
            auto selectionMesh = gameMediaDatabase.getSelectionCollisionMesh(unitDefinition.objectName);
            auto distance = selectionIntersect(entry.second, *selectionMesh.value(), ray);
            auto isMobile = unitDefinition.isMobile;
            if (distance && ((!winnerIsMobile && isMobile) || distance < bestDistance))
            {
                winnerIsMobile = isMobile;
                bestDistance = *distance;
                it = entry.first;
            }
        }

        return it;
    }

    std::optional<FeatureId> GameScene::getFirstCollidingFeature(const Ray3f& ray) const
    {
        auto intersect = simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
        if (!intersect)
        {
            return std::nullopt;
        }

        auto heightmapPosition = simulation.terrain.worldToHeightmapCoordinate(*intersect);

        auto cellContents = simulation.occupiedGrid.tryGet(heightmapPosition);
        if (!cellContents)
        {
            return std::nullopt;
        }

        return cellContents->get().featureId;
    }

    std::optional<float> GameScene::selectionIntersect(const UnitState& unit, const CollisionMesh& mesh, const Ray3f& ray) const
    {
        auto inverseTransform = toFloatMatrix(unit.getInverseTransform());
        auto line = ray.toLine();
        Line3f modelSpaceLine(inverseTransform * line.start, inverseTransform * line.end);
        auto v = mesh.intersectLine(modelSpaceLine);
        if (!v)
        {
            return std::nullopt;
        }

        return ray.origin.distance(*v);
    }

    std::optional<SimVector> GameScene::getMouseTerrainCoordinate() const
    {
        if (isCursorOverMinimap())
        {
            auto transform = minimapToWorldMatrix(simulation.terrain, minimapRect);
            auto mousePos = getMousePosition();
            auto mouseX = static_cast<float>(mousePos.x) + 0.5f;
            auto mouseY = static_cast<float>(mousePos.y) + 0.5f;

            auto startPoint = transform * Vector3f(mouseX, mouseY, -1.0f);
            auto endPoint = transform * Vector3f(mouseX, mouseY, 1.0f);
            auto direction = endPoint - startPoint;
            Ray3f ray(startPoint, direction);
            return simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
        }

        if (isCursorOverWorld())
        {
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            return simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
        }

        return std::nullopt;
    }

    void GameScene::localPlayerIssueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto kind = PlayerUnitCommand::IssueOrder::IssueKind::Immediate;
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(order, kind)));

        if (std::holds_alternative<BuildOrder>(order))
        {
            if (sounds.okToBuild)
            {
                playUiSound(*sounds.okToBuild);
            }
        }
        else
        {
            const auto& unit = getUnit(unitId);
            auto handle = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Ok1);
            if (handle)
            {
                playUiSound(*handle);
            }
        }
    }

    void GameScene::localPlayerEnqueueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto kind = PlayerUnitCommand::IssueOrder::IssueKind::Queued;
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(order, kind)));

        if (std::holds_alternative<BuildOrder>(order))
        {
            if (sounds.okToBuild)
            {
                playUiSound(*sounds.okToBuild);
            }
        }

        commandWasQueued = true;
    }

    void GameScene::localPlayerStopUnit(UnitId unitId)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::Stop()));

        const auto& unit = getUnit(unitId);
        auto handle = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Ok1);
        if (handle)
        {
            playUiSound(*handle);
        }
    }

    void GameScene::localPlayerSelfDestructUnit(UnitId unitId)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SelfDestruct()));
    }

    void GameScene::localPlayerSetFireOrders(UnitId unitId, UnitFireOrders orders)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetFireOrders{orders}));
    }

    void GameScene::localPlayerSetOnOff(UnitId unitId, bool on)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetOnOff{on}));
    }

    void GameScene::localPlayerSetCloak(UnitId unitId, bool cloaked)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetCloak{cloaked}));
    }

    void GameScene::localPlayerModifyBuildQueue(UnitId unitId, const std::string& unitType, int count)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyBuildQueue{count, unitType}));

        updateUnconfirmedBuildQueueDelta(unitId, unitType, count);
        refreshBuildGuiTotal(unitId, unitType);
    }

    void GameScene::localPlayerModifyStockpile(UnitId unitId, int count)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyStockpile{count}));

        unconfirmedStockpileDelta[unitId] += count;
        refreshStockpileGuiTotal();
    }

    void GameScene::issueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            // Whatever it was doing (building, reclaiming) stops now, so the
            // arm is stowed and the nano spray ends; a later order to the same
            // target starts cleanly with StartBuilding.
            UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            unit->get().clearOrders();
            unit->get().addOrder(order);
        }
    }

    void GameScene::enqueueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            // An idle unit has nothing to queue behind, so this order starts
            // straight away — which means an aircraft part-way through setting
            // down has to break off and get back in the air for it.
            if (unit->get().orders.empty())
            {
                UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            }
            unit->get().addOrder(order);
        }
    }

    void GameScene::stopUnit(UnitId unitId)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            unit->get().clearOrders();
        }
    }

    void GameScene::cancelBuildOrderAt(UnitId unitId, const SimVector& position)
    {
        auto unit = tryGetUnit(unitId);
        if (!unit)
        {
            return;
        }
        auto cell = simulation.terrain.worldToHeightmapCoordinate(position);
        auto& orders = unit->get().orders;
        for (auto it = orders.begin(); it != orders.end(); ++it)
        {
            auto buildOrder = std::get_if<BuildOrder>(&*it);
            if (!buildOrder)
            {
                continue;
            }
            const auto& definition = simulation.unitDefinitions.at(buildOrder->unitType);
            auto rect = simulation.computeFootprintRegion(buildOrder->position, definition.movementCollisionInfo);
            if (cell.x >= rect.x && cell.x < rect.x + static_cast<int>(rect.width) && cell.y >= rect.y && cell.y < rect.y + static_cast<int>(rect.height))
            {
                // Only the plan is dropped; a building already started stays.
                if (it == orders.begin() && std::holds_alternative<UnitBehaviorStateBuilding>(unit->get().behaviourState))
                {
                    return;
                }
                orders.erase(it);
                return;
            }
        }
    }

    void GameScene::setFireOrders(UnitId unitId, UnitFireOrders orders)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            unit->get().setFireOrders(orders);

            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && *selectedUnit == unitId)
            {
                fireOrders.next(orders);
            }
        }
    }

    void GameScene::startTrack()
    {
        // sort selection by unit id so repeated 'T' keydown cycles through all units in a group consistently
        std::vector<UnitId> unitIds;
        for (const auto& u : selectedUnits)
        {
            unitIds.push_back(u);
        }
        std::sort(unitIds.begin(), unitIds.end());

        startTrackInternal(unitIds);
    }

    void GameScene::startTrackInternal(const std::vector<UnitId>& unitIds)
    {
        // Only allow tracking in free camera mode or if we are already tracking.
        auto canStartTracking = match(
            cameraControlState,
            [&](const CameraControlStateFree&) {
                return true;
            },
            [&](const CameraControlStateTrackingUnit&) {
                return true;
            },
            [&](const CameraControlStateMiddleMousePan&) {
                return false;
            });

        if (!canStartTracking)
        {
            return;
        }

        // If 'T' is pressed and no units are selected, stop tracking.
        if (unitIds.empty())
        {
            cameraControlState = CameraControlStateFree();
            return;
        }

        // If already tracking, check if currently tracked unit is in this selection. If it is, select the next id in the group.
        if (trackedUnitId)
        {
            auto it = std::find(unitIds.begin(), unitIds.end(), trackedUnitId);
            if (it != unitIds.end() && ++it != unitIds.end())
            {
                trackedUnitId = *it;
            }
            else
            {
                trackedUnitId = unitIds[0];
            }
        }
        else
        {
            trackedUnitId = unitIds[0];
        }

        cameraControlState = CameraControlStateTrackingUnit();
    }

    bool GameScene::isCtrlDown() const
    {
        return leftCtrlDown || rightCtrlDown;
    }

    bool GameScene::isShiftDown() const
    {
        return leftShiftDown || rightShiftDown;
    }

    namespace
    {
        /** Fills the GAMES listbox and mirrors clicks into the name box. */
        void wireSaveList(UiPanel& panel)
        {
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
                if (games && box && *index < games->get().getItems().size())
                {
                    box->get().setText(games->get().getItems()[*index]);
                }
            });
            games->get().addSubscription(std::move(sub));
        }
    }

    void GameScene::openSaveDialog()
    {
        auto panel = uiFactory.panelFromGuiFile("SAVEGAME");
        wireSaveList(*panel);
        if (auto box = panel->find<UiTextBox>("GAMENAME"))
        {
            box->get().setText("savegame");
        }
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
        sceneContext.audioService->stopMusic();
        auto scene = std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            parameters);
        sceneContext.sceneManager->setNextScene(scene);
    }

    void GameScene::applyLoadedGame(const SaveFile& save)
    {
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
            button->get().setEnabled(false);
        }
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::openInGameOptions(const std::string& page)
    {
        // The in-game options screen is the front end's composite with the
        // in-game skins: PREFS.GUI carries the tabs and OK/Cancel, the RT
        // pages carry the controls, and the backgrounds resolve out of
        // commongui.GAF through the ordinary gadget lookup.
        auto prefsEntries = sceneContext.vfs->readGuiOrThrow(sceneContext.pathMapping->guis + "/PREFS.GUI");
        if (!page.empty())
        {
            auto pageEntries = sceneContext.vfs->readGuiOrThrow(sceneContext.pathMapping->guis + "/" + page + ".GUI");
            prefsEntries.insert(prefsEntries.end(), pageEntries.begin() + 1, pageEntries.end());
        }

        auto panel = uiFactory.panelFromGuiFile("PREFS", prefsEntries);

        auto state = currentInGameOptions();
        if (auto bar = panel->find<UiScrollBar>("FXVOL"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent(static_cast<float>(state.soundVolume) / 100.0f);
            auto sub = bar->get().scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setSoundVolume(v);
            });
            bar->get().addSubscription(std::move(sub));
        }
        if (auto bar = panel->find<UiScrollBar>("MUSICVOL"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent(static_cast<float>(state.musicVolume) / 100.0f);
            auto sub = bar->get().scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setMusicVolume(v);
            });
            bar->get().addSubscription(std::move(sub));
        }
        if (auto toggle = panel->find<UiStagedButton>("NOTRAK"))
        {
            toggle->get().setStage(state.musicEnabled ? 1 : 0);
        }
        if (auto bar = panel->find<UiScrollBar>("VIDSLDR"))
        {
            bar->get().setScrollBarPercent(0.34f);
            auto modeToPercent = pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f);
            bar->get().setScrollPercent(modeToPercent);
            auto sub = bar->get().scrollChanged().subscribe([this](float v) {
                pendingWindowMode = v < 0.33f ? "bordered" : (v < 0.67f ? "borderless" : "fullscreen");
                if (!gameMenuPanels.empty())
                {
                    if (auto label = gameMenuPanels.front()->find<UiLabel>("VIDVAL"))
                    {
                        label->get().setText(pendingWindowMode == "borderless" ? "Borderless" : (pendingWindowMode == "fullscreen" ? "Fullscreen" : "Window"));
                    }
                }
            });
            bar->get().addSubscription(std::move(sub));
        }
        if (auto label = panel->find<UiLabel>("VIDVAL"))
        {
            label->get().setText(pendingWindowMode == "borderless" ? "Borderless" : (pendingWindowMode == "fullscreen" ? "Fullscreen" : "Window"));
        }

        if (auto toggle = panel->find<UiStagedButton>("BSHADOWS"))
        {
            toggle->get().setStage(shadowsEnabled ? 1 : 0);
        }

        // Screen scroll: 25 to 200 percent across the slider's travel.
        if (auto bar = panel->find<UiScrollBar>("SCREEN"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent((static_cast<float>(scrollSpeedSetting) - 25.0f) / 175.0f);
            auto sub = bar->get().scrollChanged().subscribe([this](float v) {
                scrollSpeedSetting = 25u + static_cast<unsigned int>(v * 175.0f);
            });
            bar->get().addSubscription(std::move(sub));
        }

        // Game speed maps the slider across the discrete speed steps, applied
        // through the same command path as the +/- keys.
        if (auto bar = panel->find<UiScrollBar>("GAME"))
        {
            bar->get().setScrollBarPercent(0.2f);
            bar->get().setScrollPercent(static_cast<float>(gameSpeed.index()) / static_cast<float>(GameSpeed::MaxIndex));
            auto sub = bar->get().scrollChanged().subscribe([this](float v) {
                auto index = static_cast<int>((v * static_cast<float>(GameSpeed::MaxIndex)) + 0.5f);
                localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{GameSpeed(index).index()});
            });
            bar->get().addSubscription(std::move(sub));
        }

        // What RWE has no machinery behind stays visible but grey, the way
        // the original greys what does not apply.
        for (const auto* name : {"SHADING", "ANTI", "SPEECH", "MODE", "LEFTCLICK", "UNITCHAT"})
        {
            if (auto button = panel->find<UiStagedButton>(name))
            {
                button->get().setEnabled(false);
            }
        }

        setGameMenuPanel(std::move(panel));
        inGameOptionsPage = page;
    }

    void GameScene::closeGameMenu()
    {
        gameMenuPanels.clear();
        inGameOptionsPage.clear();
        setMenuPause(false);
    }

    GameScene::InGameOptionsState GameScene::currentInGameOptions() const
    {
        return InGameOptionsState{
            static_cast<unsigned int>(sceneContext.audioService->getSoundVolume() * 100.0f),
            static_cast<unsigned int>(sceneContext.audioService->getMusicVolume() * 100.0f),
            sceneContext.audioService->isMusicEnabled(),
            pendingWindowMode,
            shadowsEnabled,
            scrollSpeedSetting};
    }

    void GameScene::applyInGameOptions(const InGameOptionsState& state)
    {
        auto* audio = sceneContext.audioService;
        audio->setSoundVolume(static_cast<float>(state.soundVolume) / 100.0f);
        audio->setMusicVolume(static_cast<float>(state.musicVolume) / 100.0f);
        audio->setMusicEnabled(state.musicEnabled);
        pendingWindowMode = state.windowMode;
        shadowsEnabled = state.shadows;
        scrollSpeedSetting = state.scrollSpeed;
    }

    void GameScene::saveInGameOptions()
    {
        auto localDataPath = getLocalDataPath();
        if (!localDataPath)
        {
            return;
        }
        auto state = currentInGameOptions();
        updateConfigFile(*localDataPath / "rwe.cfg", {
                                                         {"sound-volume", std::to_string(state.soundVolume)},
                                                         {"music-volume", std::to_string(state.musicVolume)},
                                                         {"music", state.musicEnabled ? "true" : "false"},
                                                         {"window-mode", state.windowMode},
                                                         {"shadows", state.shadows ? "true" : "false"},
                                                         {"scroll-speed", std::to_string(state.scrollSpeed)},
                                                     });
    }

    void GameScene::exitToMainMenu()
    {
        sceneContext.audioService->stopMusic();
        auto menu = std::make_shared<MainMenuScene>(
            sceneContext,
            audioLookup,
            sceneContext.viewport->width(),
            sceneContext.viewport->height());
        sceneContext.sceneManager->setNextScene(menu);
    }

    void GameScene::gameMenuMessage(const std::string& topic, const std::string& control)
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
            else if (control == "SAVE")
            {
                std::string name = "savegame";
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                saveCurrentGame(name);
                openGameMenuRoot();
            }
        }
        else if (topic == "LOADGAME")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
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
            else if (control == "EXITGAME")
            {
                sceneContext.sceneManager->requestExit();
            }
            else if (control == "MAINMENU")
            {
                exitToMainMenu();
            }
        }
        else if (topic == "PREFS")
        {
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
                // The button says OK: keep the settings and go back.
                saveInGameOptions();
                openGameMenuRoot();
            }
            else if (control == "CANCEL")
            {
                applyInGameOptions(gameOptionsUndo);
                openGameMenuRoot();
            }
            else if (control == "RESTORE")
            {
                applyInGameOptions(InGameOptionsState{100, 100, true, "bordered", true, 100});
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
            else if (control == "TEST")
            {
                if (auto sound = sceneContext.audioService->loadSound("BUTTON10"))
                {
                    sceneContext.audioService->playSound(*sound);
                }
            }
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
        auto scene = std::make_unique<MainMenuScene>(
            sceneContext,
            audioLookup,
            static_cast<float>(sceneContext.viewport->width()),
            static_cast<float>(sceneContext.viewport->height()));
        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(std::move(scene)));
    }

    bool GameScene::unitIsVisibleToLocalPlayer(const UnitState& unit) const
    {
        // Stowed inside a ship's hold (attached to no piece): out of sight until unloaded.
        if (unit.carriedBy && unit.carriedPiece.empty())
        {
            if (auto transport = tryGetUnit(*unit.carriedBy); transport && simulation.unitDefinitions.at(transport->get().unitType).floater)
            {
                return false;
            }
        }
        if (!fogOfWarEnabled)
        {
            return true;
        }

        // Same three questions the original's draw predicate asks, in the same
        // order: whose it is, whether it is cloaked, and only then whether the
        // ground under it is lit. It has to agree with the simulation's
        // canSeeUnit or a cloaked unit would be drawn to an enemy who cannot
        // target it.
        auto style = computeUnitDrawStyle(unit.isOwnedBy(localPlayerId), unit.cloaked, simulation.isVisibleTo(localPlayerId, unit.position));
        return style != UnitDrawStyle::Hidden;
    }

    bool GameScene::unitIsDetectableByLocalPlayer(const UnitState& unit) const
    {
        return unitIsVisibleToLocalPlayer(unit) || simulation.isOnRadarOf(localPlayerId, unit.position);
    }

    bool GameScene::positionIsExploredByLocalPlayer(const SimVector& position) const
    {
        return !fogOfWarEnabled || simulation.isExploredBy(localPlayerId, position);
    }

    bool GameScene::positionIsVisibleToLocalPlayer(const SimVector& position) const
    {
        return !fogOfWarEnabled || simulation.isVisibleTo(localPlayerId, position);
    }

    std::optional<SimVector> GameScene::plannedBuildOrderAt(UnitId unitId, const SimVector& position) const
    {
        auto unit = tryGetUnit(unitId);
        if (!unit)
        {
            return std::nullopt;
        }
        auto cell = simulation.terrain.worldToHeightmapCoordinate(position);
        for (const auto& order : unit->get().orders)
        {
            auto buildOrder = std::get_if<BuildOrder>(&order);
            if (!buildOrder)
            {
                continue;
            }
            const auto& definition = simulation.unitDefinitions.at(buildOrder->unitType);
            auto rect = simulation.computeFootprintRegion(buildOrder->position, definition.movementCollisionInfo);
            if (cell.x >= rect.x && cell.x < rect.x + static_cast<int>(rect.width) && cell.y >= rect.y && cell.y < rect.y + static_cast<int>(rect.height))
            {
                return buildOrder->position;
            }
        }
        return std::nullopt;
    }

    void GameScene::localPlayerCancelBuildOrder(UnitId unitId, const SimVector& position)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::CancelBuildOrder{position}));
    }

    std::unique_ptr<UiPanel> GameScene::createOrdersPanel()
    {
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        auto panel = uiFactory.panelFromGuiFile(sidePrefix + "GEN");

        // TA shows only the orders the selection can carry out, and it looks at
        // the whole selection rather than at one unit: the accumulator loop at
        // 0x41B49F-0x41B524 ORs each capability bit together, so a button is
        // offered when *any* selected unit names it. Picking up a transport
        // along with a squad of Peewees therefore gets you LOAD, and picking up
        // a solar collector with them does not take MOVE away.
        std::vector<OrderButtonUnit> selection;
        for (const auto& selectedUnitId : selectedUnits)
        {
            auto selectedUnit = tryGetUnit(selectedUnitId);
            if (!selectedUnit)
            {
                continue;
            }

            bool hasCommandFireWeapon = false;
            for (const auto& weapon : selectedUnit->get().weapons)
            {
                if (weapon && simulation.weaponDefinitions.at(weapon->weaponType).commandFire)
                {
                    hasCommandFireWeapon = true;
                }
            }

            selection.push_back(OrderButtonUnit{&simulation.unitDefinitions.at(selectedUnit->get().unitType), hasCommandFireWeapon});
        }

        if (selection.empty())
        {
            return panel;
        }

        // The original greys these out rather than taking them away, except
        // LOAD and BLAST which share a slot and so have to be hidden
        // (0x41A412 and 0x41A471 call the "make inactive" helper, everything
        // else calls the "grey" one). The greyed frame is in every button's
        // own GAF, one past the pressed frame.
        std::vector<std::string> doomed;
        for (const auto& child : panel->getChildren())
        {
            const auto& name = child->getName();
            if (!startsWith(name, sidePrefix))
            {
                continue;
            }

            auto button = orderButtonFromName(name.substr(sidePrefix.size()));
            if (button && !selectionOffersOrderButton(selection, *button))
            {
                if (*button == OrderButton::Load || *button == OrderButton::Blast)
                {
                    doomed.push_back(name);
                }
                else if (auto stagedButton = dynamic_cast<UiStagedButton*>(child.get()); stagedButton != nullptr)
                {
                    stagedButton->setEnabled(false);
                }
            }
        }

        for (const auto& name : doomed)
        {
            panel->removeChildrenNamed(name);
        }

        return panel;
    }

    void GameScene::spawnDebris(const PieceExplodedEvent& e)
    {
        if (!positionIsVisibleToLocalPlayer(e.position))
        {
            return;
        }

        const auto& unitDefinition = simulation.unitDefinitions.at(e.unitType);
        auto position = simVectorToFloat(e.position);

        // TA's explode flags.
        const unsigned int shatter = 1u;
        const unsigned int bitmapOnly = 32u;

        // BITMAP1..5 (bits 6-10) choose an explosion sprite to show at the piece.
        static const char* const bitmapAnims[] = {"Explode2", "Explode3", "Explode4", "Explode5", "Explosion"};
        for (unsigned int i = 0; i < 5; ++i)
        {
            if ((e.flags & (64u << i)) && gameMediaDatabase.getSpriteSeries("FX", bitmapAnims[i]))
            {
                spawnExplosion(position, AnimLocation{"FX", bitmapAnims[i]});
                break;
            }
        }
        // Only buildings break into flying pieces; mobile units just get the
        // explosion sprites. A piece the model does not have cannot fly either.
        if ((e.flags & bitmapOnly) || unitDefinition.isMobile || e.pieceName.empty())
        {
            return;
        }

        std::uniform_real_distribution<float> sideways(-2.5f, 2.5f);
        std::uniform_real_distribution<float> upwards(3.0f, 7.0f);
        std::uniform_real_distribution<float> spin(-0.3f, 0.3f);
        std::uniform_int_distribution<unsigned int> lifetime(60u, 120u);

        auto makeDebris = [&](bool shard) {
            Debris d;
            d.objectName = unitDefinition.objectName;
            d.pieceName = e.pieceName;
            d.color = getPlayer(e.owner).color;
            d.position = position;
            d.velocity = Vector3f(sideways(effectsRng), upwards(effectsRng), sideways(effectsRng));
            d.rotation = Vector3f(0.0f, toRadians(e.rotation).value, 0.0f);
            d.angularVelocity = Vector3f(spin(effectsRng), spin(effectsRng), spin(effectsRng));
            d.endTime = simulation.gameTime + GameTime(lifetime(effectsRng));
            d.nextTrail = simulation.gameTime;
            d.flags = e.flags;
            d.shard = shard;
            debris.push_back(d);
        };

        if (e.flags & shatter)
        {
            // The piece breaks up: a handful of fragments instead of the mesh.
            for (int i = 0; i < 6; ++i)
            {
                makeDebris(true);
            }
        }
        else
        {
            makeDebris(false);
        }
    }

    void GameScene::updateDebris()
    {
        const unsigned int explodeOnHit = 2u;
        const unsigned int smoke = 8u;
        const unsigned int fire = 16u;
        const float gravity = 0.3f;

        auto corner = simVectorToFloat(simulation.terrain.heightmapIndexToWorldCorner(0, 0));
        auto tile = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
        auto mapWidth = static_cast<float>(simulation.terrain.getHeightMap().getWidth()) * tile;
        auto mapHeight = static_cast<float>(simulation.terrain.getHeightMap().getHeight()) * tile;

        auto end = debris.end();
        for (auto it = debris.begin(); it != end;)
        {
            auto& d = *it;
            d.velocity.y -= gravity;
            d.position += d.velocity;
            d.rotation += d.angularVelocity;

            bool onMap = d.position.x > corner.x + tile && d.position.x < corner.x + mapWidth - tile
                && d.position.z > corner.z + tile && d.position.z < corner.z + mapHeight - tile;

            if (onMap && (d.flags & (smoke | fire)) && simulation.gameTime >= d.nextTrail)
            {
                d.nextTrail = simulation.gameTime + GameTime(3);
                if ((d.flags & fire) && gameMediaDatabase.getSpriteSeries("FX", "fire1"))
                {
                    spawnExplosion(d.position, AnimLocation{"FX", "fire1"});
                }
                else
                {
                    spawnSmoke(d.position, "FX", "smoke 1", ParticleFinishTimeEndOfFrames(), GameTime(2));
                }
            }

            auto ground = onMap ? simScalarToFloat(simulation.terrain.getHeightAt(SimScalar(d.position.x), SimScalar(d.position.z))) : d.position.y;
            bool landed = onMap && d.position.y <= ground;
            if (!onMap || landed || simulation.gameTime >= d.endTime)
            {
                if (landed && (d.flags & explodeOnHit) && gameMediaDatabase.getSpriteSeries("FX", "Explode2"))
                {
                    spawnExplosion(Vector3f(d.position.x, ground, d.position.z), AnimLocation{"FX", "Explode2"});
                }
                *it = std::move(*--end);
                continue;
            }
            ++it;
        }
        debris.erase(end, debris.end());
    }

    void GameScene::updateFogSprite()
    {
        if (!fogOfWarEnabled)
        {
            return;
        }

        if (!fogTiles)
        {
            // TA's own fog artwork. Without it we fall back to square-edged
            // shapes: uglier, but the fog still reads correctly.
            fogTiles = loadFogTileSet(*sceneContext.vfs, "anims/fog.gaf").value_or(makeSquareFogTileSet());
        }

        const auto& vis = simulation.playerVisibility.at(localPlayerId.value);

        auto cellsWide = vis.explored.getWidth();
        auto cellsHigh = vis.explored.getHeight();
        // The vision grid starts at the map's top-left corner and covers whole
        // cells, which may extend slightly past the map's edge.
        auto cellWorldUnits = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits) * static_cast<float>(PlayerVisibility::VisionCellSizeInTiles);
        auto corner = simVectorToFloat(simulation.terrain.heightmapIndexToWorldCorner(0, 0));

        // The cells the camera can see. The fog grid is indexed in projected
        // space, and the terrain sheet is drawn flat, so the sheet's own x and
        // z are already that space and no skew is needed here. A whole terrain
        // tile of slack covers the tiles that hang over the camera's edge.
        auto camera = worldCameraState.getRoundedPosition();
        auto halfWidth = worldCameraState.scaleDimension(static_cast<float>(worldViewport.width())) / 2.0f;
        auto halfHeight = worldCameraState.scaleDimension(static_cast<float>(worldViewport.height())) / 2.0f;
        auto toCellX = [&](float worldX) {
            return std::clamp(static_cast<int>(std::floor((worldX - corner.x) / cellWorldUnits)), 0, cellsWide - 1);
        };
        auto toCellY = [&](float worldZ) {
            return std::clamp(static_cast<int>(std::floor((worldZ - corner.z) / cellWorldUnits)), 0, cellsHigh - 1);
        };
        auto viewX0 = toCellX(camera.x - halfWidth - cellWorldUnits);
        auto viewY0 = toCellY(camera.z - halfHeight - cellWorldUnits);
        auto viewX1 = toCellX(camera.x + halfWidth + cellWorldUnits);
        auto viewY1 = toCellY(camera.z + halfHeight + cellWorldUnits);
        GridRegion cellsInView(viewX0, viewY0, (viewX1 - viewX0) + 1, (viewY1 - viewY0) + 1);

        // The grids only change on sim ticks, and a couple of ticks of lag in
        // the fog itself is invisible. The window is another matter: once the
        // camera leaves it the edge of its texture would show, so a scroll off
        // the end is never put off.
        if (fogSprite && (simulation.gameTime.value - fogSpriteTime.value) < 2 && fogRasterizer.covers(cellsInView))
        {
            return;
        }
        fogSpriteTime = simulation.gameTime;

        // Rebuilding is the expensive part. The rasteriser keeps a window a
        // little larger than the view, tracks the corner codes it last drew,
        // and reports only the patch of texture that moved.
        auto update = fogRasterizer.update(*fogTiles, vis.visible, vis.explored, cellsInView);
        if (update)
        {
            auto overlayWidth = static_cast<unsigned int>(fogRasterizer.getWidth());
            auto overlayHeight = static_cast<unsigned int>(fogRasterizer.getHeight());
            if (update->windowChanged || !fogOverlayTexture.isValid() || fogOverlayWidth != overlayWidth || fogOverlayHeight != overlayHeight)
            {
                fogOverlayTexture = SharedTextureHandle(sceneContext.graphics->createSingleChannelTexture(overlayWidth, overlayHeight, fogRasterizer.getData()));
                fogOverlayWidth = overlayWidth;
                fogOverlayHeight = overlayHeight;
                fogOverlayBounds = Rectangle2f::fromTopLeft(
                    corner.x + static_cast<float>(fogRasterizer.getOffsetX()),
                    corner.z + static_cast<float>(fogRasterizer.getOffsetY()),
                    static_cast<float>(overlayWidth),
                    static_cast<float>(overlayHeight));
            }
            else
            {
                sceneContext.graphics->updateSingleChannelTexture(
                    fogOverlayTexture.get(),
                    overlayWidth,
                    static_cast<unsigned int>(update->dirty.x),
                    static_cast<unsigned int>(update->dirty.y),
                    static_cast<unsigned int>(update->dirty.width),
                    static_cast<unsigned int>(update->dirty.height),
                    fogRasterizer.getData());
            }
        }

        // The minimap keeps its own one-texel-per-cell copy of the whole map.
        // It is only a hundred-odd pixels across, so the authored tiles would
        // be thrown away by the downscale anyway, and this way it does not have
        // to care where the world's window happens to be.
        if (fogSprite && vis.visible.getVector() == fogVisibleSnapshot && vis.explored.getVector() == fogExploredSnapshot)
        {
            return;
        }
        fogVisibleSnapshot = vis.visible.getVector();
        fogExploredSnapshot = vis.explored.getVector();

        std::vector<Color> pixels;
        pixels.reserve(static_cast<size_t>(cellsWide) * static_cast<size_t>(cellsHigh));
        for (int y = 0; y < cellsHigh; ++y)
        {
            for (int x = 0; x < cellsWide; ++x)
            {
                if (vis.visible.get(x, y) != 0)
                {
                    pixels.emplace_back(0, 0, 0, 0);
                }
                else if (vis.explored.get(x, y))
                {
                    pixels.emplace_back(0, 0, 0, 120);
                }
                else
                {
                    pixels.emplace_back(0, 0, 0, 255);
                }
            }
        }

        SharedTextureHandle texture(sceneContext.graphics->createTexture(cellsWide, cellsHigh, pixels.data()));

        auto bounds = Rectangle2f::fromTopLeft(corner.x, corner.z, cellsWide * cellWorldUnits, cellsHigh * cellWorldUnits);
        auto region = Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
        fogSprite = sceneContext.graphics->createSprite(bounds, region, texture);
    }

    void GameScene::renderHelpOverlay()
    {
        if (!helpVisible)
        {
            return;
        }

        // Two columns of "key   what it does".
        static const std::vector<std::pair<std::string, std::string>> leftColumn{
            {"F1", "Show or hide this help"},
            {"Left click", "Select unit, or drag a box"},
            {"Right click", "Move / attack / assist (right-click mode)"},
            {"Shift + order", "Queue the order"},
            {"Esc", "Cancel cursor mode / deselect"},
            {"A", "Attack"},
            {"M", "Move"},
            {"D", "Guard"},
            {"P", "Patrol"},
            {"R", "Repair"},
            {"E", "Reclaim"},
            {"C", "Capture"},
            {"S", "Stop"},
            {"T", "Track selected unit"},
            {"Ctrl+D", "Self-destruct (again to cancel)"},
            {"Ctrl+1..9 / 1..9", "Assign / select group"},
            {"+ / -", "Game speed"},
            {"Pause", "Pause"},
            {"Arrows", "Scroll the map"},
            {"`", "Health bars"},
            {"F10 / F11", "Debug menus"},
        };
        static const std::vector<std::pair<std::string, std::string>> rightColumn{
            {"Ctrl+A", "Select all units"},
            {"Ctrl+B", "Next idle builder"},
            {"Ctrl+C", "Select commander"},
            {"Ctrl+F", "Fight (attack-move)"},
            {"Ctrl+G", "Armed ground units"},
            {"Ctrl+H", "Armed hovercraft"},
            {"Ctrl+J", "Metal makers"},
            {"Ctrl+K", "Armed kbots"},
            {"Ctrl+L", "Long range artillery"},
            {"Ctrl+M", "Mines"},
            {"Ctrl+N", "Armed naval units"},
            {"Ctrl+O", "Fighters"},
            {"Ctrl+P", "Armed aircraft"},
            {"Ctrl+Q", "Bombers"},
            {"Ctrl+R", "Radar / sonar / jammers"},
            {"Ctrl+S", "Armed units on screen"},
            {"Ctrl+T", "Transports"},
            {"Ctrl+U", "Armed underwater units"},
            {"Ctrl+V", "Armed vehicles"},
            {"Ctrl+W", "Guard mode"},
            {"Ctrl+X", "Defensive buildings"},
            {"Ctrl+Y", "Torpedo bombers"},
            {"Ctrl+Z", "All units of the selected types"},
        };

        const float lineHeight = 14.0f;
        const float columnWidth = 300.0f;
        const float keyWidth = 110.0f;
        auto rows = std::max(leftColumn.size(), rightColumn.size());
        const float boxWidth = (columnWidth * 2.0f) + 24.0f;
        const float boxHeight = (rows + 3) * lineHeight;
        auto centerX = worldViewport.left() + (worldViewport.width() / 2.0f);
        auto centerY = worldViewport.top() + (worldViewport.height() / 2.0f);
        auto boxX = centerX - (boxWidth / 2.0f);
        auto boxY = centerY - (boxHeight / 2.0f);

        chromeUiRenderService.fillColor(boxX, boxY, boxWidth, boxHeight, Color(0, 0, 0, 215));
        chromeUiRenderService.drawBoxOutline(boxX, boxY, boxWidth, boxHeight, Color(180, 180, 180), 1.0f);
        chromeUiRenderService.drawTextCenteredX(centerX, boxY + (lineHeight * 0.5f), "HOTKEYS", *guiFont);

        auto drawColumn = [&](const std::vector<std::pair<std::string, std::string>>& column, float x) {
            auto y = boxY + (lineHeight * 2.0f);
            for (const auto& [key, action] : column)
            {
                chromeUiRenderService.drawText(x, y, key, *guiFont, Color(83, 223, 79));
                chromeUiRenderService.drawText(x + keyWidth, y, action, *guiFont);
                y += lineHeight;
            }
        };
        drawColumn(leftColumn, boxX + 12.0f);
        drawColumn(rightColumn, boxX + 12.0f + columnWidth);
    }

    void GameScene::renderGameOverOverlay()
    {
        if (!gameOver)
        {
            return;
        }

        const auto& localPlayer = getPlayer(localPlayerId);
        auto title = match(
            *gameOver,
            [&](const WinStatusWon& w) { return w.winner == localPlayerId ? std::string("VICTORY") : std::string("DEFEAT"); },
            [&](const WinStatusDraw&) { return std::string("DRAW"); },
            [&](const WinStatusUndecided&) { return std::string(); });

        auto totalSeconds = gameOverTime.value / static_cast<unsigned int>(SimTicksPerSecond);
        auto minutes = totalSeconds / 60;
        auto seconds = totalSeconds % 60;
        std::string timeText = "Game time " + std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds);

        std::vector<std::string> lines{
            title,
            timeText,
            "Units destroyed: " + std::to_string(localPlayer.unitsKilled),
            "Units lost: " + std::to_string(localPlayer.unitsLost),
            "",
            "Press ESC to return to the main menu",
        };

        const float lineHeight = 16.0f;
        const float boxWidth = 300.0f;
        const float boxHeight = (lines.size() + 2) * lineHeight;
        auto centerX = worldViewport.left() + (worldViewport.width() / 2.0f);
        auto centerY = worldViewport.top() + (worldViewport.height() / 2.0f);
        auto boxX = centerX - (boxWidth / 2.0f);
        auto boxY = centerY - (boxHeight / 2.0f);

        chromeUiRenderService.fillColor(boxX, boxY, boxWidth, boxHeight, Color(0, 0, 0, 210));
        chromeUiRenderService.drawBoxOutline(boxX, boxY, boxWidth, boxHeight, title == "VICTORY" ? Color(83, 223, 79) : Color(255, 71, 0), 2.0f);

        auto y = boxY + (lineHeight * 1.5f);
        for (const auto& line : lines)
        {
            chromeUiRenderService.drawTextCentered(centerX, y, line, *guiFont);
            y += lineHeight;
        }
    }

    UnitState& GameScene::getUnit(UnitId id)
    {
        return simulation.getUnitState(id);
    }

    const UnitState& GameScene::getUnit(UnitId id) const
    {
        return simulation.getUnitState(id);
    }

    std::optional<std::reference_wrapper<UnitState>> GameScene::tryGetUnit(UnitId id)
    {
        return simulation.tryGetUnitState(id);
    }

    std::optional<std::reference_wrapper<const UnitState>> GameScene::tryGetUnit(UnitId id) const
    {
        return simulation.tryGetUnitState(id);
    }

    GamePlayerInfo& GameScene::getPlayer(PlayerId player)
    {
        return simulation.getPlayer(player);
    }

    const GamePlayerInfo& GameScene::getPlayer(PlayerId player) const
    {
        return simulation.getPlayer(player);
    }

    bool GameScene::isEnemy(UnitId id) const
    {
        // TODO: consider allies/teams here
        return !getUnit(id).isOwnedBy(localPlayerId);
    }

    bool GameScene::isFriendly(UnitId id) const
    {
        return !isEnemy(id);
    }

    void GameScene::updateProjectiles()
    {
        for (auto& [projectileId, projectile] : simulation.projectiles)
        {
            const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(projectile.weaponType);
            auto& renderInfo = projectileRenderInfos[projectileId];

            // emit smoke trail
            if (weaponMediaInfo.smokeTrail)
            {
                auto gameTime = getGameTime();
                if (gameTime > projectile.lastSmoke + *weaponMediaInfo.smokeTrail)
                {
                    createLightSmoke(simVectorToFloat(projectile.position));
                    projectile.lastSmoke = gameTime;
                }
            }
        }
    }

    void GameScene::processSimEvents()
    {
        for (const auto& event : simulation.events)
        {
            match(
                event,
                [&](const FeatureReclaimedEvent& e) {
                    // The feature's reclaim sequence (TA's golden swirl) plays once where it stood.
                    if (!positionIsVisibleToLocalPlayer(e.position))
                    {
                        return;
                    }
                    const auto& featureMediaInfo = gameMediaDatabase.getFeature(e.featureType);
                    if (featureMediaInfo.fileName.empty() || featureMediaInfo.seqNameReclamate.empty())
                    {
                        return;
                    }
                    if (!gameMediaDatabase.getSpriteSeries(featureMediaInfo.fileName, featureMediaInfo.seqNameReclamate))
                    {
                        return;
                    }
                    spawnExplosion(simVectorToFloat(e.position), AnimLocation{featureMediaInfo.fileName, featureMediaInfo.seqNameReclamate});
                },
                [&](const PieceExplodedEvent& e) {
                    spawnDebris(e);
                },
                [&](const FireWeaponEvent& e) {
                    const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(e.weaponType);

                    if (e.shotNumber == 0 || weaponMediaInfo.soundTrigger)
                    {
                        playWeaponStartSound(simVectorToFloat(e.firePoint), e.weaponType);
                    }

                    if (e.shotNumber == 0 && weaponMediaInfo.startSmoke)
                    {
                        createWeaponSmoke(simVectorToFloat(e.firePoint));
                    }
                },
                [&](const UnitArrivedEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Arrived1);
                    }
                },
                [&](const UnitActivatedEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Activate);

                        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && *selectedUnit == e.unitId)
                        {
                            onOff.next(true);
                        }
                    }
                },
                [&](const UnitDeactivatedEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Deactivate);

                        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && *selectedUnit == e.unitId)
                        {
                            onOff.next(false);
                        }
                    }
                },
                [&](const UnitCompleteEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::UnitComplete);
                    }
                },
                [&](const EmitParticleFromPieceEvent& e) {
                    if (!simulation.unitExists(e.unitId))
                    {
                        return;
                    }

                    switch (e.sfxType)
                    {
                        case EmitParticleFromPieceEvent::SfxType::LightSmoke:
                            emitLightSmokeFromPiece(e.unitId, e.pieceName);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::BlackSmoke:
                            emitBlackSmokeFromPiece(e.unitId, e.pieceName);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Wake1:
                            emitWakeFromPiece(e.unitId, e.pieceName, false, 16);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Wake2:
                            emitWakeFromPiece(e.unitId, e.pieceName, false, 8);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::ReverseWake1:
                            emitWakeFromPiece(e.unitId, e.pieceName, true, 16);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::ReverseWake2:
                            emitWakeFromPiece(e.unitId, e.pieceName, true, 8);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Vtol:
                            emitVtolFromPiece(e.unitId, e.pieceName, 6);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Thrust:
                            emitVtolFromPiece(e.unitId, e.pieceName, 7);
                            break;
                        default:
                            throw std::logic_error("unknown particle type");
                    }
                },
                [&](const UnitSpawnedEvent& e) {
                    // initialise local-player-specific UI data
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        const auto& unitDefinition = simulation.unitDefinitions.at(unit->get().unitType);
                        unitGuiInfos.insert_or_assign(e.unitId, UnitGuiInfo{unitDefinition.builder ? UnitGuiInfo::Section::Build : UnitGuiInfo::Section::Orders, 0});
                    }
                },

                [&](const UnitDamagedEvent& e) {
                    // One point per weapon hit involving the local player,
                    // either side of it -- the original's scoring.
                    if (e.victimOwner == localPlayerId || (e.attackerOwner && *e.attackerOwner == localPlayerId))
                    {
                        addBattlePoints(1);
                    }
                },
                [&](const UnitDiedEvent& e) {
                    const auto& unitDefinition = simulation.unitDefinitions.at(e.unitType);

                    // Five points per unit the local player kills.
                    if (e.killerOwner && *e.killerOwner == localPlayerId)
                    {
                        addBattlePoints(5);
                    }


                    const auto& selfDestructExplosion = unitDefinition.selfDestructAs.empty() ? unitDefinition.explodeAs : unitDefinition.selfDestructAs;
                    switch (e.deathType)
                    {
                        case UnitDiedEvent::DeathType::NormalExploded:
                            if (!unitDefinition.explodeAs.empty())
                            {
                                doProjectileImpact(e.position, unitDefinition.explodeAs, ImpactType::Normal);
                                addScreenShakeFromWeapon(gameMediaDatabase.getWeapon(unitDefinition.explodeAs));
                            }
                            break;
                        case UnitDiedEvent::DeathType::WaterExploded:
                            if (!unitDefinition.explodeAs.empty())
                            {
                                doProjectileImpact(e.position, unitDefinition.explodeAs, ImpactType::Water);
                                addScreenShakeFromWeapon(gameMediaDatabase.getWeapon(unitDefinition.explodeAs));
                            }
                            break;
                        case UnitDiedEvent::DeathType::SelfDestructed:
                            // The last word of the countdown belongs to the
                            // detonation itself: count0 is mapped to COUNT6,
                            // the recording the shipped data saves for the end.
                            if (auto deadUnit = tryGetUnit(e.unitId); deadUnit && deadUnit->get().isOwnedBy(localPlayerId))
                            {
                                printConsole(unitDefinition.unitName + ": zero");
                                playUnitNotificationSound(localPlayerId, e.unitType, UnitSoundType::Count0);
                            }
                            if (!selfDestructExplosion.empty())
                            {
                                doProjectileImpact(e.position, selfDestructExplosion, ImpactType::Normal);
                                addScreenShakeFromWeapon(gameMediaDatabase.getWeapon(selfDestructExplosion));
                            }
                            break;
                        case UnitDiedEvent::DeathType::Deleted:
                            // do nothing
                            break;
                    }

                    deselectUnit(e.unitId);

                    if (hoveredUnit && *hoveredUnit == e.unitId)
                    {
                        hoveredUnit = std::nullopt;
                    }

                    unitGuiInfos.erase(e.unitId);
                },
                [&](const UnitStartedBuildingEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Build);
                    }
                },
                [&](const UnitCapturedEvent& e) {
                    // A unit we lost must not linger in our selection.
                    if (e.previousOwner == localPlayerId)
                    {
                        deselectUnit(e.unitId);
                    }
                },
                [&](const ProjectileSpawnedEvent& e) {
                    projectileRenderInfos.insert({e.projectileId, ProjectileRenderInfo{getGameTime()}});
                },
                [&](const ProjectileDiedEvent& e) {
                    const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(e.weaponType);
                    if (weaponMediaInfo.endSmoke)
                    {
                        createLightSmoke(simVectorToFloat(e.position));
                    }

                    projectileRenderInfos.erase(e.projectileId);

                    switch (e.deathType)
                    {
                        case ProjectileDiedEvent::DeathType::NormalImpact:
                            doProjectileImpact(e.position, e.weaponType, ImpactType::Normal);
                            addScreenShakeFromWeapon(weaponMediaInfo);
                            break;
                        case ProjectileDiedEvent::DeathType::WaterImpact:
                            doProjectileImpact(e.position, e.weaponType, ImpactType::Water);
                            addScreenShakeFromWeapon(weaponMediaInfo);
                            break;
                        case ProjectileDiedEvent::DeathType::OutOfBounds:
                        case ProjectileDiedEvent::DeathType::EndOfLife:
                            // do nothing
                            break;
                    }
                });
        }

        simulation.events.clear();
    }

    void GameScene::addScreenShakeFromWeapon(const WeaponMediaInfo& weaponMediaInfo)
    {
        if (weaponMediaInfo.shakeMagnitude == 0 || weaponMediaInfo.shakeDuration == 0)
        {
            return;
        }

        // No falloff with distance, and none in the original either: 0x499FAB
        // hands the weapon's two numbers straight to the shake without ever
        // looking at where the explosion was or where the camera is. A blast
        // in the far corner of the map shakes the screen exactly as hard as
        // one under the cursor.
        //
        // In practice this fires on deaths rather than on shots: every weapon
        // in the shipped data that sets the keys is an explodeAs or
        // selfDestructAs -- LARGE_BUILDING, BIG_UNIT, COMMANDER_BLAST,
        // ATOMIC_BLAST and friends -- and none of them is ever fired at
        // anything. The projectile path is wired up anyway because the
        // original's single call site is in the detonation routine and applies
        // to both.
        accumulateScreenShake(
            screenShake,
            static_cast<int>(weaponMediaInfo.shakeMagnitude),
            static_cast<int>(weaponMediaInfo.shakeDuration));
    }

    void GameScene::updateScreenShake()
    {
        // Take off whatever the last frame put on, so the jitter is a wobble
        // about where the player actually left the camera rather than a random
        // walk away from it. See the note on appliedShakeOffset.
        worldCameraState.position -= appliedShakeOffset;
        appliedShakeOffset = Vector3f(0.0f, 0.0f, 0.0f);

        auto [ampX, ampY] = screenShakeAmplitudes(screenShake);
        if (ampX > 0 || ampY > 0)
        {
            // Uniform on [-amp/2, amp/2), which is what 0x41C737-0x41C755
            // builds out of a rand() and a divide by 0x8000.
            std::uniform_int_distribution<int> distX(0, ampX > 0 ? ampX - 1 : 0);
            std::uniform_int_distribution<int> distY(0, ampY > 0 ? ampY - 1 : 0);
            appliedShakeOffset = Vector3f(
                static_cast<float>(distX(effectsRng) - ampX / 2),
                0.0f,
                static_cast<float>(distY(effectsRng) - ampY / 2));
            worldCameraState.position += appliedShakeOffset;
        }

        advanceScreenShake(screenShake);
    }

    void GameScene::updateFlashes()
    {
        flashes.erase(
            std::remove_if(
                flashes.begin(),
                flashes.end(),
                [&](const auto& flash) { return flash.isFinished(simulation.gameTime); }),
            flashes.end());
    }

    void GameScene::doProjectileImpact(const SimVector& position, const std::string& weaponType, ImpactType impactType)
    {
        playWeaponImpactSound(simVectorToFloat(position), weaponType, impactType);
        spawnWeaponImpactExplosion(simVectorToFloat(position), weaponType, impactType);
    }

    void GameScene::createLightSmoke(const Vector3f& position)
    {
        spawnSmoke(position, "FX", "smoke 1", ParticleFinishTimeEndOfFrames(), GameTime(2));
    }

    void GameScene::createWeaponSmoke(const Vector3f& position)
    {
        auto anim = sceneContext.textureService->getGafEntry("anims/FX.GAF", "smoke 1");
        spawnSmoke(position, "FX", "smoke 1", ParticleFinishTimeFixedTime{simulation.gameTime + GameTime(30)}, GameTime(15));
    }

    void GameScene::emitLightSmokeFromPiece(UnitId unitId, const std::string& pieceName)
    {
        auto position = simulation.getUnitPiecePosition(unitId, pieceName);
        spawnSmokePuff(simVectorToFloat(position), "smoke 1", 0.5f);
    }

    void GameScene::emitBlackSmokeFromPiece(UnitId unitId, const std::string& pieceName)
    {
        auto position = simulation.getUnitPiecePosition(unitId, pieceName);
        spawnSmokePuff(simVectorToFloat(position), "smoke 2", 0.5f);
    }

    float randomFloat(float low, float high)
    {
        return low + ((static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * (high - low));
    }

    void GameScene::emitWakeFromPiece(UnitId unitId, const std::string& pieceName, bool reverse, unsigned int rampPeriod)
    {
        const auto& unit = getUnit(unitId);
        if (!positionIsVisibleToLocalPlayer(unit.position))
        {
            // The original refuses every emit-sfx for a unit the local player
            // cannot see, before it works anything else out (0x480EEA).
            return;
        }

        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
        auto pieceTransform = toFloatMatrix(simulation.getUnitPieceTransform(unitId, pieceName));
        const auto& pieceMesh = gameMediaDatabase.getUnitPieceMesh(unitDefinition.objectName, pieceName).value().get();

        // All four wake types are one routine. The only thing that separates
        // Wake from ReverseWake is which of the emitting piece's two vertices
        // the foam starts at, which turns the drift round; the only thing that
        // separates 1 from 2 is the ramp period, and with it the life.
        auto firstVertex = pieceTransform * pieceMesh.firstVertexPosition;
        auto secondVertex = pieceTransform * pieceMesh.secondVertexPosition;
        auto emission = computeWakeEmission(firstVertex, secondVertex, reverse, rampPeriod);
        const auto& spawnPosition = emission.spawnPosition;
        const auto& velocity = emission.velocity;
        auto duration = emission.duration;

        // A whole number of world units on each of the three axes, y included
        // -- the original adds its roll to the high word of each coordinate,
        // so the scatter is never fractional and is not confined to the
        // horizontal.
        std::uniform_int_distribution<int> jitter(-3, 3);
        auto scattered = [&]() {
            return Vector3f(
                spawnPosition.x + static_cast<float>(jitter(effectsRng)),
                spawnPosition.y + static_cast<float>(jitter(effectsRng)),
                spawnPosition.z + static_cast<float>(jitter(effectsRng)));
        };

        // Two dots per call: one now and one on the following tick. The
        // emitter is due again the tick after it is created and then never
        // again, so the repetition rate is entirely up to the ship's script.
        //
        // Particles drift whether or not they have started, so the second one
        // is seeded a tick's travel upstream to land on the emitter's anchor
        // at the moment it appears, which is where the original puts it.
        spawnWake(scattered(), velocity, duration, rampPeriod, simulation.gameTime);
        spawnWake(scattered() - velocity, velocity, duration, rampPeriod, simulation.gameTime + GameTime(1));
    }

    void GameScene::modifyBuildQueue(UnitId unitId, const std::string& unitType, int count)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            unit->get().modifyBuildQueue(unitType, count);

            updateUnconfirmedBuildQueueDelta(unitId, unitType, -count);
            refreshBuildGuiTotal(unitId, unitType);
        }
    }

    void GameScene::modifyStockpileQueue(UnitId unitId, int count)
    {
        simulation.modifyStockpileQueue(unitId, count);

        auto it = unconfirmedStockpileDelta.find(unitId);
        if (it != unconfirmedStockpileDelta.end())
        {
            it->second -= count;
            if (it->second == 0)
            {
                unconfirmedStockpileDelta.erase(it);
            }
        }
    }

    struct CorpseSpawnInfo
    {
        std::string featureName;
        SimVector position;
        SimAngle rotation;
    };

    void GameScene::processActions()
    {
        for (auto& a : actions)
        {
            if (!a)
            {
                continue;
            }

            if (sceneTime < a->triggerTime)
            {
                continue;
            }

            a->callback();
            a = std::nullopt;
        }
    }

    void GameScene::processPlayerCommands(const std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>& commands)
    {
        for (const auto& [issuingPlayer, playerCommands] : commands)
        {
            for (const auto& command : playerCommands)
            {
                processPlayerCommand(issuingPlayer, command);
            }
        }
    }

    void GameScene::attachOrdersMenuEventHandlers()
    {
        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "ATTACK"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<AttackCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "MOVE"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<MoveCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "DEFEND"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<GuardCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "REPAIR"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<RepairCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "PATROL"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<PatrolCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "CAPTURE"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<CaptureCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "LOAD"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<LoadCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "UNLOAD"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<UnloadCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "FIREORD"))
        {
            p->get().addSubscription(fireOrders.subscribe([&p = p->get()](const auto& v) {
                switch (v)
                {
                    case UnitFireOrders::HoldFire:
                        p.setStage(0);
                        break;
                    case UnitFireOrders::ReturnFire:
                        p.setStage(1);
                        break;
                    case UnitFireOrders::FireAtWill:
                        p.setStage(2);
                        break;
                    default:
                        throw std::logic_error("Invalid FireOrders value");
                } }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "ONOFF"))
        {
            p->get().addSubscription(onOff.subscribe([&p = p->get()](const auto& v) { p.setStage(v ? 1 : 0); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "CLOAK"))
        {
            p->get().addSubscription(cloak.subscribe([&p = p->get()](const auto& v) { p.setStage(v ? 1 : 0); }));
        }

        currentPanel->groupMessages().subscribe([this](const auto& msg) {
            if (auto activateMessage = std::get_if<ActivateMessage>(&msg.message); activateMessage != nullptr)
            {
                onMessage(msg.controlName, activateMessage->type);
            } });
    }

    UnitFireOrders nextFireOrders(UnitFireOrders orders)
    {
        switch (orders)
        {
            case UnitFireOrders::HoldFire:
                return UnitFireOrders::ReturnFire;
            case UnitFireOrders::ReturnFire:
                return UnitFireOrders::FireAtWill;
            case UnitFireOrders::FireAtWill:
                return UnitFireOrders::HoldFire;
            default:
                throw std::logic_error("Invalid UnitFireOrders value");
        }
    }

    void GameScene::onMessage(const std::string& message, ActivateMessage::Type type)
    {
        if (matchesWithSidePrefix("ATTACK", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<AttackCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(AttackCursorMode());
            }
        }
        else if (matchesWithSidePrefix("MOVE", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<MoveCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(MoveCursorMode());
            }
        }
        else if (matchesWithSidePrefix("DEFEND", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<GuardCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(GuardCursorMode());
            }
        }
        else if (matchesWithSidePrefix("STOP", message))
        {
            if (sounds.immediateOrders)
            {
                sceneContext.audioService->playSound(*sounds.immediateOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                cursorMode.next(NormalCursorMode());
                localPlayerStopUnit(selectedUnit);
            }
        }
        else if (matchesWithSidePrefix("RECLAIM", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<ReclaimCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(ReclaimCursorMode());
            }
        }
        else if (matchesWithSidePrefix("REPAIR", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<RepairCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(RepairCursorMode());
            }
        }
        else if (matchesWithSidePrefix("PATROL", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<PatrolCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(PatrolCursorMode());
            }
        }
        else if (matchesWithSidePrefix("CAPTURE", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<CaptureCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(CaptureCursorMode());
            }
        }
        else if (matchesWithSidePrefix("LOAD", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<LoadCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(LoadCursorMode());
            }
        }
        else if (matchesWithSidePrefix("UNLOAD", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<UnloadCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(UnloadCursorMode());
            }
        }
        else if (matchesWithSidePrefix("FIREORD", message))
        {
            if (sounds.setFireOrders)
            {
                sceneContext.audioService->playSound(*sounds.setFireOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                // FIXME: should set all to a consistent single fire order rather than advancing all
                auto& u = getUnit(selectedUnit);

                // The original gathers this button out of FireStandOrders and
                // skips any unit in the selection that does not name it, so a
                // transport picked up along with an escort keeps its own order
                // instead of being dragged round the cycle with everything else.
                if (!simulation.unitDefinitions.at(u.unitType).fireStandOrders)
                {
                    continue;
                }

                auto newFireOrders = nextFireOrders(u.fireOrders);
                localPlayerSetFireOrders(selectedUnit, newFireOrders);
            }
        }
        else if (matchesWithSidePrefix("ONOFF", message))
        {
            if (sounds.immediateOrders)
            {
                sceneContext.audioService->playSound(*sounds.immediateOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                auto& u = getUnit(selectedUnit);
                auto newOnOff = !u.activated;
                localPlayerSetOnOff(selectedUnit, newOnOff);
            }
        }
        else if (matchesWithSidePrefix("CLOAK", message))
        {
            if (sounds.immediateOrders)
            {
                sceneContext.audioService->playSound(*sounds.immediateOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                auto& u = getUnit(selectedUnit);

                // The original offers the button only where CloakCost is set,
                // so a mixed selection leaves everything else alone.
                if (!simulation.unitDefinitions.at(u.unitType).cloakable)
                {
                    continue;
                }

                auto newCloak = !u.cloakRequested;
                localPlayerSetCloak(selectedUnit, newCloak);
                if (auto singleUnit = getSingleSelectedUnit(); singleUnit && *singleUnit == selectedUnit)
                {
                    cloak.next(newCloak);
                }
            }
        }
        else if (matchesWithSidePrefix("NEXT", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.nextBuildMenu)
                {
                    sceneContext.audioService->playSound(*sounds.nextBuildMenu);
                }

                const auto& unit = getUnit(*selectedUnit);
                auto& guiInfo = getGuiInfo(*selectedUnit);
                auto pages = getBuildPageCount(builderGuisDatabase, unit.unitType);
                if (pages == 0)
                {
                    // No build pages for this unit at all; nothing to leaf through.
                    return;
                }
                guiInfo.currentBuildPage = (guiInfo.currentBuildPage + 1) % pages;

                auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
                if (buildPanelDefinition)
                {
                    setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                }
            }
        }
        else if (matchesWithSidePrefix("PREV", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.nextBuildMenu)
                {
                    sceneContext.audioService->playSound(*sounds.nextBuildMenu);
                }

                const auto& unit = getUnit(*selectedUnit);
                auto& guiInfo = getGuiInfo(*selectedUnit);
                auto pages = getBuildPageCount(builderGuisDatabase, unit.unitType);
                if (pages == 0)
                {
                    return;
                }
                guiInfo.currentBuildPage = guiInfo.currentBuildPage == 0 ? pages - 1 : guiInfo.currentBuildPage - 1;

                auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
                if (buildPanelDefinition)
                {
                    setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                }
            }
        }
        else if (matchesWithSidePrefix("BUILD", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.buildButton)
                {
                    sceneContext.audioService->playSound(*sounds.buildButton);
                }

                const auto& unit = getUnit(*selectedUnit);
                auto& guiInfo = getGuiInfo(*selectedUnit);
                guiInfo.section = UnitGuiInfo::Section::Build;

                auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
                if (buildPanelDefinition)
                {
                    setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                }
            }
        }
        else if (matchesWithSidePrefix("ORDERS", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.ordersButton)
                {
                    sceneContext.audioService->playSound(*sounds.ordersButton);
                }

                auto& guiInfo = getGuiInfo(*selectedUnit);
                guiInfo.section = UnitGuiInfo::Section::Orders;

                setNextPanel(createOrdersPanel());
            }
        }
        else if (isStockpileButtonName(message))
        {
            // A launcher's build page has one live button and it orders a round
            // rather than a unit, so the original tests for it before it tries
            // the name as a unit type (0x419B3C, ahead of the lookup at
            // 0x419B61) and turns it into the same "BUILDWEAPON" command
            // whichever of the two names it matched.
            if (sounds.addBuild)
            {
                sceneContext.audioService->playSound(*sounds.addBuild);
            }

            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                int count = (isShiftDown() ? 5 : 1) * (type == ActivateMessage::Type::Primary ? 1 : -1);
                localPlayerModifyStockpile(*selectedUnit, count);
            }
        }
        else if (isValidUnitType(simulation, message))
        {
            if (sounds.addBuild)
            {
                sceneContext.audioService->playSound(*sounds.addBuild);
            }

            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                const auto& unit = getUnit(*selectedUnit);
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                if (unitDefinition.isMobile)
                {
                    cursorMode.next(BuildCursorMode{message});
                }
                else
                {
                    int count = (isShiftDown() ? 5 : 1) * (type == ActivateMessage::Type::Primary ? 1 : -1);
                    localPlayerModifyBuildQueue(*selectedUnit, message, count);
                }
            }
        }
    }

    bool GameScene::matchesWithSidePrefix(const std::string& suffix, const std::string& value) const
    {
        for (const auto& [_, side] : *sceneContext.sideData)
        {
            if (side.namePrefix + suffix == value)
            {
                return true;
            }
        }

        return false;
    }

    std::optional<UnitId> GameScene::getSingleSelectedUnit() const
    {
        return selectedUnits.size() == 1
            ? std::make_optional(*selectedUnits.begin())
            : std::nullopt;
    }

    void GameScene::selectUnitsInBandbox(const DiscreteRect& box)
    {
        const auto cameraPos = worldCameraState.getRoundedPosition();
        auto cameraBox = box.translate(-cameraPos.x, -cameraPos.z);
        const auto& matrix = computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        std::unordered_set<UnitId> units;

        for (const auto& e : simulation.units)
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(e.second.unitType);
            if (!e.second.isSelectableBy(unitDefinition, localPlayerId))
            {
                continue;
            }

            const auto& worldPos = e.second.position;
            auto clipPos = matrix * simVectorToFloat(worldPos);
            Point viewportPos = worldViewport.toViewportSpace(clipPos.x, clipPos.y);
            if (!cameraBox.contains(viewportPos))
            {
                continue;
            }

            units.insert(e.first);
        }

        if (isShiftDown())
        {
            toggleUnitSelection(units);
        }
        else
        {
            replaceUnitSelection(units);
        }
    }

    void GameScene::selectAllOnScreen()
    {
        // Compute the camera's visible world rectangle directly. Matrix-based
        // projection here doesn't perform the perspective divide (see
        // Matrix4x.h:506), so we'd otherwise have no reliable on-screen test.
        const auto cameraPos = worldCameraState.getRoundedPosition();
        const float halfWidth = worldCameraState.scaleDimension(worldViewport.width()) / 2.0f;
        const float halfHeight = worldCameraState.scaleDimension(worldViewport.height()) / 2.0f;
        const float minX = cameraPos.x - halfWidth;
        const float maxX = cameraPos.x + halfWidth;
        const float minZ = cameraPos.z - halfHeight;
        const float maxZ = cameraPos.z + halfHeight;

        std::unordered_set<UnitId> units;
        for (const auto& e : simulation.units)
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(e.second.unitType);
            if (!e.second.isSelectableBy(unitDefinition, localPlayerId))
            {
                continue;
            }

            const auto worldPos = simVectorToFloat(e.second.position);
            if (worldPos.x < minX || worldPos.x > maxX
                || worldPos.z < minZ || worldPos.z > maxZ)
            {
                continue;
            }

            units.insert(e.first);
        }

        replaceUnitSelection(units);
    }

    void GameScene::toggleUnitSelection(const rwe::UnitId& unitId)
    {
        auto it = selectedUnits.find(unitId);
        if (it != selectedUnits.end())
        {
            deselectUnit(unitId);
            return;
        }

        selectAdditionalUnit(unitId);
    }

    void GameScene::toggleUnitSelection(const std::unordered_set<UnitId>& units)
    {
        std::unordered_set<UnitId> newSelection(selectedUnits);
        for (const auto& unitId : units)
        {
            auto [it, inserted] = newSelection.insert(unitId);
            if (!inserted)
            {
                newSelection.erase(it);
            }
        }

        replaceUnitSelection(newSelection);
    }

    void GameScene::selectAdditionalUnit(const rwe::UnitId& unitId)
    {
        selectedUnits.insert(unitId);

        const auto& unit = getUnit(unitId);
        auto selectionSound = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Select1);
        if (selectionSound)
        {
            playUiSound(*selectionSound);
        }

        onSelectedUnitsChanged();
    }

    void GameScene::replaceUnitSelection(const UnitId& unitId)
    {
        selectedUnits.clear();
        selectAdditionalUnit(unitId);
    }

    void GameScene::deselectUnit(const UnitId& unitId)
    {
        selectedUnits.erase(unitId);
        onSelectedUnitsChanged();
    }

    void GameScene::clearUnitSelection()
    {
        selectedUnits.clear();
        onSelectedUnitsChanged();
    }

    void GameScene::replaceUnitSelection(const std::unordered_set<UnitId>& units)
    {
        selectedUnits = units;

        if (selectedUnits.size() == 1)
        {
            const auto& unit = getUnit(*units.begin());
            auto selectionSound = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Select1);
            if (selectionSound)
            {
                playUiSound(*selectionSound);
            }
        }
        else if (selectedUnits.size() > 0)
        {
            if (sounds.selectMultipleUnits)
            {
                playUiSound(*sounds.selectMultipleUnits);
            }
        }

        onSelectedUnitsChanged();
    }

    void GameScene::onSelectedUnitsChanged()
    {
        if (selectedUnits.empty())
        {
            const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
            setNextPanel(uiFactory.panelFromGuiFile(sidePrefix + "MAIN2"));
        }
        else if (auto unitId = getSingleSelectedUnit(); unitId)
        {
            // Use tryGetUnit: when several units in the selection self-destruct
            // in the same tick, the first UnitDiedEvent triggers this callback
            // while the remaining selected units have already been removed from
            // the simulation but not yet from selectedUnits.
            auto unitRef = tryGetUnit(*unitId);
            if (!unitRef)
            {
                return;
            }
            const auto& unit = unitRef->get();
            fireOrders.next(unit.fireOrders);
            onOff.next(unit.activated);
            cloak.next(unit.cloakRequested);

            const auto& guiInfo = getGuiInfo(*unitId);
            auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
            if (guiInfo.section == UnitGuiInfo::Section::Build && buildPanelDefinition)
            {
                setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
            }
            else
            {
                setNextPanel(createOrdersPanel());
            }
        }
        else
        {
            setNextPanel(createOrdersPanel());
        }
    }

    UnitGuiInfo& GameScene::getGuiInfo(const UnitId& unitId)
    {
        auto it = unitGuiInfos.find(unitId);
        if (it != unitGuiInfos.end())
        {
            return it->second;
        }

        // No panel state yet. This used to throw, which took the game down
        // whenever a unit was selected before its spawn event had been
        // processed — placing one from the debug window and clicking it
        // straight away, for instance. Set it up on the spot instead: a
        // builder opens on its build page, anything else on its orders.
        auto section = UnitGuiInfo::Section::Orders;
        if (auto unit = tryGetUnit(unitId))
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(unit->get().unitType);
            if (unitDefinition.builder)
            {
                section = UnitGuiInfo::Section::Build;
            }
        }
        return unitGuiInfos.insert_or_assign(unitId, UnitGuiInfo{section, 0}).first->second;
    }

    void GameScene::setNextPanel(std::unique_ptr<UiPanel>&& panel)
    {
        nextPanel = std::move(panel);
    }

    void GameScene::refreshBuildGuiTotal(UnitId unitId, const std::string& unitType)
    {
        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit == unitId)
        {
            const auto& unit = getUnit(*selectedUnit);
            auto total = unit.getBuildQueueTotal(unitType) + getUnconfirmedBuildQueueCount(unitId, unitType);
            auto button = currentPanel->find<UiStagedButton>(unitType);
            if (button)
            {
                button->get().setLabel(total > 0 ? "+" + std::to_string(total) : "");
            }
        }
    }

    void GameScene::updateUnconfirmedBuildQueueDelta(UnitId unitId, const std::string& unitType, int count)
    {
        auto it = unconfirmedBuildQueueDelta.find(unitId);
        if (it == unconfirmedBuildQueueDelta.end())
        {
            unconfirmedBuildQueueDelta.emplace(unitId, std::unordered_map<std::string, int>{{unitType, count}});
        }
        else
        {
            auto it2 = it->second.find(unitType);
            if (it2 == it->second.end())
            {
                it->second.emplace(unitType, count);
            }
            else
            {
                int newTotal = it2->second + count;
                if (newTotal != 0)
                {
                    it2->second = newTotal;
                }
                else
                {
                    it->second.erase(it2);
                }
            }
        }
    }

    int GameScene::getUnconfirmedBuildQueueCount(UnitId unitId, const std::string& unitType) const
    {
        auto it = unconfirmedBuildQueueDelta.find(unitId);
        if (it == unconfirmedBuildQueueDelta.end())
        {
            return 0;
        }

        auto it2 = it->second.find(unitType);
        if (it2 == it->second.end())
        {
            return 0;
        }

        return it2->second;
    }

    void GameScene::refreshStockpileGuiTotal()
    {
        auto selectedUnit = getSingleSelectedUnit();
        if (!selectedUnit)
        {
            return;
        }

        auto weapon = simulation.tryGetStockpileWeapon(*selectedUnit);
        if (!weapon)
        {
            return;
        }

        auto it = unconfirmedStockpileDelta.find(*selectedUnit);
        auto queued = weapon->get().queuedRounds + (it == unconfirmedStockpileDelta.end() ? 0 : it->second);
        auto label = stockpileButtonLabel(weapon->get().stockedRounds, std::max(0, queued));

        // The gadget's name is whatever the launcher's own GUI file called it,
        // so it has to be found the way the original's readout loop finds it:
        // by walking the panel rather than by looking a name up.
        for (const auto& child : currentPanel->getChildren())
        {
            if (isStockpileButtonName(child->getName()))
            {
                if (auto button = dynamic_cast<UiStagedButton*>(child.get()); button != nullptr)
                {
                    button->setLabel(label);
                }
            }
        }
    }

    std::unique_ptr<UiPanel> GameScene::createBuildPanel(const std::string& guiName, const std::vector<GuiEntry>& buildPanelDefinition, const std::unordered_map<std::string, int>& totals)
    {
        auto panel = uiFactory.panelFromGuiFile(guiName, buildPanelDefinition);
        for (const auto& e : totals)
        {
            auto button = panel->find<UiStagedButton>(e.first);
            if (button)
            {
                button->get().setLabel("+" + std::to_string(e.second));
            }
        }

        return panel;
    }

    void GameScene::processPlayerCommand(PlayerId issuingPlayer, const PlayerCommand& playerCommand)
    {
        match(
            playerCommand,
            [&](const PlayerUnitCommand& c) {
                processUnitCommand(c);
            },
            [&](const PlayerPauseGameCommand&) {
                // Pause is open to any player. The local player toggles
                // `paused` immediately in the key handler so the tick loop
                // can resume to process the unpause; ignoring the round-tripped
                // command here prevents a stale pause from re-applying after
                // the user has already unpaused.
                if (issuingPlayer != localPlayerId)
                {
                    paused = true;
                }
            },
            [&](const PlayerUnpauseGameCommand&) {
                if (issuingPlayer != localPlayerId)
                {
                    paused = false;
                }
            },
            [&](const PlayerSetGameSpeedCommand& c) {
                // Host-authoritative: only honor speed changes from player 0.
                // Non-host requests are silently dropped.
                if (issuingPlayer == PlayerId(0))
                {
                    gameSpeed = GameSpeed(c.speedIndex);
                }
            });
    }

    void GameScene::processUnitCommand(const PlayerUnitCommand& unitCommand)
    {
        match(
            unitCommand.command,
            [&](const PlayerUnitCommand::IssueOrder& c) {
                switch (c.issueKind)
                {
                    case PlayerUnitCommand::IssueOrder::IssueKind::Immediate:
                        issueUnitOrder(unitCommand.unit, c.order);
                        break;
                    case PlayerUnitCommand::IssueOrder::IssueKind::Queued:
                        enqueueUnitOrder(unitCommand.unit, c.order);
                        break;
                }
            },
            [&](const PlayerUnitCommand::ModifyBuildQueue& c) {
                modifyBuildQueue(unitCommand.unit, c.unitType, c.count);
            },
            [&](const PlayerUnitCommand::ModifyStockpile& c) {
                modifyStockpileQueue(unitCommand.unit, c.count);
            },
            [&](const PlayerUnitCommand::Stop&) {
                stopUnit(unitCommand.unit);
            },
            [&](const PlayerUnitCommand::SetFireOrders& c) {
                setFireOrders(unitCommand.unit, c.orders);
            },
            [&](const PlayerUnitCommand::SetOnOff& c) {
                if (c.on)
                {
                    simulation.activateUnit(unitCommand.unit);
                }
                else
                {
                    simulation.deactivateUnit(unitCommand.unit);
                }
            },
            [&](const PlayerUnitCommand::SetCloak& c) {
                // This only records what the unit is asking for. Whether it
                // actually cloaks is settled a second at a time by the energy
                // and by how close the nearest enemy is standing.
                if (auto unit = tryGetUnit(unitCommand.unit); unit)
                {
                    unit->get().cloakRequested = c.cloaked;
                }
            },
            [&](const PlayerUnitCommand::CancelBuildOrder& c) {
                cancelBuildOrderAt(unitCommand.unit, c.position);
            },
            [&](const PlayerUnitCommand::SelfDestruct&) {
                // Starts the countdown, or cancels it if pressed again.
                simulation.toggleSelfDestruct(unitCommand.unit);
            });
    }

    bool GameScene::leftClickMode() const
    {
        return sceneContext.globalConfig->leftClickInterfaceMode;
    }

    void GameScene::spawnExplosion(const Vector3f& position, const AnimLocation& anim)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = Vector3f(0.0f, 0.0f, 0.0f);
        particle.renderType = ParticleRenderTypeSprite{
            anim.gafName,
            anim.animName,
            ParticleFinishTimeEndOfFrames(),
            GameTime(2),
            false,
        };
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::spawnFlash(const Vector3f& position)
    {
        FlashEffect flash;
        flash.position = position;
        flash.startTime = simulation.gameTime;
        flash.duration = GameTime(15);
        flash.maxRadius = 30.0f;
        flash.color = Vector3f(1.0f, 1.0f, 1.0f);
        flash.maxIntensity = 1.0f;
        flashes.push_back(flash);
    }

    void GameScene::spawnSmoke(const Vector3f& position, const std::string& gaf, const std::string& anim, ParticleFinishTime duration, GameTime frameDuration)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = Vector3f(0.0f, 0.5f, 0.0f);
        particle.renderType = ParticleRenderTypeSprite{
            gaf,
            anim,
            duration,
            frameDuration,
            true,
        };
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::spawnSmokePuff(const Vector3f& position, const std::string& anim, float riseRate)
    {
        auto numberOfFrames = static_cast<int>(gameMediaDatabase.getSpriteSeries("FX", anim).value()->sprites.size());

        Particle particle;
        particle.position = position;

        // The original lifts a puff by some multiple of the map's gravity
        // every tick -- four for a damaged unit, sixteen for a thermal vent.
        // On the 112 that nearly every shipped map uses four works out at
        // 0.498 world units, so the callers here pass half a unit and two,
        // which is the right answer for all but a handful of maps and saves
        // threading the map's gravity through to reach.
        particle.velocity = Vector3f(0.0f, riseRate, 0.0f);

        particle.renderType = ParticleRenderTypeSprite{
            "FX",
            anim,
            ParticleFinishTimeEndOfFrames(),
            GameTime(2),
            true,
            false,
            makeSmokePuffFrameSchedule(numberOfFrames, [](int n) { return std::rand() % n; }),
        };
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::spawnGeoVentSteam()
    {
        // Every vent puffs on the same ticks because the original makes all
        // their emitters on the same tick, at map load, and each then counts
        // its own five ticks from there.
        if (simulation.gameTime.value % geoVentSteamIntervalTicks != 0)
        {
            return;
        }

        for (const auto& point : findGeoVentSteamPoints(simulation))
        {
            spawnSmokePuff(point, "smoke 1", geoVentSteamRiseRate);
        }
    }

    void GameScene::spawnWake(const Vector3f& position, const Vector3f& velocity, GameTime duration, unsigned int rampPeriod, GameTime startTime)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = velocity;
        particle.renderType = ParticleRenderTypeWake{startTime + duration, rampPeriod};
        particle.startTime = startTime;

        particles.push_back(particle);
    }

    void GameScene::spawnNanoParticles()
    {
        for (const auto& [_, unit] : simulation.units)
        {
            auto nanolatheTarget = unit.getActiveNanolatheTarget();
            if (!nanolatheTarget || !unitIsVisibleToLocalPlayer(unit))
            {
                continue;
            }

            // Where the spray lands. The original samples a uniform point in
            // the target's bounding box, shrunk first to the middle three
            // elevenths of each axis, so the stream fans across the middle of
            // what is being worked on rather than converging on a point.
            //
            // The one place we depart from it is the height: the original
            // samples inside the model too, which it can afford because its
            // spray is composited over the world in a late layer. Ours is
            // depth tested so that a construction aircraft can cover its own
            // beam, so it lands on the roof instead and the structure cannot
            // swallow the end of the stream.
            std::optional<Vector3f> targetCentre;
            // The width and depth of the target we scatter the landing point over.
            // Nothing in y: the height is already in targetCentre, because we
            // land on the roof rather than inside the model.
            Vector3f spread(0.0f, 0.0f, 0.0f);
            bool reclaimingFeature = false;
            match(
                std::get<0>(*nanolatheTarget),
                [&](const UnitId& targetUnitId) {
                    auto targetUnit = tryGetUnit(targetUnitId);
                    if (!targetUnit)
                    {
                        return;
                    }
                    const auto& targetDefinition = simulation.unitDefinitions.at(targetUnit->get().unitType);
                    const auto& targetModel = simulation.unitModelDefinitions.at(targetDefinition.objectName);
                    auto height = simScalarToFloat(targetModel.height);
                    targetCentre = simVectorToFloat(targetUnit->get().position) + Vector3f(0.0f, height, 0.0f);
                    auto [footprintX, footprintZ] = simulation.getFootprintXZ(targetDefinition.movementCollisionInfo);
                    auto tile = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
                    spread = Vector3f(static_cast<float>(footprintX) * tile, 0.0f, static_cast<float>(footprintZ) * tile);
                },
                [&](const FeatureId& targetFeatureId) {
                    auto targetFeature = simulation.tryGetFeature(targetFeatureId);
                    if (!targetFeature)
                    {
                        return;
                    }
                    const auto& featureDefinition = simulation.getFeatureDefinition(targetFeature->get().featureName);
                    auto height = simScalarToFloat(featureDefinition.height);
                    targetCentre = simVectorToFloat(targetFeature->get().position) + Vector3f(0.0f, height, 0.0f);
                    auto tile = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
                    spread = Vector3f(static_cast<float>(featureDefinition.footprintX) * tile, 0.0f, static_cast<float>(featureDefinition.footprintZ) * tile);
                    // The original runs two emitters when reclaiming a feature.
                    reclaimingFeature = true;
                });
            if (!targetCentre)
            {
                continue;
            }

            auto nozzle = simVectorToFloat(std::get<1>(*nanolatheTarget));
            std::uniform_real_distribution<float> centralThird(-3.0f / 11.0f, 3.0f / 11.0f);

            // The original emits a burst of five every tick, and each emitter
            // fires again on the following tick, so ten particles a tick are in
            // flight. Reclaiming a feature runs two emitters, which is what
            // makes a reclaim stream look twice as thick as a build stream.
            const int burstSize = 5;
            const int burstsPerTick = 2;
            auto emitters = reclaimingFeature ? 2 : 1;
            for (int emitter = 0; emitter < emitters; ++emitter)
            {
                for (int burst = 0; burst < burstsPerTick; ++burst)
                {
                    for (int i = 0; i < burstSize; ++i)
                    {
                        // The nozzle end has no scatter at all; the far end is
                        // a uniform point in the middle three elevenths of the
                        // target's bounding box, which is the window the
                        // original shrinks the box to before sampling it.
                        auto landing = *targetCentre + Vector3f(centralThird(effectsRng) * spread.x, 0.0f, centralThird(effectsRng) * spread.z);

                        // Each particle starts one place further along the
                        // seven-colour cycle than the last.
                        auto colorPhase = static_cast<unsigned char>(i % 7);

                        switch (std::get<2>(*nanolatheTarget))
                        {
                            case UnitState::NanolatheDirection::Forward:
                                spawnNanoParticle(nozzle, landing, colorPhase);
                                break;
                            case UnitState::NanolatheDirection::Reverse:
                                spawnNanoParticle(landing, nozzle, colorPhase);
                                break;
                            default:
                                throw std::logic_error("unhandled nanolathe direction");
                        }
                    }
                }
            }
        }
    }

    void GameScene::spawnNanoParticle(const Vector3f& from, const Vector3f& to, unsigned char colorPhase)
    {
        // Four world units a tick, as in the original: it divides the distance
        // by four to get the step count, then walks the particle along one step
        // per tick. A target closer than one step gets no particle at all.
        const float speed = 4.0f;
        auto delta = to - from;
        auto ticks = static_cast<int>(delta.length() / speed);
        if (ticks < 1)
        {
            return;
        }

        Particle particle;
        particle.position = from;
        particle.velocity = delta / static_cast<float>(ticks);
        particle.renderType = ParticleRenderTypeNano{
            simulation.gameTime + GameTime(ticks),
            colorPhase,
            // The original fills a two pixel square, which at one world unit
            // per pixel is a half-size of one.
            1.0f,
            // No nudge towards the camera: the spray leaves a nozzle
            // underneath a construction aircraft, so the aircraft has to be
            // able to cover it. It clears the structure by landing on top of
            // it instead — see where the target point is chosen above.
            0.0f};
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::emitVtolFromPiece(UnitId unitId, const std::string& pieceName, unsigned int divisor)
    {
        // `Thrust` and `Vtol` are the same emitter with one number changed:
        // the original passes 6 for one and 7 for the other, and that number
        // is both the divisor for the drift and the emitter's own lifetime,
        // so a thrust plume is one puff longer and each puff moves a little
        // more slowly. Nothing else about them differs.
        const auto& unit = getUnit(unitId);
        if (!positionIsVisibleToLocalPlayer(unit.position))
        {
            return;
        }
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
        auto pieceTransform = toFloatMatrix(simulation.getUnitPieceTransform(unitId, pieceName));
        const auto& pieceMesh = gameMediaDatabase.getUnitPieceMesh(unitDefinition.objectName, pieceName).value().get();

        // A thruster piece is a bare two-vertex segment running from hull
        // level down to a few units below it, and the exhaust comes out of
        // the bottom. TA's own models wind these inconsistently — on the
        // Atlas, two jets run bottom to top and the third runs top to bottom
        // — so taking the difference would have one rotor spraying upwards.
        // Take the lower end and blow downwards instead.
        auto a = pieceTransform * pieceMesh.firstVertexPosition;
        auto b = pieceTransform * pieceMesh.secondVertexPosition;
        const auto& lowerEnd = a.y <= b.y ? a : b;
        const auto& upperEnd = a.y <= b.y ? b : a;

        // TA draws this effect with a sprite, not with coloured dots: the
        // engine's handler for emit-sfx type 0 hands the piece's two vertices
        // to a particle that plays the `flamestream` sequence out of
        // anims/FX.GAF, which starts as a two pixel yellow speck and swells,
        // frame by frame, into a ragged yellow flame about nine pixels
        // across. The engine divides the piece vector by six for the drift
        // per tick and gives the particle six ticks to live, so one puff
        // crosses the length of the thruster while it grows.
        auto velocity = (lowerEnd - upperEnd) / static_cast<float>(divisor);

        // TA drops one of these every tick and lets a whole run of them die
        // together, so the plume is a graded line with the biggest, oldest
        // flame furthest from the nozzle. The script only calls us every
        // other tick, so lay several at once and backdate the trailing ones:
        // each starts a frame further into the animation and a step further
        // down, which is exactly where the engine's own would have got to.
        const int particlesPerEmit = 4;
        const unsigned int lifeInTicks = divisor + 1;
        std::uniform_real_distribution<float> scatter(-0.75f, 0.75f);
        for (int i = 0; i < particlesPerEmit; ++i)
        {
            auto age = std::min(static_cast<unsigned int>(i), simulation.gameTime.value);

            Particle particle;
            particle.position = lowerEnd + (velocity * static_cast<float>(i)) + Vector3f(scatter(effectsRng), 0.0f, scatter(effectsRng));
            // The puffs keep no share of the aircraft's speed: they hang
            // where they were dropped, so the aircraft draws a trail out
            // behind itself as it flies on.
            particle.velocity = velocity;
            particle.startTime = simulation.gameTime - GameTime(age);
            particle.renderType = ParticleRenderTypeSprite{
                "FX",
                "flamestream",
                ParticleFinishTimeFixedTime{simulation.gameTime + GameTime(lifeInTicks - age)},
                // One animation frame per tick, so the flame grows as fast as
                // it falls, the way the original's does.
                GameTime(1),
                false,
                // Drawn among the world's geometry: the exhaust leaves from
                // under the hull, so the hull must cover it.
                true,
            };
            particles.push_back(particle);
        }
    }

    void GameScene::recreateWorldRenderTextures()
    {
        worldFrameBuffer = sceneContext.graphics->createFrameBuffer(worldViewport.width(), worldViewport.height());
        dodgeMask = sceneContext.graphics->createEmptyTexture(worldViewport.width(), worldViewport.height());
        worldRenderTextureSize = {worldViewport.width(), worldViewport.height()};
    }

    void GameScene::nudgeCamera(int millisecondsElapsed, const Rectangle2f& cameraConstraint, int directionX, int directionZ)
    {
        assert(directionX == 1 || directionX == 0 || directionX == -1);
        assert(directionZ == 1 || directionZ == 0 || directionZ == -1);

        // The player can only nudge the camera in free mode.
        // If the camera is in a different mode, try and transition out of it.
        cameraControlState = match(
            cameraControlState,
            [&](const CameraControlStateTrackingUnit&) -> CameraControlState {
                return CameraControlStateFree();
            },
            [&](const CameraControlStateFree& s) -> CameraControlState {
                return s;
            },
            [&](const CameraControlStateMiddleMousePan& s) -> CameraControlState {
                // Middle mouse pan takes precedence over nudging the camera.
                return s;
            });

        // Only nudge the camera if it is now in free mode.
        match(
            cameraControlState,
            [&](const CameraControlStateFree&) {
                const float speed = CameraPanSpeed * (static_cast<float>(scrollSpeedSetting) / 100.0f) * millisecondsElapsed / 1000.0f;

                auto dx = directionX * speed;
                auto dz = directionZ * speed;
                const auto& cameraPos = worldCameraState.position;
                auto newPos = cameraConstraint.clamp(Vector2f(cameraPos.x + dx, cameraPos.z + dz));

                worldCameraState.position = Vector3f(newPos.x, cameraPos.y, newPos.y);
            },
            [&](const CameraControlStateTrackingUnit&) {
                // do nothing
            },
            [&](const CameraControlStateMiddleMousePan&) {
                // do nothing
            });
    }

    void GameScene::relocateCamera(const Rectangle2f& cameraConstraint, float x, float z)
    {
        // The player can only relocate the camera in free mode.
        // If the camera is in a different mode, try and transition out of it.
        cameraControlState = match(
            cameraControlState,
            [&](const CameraControlStateTrackingUnit&) -> CameraControlState {
                return CameraControlStateFree();
            },
            [&](const CameraControlStateFree& s) -> CameraControlState {
                return s;
            },
            [&](const CameraControlStateMiddleMousePan& s) -> CameraControlState {
                // Middle mouse pan takes precedence over relocating the camera.
                return s;
            });

        auto newCameraPos = cameraConstraint.clamp(Vector2f(x, z));
        worldCameraState.position = Vector3f(newCameraPos.x, worldCameraState.position.y, newCameraPos.y);
    }

    std::optional<std::string> GameScene::getUnitBuildButtonUnderCursor() const
    {
        auto cursorPosition = getMousePosition();
        auto control = currentPanel->findAtPosition<UiStagedButton>(cursorPosition.x, cursorPosition.y);
        if (!control)
        {
            return std::nullopt;
        }

        const auto& name = control->get().getName();
        if (!isValidUnitType(simulation, name))
        {
            return std::nullopt;
        }

        return name;
    }
}
