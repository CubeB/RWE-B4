#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace rwe
{
    /**
     * Where the engine keeps the user's own files: Data/, mods, rwe.cfg,
     * saves, replays, screenshots, logs and crash reports, all under one
     * directory.
     *
     * Windows: %APPDATA%\RWE. Elsewhere: "rwe" under the XDG data home,
     * which is ~/.local/share unless $XDG_DATA_HOME says otherwise (issue
     * #216), except that an existing ~/.rwe from before that move keeps
     * being used for as long as the new location does not exist, so nobody's
     * mods or saves go missing on the day the default changes. The launcher
     * applies the same rule in launcher/src/launcher/util.ts; keep the two in
     * step.
     */
    std::optional<std::filesystem::path> getLocalDataPath();

    /**
     * The candidates behind getLocalDataPath off Windows, most preferred
     * first: "rwe" under $XDG_DATA_HOME when that is set and absolute, else
     * under $HOME/.local/share, as the XDG base directory specification asks;
     * then the legacy $HOME/.rwe. Nothing without a HOME. Either pointer may
     * be null, standing for an unset variable.
     */
    std::vector<std::filesystem::path> localDataPathCandidates(const char* xdgDataHome, const char* home);

    /**
     * The first candidate that already exists, or the first candidate when
     * none does, so a fresh install lands in the new place and an old one
     * stays where its files are. Nothing for no candidates.
     */
    std::optional<std::filesystem::path> chooseLocalDataPath(const std::vector<std::filesystem::path>& candidates, const std::function<bool(const std::filesystem::path&)>& exists);

    /**
     * Rewrites the given key=value settings into a config file of the format
     * OpaqueArgs::parseConfig reads, keeping every line it does not
     * understand -- comments, other keys -- exactly as it found them.
     */
    void updateConfigFile(const std::filesystem::path& path, const std::vector<std::pair<std::string, std::string>>& values);
    std::optional<std::filesystem::path> getSearchPath();

    float toleranceToRadians(unsigned int angle);
}
