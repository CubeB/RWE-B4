#pragma once

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <rwe/game/SceneTime.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * What became of a game, as the control channel reports it when it ends.
     *
     * Built from state the simulation had already arrived at, and sent once
     * per game: the outcome, how far it ran, whether a desync ended it, which
     * build produced the result, and the artifacts a launcher might collect.
     * Nothing here is hashed, saved or fed back to the simulation.
     */
    struct GameEndedReport
    {
        /** "decided", "draw" or "abandoned" -- the three ways a game ends. */
        std::string outcome;

        /** The winner, for a decided game; unset otherwise. */
        std::optional<PlayerId> winner;

        /** The last tick the game ran, and its time in seconds. */
        SceneTime tick{0};
        unsigned int gameTimeSeconds{0};

        /** The first tick the peers disagreed on, when a desync ended the game. */
        std::optional<SceneTime> desyncTick;

        /** The version string this build reports. */
        std::string engineBuild;

        /** Where the artifacts are; unset when the game has none. */
        std::optional<std::filesystem::path> replayPath;
        std::optional<std::filesystem::path> hashLogPath;
        std::vector<std::filesystem::path> desyncDumpPaths;
        std::optional<std::filesystem::path> logPath;
    };

    /**
     * The report as the JSON object the control channel writes. A path with
     * nothing at it is sent as null rather than as a name the launcher would
     * fail to open, which is the only reason this reads the filesystem.
     */
    nlohmann::json gameEndedJson(const GameEndedReport& report);
}
