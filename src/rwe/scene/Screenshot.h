#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The number the next screenshot takes: one past the highest number in
     * the folder's SHOT*.pcx names, as the original's 0x4cb170 does
     * (TOTALA-EXE.md S:77, BT-161). The number is the integer parse of the
     * digits after SHOT, so SHOT12345.pcx counts and the width is not fixed;
     * a name with no number, or numbered 0, counts as 0. Anything that is not
     * SHOT...pcx, in any case, is ignored. In an empty folder the first is 1.
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
