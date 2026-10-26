#include "TextureService.h"
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <rwe/util/SpanStream.h>
#include <rwe/io/fnt/Fnt.h>
#include <rwe/io/gaf/GafArchive.h>
#include <rwe/io/pcx/pcx.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    class BufferGafAdapter : public GafReaderAdapter
    {
    private:
        GraphicsContext* graphics;
        const ColorPalette* palette;
        std::vector<Color> buffer;
        GafFrameData currentFrameHeader;

        SpriteSeries spriteSeries;

    public:
        explicit BufferGafAdapter(GraphicsContext* graphics, const ColorPalette* palette) : graphics(graphics), palette(palette), currentFrameHeader() {}

        void beginFrame(const GafFrameEntry& /*entry*/, const GafFrameData& header) override
        {
            buffer.clear();
            buffer.resize(header.width * header.height);
            std::fill(buffer.begin(), buffer.end(), Color::Transparent);
            currentFrameHeader = header;
        }

        void frameLayer(const LayerData& data) override
        {
            for (std::size_t y = 0; y < data.height; ++y)
            {
                for (std::size_t x = 0; x < data.width; ++x)
                {
                    auto outPosX = static_cast<int>(x) - (data.x - currentFrameHeader.posX);
                    auto outPosY = static_cast<int>(y) - (data.y - currentFrameHeader.posY);

                    if (outPosX < 0 || outPosX >= currentFrameHeader.width || outPosY < 0 || outPosY >= currentFrameHeader.height)
                    {
                        // Some third-party gafs (e.g. "FAVBOOM.gaf" for the CORMKL [Cybran Monkeylord] unit)
                        // contain layers whose bounds exceed the dimensions of the frame.
                        // If this happens we'll just ignore the pixels that are out of bounds.
                        continue;
                    }

                    auto colorIndex = static_cast<unsigned char>(data.data[(y * data.width) + x]);
                    if (colorIndex == data.transparencyKey)
                    {
                        continue;
                    }

                    buffer[(outPosY * currentFrameHeader.width) + outPosX] = (*palette)[colorIndex];
                }
            }
        }

        void endFrame() override
        {
            SharedTextureHandle handle(graphics->createTexture(currentFrameHeader.width, currentFrameHeader.height, buffer));

            auto bounds = Rectangle2f::fromTopLeft(
                -currentFrameHeader.posX,
                -currentFrameHeader.posY,
                currentFrameHeader.width,
                currentFrameHeader.height);

            auto region = Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);

            auto sprite = std::make_shared<Sprite>(graphics->createSprite(bounds, region, handle));
            spriteSeries.sprites.push_back(std::move(sprite));
        }

        SpriteSeries extractSpriteSeries()
        {
            return std::move(spriteSeries);
        }
    };

    TextureService::TextureService(GraphicsContext* graphics, AbstractVirtualFileSystem* fileSystem, const ColorPalette* palette)
        : graphics(graphics), fileSystem(fileSystem), palette(palette)
    {
        SharedTextureHandle handle(graphics->createColorTexture(Color(255, 0, 255)));
        auto sprite = graphics->createSprite(
            Rectangle2f(0.5f, 0.5f, 0.5f, 0.5f),
            Rectangle2f(0.5f, 0.5f, 0.5f, 0.5f),
            handle);

        auto series = std::make_shared<SpriteSeries>();
        series->sprites.push_back(std::make_shared<Sprite>(std::move(sprite)));
        defaultSpriteSeries = std::move(series);
    }

    Color TextureService::getGuiPaletteColor(unsigned int slot) const
    {
        if (slot >= 256 || palette->empty())
        {
            throw std::runtime_error("GUI palette slot or active palette is invalid");
        }
        auto guiPalette = fileSystem->readPaletteOrPcx("GUIPAL");
        if (!guiPalette || slot >= guiPalette->size())
        {
            throw std::runtime_error("Couldn't read GUI palette slot");
        }
        const auto& wanted = (*guiPalette)[slot];
        auto best = palette->begin();
        auto distance = std::numeric_limits<unsigned int>::max();
        for (auto current = palette->begin(); current != palette->end(); ++current)
        {
            auto dr = std::abs(static_cast<int>(current->r) - wanted.r);
            auto dg = std::abs(static_cast<int>(current->g) - wanted.g);
            auto db = std::abs(static_cast<int>(current->b) - wanted.b);
            auto candidate = static_cast<unsigned int>(dr + dg + db);
            if (candidate < distance)
            {
                best = current;
                distance = candidate;
            }
        }
        return *best;
    }

    std::optional<std::shared_ptr<SpriteSeries>> TextureService::getGafEntryInternal(const std::string& gafName, const std::string& entryName)
    {
        auto normEntryName = toUpper(entryName);

        auto key = gafName + "/" + normEntryName;
        auto it = animCache.find(key);
        if (it != animCache.end())
        {
            return it->second;
        }

        auto gafBytes = fileSystem->readFile(gafName);
        if (!gafBytes)
        {
            return std::nullopt;
        }

        // A GAF that will not read costs the sprites in it and nothing else:
        // it answers as the entry being absent, which the callers already
        // handle by showing the default series.
        try
        {
            rwe::SpanStream gafStream(gafBytes->data(), gafBytes->size());
            GafArchive gafArchive(&gafStream);

            auto gafEntry = gafArchive.findEntry(normEntryName);
            if (!gafEntry)
            {
                return std::nullopt;
            }

            BufferGafAdapter adapter(graphics, palette);
            gafArchive.extract(*gafEntry, adapter);
            auto ptr = std::make_shared<SpriteSeries>(adapter.extractSpriteSeries());
            animCache[key] = ptr;
            return ptr;
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "Ignoring " << gafName << " entry " << entryName << ": " << e.what();
            return std::nullopt;
        }
    }

    std::optional<std::shared_ptr<SpriteSeries>>
    TextureService::tryGetGafEntry(const std::string& gafName, const std::string& entryName)
    {
        return getGafEntryInternal(gafName, entryName);
    }

    std::shared_ptr<SpriteSeries> TextureService::getGafEntry(const std::string& gafName, const std::string& entryName)
    {
        auto entry = getGafEntryInternal(gafName, entryName);
        if (!entry)
        {
            throw std::runtime_error("Failed to load GAF entry");
        }

        return *entry;
    }

    std::optional<std::shared_ptr<SpriteSeries>>
    TextureService::getGuiTexture(const std::string& guiName, const std::string& graphicName)
    {
        auto entry = getGafEntryInternal("anims/" + guiName + ".gaf", graphicName);
        if (entry)
        {
            return entry;
        }

        entry = getGafEntryInternal("anims/commongui.gaf", graphicName);
        if (entry)
        {
            return entry;
        }

        return std::nullopt;
    }

    std::optional<std::shared_ptr<SpriteSeries>> TextureService::getUnitPic(const std::string& unitName)
    {
        auto key = toUpper(unitName);
        if (auto it = unitPicCache.find(key); it != unitPicCache.end())
        {
            return it->second;
        }

        auto entry = fileSystem->readFile("unitpics/" + unitName + ".pcx");
        if (!entry)
        {
            unitPicCache.emplace(key, std::nullopt);
            return std::nullopt;
        }

        // A picture that will not decode leaves that button blank.
        std::vector<char> decodedData;
        std::vector<PcxPaletteColor> palette;
        unsigned int width = 0;
        unsigned int height = 0;
        try
        {
            PcxDecoder<std::vector<char>::const_iterator> decoder(entry->begin(), entry->end());
            decodedData = decoder.decodePalettedImage();
            palette = decoder.decodePalette();
            width = decoder.getWidth();
            height = decoder.getHeight();
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "Ignoring unit picture " << unitName << ": " << e.what();
            unitPicCache.emplace(key, std::nullopt);
            return std::nullopt;
        }

        std::vector<Color> buffer(decodedData.size());
        for (std::size_t i = 0; i < decodedData.size(); ++i)
        {
            auto paletteIndex = static_cast<unsigned char>(decodedData[i]);
            assert(paletteIndex < palette.size());
            buffer[i] = palette[paletteIndex].toColor();
        }

        SharedTextureHandle handle(graphics->createTexture(width, height, buffer));
        auto bounds = Rectangle2f::fromTopLeft(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        auto region = Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
        auto sprite = std::make_shared<Sprite>(graphics->createSprite(bounds, region, handle));

        // Three faces, because that is what UiFactory reads a button's GAF
        // entry as: the normal stage, then pressed, then disabled.
        auto series = std::make_shared<SpriteSeries>();
        series->sprites = {sprite, sprite, sprite};
        unitPicCache.emplace(key, series);
        return series;
    }

    SharedTextureHandle TextureService::getBitmap(const std::string& bitmapName)
    {
        auto info = getBitmapInternal(bitmapName);
        return info.handle;
    }

    std::shared_ptr<SpriteSeries> TextureService::getDefaultSpriteSeries()
    {
        return defaultSpriteSeries;
    }

    SharedTextureHandle TextureService::getDefaultTexture()
    {
        return defaultSpriteSeries->sprites[0]->texture;
    }

    std::shared_ptr<Sprite> TextureService::getBitmapRegion(const std::string& bitmapName, int x, int y, int width, int height)
    {
        auto bitmap = getBitmapInternal(bitmapName);
        auto region = Rectangle2f::fromTopLeft(
            static_cast<float>(x) / static_cast<float>(bitmap.width),
            static_cast<float>(y) / static_cast<float>(bitmap.height),
            static_cast<float>(width) / static_cast<float>(bitmap.width),
            static_cast<float>(height) / static_cast<float>(bitmap.height));
        auto bounds = Rectangle2f::fromTopLeft(x, y, width, height);
        return std::make_shared<Sprite>(graphics->createSprite(bounds, region, bitmap.handle));
    }

    TextureService::TextureInfo TextureService::getBitmapInternal(const std::string& bitmapName)
    {
        auto it = bitmapCache.find(bitmapName);
        if (it != bitmapCache.end())
        {
            return it->second;
        }

        auto entry = fileSystem->readFile("bitmaps/" + bitmapName + ".pcx");
        if (!entry)
        {
            throw std::runtime_error("bitmap not found");
        }

        // A bitmap that will not decode shows as the default colour in its
        // place, once and logged once, rather than ending the launch.
        std::vector<char> decodedData;
        std::vector<PcxPaletteColor> palette;
        unsigned int width = 0;
        unsigned int height = 0;
        try
        {
            PcxDecoder<std::vector<char>::const_iterator> decoder(entry->begin(), entry->end());
            decodedData = decoder.decodePalettedImage();
            palette = decoder.decodePalette();
            width = decoder.getWidth();
            height = decoder.getHeight();
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "Ignoring bitmap " << bitmapName << ": " << e.what();
            TextureInfo fallback(1, 1, getDefaultTexture());
            bitmapCache[bitmapName] = fallback;
            return fallback;
        }

        std::vector<Color> buffer(decodedData.size());
        for (std::size_t i = 0; i < decodedData.size(); ++i)
        {
            auto paletteIndex = static_cast<unsigned char>(decodedData[i]);
            assert(paletteIndex < palette.size());
            buffer[i] = palette[paletteIndex].toColor();
        }

        SharedTextureHandle handle(graphics->createTexture(width, height, buffer));
        TextureInfo info(width, height, handle);
        bitmapCache[bitmapName] = info;
        return info;
    }

    std::shared_ptr<Sprite> TextureService::getMinimap(const std::string& mapName)
    {
        auto it = minimapCache.find(mapName);
        if (it != minimapCache.end())
        {
            return it->second;
        }

        auto tntData = fileSystem->readFile("maps/" + mapName + ".tnt");
        if (!tntData)
        {
            throw std::runtime_error("map tnt not found!");
        }

        rwe::SpanStream tntStream(tntData->data(), tntData->size());
        TntArchive tnt(&tntStream);
        auto minimap = tnt.readMinimap();

        std::vector<Color> rgbMinimap;
        std::transform(minimap.data.begin(), minimap.data.end(), std::back_inserter(rgbMinimap), [p = palette](unsigned char pixel) {
            assert(pixel >= 0 && pixel <= 255);
            return (*p)[pixel];
        });

        minimapPixelCache[mapName] = MinimapPixels{static_cast<unsigned int>(minimap.width), static_cast<unsigned int>(minimap.height), rgbMinimap};

        SharedTextureHandle texture(graphics->createTexture(minimap.width, minimap.height, rgbMinimap));
        auto sprite = graphics->createSprite(
            Rectangle2f::fromTopLeft(0.0f, 0.0f, minimap.width, minimap.height),
            Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f),
            texture);
        auto spritePtr = std::make_shared<Sprite>(std::move(sprite));

        minimapCache.insert({mapName, spritePtr});

        return spritePtr;
    }

    const TextureService::MinimapPixels* TextureService::tryGetMinimapPixels(const std::string& mapName) const
    {
        auto it = minimapPixelCache.find(mapName);
        return it == minimapPixelCache.end() ? nullptr : &it->second;
    }

    std::shared_ptr<Sprite> TextureService::getDefaultSprite()
    {
        return defaultSpriteSeries->sprites[0];
    }

    std::shared_ptr<SpriteSeries> TextureService::getFont(const std::string& fontName)
    {
        auto fntBytes = fileSystem->readFile(fontName);
        if (!fntBytes)
        {
            throw std::runtime_error("font not found!");
        }

        rwe::SpanStream fntStream(fntBytes->data(), fntBytes->size());
        FntArchive fnt(&fntStream);

        auto series = std::make_shared<SpriteSeries>();

        // The blitter draws the glyph's first row at the requested Y minus the
        // file's signed vertical offset, so text sits that many rows above the
        // Y it was asked for (BT-162).
        series->verticalOffset = static_cast<float>(fnt.verticalOffset());

        std::vector<char> glyphBits;
        for (unsigned int i = 0; i < 256; ++i)
        {
            auto glyph = fnt.extractGlyph(i, glyphBits);
            auto width = glyph.width;
            auto bytesRead = glyph.byteCount;

            std::vector<Color> rgbGlyph;
            rgbGlyph.reserve(bytesRead * 8);
            for (unsigned int j = 0; j < bytesRead; ++j)
            {
                auto byte = static_cast<unsigned char>(glyphBits[j]);
                for (unsigned int k = 0; k < 8; ++k)
                {
                    rgbGlyph.push_back(byte & (128u >> k) ? Color(255, 255, 255) : Color(0, 0, 0, 0));
                }
            }

            // read data might have been ragged at the end,
            // the last font in the file is often missing the last byte or two.
            rgbGlyph.resize(width * fnt.glyphHeight());

            SharedTextureHandle texture(graphics->createTexture(width, fnt.glyphHeight(), rgbGlyph));
            auto sprite = std::make_shared<Sprite>(graphics->createSprite(
                Rectangle2f::fromTopLeft(0.0f, 0.0f, width, fnt.glyphHeight()),
                Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f),
                texture));

            series->sprites.push_back(sprite);
        }

        return series;
    }

    TextureService::TextureInfo::TextureInfo(unsigned int width, unsigned int height, const SharedTextureHandle& handle)
        : width(width), height(height), handle(handle)
    {
    }
}
