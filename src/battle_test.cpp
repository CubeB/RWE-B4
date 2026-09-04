// A standing battle, for watching what a fight does to the engine.
//
// Spawns a fixed number of units for each of two players at opposite start
// positions, walks them at each other, and replaces them as they die, for as
// long as the window is open. The count is a slider in the debug panel (the
// backtick key opens it), so the fight can be pushed until something gives
// without restarting.
//
// It launches the real game -- the same renderer, simulation and scene loop
// as rwe.exe, through the same run() -- rather than a reduced imitation of
// it, which is the only way the numbers mean anything.
//
//   battle_test --map "Coast To Coast" --units 100
//   battle_test --map "Coast To Coast" --units 200 --unit-type CORAK
//
// Everything rwe.exe accepts (--data-path, --width, --window-mode, ...) works
// here too.
#include <SDL3/SDL.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/util.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/SimpleLogger.h>

namespace fs = std::filesystem;

namespace
{
    std::shared_ptr<rwe::SimpleLogger> createLogger(const fs::path& logDir)
    {
        return std::make_shared<rwe::SimpleLogger>((logDir / "battle_test.log").string(), true);
    }

    rwe::PathMapping constructDefaultPathMapping()
    {
        rwe::PathMapping m;
        m.ai = "ai";
        m.anims = "anims";
        m.bitmaps = "bitmaps";
        m.camps = "camps";
        m.downloads = "downloads";
        m.features = "features";
        m.fonts = "fonts";
        m.gamedata = "gamedata";
        m.guis = "guis";
        m.maps = "maps";
        m.objects3d = "objects3d";
        m.palettes = "palettes";
        m.scripts = "scripts";
        m.sounds = "sounds";
        m.textures = "textures";
        m.unitpics = "unitpics";
        m.units = "units";
        m.weapons = "weapons";
        return m;
    }
}

int main(int argc, char* argv[])
{
    try
    {
        auto localDataPath = rwe::getLocalDataPath();
        if (!localDataPath)
        {
            throw std::runtime_error("Failed to determine local data path");
        }
        fs::create_directories(*localDataPath);

        rwe::OpaqueArgs args;
        args.parse(argc, argv);
        args.parseConfig((*localDataPath / "rwe.cfg").string());

        if (args.isHelpRequested())
        {
            std::cout << "battle_test --map <name> [--units N] [--unit-type ARMPW]\n"
                      << "            [--data-path <dir>] [--width N] [--height N]\n"
                      << "            [--window-mode windowed|borderless|fullscreen]\n\n"
                      << "Two players, N units each, walking at one another for ever.\n"
                      << "The count is a slider in the debug panel; ` opens it.\n";
            return 0;
        }

        auto logger = createLogger(*localDataPath);

        rwe::GlobalConfig config;
        config.soundVolume = args.getUint("sound-volume", 100);
        config.musicVolume = args.getUint("music-volume", 100);
        config.musicEnabled = args.getString("music", "true") != "false";
        config.shadows = args.getString("shadows", "true") != "false";
        config.scrollSpeed = std::clamp(args.getUint("scroll-speed", 100), 25u, 200u);
        config.soundMode = std::min(2u, args.getUint("sound-mode", 2));
        config.unitSpeech = std::min(2u, args.getUint("unit-speech", 2));
        config.gamma = std::clamp(args.getUint("gamma", 100), 50u, 133u);
        config.shading = args.getString("shading", "true") != "false";
        config.antiAlias = args.getString("anti-alias", "true") != "false";

        rwe::GameParameters parameters(args.getString("map", "Coast To Coast"), 0);
        parameters.localNetworkPort = args.getString("port", "1337");
        parameters.aiDifficulty = rwe::AiDifficulty::Standard;

        // One local human -- the engine allows exactly one -- and the
        // opposition in a computer slot with its AI suppressed, so nothing
        // moves on either side but what the harness puts there. An AI left
        // running would take its own units over and the fight would stop
        // being the thing under test.
        parameters.players[0] = rwe::parsePlayerInfoFromArg("left;Human;ARM;0");
        parameters.players[1] = rwe::parsePlayerInfoFromArg("right;Computer;CORE;1");

        parameters.battleTestUnitsPerSide = args.getUint("units", 100);
        parameters.battleTestUnitType = args.getString("unit-type", "ARMPW");

        std::vector<fs::path> gameDataPaths;
        auto dataPaths = args.getMulti("data-path");
        if (!dataPaths.empty())
        {
            gameDataPaths.insert(gameDataPaths.end(), dataPaths.begin(), dataPaths.end());
        }
        else
        {
            gameDataPaths.emplace_back(*localDataPath) /= "Data";
        }

        auto windowMode = rwe::WindowMode::Bordered;
        auto windowModeString = args.getString("window-mode", "");
        if (windowModeString == "borderless")
        {
            windowMode = rwe::WindowMode::Borderless;
        }
        else if (windowModeString == "fullscreen")
        {
            windowMode = rwe::WindowMode::Fullscreen;
        }

        return rwe::run(
            gameDataPaths,
            constructDefaultPathMapping(),
            parameters,
            args.getUint("width", 1280),
            args.getUint("height", 800),
            windowMode,
            (*localDataPath / "imgui.ini").string(),
            config);
    }
    catch (const std::exception& e)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "battle_test", e.what(), nullptr);
        std::cerr << e.what() << "\n";
        return 1;
    }
}
