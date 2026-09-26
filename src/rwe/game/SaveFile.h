#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/game/GameParameters.h>
#include <rwe/math/Vector3f.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The saved-game container. The header carries everything the front end
     * needs to rebuild the loading pipeline -- the map, the players, the
     * difficulty -- plus the camera; the simulation state itself is a nested
     * document produced and consumed by save_util.
     */
    struct SaveFile
    {
        GameParameters parameters;
        Vector3f cameraPosition{0.0f, 0.0f, 0.0f};
        /**
         * The simulation's elapsed game time when the save was written, for
         * the save list's TIME column. Nothing but the list reads it, so a
         * save written before this field existed loads with it empty rather
         * than failing.
         */
        std::optional<unsigned int> gameTimeSeconds;

        /**
         * A campaign saved from the screen between missions: the header
         * alone, the campaign's progress in it pointing at the mission to
         * brief next, and no world (BetweenMissions, 0x432942). Loading one
         * opens that briefing rather than a game.
         */
        bool betweenMissions{false};
        nlohmann::json simulation;

        explicit SaveFile(const GameParameters& parameters) : parameters(parameters) {}
    };

    /** Where saved games live: <local data path>/saves. Created on demand. */
    std::filesystem::path getSaveDirectory();

    /** The names (no extension) of every save on disk, newest first. */
    std::vector<std::string> listSaveGames();

    std::filesystem::path savePathForName(const std::string& name);

    void writeSaveFile(const std::filesystem::path& path, const SaveFile& save);

    std::optional<SaveFile> readSaveFile(const std::filesystem::path& path);

    /** The AI difficulty as a word in sentence case ("Standard"), for the save list's DIFF column -- not the save format's own lowercase spelling. */
    std::string aiDifficultyDisplayName(AiDifficulty d);
}
