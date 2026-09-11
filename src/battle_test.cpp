// A standing battle, for watching what a fight does to the engine.
//
// Spawns a fixed number of units for each of two players at opposite start
// positions, walks them at each other, and replaces them as they die, for as
// long as the window is open. The count is a slider in the debug panel (F10
// opens it), so the fight can be pushed until something gives, and pulled
// back down again, without restarting.
//
// It launches the real game -- the same renderer, simulation and scene loop
// as rwe.exe, through the same run() -- rather than a reduced imitation of
// it, which is the only way the numbers mean anything.
//
//   battle_test --map "Coast To Coast" --units 100
//   battle_test --map "Coast To Coast" --units 200 --unit-type CORAK
//   battle_test --list-maps
//   battle_test --list-units
//
// Everything rwe.exe accepts (--data-path, --width, --window-mode, ...) works
// here too. The run writes to battle_test.log in the local data directory, a
// line at a time and flushed, since these runs normally end under taskkill.
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/util.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

namespace fs = std::filesystem;

namespace
{
    /** The slider in the debug panel runs 0-500; the command line matches it. */
    constexpr unsigned int MaxUnitsPerSide = 500;

    std::shared_ptr<rwe::SimpleLogger> createLogger(const fs::path& logDir)
    {
        return std::make_shared<rwe::SimpleLogger>((logDir / "battle_test.log").string(), true);
    }

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

    std::unique_ptr<rwe::CompositeVirtualFileSystem> openDataPaths(const std::vector<fs::path>& paths)
    {
        auto vfs = std::make_unique<rwe::CompositeVirtualFileSystem>();
        for (const auto& path : paths)
        {
            rwe::addToVfs(*vfs, path.string());
        }
        return vfs;
    }

    /** File names in a data directory, without their extension and in order. */
    std::vector<std::string> listNames(rwe::CompositeVirtualFileSystem& vfs, const std::string& directory, const std::string& extension)
    {
        auto names = vfs.getFileNames(directory, extension);
        for (auto& name : names)
        {
            name = fs::path(name).stem().string();
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return names;
    }

    /**
     * The handful of names closest to what was asked for, so a typo or a
     * half-remembered map comes back with something to try rather than a
     * bare refusal.
     */
    std::vector<std::string> suggestNames(const std::vector<std::string>& names, const std::string& wanted)
    {
        auto needle = rwe::toUpper(wanted);
        std::vector<std::string> hits;
        for (const auto& name : names)
        {
            if (rwe::toUpper(name).find(needle) != std::string::npos && hits.size() < 10)
            {
                hits.push_back(name);
            }
        }
        if (!hits.empty())
        {
            return hits;
        }

        // Nothing contained what was typed, which is what a typo looks like.
        // Fall back on a shared opening: "Coast to Coasts" still finds
        // "Coast To Coast" that way.
        for (const auto& name : names)
        {
            auto candidate = rwe::toUpper(name);
            std::size_t shared = 0;
            while (shared < candidate.size() && shared < needle.size() && candidate[shared] == needle[shared])
            {
                ++shared;
            }
            if (shared >= 4 && hits.size() < 10)
            {
                hits.push_back(name);
            }
        }
        return hits;
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
            std::cout << "battle_test [--map <name>] [--units N] [--unit-type ARMAH]\n"
                      << "            [--unit-type-2 CORAH] [--data-path <dir>]\n"
                      << "            [--width N] [--height N]\n"
                      << "            [--window-mode windowed|borderless|fullscreen]\n"
                      << "            [--list-maps] [--list-units]\n\n"
                      << "Two players, N units each, walking at one another for ever.\n"
                      << "The count is a slider in the debug panel; F10 opens it, and it\n"
                      << "takes units away as well as adding them.\n\n"
                      << "  --units N        per side, 0-" << MaxUnitsPerSide << " (default 100)\n"
                      << "  --unit-type X    what both sides field, unless --unit-type-2 says\n"
                      << "                   otherwise (default ARMAH v CORAH -- hover tanks,\n"
                      << "                   which can cross the water most stock maps have)\n"
                      << "  --list-maps      map names this data defines\n"
                      << "  --list-units     unit names this data defines\n\n"
                      << "Everything rwe.exe accepts works here too. The run logs to\n"
                      << (*localDataPath / "battle_test.log").string() << ".\n";
            return 0;
        }

        auto logger = createLogger(*localDataPath);
        rwe::setGlobalLogger(logger);

        // This is where crashes get reproduced, so it is where a report is
        // worth most.
        rwe::installCrashHandler(*localDataPath);

        auto dataPaths = resolveDataPaths(args, *localDataPath);

        // --list-units prints what the loaded data actually contains, which
        // beats guessing at a unit name and getting an empty battlefield.
        if (args.getBool("list-units") || args.getBool("list-maps"))
        {
            auto vfs = openDataPaths(dataPaths);
            auto listMaps = args.getBool("list-maps");
            for (const auto& name : listNames(*vfs, listMaps ? "maps" : "units", listMaps ? ".ota" : ".fbi"))
            {
                std::cout << name << "\n";
            }
            return 0;
        }

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
        // "shading" was a plain bool before the switch grew four states. An
        // existing rwe.cfg still carries it, so it decides the default that
        // "shading-mode" then overrides -- otherwise upgrading would silently
        // turn shading back on for someone who had switched it off.
        auto shadingWasOn = args.getString("shading", "true") != "false";
        config.shadingMode = std::min(3u, args.getUint("shading-mode", shadingWasOn ? 3u : 0u));
        config.shadingStrengthUnits = std::min(100u, args.getUint("shading-strength-units", 40));
        config.shadingStrengthBuildings = std::min(100u, args.getUint("shading-strength-buildings", 40));
        config.antiAlias = args.getString("anti-alias", "true") != "false";
        config.buildingHalo = args.getString("building-halo", "true") != "false";
        config.antiAliasUnits = args.getString("anti-alias-units", "true") != "false";
        config.buildingHaloStrength = std::min(100u, args.getUint("building-halo-strength", 100));
        config.buildingHaloSaturation = std::min(100u, args.getUint("building-halo-saturation", 65));
        config.buildingHaloRedShift = std::min(100u, args.getUint("building-halo-red-shift", 50));

        auto mapName = args.getString("map", "Coast To Coast");

        // Settle the map before bringing up a window and half a minute of
        // loading. A name that is not in the data, or a map that only ever
        // had one start position -- most of the campaign missions -- fails
        // deep in the loader otherwise, and the two sides of a battle test
        // need somewhere each to stand.
        {
            auto vfs = openDataPaths(dataPaths);
            auto otaBytes = vfs->readFile("maps/" + mapName + ".ota");
            if (!otaBytes)
            {
                std::cerr << "No map called '" << mapName << "'.\n";
                auto matches = suggestNames(listNames(*vfs, "maps", ".ota"), mapName);
                if (!matches.empty())
                {
                    std::cerr << "Did you mean:\n";
                    for (const auto& name : matches)
                    {
                        std::cerr << "  " << name << "\n";
                    }
                }
                std::cerr << "battle_test --list-maps prints them all.\n";
                return 1;
            }

            std::string otaString(otaBytes->begin(), otaBytes->end());
            auto ota = rwe::parseOta(rwe::parseTdfFromString(otaString));
            if (ota.schemas.empty())
            {
                std::cerr << "Map '" << mapName << "' defines no schema to play on.\n";
                return 1;
            }
            auto startPositions = std::count_if(
                ota.schemas.front().specials.begin(),
                ota.schemas.front().specials.end(),
                [](const rwe::OtaSpecial& s) { return s.specialWhat.rfind("StartPos", 0) == 0; });
            if (startPositions < 2)
            {
                std::cerr << "Map '" << mapName << "' has " << startPositions
                          << " start position(s); a battle test needs two.\n"
                          << "Try a skirmish map -- battle_test --list-maps prints them all.\n";
                return 1;
            }
        }

        rwe::GameParameters parameters(mapName, 0);
        parameters.localNetworkPort = args.getString("port", "1337");
        parameters.aiDifficulty = rwe::AiDifficulty::Standard;

        // One local human -- the engine allows exactly one -- and the
        // opposition in a computer slot with its AI suppressed, so nothing
        // moves on either side but what the harness puts there. An AI left
        // running would take its own units over and the fight would stop
        // being the thing under test.
        parameters.players[0] = rwe::parsePlayerInfoFromArg("left;Human;ARM;0");
        parameters.players[1] = rwe::parsePlayerInfoFromArg("right;Computer;CORE;1");

        auto unitsPerSide = args.getUint("units", 100);
        if (unitsPerSide > MaxUnitsPerSide)
        {
            // The slider tops out at 500 and the value has to stay somewhere
            // it can be dragged back from, so say what happened rather than
            // quietly handing the panel a number it cannot represent.
            std::cerr << "--units " << unitsPerSide << " is past the slider's ceiling of "
                      << MaxUnitsPerSide << "; using " << MaxUnitsPerSide << ".\n";
            unitsPerSide = MaxUnitsPerSide;
        }
        parameters.battleTestUnitsPerSide = unitsPerSide;

        // --unit-type on its own puts the same unit on both sides, which is
        // the usual way to ask a question about one unit; --unit-type-2 is
        // there for the matchups.
        parameters.battleTestUnitTypes = {
            args.getString("unit-type", "ARMAH"),
            args.getString("unit-type-2", args.getString("unit-type", "CORAH"))};

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
            dataPaths,
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
        // stderr and the log first, the box second: the box is modal and
        // waits for a person, and a run driven from a script has none, so
        // whatever it was going to say has to be on disk before it opens.
        std::cerr << e.what() << "\n";
        LOG_ERROR << "battle_test failed: " << e.what();
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "battle_test", e.what(), nullptr);
        return 1;
    }
}
