#pragma once

#include <optional>
#include <rwe/ColorPalette.h>
#include <rwe/io/gui/gui.h>
#include <string>
#include <vector>

namespace rwe
{
    class AbstractVirtualFileSystem
    {
    public:
        virtual ~AbstractVirtualFileSystem() = default;
        virtual std::optional<std::vector<char>> readFile(const std::string& filename) const = 0;
        virtual std::vector<std::string> getFileNames(const std::string& directory, const std::string& extension) = 0;
        virtual std::vector<std::string> getDirectoryNames(const std::string& directory) = 0;
        virtual std::vector<std::string> getFileNamesRecursive(const std::string& directory, const std::string& extension) = 0;

        std::vector<char> readFileOrThrow(const std::string& filename) const;
        std::vector<GuiEntry> readGuiOrThrow(const std::string& filename) const;

        /**
         * The palette `palettes/<stem>.PAL`, or, if there is none (or it is
         * unreadable), the one carried at the end of `palettes/<stem>.PCX`.
         * The original decodes the PCX and caches the .PAL (#650); a mod that
         * ships only the picture used to end the launch. Nullopt if neither
         * can be read, and the caller says which palette was missing.
         */
        std::optional<ColorPalette> readPaletteOrPcx(const std::string& stem) const;
    };

    class LeafVirtualFileSystem : public AbstractVirtualFileSystem
    {
    public:
        virtual const std::string& getPath() const = 0;
    };
}
