#include "Screenshot.h"

#include <GL/glew.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <rwe/io/pcx/pcx.h>

namespace rwe
{
    unsigned int nextScreenshotNumber(const std::vector<std::string>& fileNames)
    {
        std::optional<unsigned int> highest;
        for (const auto& name : fileNames)
        {
            if (name.size() != 12)
            {
                continue;
            }
            std::string lower(name);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.compare(0, 4, "shot") != 0 || lower.compare(8, 4, ".pcx") != 0)
            {
                continue;
            }
            if (!std::all_of(lower.begin() + 4, lower.begin() + 8, [](unsigned char c) { return std::isdigit(c) != 0; }))
            {
                continue;
            }
            auto number = static_cast<unsigned int>(std::stoul(lower.substr(4, 4)));
            if (!highest || number > *highest)
            {
                highest = number;
            }
        }
        return highest ? *highest + 1 : 0;
    }

    std::string screenshotFileName(unsigned int number)
    {
        auto digits = std::to_string(number);
        if (digits.size() < 4)
        {
            digits.insert(0, 4 - digits.size(), '0');
        }
        return "SHOT" + digits + ".pcx";
    }

    std::optional<std::filesystem::path> saveScreenshot(const std::filesystem::path& directory, unsigned int width, unsigned int height)
    {
        if (width == 0 || height == 0)
        {
            return std::nullopt;
        }

        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec)
        {
            return std::nullopt;
        }

        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(directory, ec))
        {
            names.push_back(entry.path().filename().string());
        }
        auto path = directory / screenshotFileName(nextScreenshotNumber(names));

        // Whatever the scene last bound, read the window's own back buffer,
        // tightly packed, and put the state back afterwards.
        GLint previousReadFramebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        GLint previousPackAlignment = 4;
        glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);

        std::vector<uint8_t> bottomUp(static_cast<std::size_t>(width) * height * 3);
        glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGB, GL_UNSIGNED_BYTE, bottomUp.data());

        glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));

        // GL's first row is the bottom one; PCX's is the top.
        std::vector<uint8_t> topDown(bottomUp.size());
        auto rowBytes = static_cast<std::size_t>(width) * 3;
        for (unsigned int y = 0; y < height; ++y)
        {
            std::copy_n(bottomUp.data() + (static_cast<std::size_t>(height - 1 - y) * rowBytes), rowBytes, topDown.data() + (static_cast<std::size_t>(y) * rowBytes));
        }

        auto bytes = encodePcx24(width, height, topDown);
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            return std::nullopt;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out)
        {
            return std::nullopt;
        }
        return path;
    }
}
