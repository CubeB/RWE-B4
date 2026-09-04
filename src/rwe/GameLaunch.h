#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/game/GameParameters.h>

namespace rwe
{
    enum class WindowMode
    {
        /** An ordinary window with a title bar: draggable, resizable. */
        Bordered,
        /** A borderless window filling the desktop. */
        Borderless,
        /** Exclusive fullscreen at the requested resolution. */
        Fullscreen,
    };

    /**
     * Brings up SDL, the GL context, the virtual file system and every
     * service the game needs, then runs the scene loop until it exits.
     * Shared so that a test harness can launch the real game rather than
     * a reduced imitation of it.
     */
    int run(
        const std::vector<std::filesystem::path>& searchPath,
        const PathMapping& pathMapping,
        const std::optional<GameParameters>& gameParameters,
        unsigned int desiredWindowWidth,
        unsigned int desiredWindowHeight,
        WindowMode windowMode,
        const std::string& imGuiIniPath,
        GlobalConfig& globalConfig);

    /** Parses a `name;Controller;SIDE;colour` player argument. */
    std::optional<PlayerInfo> parsePlayerInfoFromArg(const std::string& playerString);
}
