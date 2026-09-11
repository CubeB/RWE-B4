// Watches a recorded game.
//
// A replay is the conditions a game started under plus every command issued
// into it. The simulation is lockstep, so playing those commands back through
// it reproduces the game exactly -- this is not a recording of pictures, it is
// the game being played again with nobody at the controls.
//
// It launches the real game through the same run() rwe.exe uses, so what you
// are watching is the renderer, simulation and scene loop the game actually
// has, rather than a viewer's impression of them.
//
//   replay_viewer                        the most recent replay
//   replay_viewer my-game                a replay in the Replays folder
//   replay_viewer D:\somewhere\game.rwereplay
//   replay_viewer --list                 what is on disk
//
// Everything rwe.exe accepts (--data-path, --width, --window-mode, ...) works
// here too. Once it is up, the Replay window has the scrub bar, the speed, and
// a list of the other recordings to switch between.
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/game/ReplayFile.h>
#include <rwe/util.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/SimpleLogger.h>

namespace fs = std::filesystem;

namespace
{
    std::vector<fs::path> resolveDataPaths(const rwe::OpaqueArgs& args, const fs::path& localDataPath)
    {
        std::vector<fs::path> paths;
        auto given = args.getMulti("data-path");
        if (!given.empty())
        {
            paths.insert(paths.end(), given.begin(), given.end());
        }
        else
        {
            paths.emplace_back(localDataPath) /= "Data";
        }
        return paths;
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

    std::string describe(const rwe::ReplaySummary& s)
    {
        auto minutes = s.seconds / 60;
        auto seconds = s.seconds % 60;
        return s.path.stem().string() + "  " + s.mapName + "  " + s.players
            + "  " + std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds);
    }
}

int main(int argc, char* argv[])
{
    try
    {
        // The replay to watch is the first argument, if it is not a switch.
        // Only the first: OpaqueArgs takes "--key value" as a pair, so
        // anything later that does not start with -- is somebody's value and
        // taking it would silently change what they asked for.
        std::string named;
        std::vector<char*> flags{argv[0]};
        for (int i = 1; i < argc; ++i)
        {
            if (i == 1 && std::string(argv[i]).rfind("--", 0) != 0)
            {
                named = argv[i];
                continue;
            }
            flags.push_back(argv[i]);
        }

        rwe::OpaqueArgs args;
        args.parse(static_cast<int>(flags.size()), flags.data());

        auto localDataPath = rwe::getLocalDataPath();
        if (!localDataPath)
        {
            throw std::runtime_error("Failed to determine local data path");
        }

        auto replays = rwe::listReplays();

        if (args.getBool("list"))
        {
            if (replays.empty())
            {
                std::cout << "No replays in " << rwe::replaysDirectory().value_or("(nowhere)").string() << "\n";
                return 0;
            }
            for (const auto& r : replays)
            {
                std::cout << describe(r) << "\n";
            }
            return 0;
        }

        if (args.isHelpRequested())
        {
            std::cout << "Usage: replay_viewer [name-or-path] [options]\n\n"
                      << "  (no argument)    the most recent replay\n"
                      << "  <name>           a replay in the Replays folder\n"
                      << "  <path>           a .rwereplay file anywhere\n"
                      << "  --list           what is on disk\n\n"
                      << "Everything rwe.exe accepts works here too. Replays live in\n"
                      << rwe::replaysDirectory().value_or("(nowhere)").string() << ".\n";
            return 0;
        }

        // A bare word is a replay in the folder; anything with a separator or
        // an extension is taken as a path.
        fs::path replayPath;
        if (!named.empty())
        {
            replayPath = rwe::replayPathForName(named);
        }
        else if (!replays.empty())
        {
            replayPath = replays.front().path;
        }
        else
        {
            throw std::runtime_error(
                "No replays found in " + rwe::replaysDirectory().value_or("(nowhere)").string()
                + "\n\nRecord one with:\n  rwe.exe --record-replay <name> --map \"Painted Desert\" ...");
        }

        auto replay = rwe::readReplayFile(replayPath);
        if (!replay)
        {
            throw std::runtime_error("Could not read replay: " + replayPath.string());
        }

        auto logger = std::make_shared<rwe::SimpleLogger>((*localDataPath / "replay_viewer.log").string(), true);
        rwe::setGlobalLogger(logger);
        rwe::installCrashHandler(*localDataPath);

        auto parameters = rwe::gameParametersFromReplayHeader(replay->header);
        parameters.replayFile = replayPath.string();

        rwe::GlobalConfig config;
        config.soundVolume = args.getUint("sound-volume", 100);
        config.musicVolume = args.getUint("music-volume", 100);
        config.musicEnabled = args.getString("music", "true") != "false";
        config.shadows = args.getString("shadows", "true") != "false";
        config.vehicleShadows = args.getString("vehicle-shadows", "true") != "false";
        config.scrollSpeed = std::clamp(args.getUint("scroll-speed", 100), 25u, 200u);
        config.soundMode = std::min(2u, args.getUint("sound-mode", 2));
        config.unitSpeech = std::min(2u, args.getUint("unit-speech", 2));
        config.musicTrackMode = std::min(3u, args.getUint("music-mode", 3));
        config.gamma = std::clamp(args.getUint("gamma", 100), 50u, 133u);
        auto shadingWasOn = args.getString("shading", "true") != "false";
        config.shadingMode = std::min(3u, args.getUint("shading-mode", shadingWasOn ? 3u : 0u));
        config.shadingStrengthUnits = std::min(100u, args.getUint("shading-strength-units", 25));
        config.shadingStrengthBuildings = std::min(100u, args.getUint("shading-strength-buildings", 40));
        config.antiAlias = args.getString("anti-alias", "true") != "false";
        config.buildingHalo = args.getString("building-halo", "true") != "false";
        config.antiAliasUnits = args.getString("anti-alias-units", "false") == "true";
        config.buildingHaloStrength = std::min(100u, args.getUint("building-halo-strength", 100));
        config.buildingHaloSaturation = std::min(100u, args.getUint("building-halo-saturation", 65));
        config.buildingHaloRedShift = std::min(100u, args.getUint("building-halo-red-shift", 50));

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

        LOG_INFO << "Watching " << replayPath.string();

        return rwe::run(
            resolveDataPaths(args, *localDataPath),
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
        // stderr and the log first, the box second: the box is modal and waits
        // for a person, and a run driven from a script has none.
        std::cerr << e.what() << "\n";
        LOG_ERROR << "replay_viewer failed: " << e.what();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Replay viewer", e.what(), nullptr);
        return 1;
    }
}
