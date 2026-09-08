#pragma once

#include <rwe/ColorPalette.h>
#include <rwe/geometry/Rectangle2f.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/render/TextureHandle.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace rwe
{
    struct TextureAtlasInfo
    {
        SharedTextureHandle textureAtlas;
        std::unordered_map<std::string, Rectangle2f> textureAtlasMap;
        std::vector<Vector2f> colorAtlasMap;

        std::vector<SharedTextureHandle> teamTextureAtlases;
        std::unordered_map<std::string, Rectangle2f> teamTextureAtlasMap;

        /**
         * The palette index of every texel of textureAtlas: same size, same
         * layout, one byte a texel, so the same texture coordinate reaches
         * the same texel in both. This is what the shade table is indexed by.
         */
        SharedTextureHandle paletteIndexAtlas;
        /** The same again for each of the team colour atlases, in their order. */
        std::vector<SharedTextureHandle> teamPaletteIndexAtlases;
        /** palettes/PALETTE.SHD as a 256x32 image; see ShadeTable.h. */
        SharedTextureHandle shadeTableTexture;
    };

    TextureAtlasInfo createTextureAtlases(AbstractVirtualFileSystem* vfs, GraphicsContext* graphics, const ColorPalette* palette);
}
