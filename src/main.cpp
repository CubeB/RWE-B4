#include <filesystem>
#include <iostream>
#include <memory>
#include <SDL3/SDL.h>
#include <rwe/GameLaunch.h>
#include <rwe/GlobalConfig.h>
#include <rwe/PathMapping.h>
#include <rwe/setup/TaInstall.h>
#include <rwe/game/SaveFile.h>
#include <rwe/Viewport.h>
#include <rwe/config.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/util.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/Result.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

namespace fs = std::filesystem;



std::shared_ptr<rwe::SimpleLogger> createLogger(const fs::path& logFile)
{
    return std::make_shared<rwe::SimpleLogger>(logFile.string(), true);
}

std::shared_ptr<rwe::SimpleLogger> createLoggerInDir(const fs::path& logDir)
{
    for (int i = 0; i < 3; ++i)
    {
        fs::path logPath(logDir);
        if (i == 0)
        {
            logPath /= "rwe.log";
        }
        else
        {
            logPath /= "rwe" + std::to_string(i) + ".log";
        }

        try
        {
            return std::make_shared<rwe::SimpleLogger>(logPath.string(), true);
        }
        catch (const std::exception&)
        {
        }
    }

    throw std::runtime_error("Failed to create logger");
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

        fs::path configFilePath(*localDataPath);
        configFilePath /= "rwe.cfg";

        fs::path imGuiIniFilePath(*localDataPath);
        imGuiIniFilePath /= "imgui.ini";

        rwe::OpaqueArgs args;
        args.parse(argc, argv);
        args.parseConfig(configFilePath.string());

        if (args.isHelpRequested())
        {
            std::cout << "Usage: rwe [options]\n"
                      << "  --help                Show this message\n"
                      << "  --log <path>          Log output file path\n"
                      << "  --state-log <path>    Sim-state log file (desync debugging)\n"
                      << "  --ai-difficulty <d>   easy | standard | hard | brutal (default: standard)\n"
                      << "  --width <pixels>      Window width (default: 800)\n"
                      << "  --height <pixels>     Window height (default: 600)\n"
                      << "  --fullscreen          Start in fullscreen mode (same as --window-mode fullscreen)\n"
                      << "  --window-mode <m>     bordered | borderless | fullscreen (default: bordered)\n"
                      << "  --interface-mode <m>  left-click or right-click (default: left-click)\n"
                      << "  --data-path <path>    Game data search path (repeatable)\n"
                      << "  --map <name>          Launch directly into a game on this map\n"
                      << "  --port <port>         Network port (default: 1337)\n"
                      << "  --player <spec>       Player spec: name;type;side;color (repeatable)\n"
                      << "  --dir-<name> <dir>    Override directory name for a data category\n"
                      << std::endl;
            return 0;
        }

        auto logger = args.contains("log") ? createLogger(fs::path(args.getString("log"))) : createLoggerInDir(*localDataPath);
        rwe::setGlobalLogger(logger);

        try
        {
            rwe::GlobalConfig config;
            config.leftClickInterfaceMode = args.getString("interface-mode", "left-click") != "right-click";
            config.soundVolume = std::min(100u, args.getUint("sound-volume", 100));
            config.musicVolume = std::min(100u, args.getUint("music-volume", 100));
            config.musicEnabled = args.getString("music", "true") != "false";
            config.shadows = args.getString("shadows", "true") != "false";
            config.scrollSpeed = std::clamp(args.getUint("scroll-speed", 100), 25u, 200u);
            config.soundMode = std::min(2u, args.getUint("sound-mode", 2));
            config.unitSpeech = std::min(2u, args.getUint("unit-speech", 2));
            config.gamma = std::clamp(args.getUint("gamma", 100), 50u, 133u);
            config.shading = args.getString("shading", "true") != "false";
            config.antiAlias = args.getString("anti-alias", "true") != "false";
            std::optional<rwe::GameParameters> gameParameters;
            if (args.contains("load"))
            {
                // Resume a saved game straight from the command line, the same
                // way the front end does it: the save header carries the map
                // and players.
                auto savePath = rwe::savePathForName(args.getString("load"));
                auto save = rwe::readSaveFile(savePath);
                if (!save)
                {
                    throw std::runtime_error("Could not read save file: " + savePath.string());
                }
                gameParameters = save->parameters;
                gameParameters->loadFromSaveFile = savePath.string();
            }
            else if (args.contains("map"))
            {
                const auto& mapName = args.getString("map");
                const auto& players = args.getMulti("player");

                gameParameters = rwe::GameParameters{mapName, 0};
                if (args.contains("state-log"))
                {
                    gameParameters->stateLogFile = args.getString("state-log");
                }
                gameParameters->localNetworkPort = args.getString("port", "1337");
                auto difficulty = args.getString("ai-difficulty", "standard");
                for (auto& c : difficulty)
                {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (difficulty == "easy")
                {
                    gameParameters->aiDifficulty = rwe::AiDifficulty::Easy;
                }
                else if (difficulty == "hard")
                {
                    gameParameters->aiDifficulty = rwe::AiDifficulty::Hard;
                }
                else if (difficulty == "brutal")
                {
                    gameParameters->aiDifficulty = rwe::AiDifficulty::Brutal;
                }
                else
                {
                    gameParameters->aiDifficulty = rwe::AiDifficulty::Standard;
                }
                unsigned int playerIndex = 0;
                if (players.size() > 10)
                {
                    throw std::runtime_error("too many players");
                }
                for (const auto& playerString : players)
                {
                    gameParameters->players[playerIndex] = rwe::parsePlayerInfoFromArg(playerString);
                    ++playerIndex;
                }
            }

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

            auto screenWidth = args.getUint("width", 800);
            auto screenHeight = args.getUint("height", 600);
            auto windowMode = rwe::WindowMode::Bordered;
            auto windowModeString = args.getString("window-mode", "");
            if (windowModeString == "borderless")
            {
                windowMode = rwe::WindowMode::Borderless;
            }
            else if (windowModeString == "windowed")
            {
                windowMode = rwe::WindowMode::Bordered;
                windowModeString = "windowed";
            }
            else if (windowModeString == "fullscreen" || args.getBool("fullscreen"))
            {
                windowMode = rwe::WindowMode::Fullscreen;
                windowModeString = "fullscreen";
            }
            if (windowModeString.empty() || windowModeString == "bordered")
            {
                windowModeString = "windowed";
            }
            config.windowMode = windowModeString;

            auto pathMapping = constructDefaultPathMapping();
            pathMapping.ai = args.getString("dir-ai", "ai");
            pathMapping.anims = args.getString("dir-anims", "anims");
            pathMapping.bitmaps = args.getString("dir-bitmaps", "bitmaps");
            pathMapping.camps = args.getString("dir-camps", "camps");
            pathMapping.downloads = args.getString("dir-downloads", "downloads");
            pathMapping.fonts = args.getString("dir-fonts", "fonts");
            pathMapping.gamedata = args.getString("dir-gamedata", "gamedata");
            pathMapping.guis = args.getString("dir-guis", "guis");
            pathMapping.maps = args.getString("dir-maps", "maps");
            pathMapping.objects3d = args.getString("dir-objects3d", "objects3d");
            pathMapping.palettes = args.getString("dir-palettes", "palettes");
            pathMapping.scripts = args.getString("dir-scripts", "scripts");
            pathMapping.sounds = args.getString("dir-sounds", "sounds");
            pathMapping.textures = args.getString("dir-textures", "textures");
            pathMapping.unitpics = args.getString("dir-unitpics", "unitpics");
            pathMapping.units = args.getString("dir-units", "units");
            pathMapping.weapons = args.getString("dir-weapons", "weapons");

            // Nothing below here copes with there being no game data: the
            // first thing to touch the directory is a directory_iterator
            // inside the VFS, which throws a filesystem error naming a path
            // and nothing else. Say what is wrong and what fixes it instead.
            if (!rwe::anyPathHasGameData(gameDataPaths))
            {
                std::string message = "No Total Annihilation data found in:\n";
                for (const auto& path : gameDataPaths)
                {
                    message += "  " + path.string() + "\n";
                }
                message += "\nRun rwe_setup to copy it from your Total Annihilation\n";
                message += "installation. It will find one by itself if it can:\n\n";
                message += "    rwe_setup\n\n";
                message += "or point it at one:\n\n";
                message += "    rwe_setup --from \"C:/GOG Games/Total Annihilation\"";

                throw std::runtime_error(message);
            }

            return rwe::run(gameDataPaths, pathMapping, gameParameters, screenWidth, screenHeight, windowMode, imGuiIniFilePath.string(), config);
        }
        catch (const std::exception& e)
        {
            LOG_CRITICAL << e.what();
            throw;
        }
    }
    catch (const std::exception& e)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Critical Error", e.what(), nullptr);
        return 1;
    }
}
