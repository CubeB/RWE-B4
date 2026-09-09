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

// The drawing half of GameScene, split out of GameScene.cpp on 2026-09-08 --
// not for tidiness but because the single translation unit had stopped
// linking. A COFF object addresses its sections with a signed 16-bit index,
// so 32767 is the ceiling; at -O0 nothing is inlined and every implicitly
// instantiated template lands in its own COMDAT, each carrying .text$,
// .xdata$, .pdata$ and .debug_frame$. GameScene.cpp had reached 11466 such
// functions -- 46124 sections -- and the assembler quietly switched the
// object to the pe-bigobj format to hold them. Newer binutils reads that
// back; the toolchain on the MinGW64 CI runner did not, and dropped every
// COMDAT definition while keeping the references, so the Debug job failed
// with 1124 undefined symbols in four executables while Release, where the
// optimiser folds those instantiations away into 1388 sections, passed.
//
// So the rule this file exists to keep is a size one: no translation unit
// should need bigobj. Splitting the renderers out puts both halves back
// under the limit with room to spare. If a future addition pushes either
// one near it again, split again rather than relying on the format switch.

namespace rwe
{
#ifdef RWE_ENABLE_RENDERPROF
    namespace
    {
        // Per-phase frame timing, reported every two seconds, in the same
        // shape as the sim's SIMPROF line so the two can be read together.
        // The slots live in render/render_prof.h so RenderService and
        // SceneManager can add their own without a second mechanism; they are
        // zeroed rather than erased so a reference held at a call site stays
        // good.
        std::chrono::steady_clock::time_point renderProfLastReport = std::chrono::steady_clock::now();
        int renderProfFrames = 0;

        void renderProfReport()
        {
            ++renderProfFrames;
            auto now = std::chrono::steady_clock::now();
            auto span = std::chrono::duration<double, std::milli>(now - renderProfLastReport).count();
            if (span < 2000.0)
            {
                return;
            }
            auto frames = renderProfFrames == 0 ? 1 : renderProfFrames;
            std::string line;
            for (auto& [name, total] : renderProfTotals)
            {
                line += " " + name + "=" + std::to_string(static_cast<int>(total / frames * 1000.0)) + "us";
                total = 0.0;
            }
            for (auto& [name, total] : renderProfCounts)
            {
                line += " " + name + "=" + std::to_string(static_cast<int>(total / frames));
                total = 0.0;
            }
            LOG_INFO << "RENDERPROF frames=" << renderProfFrames
                     << " fps=" << static_cast<int>(renderProfFrames * 1000.0 / span)
                     << line;
            renderProfFrames = 0;
            renderProfLastReport = now;
        }
    }
#endif

    void GameScene::render()
    {
        {
            RWE_RENDERPROF("frame");

            if (guiVisible)
            {
                RWE_RENDERPROF("ui");
                renderUi();
            }

            sceneContext.graphics->enableDepthBuffer();

            {
                RWE_RENDERPROF("world");
                renderWorld();
            }
            sceneContext.graphics->disableDepthBuffer();

            if (guiVisible)
            {
                RWE_RENDERPROF("overlay");
                renderOverlay();
            }
        }
#ifdef RWE_ENABLE_RENDERPROF
        renderProfReport();
#endif

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

        const auto& localSideData = sceneContext.sideData->at(getPlayer(hudPlayerId()).side);

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
            auto playerColorIndex = getPlayer(hudPlayerId()).color;
            const auto& rect = localSideData.logo.toDiscreteRect();
            chromeUiRenderService.drawSpriteAbs(rect.x, rect.y, rect.width, rect.height, *(*logos)->sprites.at(playerColorIndex.value));
        }

        // The bars show the stockpile and nothing else. A stall is not
        // announced here: the original's bar keeps drawing the (empty)
        // stockpile in its usual colour, and the red consumption figure
        // beside it is the only sign. RWE used to flash the whole bar red
        // while stalled, which the original never does.

        // draw energy bar
        {
            const auto& rect = localSideData.energyBar.toDiscreteRect();
            const auto& localPlayer = getPlayer(hudPlayerId());
            auto rectWidth = localPlayer.maxEnergy == Energy(0) ? 0 : (rect.width * std::max(Energy(0), localPlayer.energy).value) / localPlayer.maxEnergy.value;
            const auto& colorIndex = localSideData.energyColor;
            const auto& color = sceneContext.palette->at(colorIndex);
            chromeUiRenderService.fillColor(rect.x, rect.y, rectWidth, rect.height, color);
        }
        {
            const auto& rect = localSideData.energy0;
            chromeUiRenderService.drawText(rect.x1, rect.y1, formatResource(Energy(0)), *guiFont);
        }
        {
            const auto& rect = localSideData.energyMax;
            auto text = formatResource(getPlayer(hudPlayerId()).maxEnergy);
            chromeUiRenderService.drawTextAlignRight(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.energyNum;
            auto text = formatResource(std::max(Energy(0), getPlayer(hudPlayerId()).energy));
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.energyProduced;
            auto text = formatResourceDelta(getPlayer(hudPlayerId()).previousEnergyProductionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(83, 223, 79));
        }
        {
            const auto& rect = localSideData.energyConsumed;
            auto text = formatResourceDelta(getPlayer(hudPlayerId()).previousDesiredEnergyConsumptionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(255, 71, 0));
        }

        // draw metal bar
        {
            const auto& rect = localSideData.metalBar.toDiscreteRect();
            const auto& localPlayer = getPlayer(hudPlayerId());
            auto rectWidth = localPlayer.maxMetal == Metal(0) ? 0 : (rect.width * std::max(Metal(0), localPlayer.metal).value) / localPlayer.maxMetal.value;
            const auto& colorIndex = localSideData.metalColor;
            const auto& color = sceneContext.palette->at(colorIndex);
            chromeUiRenderService.fillColor(rect.x, rect.y, rectWidth, rect.height, color);
        }
        {
            const auto& rect = localSideData.metal0;
            chromeUiRenderService.drawText(rect.x1, rect.y1, "0", *guiFont);
        }
        {
            const auto& rect = localSideData.metalMax;
            auto text = formatResource(getPlayer(hudPlayerId()).maxMetal);
            chromeUiRenderService.drawTextAlignRight(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.metalNum;
            auto text = formatResource(std::max(Metal(0), getPlayer(hudPlayerId()).metal));
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.metalProduced;
            auto text = formatResourceDelta(getPlayer(hudPlayerId()).previousMetalProductionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(83, 223, 79));
        }
        {
            const auto& rect = localSideData.metalConsumed;
            auto text = formatResourceDelta(getPlayer(hudPlayerId()).previousDesiredMetalConsumptionBuffer);
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

            auto stockpileWeapon = simulation.tryGetStockpileWeapon(*hoveredUnit);

            // The four rates, the kills line and the mission line are all
            // behind one ownership test in the original (0x46B119 compares
            // the unit's owner with the local player and jumps past the lot),
            // so an enemy shows you its name and its health and nothing else.
            if (unit.isOwnedBy(localPlayerId))
            {
                // Each rate is max(value, 0) before it is formatted: the
                // fcomp against the zero at 0x4FD568 in front of all four
                // sprintf calls. Metal takes one decimal place and energy
                // none, which is not tidying on RWE's part -- the format
                // strings really are "+%.1f"/"-%.1f" for metal (0x50788C,
                // 0x50787C) and "+%.0f"/"-%.0f" for energy (0x507884,
                // 0x507874).
                {
                    const auto& rect = localSideData.unitMetalMake;
                    auto text = "+" + formatResourceDelta(std::max(Metal(0), unit.getMetalMake()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(83, 223, 79));
                }
                {
                    const auto& rect = localSideData.unitMetalUse;
                    auto text = "-" + formatResourceDelta(std::max(Metal(0), unit.getMetalUse()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(255, 71, 0));
                }
                {
                    const auto& rect = localSideData.unitEnergyMake;
                    auto text = "+" + formatResourceDelta(std::max(Energy(0), unit.getEnergyMake()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(83, 223, 79));
                }
                {
                    const auto& rect = localSideData.unitEnergyUse;
                    auto text = "-" + formatResourceDelta(std::max(Energy(0), unit.getEnergyUse()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(255, 71, 0));
                }

                // Kills, and Veteran from the fifth kill on. There is no
                // rectangle for this in SIDEDATA.TDF: 0x46B2D6 takes the
                // damage bar's own left edge and its bottom plus two, so the
                // line hangs off the bar it belongs to. Colour is interface
                // slot 0x0F, which resolves to white.
                if (auto caption = killsCaption(unit.kills); !caption.empty())
                {
                    const auto& bar = localSideData.damageBar.toDiscreteRect();
                    chromeUiRenderService.drawText(
                        static_cast<float>(bar.x),
                        static_cast<float>(extraBottom + bar.y + bar.height + 2),
                        caption,
                        *guiFont,
                        Color(255, 255, 255));
                }

                {
                    // Not composed here and not composed there either: every
                    // mission record carries its display name at +0x00 and
                    // 0x439DF0 fetches the one belonging to the unit's
                    // current mission.
                    const auto& rect = localSideData.missionText;
                    auto weaponQueued = stockpileWeapon && stockpileWeapon->get().queuedRounds > 0;
                    auto text = missionDisplayName(unitActivity(unit, unit.isBeingBuilt(unitDefinition), weaponQueued));
                    chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, text, *guiFont);
                }
            }

            // The second name-and-bar slot: what this unit's current order is
            // pointed at. 0x46B445 asks 0x439D20 for a weapon build first and
            // falls through to 0x439DD0, the current mission's target unit, so
            // a builder shows what it is building and how far along it is, a
            // guard shows what it is guarding, and a launcher shows the round
            // on the way. Unlike the block above this is not owner-gated --
            // only the weapon branch is (0x46B471) -- though the bar still
            // honours hidedamage.
            {
                auto weaponPercent = 0;
                if (stockpileWeapon && unit.isOwnedBy(localPlayerId))
                {
                    const auto& weapon = stockpileWeapon->get();
                    const auto& weaponDefinition = simulation.weaponDefinitions.at(weapon.weaponType);

                    // ticksPaid * 100 / (reloadtime * 30), the arithmetic of
                    // 0x439D41-0x439D65 exactly: an integer percentage, and
                    // zero means there is nothing to show.
                    auto totalTicks = std::max(1, static_cast<int>(deltaSecondsToTicks(weaponDefinition.reloadTime).value));
                    weaponPercent = std::clamp(weapon.stockpileProgress * 100 / totalTicks, 0, 100);
                }

                if (weaponPercent > 0)
                {
                    {
                        const auto& rect = localSideData.unitName2;
                        chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, "Weapon", *guiFont);
                    }

                    const auto& bar = localSideData.damageBar2.toDiscreteRect();
                    chromeUiRenderService.drawHealthBar2(
                        static_cast<float>(bar.x),
                        static_cast<float>(extraBottom + bar.y),
                        static_cast<float>(bar.width),
                        static_cast<float>(bar.height),
                        static_cast<float>(weaponPercent) / 100.0f);
                }
                else if (auto targetId = unitOrderTargetUnit(unit); targetId)
                {
                    if (auto target = tryGetUnit(*targetId); target && unitIsDetectableByLocalPlayer(*targetId, target->get()))
                    {
                        const auto& targetUnit = target->get();
                        const auto& targetDefinition = simulation.unitDefinitions.at(targetUnit.unitType);

                        {
                            const auto& rect = localSideData.unitName2;
                            chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, targetDefinition.unitName, *guiFont);
                        }

                        if (targetUnit.isOwnedBy(localPlayerId) || !targetDefinition.hideDamage)
                        {
                            const auto& bar = localSideData.damageBar2.toDiscreteRect();
                            chromeUiRenderService.drawHealthBar2(
                                static_cast<float>(bar.x),
                                static_cast<float>(extraBottom + bar.y),
                                static_cast<float>(bar.width),
                                static_cast<float>(bar.height),
                                std::clamp(static_cast<float>(targetUnit.hitPoints) / static_cast<float>(targetDefinition.maxHitPoints), 0.0f, 1.0f));
                        }
                    }
                }
            }
        }
        // Resolved rather than dereferenced: the id is picked during update
        // and read again here, so the feature can be reclaimed in between --
        // which used to assert inside getFeature and take the game down.
        else if (auto hoveredFeatureState = tryGetHoveredFeature(simulation, hoveredFeature); hoveredFeatureState)
        {
            const auto& feature = hoveredFeatureState->get();
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

        // The menu screens are drawn over the game view -- the original folds
        // its options out across the world rather than tucking them behind
        // it. This has to be the overlay pass: renderUi runs before the world
        // and anything it draws outside the sidebar is painted over.
        for (auto& panel : gameMenuPanels)
        {
            panel->render(chromeUiRenderService);
        }

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
        if (fogSprite)
        {
            chromeUiRenderService.drawSpriteAbs(minimapRect, *fogSprite);
        }

        auto cameraInverse = computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        auto worldToMinimap = worldToMinimapMatrix(simulation.terrain, minimapRect);

        // draw minimap dots
        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unitIsDetectableByLocalPlayer(unitId, unit) || unit.carriedBy)
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

    namespace
    {
        /**
         * Liang-Barsky. The original's circle routine clips every segment to
         * the surface it is drawing on before it hands it to the Bresenham
         * (0x4C00C3 onward), which is what keeps a ring bigger than the
         * minimap inside the minimap. RWE draws its rings into the whole
         * chrome viewport, so without this a large ring paints over the top
         * bar and the panel.
         */
        bool clipSegmentToRect(Vector2f& a, Vector2f& b, const Rectangle2f& rect)
        {
            auto dx = b.x - a.x;
            auto dy = b.y - a.y;
            float t0 = 0.0f;
            float t1 = 1.0f;

            const float p[4] = {-dx, dx, -dy, dy};
            const float q[4] = {a.x - rect.left(), rect.right() - a.x, a.y - rect.top(), rect.bottom() - a.y};

            for (int i = 0; i < 4; ++i)
            {
                if (p[i] == 0.0f)
                {
                    if (q[i] < 0.0f)
                    {
                        return false;
                    }
                    continue;
                }

                auto t = q[i] / p[i];
                if (p[i] < 0.0f)
                {
                    t0 = std::max(t0, t);
                }
                else
                {
                    t1 = std::min(t1, t);
                }
            }

            if (t0 > t1)
            {
                return false;
            }

            auto ax = a.x;
            auto ay = a.y;
            a = Vector2f(ax + (t0 * dx), ay + (t0 * dy));
            b = Vector2f(ax + (t1 * dx), ay + (t1 * dy));
            return true;
        }

        /**
         * One ring as a list of clipped segments, ready for a single
         * drawLines call. The original's circle is a closed 32-gon of
         * one-pixel Bresenham lines with no thickness and no smoothing
         * (0x4C0070), and its dashed twin (0x4C01A0) draws alternate
         * segments with the starting parity taken from the minimap's blink
         * bit.
         */
        std::vector<Vector2f> buildMinimapRing(const Vector2f& centre, float radius, const Rectangle2f& clip, bool dashed, int parity)
        {
            const int segments = 32;
            std::vector<Vector2f> points;
            points.reserve(segments * 2);

            for (int i = 0; i < segments; ++i)
            {
                if (dashed && (i % 2) != parity)
                {
                    continue;
                }

                auto angleA = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(segments);
                auto angleB = (2.0f * Pif * static_cast<float>(i + 1)) / static_cast<float>(segments);
                Vector2f a(centre.x + (std::cos(angleA) * radius), centre.y + (std::sin(angleA) * radius));
                Vector2f b(centre.x + (std::cos(angleB) * radius), centre.y + (std::sin(angleB) * radius));
                if (clipSegmentToRect(a, b, clip))
                {
                    points.push_back(a);
                    points.push_back(b);
                }
            }

            return points;
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
        // The colours are bytes out of the interface colour table at
        // cfg+0xDCB, which S:50 resolved: it is filled from a different base
        // (0x4AC7D0 writes it as cfg+0x519+0x8B2) by nearest-matching
        // GUIPAL.PAL into the screen palette, and the two slots in play here
        // are 0x0A for the detectors and 0x0C for the jammers.
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

            auto centre = worldToMinimap * simVectorToFloat(unit.position);

            // The anti-missile coverage rings sit outside the on/off gate:
            // 0x466F6A jumps past the four detection rings and lands at
            // 0x46707C, which is where the coverage block starts. So a
            // switched-off radar loses its ring and a switched-off launcher
            // keeps its.
            renderMinimapCoverageRings(unit, unitDefinition, centre, worldUnitsToMinimapPixels);

            if (unitDefinition.onOffable && !unit.activated)
            {
                continue;
            }

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

                chromeUiRenderService.drawLines(
                    buildMinimapRing(Vector2f(centre.x, centre.y), radius, minimapRect, false, 0),
                    color);
            }
        }
    }

    void GameScene::renderMinimapCoverageRings(const UnitState& unit, const UnitDefinition& unitDefinition, const Vector3f& centre, float worldUnitsToMinimapPixels)
    {
        // 0x46707C: one ring per weapon slot whose weapon carries the
        // interceptor flag, radius coverage - 512, in interface colour 0x0F
        // (white). The minus 512 is literal in the binary and unexplained --
        // half a map square shaved off, so the drawn circle understates the
        // guaranteed intercept area rather than overstating it.
        //
        // Where RWE differs, and it is a data question rather than a choice:
        // the original gates the whole block on antiweapons, FBI flags bit 29,
        // which is a pure display flag -- nothing in its simulation reads it.
        // RWE has never parsed it, so the gate here is the interceptor weapon
        // itself. In the shipped data the two are the same set: ARMAMD and
        // CORFMD are the only units with antiweapons=1 and AMD_ROCKET and
        // FMD_ROCKET the only weapons with interceptor=1.
        for (const auto& weapon : unit.weapons)
        {
            if (!weapon)
            {
                continue;
            }

            const auto& weaponDefinition = simulation.weaponDefinitions.at(weapon->weaponType);
            if (!weaponDefinition.interceptor)
            {
                continue;
            }

            auto coverage = simScalarToFloat(weaponDefinition.coverage) - 512.0f;
            if (coverage <= 0.0f)
            {
                continue;
            }

            auto radius = coverage * worldUnitsToMinimapPixels;
            if (radius < 1.0f)
            {
                continue;
            }

            // Dashed while the launcher has a round in the magazine, solid
            // while it is empty: 0x4670D8 tests the magazine byte at
            // weaponSlot+0x0E and picks the dashed circle 0x4C01A0 over the
            // plain one 0x4C0070. Sixteen of the thirty-two segments are
            // drawn, and the parity comes from the minimap's blink bit, so
            // the gaps chase round the ring.
            // Eight frames a half-cycle: the counter at cfg+0x142EF is
            // reloaded with 7 and the bit flipped when it runs out
            // (0x466580), once per pass of the main loop, and RWE's scene
            // clock runs at the same thirty a second.
            const unsigned int minimapBlinkTicks = 8;
            auto dashed = weapon->stockedRounds > 0;
            auto parity = dashed && ((sceneTime.value / minimapBlinkTicks) % 2 == 1) ? 1 : 0;

            chromeUiRenderService.drawLines(
                buildMinimapRing(Vector2f(centre.x, centre.y), radius, minimapRect, dashed, parity),
                Color(255, 255, 255));
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

    void GameScene::renderCloakRadius(const UnitState& unit)
    {
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
        if (!unitDefinition.cloakable || !unit.cloaked)
        {
            return;
        }

        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());

        // Projected a point at a time rather than drawn as a screen-space
        // circle, so the ring lies on the ground the way the build boxes do
        // instead of standing up to face the camera.
        auto radius = static_cast<float>(unitDefinition.minCloakDistance);
        auto centre = simVectorToFloat(unit.position);

        constexpr int segments = 48;
        std::vector<Vector2f> points;
        points.reserve(segments);
        for (int i = 0; i < segments; ++i)
        {
            auto angle = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(segments);
            auto worldPoint = Vector3f(
                centre.x + (std::cos(angle) * radius),
                centre.y,
                centre.z + (std::sin(angle) * radius));
            auto uiPoint = worldToUi * worldPoint;
            points.emplace_back(uiPoint.x, uiPoint.y);
        }

        worldUiRenderService.drawLineLoop(points, Color(255, 255, 255));
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
                [&](const LandOnAirBaseOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const DgunOrder& o) {
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
                        [&](const SimVector& v) { return v; });
                },
                [&](const ResurrectOrder& o) {
                    const auto& f = simulation.tryGetFeature(o.target);
                    if (!f)
                    {
                        return pos;
                    }
                    return f->get().position;
                },
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
                [&](const ResurrectOrder&) { return std::optional<CursorType>(CursorType::Reclaim); },
                [&](const RepairOrder&) { return std::optional<CursorType>(CursorType::Repair); },
                [&](const PatrolOrder&) { return std::optional<CursorType>(CursorType::Patrol); },
                [&](const CaptureOrder&) { return std::optional<CursorType>(CursorType::Capture); },
                [&](const LoadOrder&) { return std::optional<CursorType>(CursorType::Load); },
                [&](const UnloadOrder&) { return std::optional<CursorType>(CursorType::Unload); },
                // The original has no cursor of its own for the D-gun; the
                // attack cursor is what CURSORS.GAF offers and what it uses.
                [&](const DgunOrder&) { return std::optional<CursorType>(CursorType::Attack); },
                // The original has a landing cursor of its own -- 0x43EA83
                // gives cursor 13 for an aircraft over an air base -- but RWE
                // has no sprite loaded for it, so the move cursor stands in.
                [&](const LandOnAirBaseOrder&) { return std::optional<CursorType>(CursorType::Move); });

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
                    [&](const ResurrectOrder&) { return true; },
                    [&](const RepairOrder&) { return true; },
                    [&](const PatrolOrder&) { return true; },
                    [&](const CaptureOrder&) { return true; },
                    [&](const LoadOrder&) { return true; },
                    [&](const UnloadOrder&) { return true; },
                    // No line, for the same reason an attack draws none.
                    [&](const DgunOrder&) { return false; },
                    [&](const LandOnAirBaseOrder&) { return true; });

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

        {
            RWE_RENDERPROF("w.fog");
            updateFogSprite();
        }

        sceneContext.graphics->bindFrameBuffer(worldFrameBuffer.frameBuffer.get());
        sceneContext.graphics->setViewport(
            0,
            0,
            worldViewport.width() * worldRenderTextureScale,
            worldViewport.height() * worldRenderTextureScale);

        sceneContext.graphics->clear();

        const auto& viewProjectionMatrix = computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        RenderService worldRenderService(sceneContext.graphics, sceneContext.shaders, &viewProjectionMatrix);

        sceneContext.graphics->disableDepthBuffer();

        // Fog of war is applied by the terrain shader: remembered ground goes
        // grey, unknown ground black, along the ragged boundary that TA's own
        // fog tiles have been rasterised into fogOverlayTexture. That texture
        // covers a window around the camera, not the whole map. With the fog
        // switched off it is rasterised from a fully revealed grid and comes
        // out empty, so the same path draws the plain terrain.
        std::optional<FogOverlay> fogOverlay;
        if (fogOverlayTexture.isValid())
        {
            fogOverlay = FogOverlay{fogOverlayTexture.get(), fogOverlayBounds.left(), fogOverlayBounds.top(), fogOverlayBounds.width(), fogOverlayBounds.height()};
        }
        {
            RWE_RENDERPROF("w.terrain");
            worldRenderService.drawMapTerrain(terrainGraphics, worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), fogOverlay);
        }

        SpriteBatch flatFeatureBatch;
        SpriteBatch flatFeatureShadowBatch;
        {
            RWE_RENDERPROF("w.flatfeat.build");
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
        }
        {
            RWE_RENDERPROF("w.flatfeat.draw");
            RWE_RENDERPROF_COUNT("n.flatfeat", flatFeatureBatch.sprites.size() + flatFeatureShadowBatch.sprites.size());
            worldRenderService.drawSpriteBatch(flatFeatureShadowBatch);
            worldRenderService.drawSpriteBatch(flatFeatureBatch);
        }

        {
            RWE_RENDERPROF("w.wake");
            wakeBatch.lines.clear();
            wakeBatch.triangles.clear();
            for (const auto& particle : particles)
            {
                drawWakeParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, wakeBatch);
            }
            RWE_RENDERPROF_COUNT("n.particles", particles.size());
            RWE_RENDERPROF_COUNT("n.waketri", wakeBatch.triangles.size());
            worldRenderService.drawBatch(wakeBatch, viewProjectionMatrix);
        }

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

        {
            RWE_RENDERPROF("w.terrainoverlay");
            worldRenderService.drawBatch(terrainOverlayBatch, viewProjectionMatrix);
        }

        auto interpolationFraction = static_cast<float>(millisecondsBuffer) / static_cast<float>(SimMillisecondsPerTick);
        ColoredMeshesBatch selectionRectBatch;
        {
            RWE_RENDERPROF("w.selection");
            for (const auto& selectedUnitId : selectedUnits)
            {
                const auto& unit = getUnit(selectedUnitId);
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                drawSelectionRect(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, interpolationFraction, selectionRectBatch);
            }
            worldRenderService.drawLineLoopsBatch(selectionRectBatch);
        }

        auto seaLevel = simulation.terrain.getSeaLevel();

        // What is off screen costs nothing from here on. Working out the
        // transform of every piece of every unit on the map, and handing the
        // driver a draw call for each, was the largest cost in the frame at
        // eight hundred units, and a screenful is a fraction of them.
        auto viewCull = makeViewCullTest(viewProjectionMatrix);

        // Everything below that draws a model draws it from the same four
        // atlases, so they are gathered once rather than named at each call.
        UnitTextureAtlases unitAtlases{unitTextureAtlas.get(), unitPaletteIndexAtlas.get(), &unitTeamTextureAtlases, &unitTeamPaletteIndexAtlases};

        UnitShadowMeshBatch unitShadowMeshBatch;
        {
            RWE_RENDERPROF("w.shadow.build");
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unitId, unit))
                {
                    continue;
                }
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                if (!shadowsEnabled || !unitCastsShadow(unitDefinition))
                {
                    continue;
                }
                // The original's second shadow bit gates the vehicle pass on
                // its own, so buildings keep casting when it is clear.
                if (unitDefinition.isMobile && !vehicleShadowsEnabled)
                {
                    continue;
                }
                const auto& modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);

                auto groundHeight = simulation.terrain.getHeightAt(unit.position.x, unit.position.z);
                if (unitDefinition.floater || unitDefinition.canHover)
                {
                    groundHeight = rweMax(groundHeight, seaLevel);
                }

                // The cull goes on where the shadow lands rather than on where
                // the unit is: an aircraft's shadow sits on the ground under it,
                // which can be well inside the view while the aircraft itself is
                // above the top of it.
                auto shadowPosition = Vector3f(simScalarToFloat(unit.position.x), simScalarToFloat(groundHeight), simScalarToFloat(unit.position.z));
                if (!viewCull.couldBeVisible(shadowPosition, ViewCullModelRadius))
                {
                    continue;
                }

                drawUnitShadow(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, modelDefinition, interpolationFraction, simScalarToFloat(groundHeight), unitAtlases, unitShadowMeshBatch);

                if (unit.isBeingBuilt(unitDefinition))
                {
                    // The frame is see-through while it is built, so the shadow
                    // would show through it. Keep only the part cast outside the
                    // model's own outline.
                    drawUnitSilhouette(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, modelDefinition, interpolationFraction, unitAtlases, false, unitShadowMeshBatch.cutouts);
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

                auto shadowPosition = Vector3f(simScalarToFloat(position.x), simScalarToFloat(groundHeight), simScalarToFloat(position.z));
                if (!viewCull.couldBeVisible(shadowPosition, ViewCullModelRadius))
                {
                    continue;
                }

                drawFeatureMeshShadow(simulation.unitModelDefinitions, gameMediaDatabase, viewProjectionMatrix, feature, simScalarToFloat(groundHeight), unitAtlases, unitShadowMeshBatch);
            }
        }
        {
            RWE_RENDERPROF("w.shadow.draw");
            RWE_RENDERPROF_COUNT("n.shadowmesh", unitShadowMeshBatch.meshes.size() + unitShadowMeshBatch.cutouts.size());
            worldRenderService.drawUnitShadowMeshBatch(unitShadowMeshBatch);
        }

        sceneContext.graphics->enableDepthBuffer();

        // The buildings on their own, for the halo the post pass puts round
        // them. Only the finished ones: a nanoframe is a see-through display
        // rather than a solid model, and the original anti-aliased the
        // non-animating part of a building, which is the part that is there.
        // With anti-aliasing off there is no downsample for the halo to have
        // come out of, so neither the mask nor the meshes that fill it are
        // worth building.
        auto haloWanted = antiAliasEnabled && buildingHaloStrength > 0;
        std::vector<UnitTextureMeshRenderInfo> buildingSilhouettes;

        UnitMeshBatch unitMeshBatch;
        {
            RWE_RENDERPROF("w.unit.build");
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unitId, unit))
                {
                    continue;
                }
                if (!viewCull.couldBeVisible(simVectorToFloat(unit.position), ViewCullModelRadius))
                {
                    continue;
                }
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                const auto& unitModelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);
                drawUnit(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, unitModelDefinition, getPlayer(unit.owner).color, unitId.value, simulation.gameTime.value, interpolationFraction, shadeStrengthFor(!unitDefinition.isMobile), unitAtlases, unitMeshBatch);

                if (haloWanted && !unitDefinition.isMobile && !unit.isBeingBuilt(unitDefinition))
                {
                    drawUnitSilhouette(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, unitModelDefinition, interpolationFraction, unitAtlases, true, buildingSilhouettes);
                }
            }
            for (const auto& [_, feature] : simulation.features)
            {
                if (!positionIsExploredByLocalPlayer(feature.position))
                {
                    continue;
                }
                if (!viewCull.couldBeVisible(simVectorToFloat(feature.position), ViewCullModelRadius))
                {
                    continue;
                }
                drawMeshFeature(simulation.unitModelDefinitions, gameMediaDatabase, viewProjectionMatrix, feature, shadeStrengthFor(true), unitAtlases, unitMeshBatch);
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
                drawDebrisPiece(gameMediaDatabase, viewProjectionMatrix, d.objectName, d.pieceName, matrix, d.color, shadeStrengthFor(false), unitAtlases, unitMeshBatch);
            }
        }
        {
            RWE_RENDERPROF("w.unit.draw");
            RWE_RENDERPROF_COUNT("n.unitmesh", unitMeshBatch.meshes.size() + unitMeshBatch.buildingMeshes.size() + unitMeshBatch.cloakedMeshes.size());
            worldRenderService.drawUnitMeshBatch(unitMeshBatch, simScalarToFloat(seaLevel), shadeTableTexture.get());
        }

        // Construction wireframe: the visible polygon edges of each nanoframe,
        // drawn with the depth test on so the model hides its own back. The
        // original outlines every primitive of every piece in its second build
        // colour, a triangle wave down palette entries 160..175 and back that
        // comes round about every half second, offset per unit.
        {
            RWE_RENDERPROF("w.wireframe");
            // The direction from the scene towards the camera, in world space.
            auto inverseViewProjection = computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
            auto toCamera = ((inverseViewProjection * Vector3f(0.0f, 0.0f, -1.0f)) - (inverseViewProjection * Vector3f(0.0f, 0.0f, 0.0f))).normalized();

            ColoredMeshBatch wireframeBatch;
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unitId, unit))
                {
                    continue;
                }
                if (!viewCull.couldBeVisible(simVectorToFloat(unit.position), ViewCullModelRadius))
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
        {
            RWE_RENDERPROF("w.projectiles");
            drawProjectiles(simulation, localPlayerVisibility(), gameMediaDatabase, viewProjectionMatrix, simulation.projectiles, simulation.gameTime, interpolationFraction, unitAtlases, lineProjectilesBatch, spriteProjectilesBatch, meshProjectilesBatch);
            worldRenderService.drawBatch(lineProjectilesBatch, viewProjectionMatrix);
            worldRenderService.drawUnitMeshBatch(meshProjectilesBatch, simScalarToFloat(seaLevel), shadeTableTexture.get());
            worldRenderService.drawSpriteBatch(spriteProjectilesBatch);
        }

        sceneContext.graphics->disableDepthWrites();

        SpriteBatch featureBatch;
        SpriteBatch featureShadowBatch;
        {
            RWE_RENDERPROF("w.feature.build");
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
        }
        {
            RWE_RENDERPROF("w.feature.draw");
            RWE_RENDERPROF_COUNT("n.featuresprite", featureBatch.sprites.size() + featureShadowBatch.sprites.size());
            worldRenderService.drawSpriteBatch(featureShadowBatch);
            worldRenderService.drawSpriteBatch(featureBatch);
        }

        // Particles that belong in the world rather than over it: drawn here,
        // while the depth test is still on, so what is in front of them hides
        // them. An aircraft's exhaust comes out from under the hull, and the
        // hull should cover it.
        {
            RWE_RENDERPROF("w.particles");
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
        {
            RWE_RENDERPROF("w.nano");
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
        }

        // The buildings' coverage, for the halo. This redraws geometry the
        // world pass has already drawn, so it needs GL_EQUAL and not GL_LESS:
        // unitMask.vert computes gl_Position with the same expression and the
        // same mvpMatrix as unitTexture.vert, so every fragment lands at
        // exactly the depth already stored, and under GL_LESS every one of
        // them is rejected and the mask comes out empty. It did, for a day --
        // the halo was invisible at every width and strength because it was
        // never drawn at all. Same idiom as the cloak pass in RenderService.
        //
        // Keeping the depth test (rather than turning it off) is what makes a
        // building behind a hill mark nothing and get no halo. Depth writes
        // are off because the world is finished with and the flashes pass
        // below shares the buffer.
        if (haloWanted)
        {
            RWE_RENDERPROF("w.halomask");
            sceneContext.graphics->useDepthTestEqual();
            sceneContext.graphics->disableDepthWrites();
            sceneContext.graphics->bindFrameBufferColorBuffer(buildingMask.get());
            sceneContext.graphics->clearColor();
            worldRenderService.drawUnitMaskBatch(buildingSilhouettes);
            sceneContext.graphics->bindFrameBufferColorBuffer(worldFrameBuffer.texture.get());
            sceneContext.graphics->enableDepthWrites();
            sceneContext.graphics->enableDepthTest();
        }

        sceneContext.graphics->disableDepthTest();

        {
            RWE_RENDERPROF("w.flashes");
            sceneContext.graphics->bindFrameBufferColorBuffer(dodgeMask.get());
            sceneContext.graphics->clearColor();
            worldRenderService.drawFlashes(simulation.gameTime, flashes);
            sceneContext.graphics->bindFrameBufferColorBuffer(worldFrameBuffer.texture.get());
        }

        sceneContext.graphics->unbindFrameBuffer();
        auto viewportPos = worldViewport.toOtherViewport(*sceneContext.viewport, 0, worldViewport.height());
        sceneContext.graphics->setViewport(
            viewportPos.x,
            sceneContext.viewport->height() - viewportPos.y,
            worldViewport.width(),
            worldViewport.height());

        sceneContext.graphics->disableDepthBuffer();
        {
            RWE_RENDERPROF("w.post");
            auto quadMesh = sceneContext.graphics->createUnitTexturedQuadFlipped(Rectangle2f::fromTLBR(1.0f, 0.0f, 0.0f, 1.0f));
            sceneContext.graphics->bindShader(sceneContext.shaders->worldPost.handle.get());
            sceneContext.graphics->setUniformInt(sceneContext.shaders->worldPost.dodgeMask, 1);
            sceneContext.graphics->setUniformInt(sceneContext.shaders->worldPost.buildingMask, 2);
            sceneContext.graphics->setUniformInt(sceneContext.shaders->worldPost.alphaTable, 3);
            sceneContext.graphics->setUniformFloat(sceneContext.shaders->worldPost.gamma, static_cast<float>(gammaSetting) / 100.0f);
            // With no supersampling there is no 2x buffer and so no downsample
            // for the halo to have come out of, which is exactly why the
            // original's went away with its own anti-aliasing switched off.
            sceneContext.graphics->setUniformFloat(sceneContext.shaders->worldPost.haloStrength, antiAliasEnabled ? static_cast<float>(buildingHaloStrength) / 100.0f : 0.0f);
            sceneContext.graphics->setUniformFloat(sceneContext.shaders->worldPost.haloSaturation, static_cast<float>(buildingHaloSaturation) / 100.0f);
            sceneContext.graphics->setUniformFloat(sceneContext.shaders->worldPost.haloRedShift, static_cast<float>(buildingHaloRedShift) / 100.0f);
            sceneContext.graphics->bindTexture(worldFrameBuffer.texture.get());
            sceneContext.graphics->setActiveTextureSlot1();
            sceneContext.graphics->bindTexture(dodgeMask.get());
            sceneContext.graphics->setActiveTextureSlot2();
            sceneContext.graphics->bindTexture(buildingMask.get());
            sceneContext.graphics->setActiveTextureSlot3();
            sceneContext.graphics->bindTexture(alphaTableTexture.get());
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
        }
        sceneContext.graphics->enableDepthTest();

        sceneContext.graphics->enableDepthWrites();

        // The sweep over a freshly placed building plays whether or not shift
        // is held: it is the acknowledgement of the click, and the original
        // shows it as soon as the order exists.
        RWE_RENDERPROF("w.worldui");
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

                // v3.1 feature 4: the cloaked unit under the cursor shows the
                // radius inside which an enemy will strip its cloak.
                renderCloakRadius(getUnit(*hoveredUnit));
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

            // The same height the building will stand at, which in the
            // original is the same number rather than merely the same rule:
            // the box reads what the legality test cached, or calls the
            // height routine itself. Taking the terrain under the centre
            // instead drew the box on the sea floor while the building went
            // to the surface, which is the gap a play-test saw under a tidal
            // generator.
            if (auto buildCursor = std::get_if<BuildCursorMode>(&cursorMode.getValue()); buildCursor != nullptr)
            {
                const auto& buildingDefinition = simulation.unitDefinitions.at(buildCursor->unitType);
                topLeftWorld.y = simulation.computeBuildHeight(buildingDefinition, hoverBuildInfo->rect);
            }
            else
            {
                topLeftWorld.y = simulation.terrain.getHeightAt(
                    topLeftWorld.x + ((SimScalar(hoverBuildInfo->rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
                    topLeftWorld.z + ((SimScalar(hoverBuildInfo->rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));
            }

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
            [&](const UnitBehaviorStateResurrecting&) {
                return "resurrecting";
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

        if (!battleTestPlayers.empty())
        {
            ImGui::Separator();
            ImGui::Text("Battle test");
            ImGui::SliderInt("Units per side", &battleTestUnitsPerSide, 0, 500);
            int total = 0;
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unit.isDead())
                {
                    ++total;
                }
            }

            // Per side as well as the total: the two numbers drifting apart
            // is how you see that one side cannot get out of its own spawn.
            std::string aliveText;
            for (std::size_t i = 0; i < battleTestAlive.size(); ++i)
            {
                if (i != 0)
                {
                    aliveText += " v ";
                }
                aliveText += std::to_string(battleTestAlive[i]);
            }
            ImGui::Text("alive: %s (%d in the world)", aliveText.c_str(), total);
            ImGui::Text("spawned %u, blocked %u, culled %u, re-ordered %u", battleTestSpawned, battleTestSpawnsBlocked, battleTestCulled, battleTestReordered);
        }

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
        if (ImGui::Button("Unit spawner window..."))
        {
            showUnitSpawnerWindow = true;
        }
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

        renderUnitSpawnerWindow();
    }

}
