#include "util.h"
#include <cmath>
#include <fstream>
#include <rwe/math/rwe_math.h>

namespace rwe
{
#ifdef RWE_PLATFORM_WINDOWS
    std::optional<std::filesystem::path> getLocalDataPath()
    {
        auto appData = std::getenv("APPDATA");
        if (appData == nullptr)
        {
            return std::nullopt;
        }

        std::filesystem::path path(appData);
        path /= "RWE";

        return path;
    }
#endif

    std::vector<std::filesystem::path> localDataPathCandidates(const char* xdgDataHome, const char* home)
    {
        if (home == nullptr || *home == '\0')
        {
            return {};
        }

        // The specification says a relative or empty $XDG_DATA_HOME is
        // invalid and the default applies. Absolute in the POSIX sense, a
        // leading slash: std::filesystem's is_absolute asks the host, and on
        // Windows "/mnt/data" has no drive and is relative.
        std::filesystem::path dataHome;
        if (xdgDataHome != nullptr && *xdgDataHome == '/')
        {
            dataHome = xdgDataHome;
        }
        else
        {
            dataHome = std::filesystem::path(home) / ".local" / "share";
        }

        return {dataHome / "rwe", std::filesystem::path(home) / ".rwe"};
    }

    std::optional<std::filesystem::path> chooseLocalDataPath(const std::vector<std::filesystem::path>& candidates, const std::function<bool(const std::filesystem::path&)>& exists)
    {
        for (const auto& candidate : candidates)
        {
            if (exists(candidate))
            {
                return candidate;
            }
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }
        return candidates.front();
    }

#ifdef RWE_PLATFORM_LINUX
    std::optional<std::filesystem::path> getLocalDataPath()
    {
        return chooseLocalDataPath(
            localDataPathCandidates(std::getenv("XDG_DATA_HOME"), std::getenv("HOME")),
            [](const std::filesystem::path& p) {
                std::error_code ec;
                return std::filesystem::is_directory(p, ec);
            });
    }
#endif

    std::optional<std::filesystem::path> getSearchPath()
    {
        auto path = getLocalDataPath();
        if (!path)
        {
            return std::nullopt;
        }


        *path /= "Data";

        return *path;
    }

    float toleranceToRadians(unsigned int angle)
    {
        return static_cast<float>(angle) * (Pif / 32768.0f);
    }

    void updateConfigFile(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& values)
    {
        std::vector<std::string> lines;
        {
            std::ifstream in(path);
            std::string line;
            while (std::getline(in, line))
            {
                lines.push_back(line);
            }
        }

        std::vector<bool> written(values.size(), false);
        for (auto& line : lines)
        {
            auto eq = line.find('=');
            if (eq == std::string::npos || line.empty() || line[0] == '#')
            {
                continue;
            }
            auto key = line.substr(0, eq);
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (values[i].first == key)
                {
                    line = values[i].first + "=" + values[i].second;
                    written[i] = true;
                }
            }
        }
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (!written[i])
            {
                lines.push_back(values[i].first + "=" + values[i].second);
            }
        }

        std::ofstream out(path, std::ios::trunc);
        for (const auto& line : lines)
        {
            out << line << '\n';
        }
    }
}
