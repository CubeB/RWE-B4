#include "GameScene.h"
#include <rwe/ShadeTable.h>
#include <rwe/TextureService.h>

// The minimap's picture, split out of GameScene_commands.cpp for the reason set
// out at the head of GameScene_render.cpp: that object was within a few
// hundred sections of what a COFF object can address, and this is a dense
// piece with an edge of its own.

namespace rwe
{
    void GameScene::updateMinimapPicture(const Grid<unsigned char>& visible, const Grid<unsigned char>& explored)
    {
        // Rebuilt only when the fog it shows has changed.
        if (fogSprite && visible.getVector() == fogVisibleSnapshot && explored.getVector() == fogExploredSnapshot)
        {
            return;
        }

        // The embedded image the picture is made from. Without it the plain
        // minimap stays on show.
        const auto* base = sceneContext.textureService->tryGetMinimapPixels(gameParameters.mapName);
        if (base == nullptr)
        {
            return;
        }

        fogVisibleSnapshot = visible.getVector();
        fogExploredSnapshot = explored.getVector();

        if (minimapGrayRamp.empty())
        {
            auto ramp = generateMeanGrayRamp(*sceneContext.palette);
            minimapGrayRamp.assign(ramp.begin(), ramp.end());
        }

        // One pixel to a texel: the picture is built at the size it is shown
        // at (126 on its long side, BT-152), so nothing is resampled again.
        auto pictureWidth = std::max(1, static_cast<int>(minimapRectBase.width()));
        auto pictureHeight = std::max(1, static_cast<int>(minimapRectBase.height()));

        minimapFogPixels = composeMinimapPicture(
            pictureWidth,
            pictureHeight,
            base->pixels,
            static_cast<int>(base->width),
            static_cast<int>(base->height),
            minimapGrayRamp,
            *sceneContext.palette,
            visible.getWidth(),
            visible.getHeight(),
            [&](int x, int y) {
                if (visible.get(x, y) != 0)
                {
                    return MinimapCellState::Seen;
                }
                return explored.get(x, y) ? MinimapCellState::Remembered : MinimapCellState::Unexplored;
            });

        if (!minimapFogTexture.isValid() || minimapFogWidth != pictureWidth || minimapFogHeight != pictureHeight)
        {
            minimapFogTexture = SharedTextureHandle(sceneContext.graphics->createTexture(pictureWidth, pictureHeight, minimapFogPixels.data()));
            minimapFogWidth = pictureWidth;
            minimapFogHeight = pictureHeight;

            auto bounds = Rectangle2f::fromTopLeft(0.0f, 0.0f, static_cast<float>(pictureWidth), static_cast<float>(pictureHeight));
            auto region = Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
            fogSprite = sceneContext.graphics->createSprite(bounds, region, minimapFogTexture);
        }
        else
        {
            sceneContext.graphics->updateTexture(
                minimapFogTexture.get(),
                static_cast<unsigned int>(pictureWidth),
                static_cast<unsigned int>(pictureHeight),
                minimapFogPixels.data());
        }
    }
}
