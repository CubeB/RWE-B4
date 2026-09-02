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

#ifdef RWE_PLATFORM_LINUX
    std::optional<std::filesystem::path> getLocalDataPath()
    {
        auto home = std::getenv("HOME");
        if (home == nullptr)
        {
            return std::nullopt;
        }

        std::filesystem::path path(home);
        path /= ".rwe";

        return path;
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
