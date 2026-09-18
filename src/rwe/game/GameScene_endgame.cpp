#include "GameScene.h"

#include <algorithm>
#include <cmath>
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

        endGameBackground = sceneContext.textureService->getBitmapRegion(
            "OUTCOME0",
            0,
            0,
            static_cast<int>(EndGameScreenWidth),
            static_cast<int>(EndGameScreenHeight));

        // The face the artwork's own column headings are set in, and the one
        // every gui label in the game uses.
        endGameChartFont = sceneContext.textureService->getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)");

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

    void GameScene::updateEndGameSequence()
    {
        if (!gameOver)
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

                endGamePhase = EndGamePhase::Chart;
                endGamePhaseStart = sceneTime;
                buildEndGameChart();
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

        if (endGameBackground)
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
