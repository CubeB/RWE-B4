/**
 * ai_arena -- the computer-versus-computer arena with no window.
 *
 * `rwe --ai-arena` measures a game but still brings up SDL, a GL context and
 * an ImGui context first, so it cannot run on a machine without a display --
 * which is exactly where a batch of games wants to run, and exactly where the
 * CI and the sandbox that found this were. This harness builds the same
 * simulation through the same loader and runs the same tick loop, draining the
 * AI's commands into a PlayerCommandService the way GameScene does, but never
 * touches a GPU.
 *
 * It must stay in step with the windowed run: on seed N it writes the same
 * AI-ARENA-RESULT and the same RWE_HASH_LOG lines as `rwe --ai-arena`. That is
 * the check that makes it an instrument rather than a second opinion, and it
 * is why the command-buffer depth and the command application are shared code
 * rather than copies.
 *
 *   ai_arena --map "Coast To Coast" --ai-arena 900 --seed 7 \
 *       --player "A;Computer;ARM;0" --player "B;Computer;ARM;1" \
 *       [--ai-difficulty standard] [--start-location random] \
 *       [--ai-tune 0:knob=value] [--out <dir>] [--data-path <dir>]
 *
 * RWE_HASH_LOG and RWE_STATE_DUMP work here as they do in the game.
 */

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <rwe/ColorPalette.h>
#include <rwe/GameLaunch.h>
#include <rwe/PathMapping.h>
#include <rwe/ai/AiPersonality.h>
#include <rwe/game/AiArenaReport.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/game/SimDiagnostics.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/sim/DemoRecorder.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/SpanStream.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

namespace fs = std::filesystem;

namespace
{
    rwe::PathMapping defaultPathMapping()
    {
        rwe::PathMapping m;
        m.ai = "ai";
        m.anims = "anims";
        m.bitmaps = "bitmaps";
        m.camps = "camps";
        m.downloads = "download";
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

    std::unordered_map<std::string, rwe::SideData> loadSideData(rwe::AbstractVirtualFileSystem& vfs)
    {
        auto bytes = vfs.readFile("gamedata/SIDEDATA.TDF");
        if (!bytes)
        {
            throw std::runtime_error("Missing side data");
        }
        std::string text(bytes->data(), bytes->size());

        std::unordered_map<std::string, rwe::SideData> sideDataMap;
        for (auto& side : rwe::parseSidesFromSideData(rwe::parseTdfFromString(text)))
        {
            std::string name = side.name;
            sideDataMap.insert({std::move(name), std::move(side)});
        }
        return sideDataMap;
    }

    /**
     * The commander drop from LoadingScene::createGameScene, without a scene:
     * the map's start positions were dealt by the loader, and each filled slot
     * gets its side's commander standing on its own.
     */
    void spawnCommanders(
        rwe::GameSimulation& simulation,
        const rwe::LoadedGame& loaded,
        const std::unordered_map<std::string, rwe::SideData>& sideData)
    {
        const auto& schema = loaded.ota.schemas.at(loaded.gameParameters.schemaIndex);

        for (rwe::Index i = 0; i < rwe::getSize(loaded.gameParameters.players); ++i)
        {
            const auto& player = loaded.gameParameters.players[i];
            if (!player)
            {
                continue;
            }

            auto startPos = rwe::findStartPosition(schema, *loaded.startPositionForSlot[i]);
            if (!startPos)
            {
                throw std::runtime_error(
                    "Map \"" + loaded.gameParameters.mapName + "\" has no start position for player slot " + std::to_string(*loaded.startPositionForSlot[i]));
            }

            auto worldStartPos = simulation.terrain.topLeftCoordinateToWorld(rwe::SimVector(rwe::SimScalar(startPos->xPos), rwe::SimScalar(0), rwe::SimScalar(startPos->zPos)));
            worldStartPos.y = simulation.terrain.getHeightAt(worldStartPos.x, worldStartPos.z);

            auto sideIt = sideData.find(player->side);
            if (sideIt == sideData.end())
            {
                throw std::runtime_error("Missing side data for " + player->side);
            }

            auto unitId = simulation.trySpawnUnit(sideIt->second.commander, *loaded.gamePlayers[i], worldStartPos, std::nullopt);
            if (unitId)
            {
                auto& unit = simulation.getUnitState(*unitId);
                unit.finishBuilding(simulation.unitDefinitions.at(unit.unitType));
            }
        }
    }

}

int main(int argc, char* argv[])
{
    using namespace rwe;

    auto logger = std::make_shared<SimpleLogger>("ai_arena.log", true);
    setGlobalLogger(logger);

    try
    {
        OpaqueArgs args;
        args.parse(argc, argv);

        if (args.contains("log"))
        {
            logger = std::make_shared<SimpleLogger>(args.getString("log"), true);
            setGlobalLogger(logger);
        }

        if (args.isHelpRequested())
        {
            std::cout << "Usage: ai_arena --map <name> --ai-arena <seconds> [options]\n"
                      << "  --map <name>            map to play on\n"
                      << "  --ai-arena <seconds>    game length cap\n"
                      << "  --player <spec>         name;Computer;SIDE;colour[;team] (repeatable)\n"
                      << "  --seed <n>              vary the simulation seed\n"
                      << "  --ai-difficulty <d>     idle | easy | standard | hard | brutal\n"
                      << "  --start-location <m>    fixed | random\n"
                      << "  --ai-tune <p>:<k>=<v>   override one AI knob for player p (repeatable)\n"
                      << "  --ai-personality <p>:<name>  play player p as a personality (repeatable)\n"
                      << "  --out <dir>             where ai-arena.csv is written (default: local data path)\n"
                      << "  --record-demo <f>       write a TA demo of the game to <f>\n"
                      << "  --watchdog <seconds>    abort after this much wall-clock time\n"
                      << "  --strict                exit 1 if no AI-ARENA-RESULT was produced or the watchdog fired\n"
                      << "  --data-path <path>      game data search path (repeatable)\n";
            return 0;
        }

        auto arenaSeconds = args.getUint("ai-arena", 0);
        if (arenaSeconds == 0)
        {
            throw std::runtime_error("ai_arena needs --ai-arena <seconds>");
        }
        if (!args.contains("map"))
        {
            throw std::runtime_error("ai_arena needs --map <name>");
        }
        // Watching a replay or resuming a save needs the command source and
        // the load path wired up here as well; until then, say so rather than
        // run the game as an arena and quietly measure the wrong thing.
        if (args.contains("replay") || args.contains("load"))
        {
            throw std::runtime_error("ai_arena does not support --replay or --load yet; use rwe for those");
        }

        GameParameters gameParameters{args.getString("map"), 0};
        gameParameters.aiArenaSeconds = arenaSeconds;
        if (args.getUint("seed", 0) > 0)
        {
            gameParameters.randomSeed = args.getUint("seed", 0);
        }
        if (args.contains("record-demo"))
        {
            gameParameters.recordDemoFile = args.getString("record-demo");
        }

        auto difficulty = args.getString("ai-difficulty", "standard");
        for (auto& c : difficulty)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (difficulty == "idle" || difficulty == "none" || difficulty == "off")
        {
            gameParameters.aiDifficulty = AiDifficulty::Idle;
        }
        else if (difficulty == "easy")
        {
            gameParameters.aiDifficulty = AiDifficulty::Easy;
        }
        else if (difficulty == "hard")
        {
            gameParameters.aiDifficulty = AiDifficulty::Hard;
        }
        else if (difficulty == "brutal")
        {
            gameParameters.aiDifficulty = AiDifficulty::Brutal;
        }
        else
        {
            gameParameters.aiDifficulty = AiDifficulty::Standard;
        }

        auto startLocation = args.getString("start-location", "fixed");
        for (auto& c : startLocation)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (startLocation == "random")
        {
            gameParameters.startLocation = StartLocationMode::Random;
        }

        // A batch cares whether a run actually produced its numbers; the
        // default stays exit 0 so nothing that runs this today is affected.
        const bool strict = args.getBool("strict");
        const unsigned int watchdogSeconds = args.getUint("watchdog", 0);

        for (const auto& tuning : args.getMulti("ai-tune"))
        {
            gameParameters.aiTuning.push_back(tuning);
        }

        const auto players = args.getMulti("player");
        if (players.size() > gameParameters.players.size())
        {
            throw std::runtime_error("too many players");
        }
        for (Index i = 0; i < getSize(players); ++i)
        {
            gameParameters.players[i] = parsePlayerInfoFromArg(players[i]);
        }

        const auto personalityArgs = args.getMulti("ai-personality");
        if (!personalityArgs.empty())
        {
            auto personalities = loadAiPersonalities(aiPersonalityDirectory());
            for (const auto& entry : personalityArgs)
            {
                auto colon = entry.find(':');
                if (colon == std::string::npos)
                {
                    throw std::runtime_error("--ai-personality wants <player>:<name>, got " + entry);
                }
                auto slot = std::stoul(entry.substr(0, colon));
                if (slot >= gameParameters.players.size() || !gameParameters.players[slot])
                {
                    throw std::runtime_error("--ai-personality: there is no player " + entry.substr(0, colon));
                }
                auto personality = findAiPersonality(personalities, entry.substr(colon + 1));
                if (!personality)
                {
                    throw std::runtime_error("--ai-personality: no such personality: " + entry.substr(colon + 1));
                }
                gameParameters.players[slot]->aiPersonality = personality->name;
            }
        }

        // The harness has no interface to drive a human, so every seat must be
        // a computer. The windowed arena is the same game for the same reason.
        for (const auto& p : gameParameters.players)
        {
            if (p && std::visit(IsHumanVisitor(), p->controller))
            {
                throw std::runtime_error("ai_arena only plays computer players");
            }
        }

        auto pathMapping = defaultPathMapping();

        std::vector<fs::path> gameDataPaths;
        auto dataPaths = args.getMulti("data-path");
        if (!dataPaths.empty())
        {
            gameDataPaths.insert(gameDataPaths.end(), dataPaths.begin(), dataPaths.end());
        }
        else
        {
            auto localDataPath = getLocalDataPath();
            if (!localDataPath)
            {
                throw std::runtime_error("Failed to determine local data path");
            }
            gameDataPaths.emplace_back(*localDataPath) /= "Data";
        }

        CompositeVirtualFileSystem vfs;
        for (const auto& path : gameDataPaths)
        {
            addToVfs(vfs, path.string());
        }

        auto paletteBytes = vfs.readFile("palettes/PALETTE.PAL");
        if (!paletteBytes)
        {
            throw std::runtime_error("Couldn't find palette");
        }
        auto palette = readPalette(*paletteBytes);
        if (!palette)
        {
            throw std::runtime_error("Couldn't read palette");
        }

        auto guiPaletteBytes = vfs.readFile("palettes/GUIPAL.PAL");
        if (!guiPaletteBytes)
        {
            throw std::runtime_error("Couldn't find GUI palette");
        }
        auto guiPalette = readPalette(*guiPaletteBytes);
        if (!guiPalette)
        {
            throw std::runtime_error("Couldn't read GUI palette");
        }

        auto sideData = loadSideData(vfs);

        auto otaRaw = vfs.readFile("maps/" + gameParameters.mapName + ".ota");
        if (!otaRaw)
        {
            throw std::runtime_error("Failed to read OTA file");
        }
        std::string otaStr(otaRaw->begin(), otaRaw->end());
        auto ota = parseOta(parseTdfFromString(otaStr));

        auto tntBytes = vfs.readFile("maps/" + gameParameters.mapName + ".tnt");
        if (!tntBytes)
        {
            throw std::runtime_error("Failed to load map bytes");
        }
        SpanStream tntStream(tntBytes->data(), tntBytes->size());
        TntArchive tnt(&tntStream);

        auto mapData = readMapData(tnt, ota, gameParameters.schemaIndex);

        // No graphics context: the mesh service still reads the 3DO files and
        // builds the model definitions the simulation needs, and skips only
        // the GL meshes it would have extracted.
        MeshService meshService(&vfs, nullptr, {}, {}, {});

        GameLoadServices services{
            &vfs,
            &pathMapping,
            &*palette,
            &*guiPalette,
            nullptr,
            &meshService,
            nullptr,
            nullptr};

        auto loaded = loadGameSimulation(services, gameParameters, std::move(mapData), ota);

        // Before the commanders are placed, so their creation is offered to
        // the recorder as any other unit's would be. The recorder's own
        // constructor writes the demo's header, so no packet can precede it.
        if (gameParameters.recordDemoFile)
        {
            DemoRecorderSettings demoSettings;
            demoSettings.mapName = gameParameters.mapName;
            demoSettings.unitLoadOrder = loaded.dataMaps.unitLoadOrder;
            loaded.simulation.attachDemoRecorder(
                std::make_unique<DemoRecorder>(*gameParameters.recordDemoFile, loaded.simulation, std::move(demoSettings)));
            LOG_INFO << "Recording demo to " << *gameParameters.recordDemoFile;
        }

        spawnCommanders(loaded.simulation, loaded, sideData);

        auto playerCommandService = std::make_unique<PlayerCommandService>();
        for (const auto& playerId : loaded.gamePlayers)
        {
            if (playerId)
            {
                playerCommandService->registerPlayer(*playerId);
            }
        }

        const unsigned int sampleIntervalTicks = 10u * static_cast<unsigned int>(SimTicksPerSecond);
        AiArenaReport arenaReport(sampleIntervalTicks);
        loaded.simulation.eventLog.setRecording(true);
        const unsigned int arenaEndTick = arenaSeconds * static_cast<unsigned int>(SimTicksPerSecond);
        LOG_INFO << "AI arena: running for " << arenaSeconds << " seconds of game time (" << arenaEndTick << " ticks)";

        SimDiagnostics diagnostics;

        // The watchdog bounds wall time, not game time: it is how long the
        // batch will wait for one run, so it is measured here and never
        // reaches the simulation.
        const auto runStarted = std::chrono::steady_clock::now();

        unsigned int sceneTime = 0;
        std::optional<WinStatus> gameOver;
        bool watchdogFired = false;
        bool producedResult = false;

        auto winnerFrom = [](const std::optional<WinStatus>& status) -> std::optional<int> {
            if (status)
            {
                if (const auto* won = std::get_if<WinStatusWon>(&*status))
                {
                    return static_cast<int>(won->winner.value);
                }
            }
            return std::nullopt;
        };

        auto writeReport = [&](const char* ended) {
            auto outDir = args.contains("out")
                ? fs::path(args.getString("out"))
                : (getLocalDataPath() ? fs::path(*getLocalDataPath()) : fs::path("."));
            fs::create_directories(outDir);
            auto csvPath = outDir / "ai-arena.csv";

            AiArenaRunMetadata metadata;
            metadata.generatedBy = "ai_arena";
            metadata.ended = ended;
            metadata.winner = winnerFrom(gameOver);

            auto summary = arenaReport.write(csvPath, loaded.simulation, gameParameters, metadata);
            LOG_INFO << summary << " | ended=" << ended;
            LOG_INFO << "AI arena: wrote " << csvPath.string();
            producedResult = true;
        };

        while (true)
        {
            if (watchdogSeconds > 0
                && std::chrono::steady_clock::now() - runStarted >= std::chrono::seconds(watchdogSeconds))
            {
                auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStarted).count();
                LOG_ERROR << "AI-ARENA-WATCHDOG elapsed=" << elapsed << "s budget=" << watchdogSeconds << "s";
                watchdogFired = true;
                break;
            }

            // aiCommandBufferDepth rather than a figure of the arena's own: the
            // depth decides which tick an AI order lands on, so an arena that
            // chose differently would diverge from a windowed run on the first
            // order given.
            feedAiCommands(loaded.simulation, *playerCommandService, aiCommandBufferDepth());

            auto playerCommands = playerCommandService->tryPopCommands();
            if (!playerCommands)
            {
                LOG_ERROR << "Blocked waiting for player commands";
                break;
            }

            ++sceneTime;
            for (const auto& [issuingPlayer, commands] : *playerCommands)
            {
                for (const auto& command : commands)
                {
                    if (const auto* unitCommand = std::get_if<PlayerUnitCommand>(&command))
                    {
                        applyUnitCommandToSimulation(loaded.simulation, *unitCommand);
                    }
                }
            }

            loaded.simulation.tick();

            arenaReport.update(loaded.simulation);
            if (gameOver || loaded.simulation.gameTime.value >= arenaEndTick)
            {
                writeReport(gameOver ? "decided" : "timeout");
                break;
            }

            diagnostics.record(loaded.simulation, sceneTime);

            // GameScene decides the game at the end of the tick, after the
            // arena's own check; the check above reads the decision from the
            // previous tick. Match that, or the two stop a tick apart.
            if (!gameOver && loaded.simulation.players.size() >= 2)
            {
                auto winStatus = loaded.simulation.computeWinStatus();
                if (!std::holds_alternative<WinStatusUndecided>(winStatus))
                {
                    gameOver = winStatus;
                    LOG_INFO << "Game over at tick " << loaded.simulation.gameTime.value;
                }
            }
        }

        // The watchdog stops the clock, not the game, so whatever was
        // measured up to the abort is written before exiting.
        if (watchdogFired)
        {
            writeReport("timeout");
        }

        if (strict && (!producedResult || watchdogFired))
        {
            return 1;
        }

        return 0;
    }
    catch (const std::exception& e)
    {
        LOG_CRITICAL << e.what();
        return 1;
    }
}
