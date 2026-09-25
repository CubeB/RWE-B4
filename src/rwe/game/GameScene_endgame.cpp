#include "GameScene.h"

#include <algorithm>
#include <cmath>
#include <rwe/io/campaign/campaign.h>
#include <rwe/io/gui/gui.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiStagedButton.h>

namespace rwe
{
    namespace
    {
        /**
         * The chart is laid out at 640x480 because that is what the original
         * lays it out at, and because `bitmaps/OUTCOME0.PCX` -- which paints the
         * frame, the eight column headings and the button surround -- is exactly
         * that size. Every number below is in that space, and every one of them
         * was checked against the artwork itself: the cell runs measured out of
         * the bitmap agree with the gadget geometry read out of the executable
         * to the pixel.
         */
        constexpr float EndGameScreenWidth = 640.0f;
        constexpr float EndGameScreenHeight = 480.0f;

        /**
         * The seven bar columns. The original writes each column's x into the
         * gadget rect as it builds it -- 0x70, 0xBA, 0x104, 0x14E, 0x198, 0x1E2,
         * 0x22C at 0x41E67C onwards -- which is 112 and a stride of 74. The
         * artwork's cells are 68 wide; the gadget is 67, and the extra pixel is
         * the one the cell's own edge covers.
         */
        constexpr float EndGameFirstColumnX = 112.0f;
        constexpr float EndGameColumnStride = 74.0f;
        constexpr float EndGameBarWidth = 68.0f;
        constexpr float EndGameBarHeight = 18.0f;

        /** Rows start at 0x5D and step 0x14 (0x41E462, 0x41E9F3), ten of them. */
        constexpr float EndGameFirstRowY = 93.0f;
        constexpr float EndGameRowStride = 20.0f;
        constexpr int EndGameMaxRows = 10;

        /** The name cell: x 16 to 105 in the artwork, the PlayerColor gadget's rect. */
        constexpr float EndGameNameX = 16.0f;
        constexpr float EndGameNameWidth = 90.0f;

        /** The swatch of player colour the name sits against. */
        constexpr float EndGameSwatchWidth = 7.0f;

        /**
         * The button's surround is painted into the artwork and its inset
         * measures x 460 to 579, y 416 to 435 -- 120 by 20, which is the size
         * ENDMSN.GUI gives its MainMenu button and the size of the faces
         * BUTTONS0 ships. Its `ypos` there is 395, twenty-one pixels higher:
         * that gui is the campaign's end-of-mission screen, which stacks four
         * buttons where this one has room for a single button and puts it lower.
         */
        constexpr float EndGameButtonX = 460.0f;
        constexpr float EndGameButtonY = 416.0f;
        constexpr float EndGameButtonWidth = 120.0f;
        constexpr float EndGameButtonHeight = 20.0f;

        /** The empty band the artwork leaves above its column headings. */
        constexpr float EndGameTitleY = 14.0f;

        /** Ten steps of the fade table, one a tick (0x41FA3D sets the count). */
        constexpr unsigned int EndGameFadeTicks = 10;

        /**
         * How long the banner stays over the last frame of the game before the
         * fade starts. The original has no timer here -- its banner is a flag on
         * the world renderer and the player dismisses it -- so this is the "few
         * seconds" the sequence reads as, and the one number on this screen that
         * is not the original's.
         */
        constexpr unsigned int EndGameBannerTicks = 90;

        /** Ten ticks between one column starting and the next (0x420537). */
        constexpr unsigned int EndGameColumnDelayTicks = 10;

        /** A bar is full after fifteen steps whatever it counts (0x4FD008 is 1/15). */
        constexpr float EndGameBarSteps = 15.0f;

        /**
         * The glamour picture takes no click for its first second (0x41FE5D
         * against the deadline 0x41FE02 sets) and says "Click to continue."
         * five seconds after that, twenty pixels up from the bottom (0x41FED4
         * onwards).
         */
        constexpr unsigned int EndGameGlamourDeafTicks = SimTicksPerSecond;
        constexpr unsigned int EndGameGlamourPromptTicks = 6 * SimTicksPerSecond;
        constexpr float EndGameGlamourPromptY = EndGameScreenHeight - 20.0f;
    }

    Rectangle2f GameScene::EndGameLayout::rect(float x, float y, float w, float h) const
    {
        return Rectangle2f::fromTopLeft(offsetX + (x * scale), offsetY + (y * scale), w * scale, h * scale);
    }

    GameScene::EndGameLayout GameScene::endGameLayout() const
    {
        auto windowWidth = static_cast<float>(sceneContext.viewport->width());
        auto windowHeight = static_cast<float>(sceneContext.viewport->height());
        auto scale = std::min(windowWidth / EndGameScreenWidth, windowHeight / EndGameScreenHeight);
        return EndGameLayout{
            scale,
            (windowWidth - (EndGameScreenWidth * scale)) / 2.0f,
            (windowHeight - (EndGameScreenHeight * scale)) / 2.0f};
    }

    Point GameScene::endGameScreenPoint(int windowX, int windowY) const
    {
        auto layout = endGameLayout();
        return Point(
            static_cast<int>((static_cast<float>(windowX) - layout.offsetX) / layout.scale),
            static_cast<int>((static_cast<float>(windowY) - layout.offsetY) / layout.scale));
    }

    void GameScene::beginEndGameSequence()
    {
        endGamePhase = EndGamePhase::Banner;
        endGamePhaseStart = sceneTime;

        // The mission's letter in the campaign's run, written as the game is
        // torn down (0x41DC81).
        if (gameParameters.campaign)
        {
            gameParameters.campaign->recordResult(localPlayerWon());
        }

        // Two beeps, a beat apart. Which sound the original reaches for is the
        // one piece of this sequence that is not written down anywhere we can
        // read: the endgame code plays nothing by name, `sounds/BEEP1..6.WAV`
        // are not mentioned in the executable or in SOUND.TDF, and the gui files
        // carry no sound field. BEEP6 is the shortest of the six and the only
        // one that reads as a plain tone, so that is what goes here.
        if (auto sound = sceneContext.audioService->loadSound("BEEP6"))
        {
            playUiSound(*sound);
            delay(SceneTime(8), [this, sound]() { playUiSound(*sound); });
        }
    }

    bool GameScene::endGameChartVisible() const
    {
        return gameOver && endGamePhase == EndGamePhase::Chart;
    }

    bool GameScene::endGameCoversWorld() const
    {
        return gameOver && (endGamePhase == EndGamePhase::Glamour || endGamePhase == EndGamePhase::Chart);
    }

    bool GameScene::campaignContinues() const
    {
        const auto& campaign = gameParameters.campaign;
        return campaign && (!localPlayerWon() || campaign->hasNextMission);
    }

    bool GameScene::beginGlamour()
    {
        // Only for a picture that is there: the original falls back to a
        // literal "glamour\Arm01.PCX" that has no bitmaps directory in front
        // of it (0x41DB7E), and every shipped glamour names a file.
        const auto& campaign = *gameParameters.campaign;
        if (campaign.glamour.empty() || !sceneContext.vfs->readFile("bitmaps/glamour/" + campaign.glamour + ".pcx"))
        {
            return false;
        }
        endGameGlamour = sceneContext.textureService->getBitmapRegion(
            "glamour/" + campaign.glamour,
            0,
            0,
            static_cast<int>(EndGameScreenWidth),
            static_cast<int>(EndGameScreenHeight));

        if (!campaign.glamourSound.empty())
        {
            if (auto sound = sceneContext.audioService->loadSoundFromPath("camps/briefs/" + campaign.glamourSound + ".wav"))
            {
                endGameGlamourChannel = sceneContext.audioService->playSound(*sound);
            }
        }

        endGamePhase = EndGamePhase::Glamour;
        endGamePhaseStart = sceneTime;
        return true;
    }

    void GameScene::glamourClicked()
    {
        if (sceneTime - endGamePhaseStart < SceneTime(EndGameGlamourDeafTicks))
        {
            return;
        }
        sceneContext.audioService->stopChannel(endGameGlamourChannel);
        endGameGlamourChannel = -1;
        endGameGlamour.reset();
        enterEndGameChart();
    }

    void GameScene::enterEndGameChart()
    {
        endGamePhase = EndGamePhase::Chart;
        endGamePhaseStart = sceneTime;
        buildEndGameChart();
    }

    void GameScene::finishEndGameBars()
    {
        if (!endGameStats)
        {
            return;
        }

        endGameColumnsStarted = EndGameStatCount;
        for (Index r = 0; r < getSize(endGameStats->rows); ++r)
        {
            for (int c = 0; c < EndGameStatCount; ++c)
            {
                endGameBarFill[r][c] = static_cast<float>(endGameStats->rows[r].values[c]);
            }
        }
    }

    void GameScene::buildEndGameChart()
    {
        // 0x41DC20 fills the score table and 0x41E420 lays the chart out, both
        // once, on the way in.
        endGameStats = computeEndGameStats(simulation);
        if (getSize(endGameStats->rows) > EndGameMaxRows)
        {
            // The original's table is ten records wide and so is the artwork.
            endGameStats->rows.erase(endGameStats->rows.begin() + EndGameMaxRows, endGameStats->rows.end());
        }
        endGameBarFill.assign(endGameStats->rows.size(), std::array<float, EndGameStatCount>{});
        endGameColumnsStarted = 0;
        endGameNextColumn = sceneTime;

        // The face the artwork's own column headings are set in, and the one
        // every gui label in the game uses.
        endGameChartFont = sceneContext.textureService->getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)");

        // A campaign that carries on has the whole of ENDMSN, background and
        // all; anything else keeps OUTCOME0 and its one button (0x41F18B,
        // 0x41F1C8).
        if (campaignContinues())
        {
            buildCampaignEndPanel();
        }
        if (endGameCampaignPanel)
        {
            return;
        }

        endGameBackground = sceneContext.textureService->getBitmapRegion(
            "OUTCOME0",
            0,
            0,
            static_cast<int>(EndGameScreenWidth),
            static_cast<int>(EndGameScreenHeight));

        // The same face every other 120x20 button in the game wears, resolved
        // the way UiFactory resolves one: anims/ENDMSN.GAF first, then
        // commongui.gaf, then the BUTTONS0 strip, which is where it lands --
        // ENDMSN.GAF carries only the two titles and a divider.
        endGameMainMenuButton = uiFactory.createButton(
            static_cast<int>(EndGameButtonX),
            static_cast<int>(EndGameButtonY),
            static_cast<int>(EndGameButtonWidth),
            static_cast<int>(EndGameButtonHeight),
            "ENDMSN",
            "MainMenu",
            "Main Menu");
        // createButton leaves the caption hidden; the gui reader is what
        // normally turns it on, per gadget, from the gui's own alignment.
        endGameMainMenuButton->setTextAlign(UiStagedButton::TextAlign::Center);
        endGameMainMenuButton->onClick().subscribe([this](const auto& /*event*/) {
            returnToMainMenu();
        });
    }

    void GameScene::buildCampaignEndPanel()
    {
        auto raw = sceneContext.vfs->readFile(sceneContext.pathMapping->guis + "/ENDMSN.GUI");
        auto entries = raw ? parseGuiFromBytes(*raw) : std::nullopt;
        if (!entries || entries->empty())
        {
            return;
        }

        // The gui itself, over OUTCOME1, whose frame has the list's surround
        // and the button column painted into it.
        auto panel = uiFactory.panelFromGuiFile("ENDMSN", "OUTCOME1", *entries);
        const auto& progress = *gameParameters.campaign;

        // Saving and loading between missions are still to come.
        for (const auto* name : {"LoadGame", "SaveGame"})
        {
            if (auto button = panel->find<UiStagedButton>(name))
            {
                button->get().setEnabled(false);
            }
        }

        endGameCampaignDifficulty = progress.difficulty;
        if (auto difficulty = panel->find<UiStagedButton>("Difficulty"))
        {
            difficulty->get().setStage(endGameCampaignDifficulty);
        }

        if (auto list = panel->find<UiListBox>("Missions"))
        {
            if (auto campaign = readCampaign(*sceneContext.vfs, progress.campaign))
            {
                auto& missions = list->get();
                for (std::size_t i = 0; i < campaign->missions.size(); ++i)
                {
                    auto status = i < progress.thumbs.size() ? progress.thumbs[i] : 'U';
                    missions.appendItem(campaignMissionListEntry(status, campaignMissionName(campaign->missions[i], std::string())));
                }
                // The next mission after a win, the same one again after a
                // loss (0x41F2D4).
                auto selected = localPlayerWon() ? progress.missionIndex + 1 : progress.missionIndex;
                if (selected < missions.getItems().size())
                {
                    missions.setSelectedItem(missions.getItems()[selected]);
                }
            }
        }

        panel->groupMessages().subscribe([this](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                // Deferred, as the game menu's are: Start leaves the scene
                // from inside the panel's own dispatch.
                pendingMenuActions.push_back([this, control = msg.controlName]() { campaignEndMessage(control); });
            }
        });
        endGameCampaignPanel = std::move(panel);
    }

    void GameScene::campaignEndMessage(const std::string& control)
    {
        if (control == "Start")
        {
            auto list = endGameCampaignPanel->find<UiListBox>("Missions");
            auto selected = list ? list->get().getSelectedIndex() : std::nullopt;
            if (!selected)
            {
                return;
            }
            // Any mission on the list can be picked, won or not (0x41EFBA).
            auto progress = *gameParameters.campaign;
            progress.missionIndex = *selected;
            progress.difficulty = endGameCampaignDifficulty;
            continueCampaign(progress);
        }
        else if (control == "Difficulty")
        {
            endGameCampaignDifficulty = (endGameCampaignDifficulty + 1) % 3;
            if (auto difficulty = endGameCampaignPanel->find<UiStagedButton>("Difficulty"))
            {
                difficulty->get().setStage(endGameCampaignDifficulty);
            }
        }
        else if (control == "MainMenu")
        {
            returnToMainMenu();
        }
    }

    void GameScene::updateEndGameSequence()
    {
        // Leaving for the films or the menu is a frame away; nothing more to
        // decide here meanwhile.
        if (!gameOver || leavingScene)
        {
            return;
        }

        auto elapsed = sceneTime - endGamePhaseStart;

        switch (endGamePhase)
        {
            case EndGamePhase::Banner:
            {
                if (elapsed.value >= EndGameBannerTicks)
                {
                    endGamePhase = EndGamePhase::Fade;
                    endGamePhaseStart = sceneTime;
                }
                return;
            }
            case EndGamePhase::Fade:
            {
                if (elapsed.value < EndGameFadeTicks)
                {
                    return;
                }

                // 0x41FC2A: the last mission of a campaign won ends on its
                // films, and any other campaign win shows its picture first.
                const auto& campaign = gameParameters.campaign;
                if (campaign && localPlayerWon())
                {
                    if (!campaign->hasNextMission && !campaign->noMovie)
                    {
                        playCampaignEnding();
                        return;
                    }
                    if (beginGlamour())
                    {
                        return;
                    }
                }
                enterEndGameChart();
                return;
            }
            case EndGamePhase::Glamour:
            {
                return;
            }
            case EndGamePhase::Chart:
            {
                if (endGameColumnsStarted < EndGameStatCount && sceneTime >= endGameNextColumn)
                {
                    ++endGameColumnsStarted;
                    endGameNextColumn = sceneTime + SceneTime(EndGameColumnDelayTicks);
                }

                if (!endGameStats)
                {
                    return;
                }

                for (Index r = 0; r < getSize(endGameStats->rows); ++r)
                {
                    const auto& row = endGameStats->rows[r];
                    for (int c = 0; c < endGameColumnsStarted; ++c)
                    {
                        auto target = static_cast<float>(row.values[c]);
                        if (endGameBarFill[r][c] >= target)
                        {
                            continue;
                        }
                        auto step = std::max(target / EndGameBarSteps, 1.0f);
                        endGameBarFill[r][c] = std::min(target, endGameBarFill[r][c] + step);
                    }
                }
                return;
            }
        }
    }

    bool GameScene::localPlayerWon() const
    {
        if (!gameOver)
        {
            return false;
        }

        return match(
            *gameOver,
            [&](const WinStatusWon& w) { return w.winner == localPlayerId; },
            [&](const WinStatusDraw&) { return false; },
            [&](const WinStatusUndecided&) { return false; });
    }

    void GameScene::renderEndGameSequence()
    {
        if (!gameOver)
        {
            return;
        }

        switch (endGamePhase)
        {
            case EndGamePhase::Banner:
            {
                // The same place and the same artwork as the pause banner,
                // because in the original it is the same code path: 0x46A107
                // draws whichever of `igpaused`, `igvictory` and `igdefeat` its
                // flags ask for.
                float centerX = static_cast<float>(sceneContext.viewport->width()) / 2.0f;
                float centerY = static_cast<float>(sceneContext.viewport->height()) / 2.0f;
                auto title = gameMediaDatabase.getSpriteSeries("IGTITLES", localPlayerWon() ? "igvictory" : "igdefeat");
                if (title && !(*title)->sprites.empty())
                {
                    const auto& sprite = *(*title)->sprites.front();
                    chromeUiRenderService.drawSpriteAbs(
                        std::floor(centerX - (sprite.bounds.width() / 2.0f)),
                        std::floor(centerY - (sprite.bounds.height() / 2.0f)),
                        sprite);
                }
                else
                {
                    chromeUiRenderService.drawTextCentered(centerX, centerY, localPlayerWon() ? "VICTORY" : "DEFEAT", *guiFont);
                }
                return;
            }
            case EndGamePhase::Fade:
            {
                auto step = std::min(EndGameFadeTicks, (sceneTime - endGamePhaseStart).value + 1);
                auto alpha = static_cast<int>((255.0f * static_cast<float>(step)) / static_cast<float>(EndGameFadeTicks));
                chromeUiRenderService.fillColor(
                    0.0f,
                    0.0f,
                    static_cast<float>(sceneContext.viewport->width()),
                    static_cast<float>(sceneContext.viewport->height()),
                    Color(0, 0, 0, static_cast<unsigned char>(alpha)));
                return;
            }
            case EndGamePhase::Glamour:
            {
                auto layout = endGameLayout();
                chromeUiRenderService.fillColor(
                    0.0f,
                    0.0f,
                    static_cast<float>(sceneContext.viewport->width()),
                    static_cast<float>(sceneContext.viewport->height()),
                    Color(0, 0, 0));
                chromeUiRenderService.pushMatrix();
                chromeUiRenderService.multiplyMatrix(
                    Matrix4f::translation(Vector3f(layout.offsetX, layout.offsetY, 0.0f))
                    * Matrix4f::scale(Vector3f(layout.scale, layout.scale, 1.0f)));
                if (endGameGlamour)
                {
                    chromeUiRenderService.drawSpriteAbs(Rectangle2f::fromTopLeft(0.0f, 0.0f, EndGameScreenWidth, EndGameScreenHeight), *endGameGlamour);
                }
                if (sceneTime - endGamePhaseStart >= SceneTime(EndGameGlamourPromptTicks))
                {
                    chromeUiRenderService.drawTextCentered(EndGameScreenWidth / 2.0f, EndGameGlamourPromptY, "Click to continue.", *guiFont);
                }
                chromeUiRenderService.popMatrix();
                return;
            }
            case EndGamePhase::Chart:
            {
                renderEndGameChart();
                return;
            }
        }
    }

    void GameScene::renderEndGameChart()
    {
        auto layout = endGameLayout();

        // Whatever shape the window is, everything outside the 4:3 box is black.
        chromeUiRenderService.fillColor(
            0.0f,
            0.0f,
            static_cast<float>(sceneContext.viewport->width()),
            static_cast<float>(sceneContext.viewport->height()),
            Color(0, 0, 0));

        // Everything from here down is drawn in the original's own 640x480
        // coordinates, which is what lets the gui's button draw itself where
        // the gui says it goes.
        chromeUiRenderService.pushMatrix();
        chromeUiRenderService.multiplyMatrix(
            Matrix4f::translation(Vector3f(layout.offsetX, layout.offsetY, 0.0f))
            * Matrix4f::scale(Vector3f(layout.scale, layout.scale, 1.0f)));

        if (endGameCampaignPanel)
        {
            // OUTCOME1 and ENDMSN's controls, none of which the chart's rows
            // reach down to.
            endGameCampaignPanel->render(chromeUiRenderService);
        }
        else if (endGameBackground)
        {
            chromeUiRenderService.drawSpriteAbs(Rectangle2f::fromTopLeft(0.0f, 0.0f, EndGameScreenWidth, EndGameScreenHeight), *endGameBackground);
        }

        // OUTCOME0.PCX leaves its top band empty above the column headings, and
        // this is what goes in it: `victory` and `defeat` out of ENDMSN.GAF, the
        // end-of-game screen's own titles rather than the smaller ones the world
        // renderer flashes over the battlefield.
        {
            auto won = localPlayerWon();
            auto title = sceneContext.textureService->tryGetGafEntry("anims/ENDMSN.GAF", won ? "victory" : "defeat");
            if (title && !(*title)->sprites.empty())
            {
                const auto& sprite = *(*title)->sprites.front();
                auto w = sprite.bounds.width();
                auto h = sprite.bounds.height();
                chromeUiRenderService.drawSpriteAbs(
                    Rectangle2f::fromTopLeft((EndGameScreenWidth - w) / 2.0f, EndGameTitleY, w, h),
                    sprite);
            }
            else if (endGameChartFont)
            {
                chromeUiRenderService.drawTextCentered(EndGameScreenWidth / 2.0f, EndGameTitleY + 14.0f, won ? "Victory" : "Defeat", *endGameChartFont);
            }
        }

        if (endGameStats && endGameChartFont)
        {
            // Ten solid 8x8 swatches, one per player colour, out of the same
            // LOGOS.GAF the HUD takes its logo from.
            auto swatches = sceneContext.textureService->tryGetGafEntry("textures/LOGOS.GAF", "Solid2a");
            const auto& font = *endGameChartFont;

            for (Index r = 0; r < getSize(endGameStats->rows); ++r)
            {
                const auto& row = endGameStats->rows[r];
                auto rowY = EndGameFirstRowY + (EndGameRowStride * static_cast<float>(r));
                auto textY = rowY + 14.0f;
                auto haveColor = swatches && row.color.value < (*swatches)->sprites.size();

                if (haveColor)
                {
                    chromeUiRenderService.drawSpriteAbs(
                        Rectangle2f::fromTopLeft(EndGameNameX, rowY, EndGameSwatchWidth, EndGameBarHeight),
                        *(*swatches)->sprites.at(row.color.value));
                }

                // Centred in what is left of the name cell beside the swatch,
                // the same way every figure is centred in its own.
                chromeUiRenderService.drawTextCentered(
                    EndGameNameX + EndGameSwatchWidth + ((EndGameNameWidth - EndGameSwatchWidth) / 2.0f),
                    textY,
                    row.name,
                    font);

                for (int c = 0; c < EndGameStatCount; ++c)
                {
                    auto barX = EndGameFirstColumnX + (EndGameColumnStride * static_cast<float>(c));
                    auto max = static_cast<float>(endGameStats->maxima[c]);
                    auto filled = max > 0.0f ? std::clamp(endGameBarFill[r][c] / max, 0.0f, 1.0f) : 0.0f;

                    if (filled > 0.0f && haveColor)
                    {
                        chromeUiRenderService.drawSpriteAbs(
                            Rectangle2f::fromTopLeft(barX, rowY, EndGameBarWidth * filled, EndGameBarHeight),
                            *(*swatches)->sprites.at(row.color.value));
                    }

                    if (c < endGameColumnsStarted)
                    {
                        chromeUiRenderService.drawTextCentered(
                            barX + (EndGameBarWidth / 2.0f),
                            textY,
                            std::to_string(static_cast<int>(endGameBarFill[r][c])),
                            font);
                    }
                }
            }
        }

        if (endGameMainMenuButton)
        {
            endGameMainMenuButton->render(chromeUiRenderService);
        }

        chromeUiRenderService.popMatrix();
    }
}
