#include <rwe/io/gui/gui.h>
#include <rwe/io/pcx/pcx.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>

namespace rwe
{
    std::vector<char> AbstractVirtualFileSystem::readFileOrThrow(const std::string& filename) const
    {
        auto bytes = readFile(filename);
        if (!bytes)
        {
            throw std::runtime_error("Couldn't read " + filename);
        }
        return *bytes;
    }

    std::optional<ColorPalette> AbstractVirtualFileSystem::readPaletteOrPcx(const std::string& stem) const
    {
        if (auto bytes = readFile("palettes/" + stem + ".PAL"))
        {
            if (auto palette = readPalette(*bytes))
            {
                return palette;
            }
        }

        auto pcx = readFile("palettes/" + stem + ".PCX");
        if (!pcx)
        {
            return std::nullopt;
        }

        try
        {
            PcxDecoder decoder(pcx->begin(), pcx->end());
            auto colors = decoder.decodePalette();
            ColorPalette palette(256);
            for (std::size_t i = 0; i < 256; ++i)
            {
                palette[i] = colors[i].toColor();
            }
            return palette;
        }
        catch (const PcxException&)
        {
            return std::nullopt;
        }
    }

    std::vector<GuiEntry> AbstractVirtualFileSystem::readGuiOrThrow(const std::string& filename) const
    {
        auto parsedGui = parseGuiFromBytes(readFileOrThrow(filename));
        if (!parsedGui)
        {
            throw std::runtime_error("Failed to parse GUI file: " + filename);
        }
        return *parsedGui;
    }
}
