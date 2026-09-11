#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The number the next screenshot takes: one past the highest SHOTnnnn.pcx
     * already in the folder, as the original's 0x4cb170 does (TOTALA-EXE.md
     * S:77). Anything that is not SHOT, four digits and .pcx, in any case, is
     * ignored. With none there the first is 0; where the original starts is
     * not decoded.
     */
    unsigned int nextScreenshotNumber(const std::vector<std::string>& fileNames);

    /** "SHOT0007.pcx" for 7: the original's "%s%s%s%04i.pcx". */
    std::string screenshotFileName(unsigned int number);

    /**
     * Reads the window's back buffer and writes it to <directory>/SHOTnnnn.pcx,
     * creating the directory if it is missing. Call after the frame is drawn
     * and before the swap. Returns the path written, or nothing if it could
     * not be written.
     */
    std::optional<std::filesystem::path> saveScreenshot(const std::filesystem::path& directory, unsigned int width, unsigned int height);
}
