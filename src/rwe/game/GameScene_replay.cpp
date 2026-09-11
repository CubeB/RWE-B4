#include "GameScene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/io/gui/gui.h>
#include <rwe/game/save_util.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/MainMenuScene.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/Mesh.h>
#include <rwe/camera_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/game/OrderButtons.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/matrix_util.h>
#include <rwe/render/render_prof.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/resource_io.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

// Recording a game and watching one back, split out of GameScene.cpp for the
// reason set out at the head of GameScene_render.cpp: the single translation
// unit had outgrown the 32767 sections a COFF object can address.

namespace rwe
{
    void GameScene::pushReplayCommandsForTick(unsigned int tick)
    {
        auto tickIt = replayPlayback->commands.find(tick);
        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            std::vector<PlayerCommand> commands;
            if (tickIt != replayPlayback->commands.end())
            {
                auto playerIt = tickIt->second.find(static_cast<unsigned int>(i));
                if (playerIt != tickIt->second.end())
                {
                    commands = playerIt->second;
                }
            }

            // The recorded game's pauses and speed changes are not the
            // viewer's. Replaying them would stop the playback wherever
            // whoever recorded it happened to stop, and fight the controls in
            // the replay window for the rest of the game.
            commands.erase(
                std::remove_if(commands.begin(), commands.end(), [](const PlayerCommand& c) {
                    return std::holds_alternative<PlayerPauseGameCommand>(c)
                        || std::holds_alternative<PlayerUnpauseGameCommand>(c)
                        || std::holds_alternative<PlayerSetGameSpeedCommand>(c);
                }),
                commands.end());

            playerCommandService->pushCommands(PlayerId(i), commands);
        }
    }

    void GameScene::enableReplayPlayback(Replay&& replay)
    {
        LOG_INFO << "Replay: " << replay.commands.size() << " ticks with commands, last at tick " << replay.lastTick;
        replayPlayback = std::move(replay);

        // A recording is watched, not played: show the whole map and both
        // sides. Two flags, because they are two different rules -- the fog
        // decides whether the ground is lit, and hiding a cloaked unit is not
        // a fog rule and survives switching it off.
        fogOfWarEnabled = false;
        spectatorMode = true;

        if (gameParameters.replaySeekToTick > 0)
        {
            replaySeekTarget = gameParameters.replaySeekToTick;
        }

        if (const char* noKeyframes = std::getenv("RWE_REPLAY_NO_KEYFRAMES"); noKeyframes != nullptr && *noKeyframes != '\0' && *noKeyframes != '0')
        {
            replayKeyframesDisabled = true;
            LOG_INFO << "Replay: keyframes disabled by RWE_REPLAY_NO_KEYFRAMES";
        }

        availableReplays = listReplays();
    }

    void GameScene::enableReplayRecording(const std::filesystem::path& path, const ReplayHeader& header)
    {
        replayWriter.emplace(path, header);
        LOG_INFO << "Recording replay to " << path.string();
    }

    void GameScene::renderReplayWindow()
    {
        if (!replayPlayback)
        {
            return;
        }

        // An arena run draws nothing, and SceneManager's headless path skips
        // imGuiContext->newFrame() along with everything else it does not
        // need. Calling into ImGui with no frame open does not fail, it
        // wedges: this hung the first replay run stone dead on its first
        // frame, with the log stopping mid-tick and no error anywhere.
        // renderDebugWindow only escapes the same fate because its flag
        // defaults to off.
        if (gameParameters.aiArenaSeconds)
        {
            return;
        }

        auto ticksPerSecond = static_cast<unsigned int>(SimTicksPerSecond);
        auto lastTick = std::max(replayPlayback->lastTick, 1u);
        auto nowSeconds = static_cast<int>(sceneTime.value / ticksPerSecond);
        auto endSeconds = static_cast<int>(lastTick / ticksPerSecond);

        // Tall enough for the controls and the recordings list under them,
        // which is otherwise clipped to its first row the first time it is
        // opened -- and the first time is when it matters.
        ImGui::SetNextWindowSize(ImVec2(470.0f, 360.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Replay"))
        {
            ImGui::End();
            return;
        }

        if (replaySeekTarget)
        {
            ImGui::Text("Winding forward to %us...", *replaySeekTarget / ticksPerSecond);
            ImGui::ProgressBar(static_cast<float>(sceneTime.value) / static_cast<float>(std::max(*replaySeekTarget, 1u)));
            ImGui::End();
            return;
        }

        auto atEnd = sceneTime.value >= lastTick;
        ImGui::Text("%d:%02d of %d:%02d%s",
            nowSeconds / 60, nowSeconds % 60, endSeconds / 60, endSeconds % 60,
            atEnd ? "   (end)" : "");

        if (ImGui::Button(replayPlaying ? "Pause" : "Play"))
        {
            replayPlaying = !replayPlaying;
            // Pressing play at the end means watching it again, not staring
            // at a still frame while nothing happens.
            if (replayPlaying && atEnd)
            {
                seekReplayTo(0);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Restart"))
        {
            seekReplayTo(0);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::SliderInt("Speed", &replaySpeed, 1, 16, "%dx");

        // The handle only follows the game when nobody is holding it. Setting
        // it from the playback position every frame, which is what this did,
        // means the game drags it back out from under the mouse and the bar
        // cannot be moved at all while anything is playing.
        if (!replayScrubbing)
        {
            replayScrubSeconds = nowSeconds;
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##seek", &replayScrubSeconds, 0, std::max(endSeconds, 1), "%ds");
        if (ImGui::IsItemActivated())
        {
            replayScrubbing = true;
        }
        if (ImGui::IsItemDeactivated())
        {
            // Acted on when the handle is let go rather than during the drag:
            // a wind-forward per pixel crossed would be a hundred of them.
            replayScrubbing = false;
            auto target = static_cast<unsigned int>(std::max(replayScrubSeconds, 0)) * ticksPerSecond;
            if (target != sceneTime.value)
            {
                seekReplayTo(target);
            }
        }
        ImGui::TextDisabled("%zu keyframes, %zu MB, every %us",
            replayKeyframes.size(),
            replayKeyframeBytes / (1024u * 1024u),
            ReplayKeyframeInterval / ticksPerSecond);

        ImGui::Checkbox("See everything", &spectatorMode);
        ImGui::SameLine();
        ImGui::Checkbox("Fog", &fogOfWarEnabled);
        if (fogOfWarEnabled)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(both sides)");
        }

        // Whose economy the top bar reads out. A recording has no local
        // player in the sense the interface means -- whichever slot stood in
        // for one is not necessarily the side worth watching.
        auto playerCount = getSize(simulation.players);
        if (playerCount > 1)
        {
            auto current = hudPlayerId();
            std::string label = "Player " + std::to_string(current.value) + ": " + simulation.getPlayer(current).side;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("Economy", label.c_str()))
            {
                for (Index i = 0; i < playerCount; ++i)
                {
                    PlayerId id(i);
                    auto entry = "Player " + std::to_string(i) + ": " + simulation.getPlayer(id).side;
                    if (ImGui::Selectable(entry.c_str(), id == current))
                    {
                        hudPlayerOverride = id;
                    }
                }
                ImGui::EndCombo();
            }
        }

        ImGui::Separator();
        // Open the first time it is seen: choosing what to watch is half of
        // what this window is for, and a list nobody knows is there is not a
        // chooser. Once, not always, so it stays shut if it is closed.
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Recordings"))
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 70.0f);
            if (ImGui::SmallButton("Refresh"))
            {
                availableReplays = listReplays();
            }

            ImGui::BeginChild("replay list", ImVec2(0.0f, 140.0f), true);
            for (const auto& summary : availableReplays)
            {
                auto minutes = summary.seconds / 60;
                auto seconds = summary.seconds % 60;
                auto label = summary.path.stem().string() + "  -  " + summary.mapName
                    + "  -  " + summary.players
                    + "  -  " + std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds);

                auto isCurrent = gameParameters.replayFile && summary.path == std::filesystem::path(*gameParameters.replayFile);
                if (ImGui::Selectable(label.c_str(), isCurrent) && !isCurrent)
                {
                    openReplay(summary.path);
                }
            }
            if (availableReplays.empty())
            {
                ImGui::TextUnformatted("Nothing in the Replays folder yet.");
            }
            ImGui::EndChild();
        }

        ImGui::End();
    }

    void GameScene::openReplay(const std::filesystem::path& path)
    {
        auto replay = readReplayFile(path);
        if (!replay)
        {
            printConsole("Could not read replay: " + path.string());
            return;
        }

        // Every recording brings its own map, players and seed, so switching
        // to one is starting a different game rather than pointing this one
        // somewhere else.
        auto parameters = gameParametersFromReplayHeader(replay->header);
        parameters.replayFile = path.string();
        leaveFor(std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            parameters));
    }

    void GameScene::restartReplayAt(unsigned int tick)
    {
        auto parameters = gameParameters;
        parameters.replaySeekToTick = tick;
        leaveFor(std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            parameters));
    }

    void GameScene::takeReplayKeyframe()
    {
        auto started = std::chrono::steady_clock::now();
        auto bytes = nlohmann::json::to_cbor(saveSimulationToJson(simulation));
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        replayKeyframeBytes += bytes.size();
        LOG_INFO << "Replay: keyframe at tick " << sceneTime.value << ", " << (bytes.size() / 1024) << " KB in " << elapsed << " ms, "
                 << (replayKeyframes.size() + 1) << " held in " << (replayKeyframeBytes / 1024) << " KB";
        replayKeyframes.emplace(sceneTime.value, std::move(bytes));
    }

    void GameScene::restoreReplayKeyframe(unsigned int tick, const std::vector<std::uint8_t>& keyframe)
    {
        auto started = std::chrono::steady_clock::now();
        clearSimulationForLoad(simulation);
        loadSimulationFromJson(nlohmann::json::from_cbor(keyframe), simulation);
        sceneTime = SceneTime(tick);

        // Everything the scene remembers about particular units is now about
        // units that may not exist, or may be somebody else in the same slot.
        // Effects and the like can stay: they are pictures, and wrong ones
        // for a moment is the worst they can be.
        clearUnitSelection();
        hoveredUnit = std::nullopt;
        hoveredFeature = std::nullopt;
        trackedUnitId = std::nullopt;
        if (std::holds_alternative<CameraControlStateTrackingUnit>(cameraControlState))
        {
            cameraControlState = CameraControlStateFree();
        }
        for (auto& group : controlGroups)
        {
            group.clear();
        }
        cloakedUnits.clear();
        unitGuiInfos.clear();
        unconfirmedBuildQueueDelta.clear();
        unconfirmedStockpileDelta.clear();
        selfDestructAnnounced.clear();
        buildBoxAppearedAt.clear();
        nextUnitCursor = std::nullopt;
        // Timed callbacks are keyed on a scene time that has just moved
        // backwards, and would all fire at once on the next tick.
        actions.clear();
        // Decided later than where the playback now is; decided again when
        // it gets there.
        gameOver = std::nullopt;
        gameOverTime = GameTime(0);
        combinedVisibility = std::nullopt;
        replayReachedEnd = false;
        millisecondsBuffer = 0;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        LOG_INFO << "Replay: restored keyframe at tick " << tick << " in " << elapsed << " ms";
    }

    void GameScene::seekReplayTo(unsigned int tick)
    {
        LOG_INFO << "Replay: seek to tick " << tick << " from tick " << sceneTime.value;
        if (tick >= sceneTime.value)
        {
            if (tick > sceneTime.value)
            {
                replaySeekTarget = tick;
            }
            return;
        }

        // The latest keyframe at or before the target. A lockstep game only
        // runs forwards, so going back means starting from a state that is
        // known to be earlier and winding on -- the keyframe, if there is
        // one, or the start of the game if there is not.
        auto it = replayKeyframes.upper_bound(tick);
        if (it == replayKeyframes.begin())
        {
            restartReplayAt(tick);
            return;
        }
        --it;

        try
        {
            restoreReplayKeyframe(it->first, it->second);
        }
        catch (const std::exception& e)
        {
            // A keyframe that will not load is a bug, but the viewer can
            // still do what it did before there were any.
            LOG_ERROR << "Replay: could not restore keyframe at tick " << it->first << ": " << e.what();
            restartReplayAt(tick);
            return;
        }

        if (tick > sceneTime.value)
        {
            replaySeekTarget = tick;
        }
    }

    void GameScene::tryTickGame()
    {
        if (replayPlayback)
        {
            // Before the tick's commands go in, so the keyframe is the state
            // after the previous tick and nothing else -- restoring it and
            // pushing this tick's commands is exactly what happens next
            // here. Taken while winding forward as well as while watching,
            // so that the ground a seek has crossed can be come back to.
            // Not taken twice: a second pass over the same tick after a
            // restore already has one, and it would be the same bytes.
            if (!replayKeyframesDisabled && sceneTime.value % ReplayKeyframeInterval == 0 && replayKeyframes.find(sceneTime.value) == replayKeyframes.end())
            {
                takeReplayKeyframe();
            }

            // One set per player for the tick about to run. Pushed here and
            // not in update() because the service pops exactly one set per
            // player per tick, and anywhere else means guessing how many
            // ticks this frame is about to dispatch.
            pushReplayCommandsForTick(sceneTime.value);
        }

        if (!playerCommandService->checkHashes())
        {
            std::ofstream dumpFile;
            dumpFile.open("rwe-dump-" + std::to_string(std::rand()) + ".json");
            dumpFile << dumpJson(simulation);
            dumpFile.close();
            throw std::runtime_error("Desync detected");
        }

        auto playerCommands = playerCommandService->tryPopCommands();
        if (!playerCommands)
        {
            LOG_ERROR << "Blocked waiting for player commands";
            return;
        }

        if (replayWriter)
        {
            // Recorded at the pop rather than at the push, and keyed on the
            // scene time about to be spent. What gets pushed includes padding
            // whose size is derived from live network latency, so the pushed
            // stream puts a command on a different tick from one run to the
            // next; what gets popped is what the simulation actually saw.
            for (const auto& [commandPlayerId, commands] : *playerCommands)
            {
                replayWriter->recordTick(sceneTime.value, commandPlayerId, commands);
            }
        }

        sceneTime += SceneTime(1);

        // Four relaxed stores a tick, so that a crash anywhere below can say
        // when it happened rather than only where.
        setCrashPhase(CrashPhase::SimTick);
        setCrashTick(
            sceneTime.value,
            simulation.gameTime.value,
            static_cast<uint32_t>(simulation.units.slotCount()),
            static_cast<uint32_t>(simulation.players.size()));

        processActions();

        processPlayerCommands(*playerCommands);

        {
            RWE_RENDERPROF("u.simtick");
            simulation.tick();
        }

        if (arenaReport)
        {
            arenaReport->update(simulation);

            // A game is over when somebody has won it. The time limit is a
            // cap for the games that never get there, not the point of the
            // exercise -- stopping every game at ten minutes measured two
            // economies rather than a fight, which is exactly what made the
            // AIs look like they never met.
            auto decided = gameOver.has_value();
            if (arenaEndTick && (decided || simulation.gameTime.value >= *arenaEndTick))
            {
                // Beside the log, or the working directory if there is no
                // local data path -- a measurement run that cannot find
                // %APPDATA% should still hand back its numbers.
                auto localDataPath = getLocalDataPath();
                auto csvPath = localDataPath ? *localDataPath : std::filesystem::path(".");
                csvPath /= "ai-arena.csv";
                auto summary = arenaReport->write(csvPath, simulation);
                LOG_INFO << summary << (decided ? " | ended=decided" : " | ended=timeout");
                LOG_INFO << "AI arena: wrote " << csvPath.string();
                arenaReport.reset();
                sceneContext.sceneManager->requestExit();
                return;
            }
        }

        // The sync hash is what a peer is compared against, and a replay has
        // no peer: it is one machine replaying its own recording. It is not
        // free either -- computeHashOf walks every unit, every projectile and
        // every map feature, and a map has thousands of features -- so it is
        // work with nothing to show for it. checkHashes stays happy with an
        // empty buffer, so there is nothing to keep fed.
        //
        // Measured, in case anyone hopes otherwise: this does NOT noticeably
        // speed up winding the scrub bar forward. That still runs at about
        // ten times real time, and the cost is the simulation itself rather
        // than anything around it. Periodic keyframes are the fix for that.
        if (!replayPlayback)
        {
            GameHash gameHash{0};
            {
                RWE_RENDERPROF("u.hash");
                gameHash = simulation.computeHash();
            }
            playerCommandService->pushHash(localPlayerId, gameHash);
            gameNetworkService->submitGameHash(gameHash);
        }

        if (stateLogStream)
        {
            *stateLogStream << dumpJson(simulation) << std::endl;
        }

        {
            RWE_RENDERPROF("u.events");
            processSimEvents();
        }

        updateCloakNotifications();

        updateSelfDestructNotifications();

        updateDefeatNotifications();

        updateProjectiles();

        updateFlashes();

        updateScreenShake();

        {
            RWE_RENDERPROF("u.particles");
            updateParticles(gameMediaDatabase, simulation.terrain, simulation.gameTime, particles);
        }

        {
            RWE_RENDERPROF("u.spawnnano");
            spawnNanoParticles();
        }

        spawnGeoVentSteam();

        updateDebris();

        updateBuildBoxAppearances();

        // Testing aid: RWE_DEBUG_SPAWN=<unitType>*<count>@<player>:<seconds>
        // drops finished units of that type, owned by that player, in a ring
        // around the local player's first unit at that game time (for example
        // CORAK*6@1:10 to have six AKs attack the commander at ten seconds).
        if (const char* debugSpawn = std::getenv("RWE_DEBUG_SPAWN"))
        {
            std::string spec(debugSpawn);
            auto star = spec.find('*');
            auto at = spec.find('@');
            auto colon = spec.find(':');
            if (star != std::string::npos && at != std::string::npos && colon != std::string::npos)
            {
                auto unitType = spec.substr(0, star);
                auto count = std::atoi(spec.substr(star + 1, at - star - 1).c_str());
                auto player = std::atoi(spec.substr(at + 1, colon - at - 1).c_str());
                auto rest = spec.substr(colon + 1);
                auto nearSep = rest.find(':');
                auto seconds = static_cast<unsigned int>(std::atoi(rest.substr(0, nearSep).c_str()));
                // An optional trailing :<player> puts the ring around that player's first unit instead.
                auto nearPlayer = nearSep == std::string::npos ? PlayerId(localPlayerId) : PlayerId(static_cast<unsigned int>(std::atoi(rest.substr(nearSep + 1).c_str())));
                if (seconds > 0 && simulation.gameTime.value == seconds * static_cast<unsigned int>(SimTicksPerSecond) && isValidUnitType(simulation, unitType) && player >= 0 && player < getSize(simulation.players))
                {
                    std::optional<SimVector> centre;
                    for (const auto& [unitId, unit] : simulation.units)
                    {
                        if (unit.isOwnedBy(nearPlayer) && unit.isAlive())
                        {
                            centre = unit.position;
                            break;
                        }
                    }
                    for (int i = 0; centre && i < count; ++i)
                    {
                        auto angle = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(std::max(1, count));
                        SimVector position(centre->x + SimScalar(std::cos(angle) * 96.0f), centre->y, centre->z + SimScalar(std::sin(angle) * 96.0f));
                        position.y = simulation.terrain.getHeightAt(position.x, position.z);
                        LOG_INFO << "Debug: spawning " << unitType << " for player " << player << " at " << simScalarToFloat(position.x) << "," << simScalarToFloat(position.z);
                        spawnCompletedUnit(unitType, PlayerId(static_cast<unsigned int>(player)), position);
                    }
                }
            }
        }

        // Testing aid: RWE_DEBUG_SELF_DESTRUCT=<seconds> self-destructs a
        // player's first unit (the commander) at that game time, so a crash on
        // commander death can be reproduced under a debugger. The player is
        // the local one unless RWE_DEBUG_SELF_DESTRUCT_PLAYER gives an index.
        if (const char* debugSelfDestruct = std::getenv("RWE_DEBUG_SELF_DESTRUCT"))
        {
            auto seconds = static_cast<unsigned int>(std::atoi(debugSelfDestruct));
            if (seconds > 0 && simulation.gameTime.value == seconds * static_cast<unsigned int>(SimTicksPerSecond))
            {
                auto player = localPlayerId;
                if (const char* debugPlayer = std::getenv("RWE_DEBUG_SELF_DESTRUCT_PLAYER"))
                {
                    player = PlayerId(static_cast<unsigned int>(std::atoi(debugPlayer)));
                }
                for (const auto& [unitId, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(player) && unit.isAlive())
                    {
                        LOG_INFO << "Debug: self-destructing unit " << unitId.value << " of player " << player.value;
                        if (player == localPlayerId)
                        {
                            localPlayerSelfDestructUnit(unitId);
                        }
                        else
                        {
                            simulation.toggleSelfDestruct(unitId);
                        }
                        break;
                    }
                }
            }
        }

        // A game needs an opponent before it can be decided; a lone player
        // is just exploring the map.
        if (!gameOver && simulation.players.size() >= 2)
        {
            auto winStatus = simulation.computeWinStatus();
            match(
                winStatus,
                [&](const WinStatusWon& w) {
                    gameOver = winStatus;
                    gameOverTime = simulation.gameTime;
                    LOG_INFO << "Game over: player " << w.winner.value << " won at tick " << simulation.gameTime.value;
                },
                [&](const WinStatusDraw&) {
                    gameOver = winStatus;
                    gameOverTime = simulation.gameTime;
                    LOG_INFO << "Game over: draw at tick " << simulation.gameTime.value;
                },
                [&](const WinStatusUndecided&) {
                    // do nothing, game still in progress
                });
        }
    }

}
