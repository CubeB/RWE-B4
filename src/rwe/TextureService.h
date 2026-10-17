#pragma once

#include <memory>
#include <optional>
#include <rwe/ColorPalette.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/render/SpriteSeries.h>
#include <rwe/render/TextureHandle.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <unordered_map>

namespace rwe
{
    class TextureService
    {
    private:
        struct TextureInfo
        {
            unsigned int width;
            unsigned int height;
            SharedTextureHandle handle;

            TextureInfo() = default;
            TextureInfo(unsigned int width, unsigned int height, const SharedTextureHandle& handle);
        };

    public:
        /** A map's embedded minimap as colours, row-major from the top-left. */
        struct MinimapPixels
        {
            unsigned int width{0};
            unsigned int height{0};
            std::vector<Color> pixels;
        };

    private:
        GraphicsContext* graphics;
        AbstractVirtualFileSystem* fileSystem;
        const ColorPalette* palette;

        std::shared_ptr<SpriteSeries> defaultSpriteSeries;

        std::unordered_map<std::string, std::shared_ptr<SpriteSeries>> animCache;
        std::unordered_map<std::string, TextureInfo> bitmapCache;
        std::unordered_map<std::string, std::shared_ptr<Sprite>> minimapCache;
        std::unordered_map<std::string, MinimapPixels> minimapPixelCache;
        /** Misses too, since every button on every page asks and most are not units. */
        std::unordered_map<std::string, std::optional<std::shared_ptr<SpriteSeries>>> unitPicCache;

    public:
        TextureService(GraphicsContext* graphics, AbstractVirtualFileSystem* filesystem, const ColorPalette* palette);

        std::optional<std::shared_ptr<SpriteSeries>> tryGetGafEntry(const std::string& gafName, const std::string& entryName);
        std::shared_ptr<SpriteSeries> getGafEntry(const std::string& gafName, const std::string& entryName);
        std::optional<std::shared_ptr<SpriteSeries>> getGuiTexture(const std::string& guiName, const std::string& graphicName);

        /**
         * A unit's build picture, unitpics/<unit>.pcx, as a button's three
         * faces -- normal, pressed and disabled all the one picture -- or
         * nothing when the unit ships none. This is where a button that a
         * download file added to a menu gets its art: its page's own GAF was
         * drawn before the unit existed, and every one of the 111 such
         * buttons in the shipped data has a unitpic and no frame there.
         */
        std::optional<std::shared_ptr<SpriteSeries>> getUnitPic(const std::string& unitName);
        SharedTextureHandle getBitmap(const std::string& bitmapName);
        std::shared_ptr<Sprite> getBitmapRegion(const std::string& bitmapName, int x, int y, int width, int height);
        SharedTextureHandle getDefaultTexture();
        std::shared_ptr<SpriteSeries> getDefaultSpriteSeries();
        std::shared_ptr<Sprite> getDefaultSprite();
        std::shared_ptr<Sprite> getMinimap(const std::string& mapName);

        /**
         * The colours of a minimap getMinimap has already loaded, or nothing.
         * The in-game minimap composes its own picture from them (BT-152).
         */
        const MinimapPixels* tryGetMinimapPixels(const std::string& mapName) const;
        std::shared_ptr<SpriteSeries> getFont(const std::string& fontName);

    private:
        std::optional<std::shared_ptr<SpriteSeries>> getGafEntryInternal(const std::string& gafName, const std::string& entryName);
        TextureInfo getBitmapInternal(const std::string& bitmapName);
    };
}
