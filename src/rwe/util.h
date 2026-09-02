#pragma once

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace rwe
{
    std::optional<std::filesystem::path> getLocalDataPath();

    /**
     * Rewrites the given key=value settings into a config file of the format
     * OpaqueArgs::parseConfig reads, keeping every line it does not
     * understand -- comments, other keys -- exactly as it found them.
     */
    void updateConfigFile(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& values);
    std::optional<std::filesystem::path> getSearchPath();

    float toleranceToRadians(unsigned int angle);
}
