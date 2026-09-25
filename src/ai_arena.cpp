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
 *
 * `ai_arena --replay <file.rwereplay> [--ai-arena <seconds>]` plays a
 * recorded game back through the same loader and tick loop rather than a
 * fresh --map/--player one: the map, players, seed and options all come out
 * of the replay's own header, so --map and --player cannot be given
 * alongside it. GameSimulationLoader idles every computer player's
 * AiPlayerController when a replay is in force (it is still constructed,
 * because building one draws a value from the sim RNG that has to land the
 * same as it did live) and this harness feeds every player's recorded
 * commands instead of calling feedAiCommands, the same substitution
 * GameScene::pushReplayCommandsForTick makes for a windowed viewer.
 * Without --ai-arena the whole recording plays;
 * with it, playback stops at min(given, the replay's own length). The usual
 * ai-arena.csv, ai-arena-events.csv, event-log.jsonl, run.json and
 * AI-ARENA-RESULT line are written exactly as for an ordinary run.
 *
 * `ai_arena --list-knobs [--json]` prints every AI tuning knob applyAiTuning
 * accepts -- name, type and default value -- and exits without needing
 * --map; add --json for a JSON array instead of tab-separated lines.
 */

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/ColorPalette.h>
#include <rwe/GameLaunch.h>
#include <rwe/PathMapping.h>
#include <rwe/ai/AiPersonality.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/AiArenaReport.h>
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/game/PlayerCommandApplication.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/game/ReplayFile.h>
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

    /**
     * GameScene::pushReplayCommandsForTick, without a scene: one set per
     * player for the tick about to run, out of the recording rather than out
     * of a thinking AI. A replay's computer players are watched, not
     * replayed by a fresh decision -- GameSimulationLoader idles their
     * AiPlayerController for exactly this reason -- so this is the only
     * source of commands in a replay run, in place of feedAiCommands.
     *
     * Pause, unpause and game-speed commands are dropped, matching the
     * viewer: whoever recorded the game may have paused it, and a headless
     * arena run has no clock of its own for that to mean anything to.
     */
    void pushReplayCommandsForTick(rwe::GameSimulation& simulation, rwe::PlayerCommandService& playerCommandService, const rwe::Replay& replay, unsigned int tick)
    {
        auto tickIt = replay.commands.find(tick);
        for (rwe::Index i = 0; i < rwe::getSize(simulation.players); ++i)
        {
            if (!playerCommandService.needsCommandsForTick(rwe::PlayerId(i), tick + 1))
            {
                continue;
            }

            std::vector<rwe::PlayerCommand> commands;
            if (tickIt != replay.commands.end())
            {
                auto playerIt = tickIt->second.find(static_cast<unsigned int>(i));
                if (playerIt != tickIt->second.end())
                {
                    commands = playerIt->second;
                }
            }

            commands.erase(
                std::remove_if(commands.begin(), commands.end(), [](const rwe::PlayerCommand& c) {
                    return std::holds_alternative<rwe::PlayerPauseGameCommand>(c)
                        || std::holds_alternative<rwe::PlayerUnpauseGameCommand>(c)
                        || std::holds_alternative<rwe::PlayerSetGameSpeedCommand>(c);
                }),
                commands.end());

            playerCommandService.pushCommands(rwe::PlayerId(i), commands);
        }
    }

    void printAiKnobsPlain()
    {
        for (const auto& knob : rwe::listAiKnobs())
        {
            std::cout << knob.name << '\t' << knob.type << '\t' << knob.defaultValue << '\n';
        }
    }

    void printAiKnobsJson()
    {
        auto j = nlohmann::json::array();
        for (const auto& knob : rwe::listAiKnobs())
        {
            j.push_back({{"name", knob.name}, {"type", knob.type}, {"default", knob.defaultValue}});
        }
        std::cout << j.dump(2) << '\n';
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
                      << "       ai_arena --replay <file.rwereplay> [--ai-arena <seconds>] [options]\n"
                      << "       ai_arena --list-knobs [--json]\n"
                      << "  --map <name>            map to play on\n"
                      << "  --ai-arena <seconds>    game length cap (with --replay: caps the replay's own length)\n"
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
                      << "  --data-path <path>      game data search path (repeatable)\n"
                      << "  --replay <file>         play a recorded .rwereplay headlessly instead of --map/--player\n"
                      << "  --list-knobs            print every AI tuning knob (name, type, default) and exit\n"
                      << "  --json                  with --list-knobs, print a JSON array instead of tab-separated text\n";
            return 0;
        }

        if (args.contains("list-knobs"))
        {
            if (args.getBool("json"))
            {
                printAiKnobsJson();
            }
            else
            {
                printAiKnobsPlain();
            }
            return 0;
        }

        if (args.contains("load"))
        {
            throw std::runtime_error("ai_arena does not support --load yet; use rwe for that");
        }

        const bool isReplay = args.contains("replay");

        if (!isReplay && !args.contains("map"))
        {
            throw std::runtime_error("ai_arena needs --map <name>");
        }
        if (isReplay && args.contains("map"))
        {
            throw std::runtime_error("ai_arena --replay takes its map from the replay; --map cannot be given alongside it");
        }
        if (isReplay && !args.getMulti("player").empty())
        {
            throw std::runtime_error("ai_arena --replay takes its players from the replay; --player cannot be given alongside it");
        }

        auto arenaSecondsArg = args.getUint("ai-arena", 0);
        if (arenaSecondsArg == 0 && !isReplay)
        {
            throw std::runtime_error("ai_arena needs --ai-arena <seconds>");
        }

        std::optional<Replay> replay;
        std::optional<GameParameters> gameParametersHolder;
        if (isReplay)
        {
            auto replayPath = fs::path(args.getString("replay"));
            auto readReplay = readReplayFile(replayPath);
            if (!readReplay)
            {
                throw std::runtime_error("Could not read replay file: " + replayPath.string());
            }
            replay = std::move(readReplay);
            gameParametersHolder = gameParametersFromReplayHeader(replay->header);
            gameParametersHolder->replayFile = replayPath.string();
            // The point of replaying here is to review the game, and the
            // review wants what each computer player decided and why. So its
            // AI thinks in the shadow of the recording: see the loop.
            gameParametersHolder->replayShadowAi = true;
        }
        else
        {
            gameParametersHolder = GameParameters{args.getString("map"), 0};
        }
        GameParameters& gameParameters = *gameParametersHolder;
        if (args.contains("mission"))
        {
            gameParameters.mission = true;
        }

        if (args.contains("record-demo"))
        {
            gameParameters.recordDemoFile = args.getString("record-demo");
        }

        // A batch cares whether a run actually produced its numbers; the
        // default stays exit 0 so nothing that runs this today is affected.
        // Both apply to a replay run as much as an ordinary one.
        const bool strict = args.getBool("strict");
        const unsigned int watchdogSeconds = args.getUint("watchdog", 0);

        // Everything below chooses the game: the map, the players, the
        // difficulty, the seed, the start-location deal. A replay carries all
        // of that in its own header already -- reapplying the command line on
        // top would play a different game from the one recorded, which is
        // the opposite of what watching a replay is for.
        if (!isReplay)
        {
            gameParameters.aiArenaSeconds = arenaSecondsArg;
            if (args.getUint("seed", 0) > 0)
            {
                gameParameters.randomSeed = args.getUint("seed", 0);
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
            // A replay is watched rather than played, so a human slot in its
            // header is no obstacle: its commands come out of the recording like
            // everyone else's, not out of a controller nobody is driving.
            for (const auto& p : gameParameters.players)
            {
                if (p && std::visit(IsHumanVisitor(), p->controller))
                {
                    throw std::runtime_error("ai_arena only plays computer players");
                }
            }
        } // !isReplay

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

        if (loaded.gameParameters.mission)
        {
            auto result = spawnMissionUnits(loaded.simulation, loaded.ota.schemas.at(loaded.gameParameters.schemaIndex), loaded.gamePlayers);
            LOG_INFO << "Mission: " << result.spawned.size() << " units placed, " << result.skipped.size() << " not";
            for (const auto& line : result.skipped)
            {
                LOG_WARN << "Mission unit not placed: " << line;
            }
        }
        else
        {
            spawnCommanders(loaded.simulation, loaded, sideData);
        }

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

        // A replay's own length is the default cap, and --ai-arena only ever
        // shortens it: min(given, replay length), never the other way, since
        // a replay has nothing recorded past its own end to run on. An
        // ordinary game has no such ceiling, so --ai-arena is the whole of
        // it there.
        unsigned int arenaEndTick;
        if (replay)
        {
            arenaEndTick = replay->lastTick;
            if (arenaSecondsArg > 0)
            {
                arenaEndTick = std::min(arenaEndTick, arenaSecondsArg * static_cast<unsigned int>(SimTicksPerSecond));
            }
            gameParameters.aiArenaSeconds = (arenaEndTick + static_cast<unsigned int>(SimTicksPerSecond) - 1) / static_cast<unsigned int>(SimTicksPerSecond);
        }
        else
        {
            arenaEndTick = arenaSecondsArg * static_cast<unsigned int>(SimTicksPerSecond);
        }
        LOG_INFO << "AI arena: running for " << (arenaEndTick / static_cast<unsigned int>(SimTicksPerSecond))
                 << " seconds of game time (" << arenaEndTick << " ticks)"
                 << (replay ? " [replay]" : "");

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

            if (replay)
            {
                // The recording is every player's command source, computer
                // seats included: see pushReplayCommandsForTick above.
                pushReplayCommandsForTick(loaded.simulation, *playerCommandService, *replay, sceneTime);

                // The shadow AI's own orders are the ones the recording
                // already holds; drop them, or they pile up unread.
                for (Index i = 0; i < getSize(loaded.simulation.players); ++i)
                {
                    loaded.simulation.takeAiCommandsForPlayer(PlayerId(static_cast<unsigned int>(i)));
                }
            }
            else
            {
                // aiCommandBufferDepth rather than a figure of the arena's own:
                // the depth decides which tick an AI order lands on, so an
                // arena that chose differently would diverge from a windowed
                // run on the first order given.
                feedAiCommands(loaded.simulation, *playerCommandService, aiCommandBufferDepth());
            }

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
