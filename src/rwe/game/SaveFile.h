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
}
