#include "AiPlayerController.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>

namespace rwe
{
    namespace
    {
        // A pass slower than this is worth a line of its own in the log.
        const double AiProfileSpikeThresholdMs = 2.0;

        // How often the accumulated totals are dumped.
        const unsigned int AiProfileReportIntervalTicks = 30u * static_cast<unsigned int>(SimTicksPerSecond);
    }

    bool AiProfiler::enabled()
    {
        // Read once: the answer cannot change, and getenv is not cheap.
        static const bool on = std::getenv("RWE_AI_PROFILE") != nullptr;
        return on;
    }

    void AiProfiler::record(const char* pass, double milliseconds, PlayerId player, GameTime now)
    {
        auto& stats = passes[pass];
        stats.totalMs += milliseconds;
        stats.worstMs = std::max(stats.worstMs, milliseconds);
        ++stats.calls;
        windowMs += milliseconds;
        if (milliseconds >= AiProfileSpikeThresholdMs)
        {
            ++stats.spikes;
            LOG_INFO << "AI profile: player " << player.value << " pass " << pass << " took " << milliseconds << " ms at tick " << now.value;
        }
    }

    void AiProfiler::report(PlayerId player, GameTime now)
    {
        if (passes.empty())
        {
            return;
        }
        std::string line;
        for (const auto& [name, stats] : passes)
        {
            line += " " + name + "=" + std::to_string(stats.totalMs) + "ms/" + std::to_string(stats.calls) + " (worst " + std::to_string(stats.worstMs) + "ms, " + std::to_string(stats.spikes) + " spikes)";
        }
        LOG_INFO << "AI profile summary: player " << player.value << " at tick " << now.value << ", total " << windowMs << " ms:" << line;
        passes.clear();
        windowMs = 0.0;
    }

    AiPlayerController::AiPlayerController(
        PlayerId playerId,
        AiTuningProfile profile,
        std::uint64_t rngSeed)
        : playerId(playerId),
          profile(std::move(profile)),
          rng(static_cast<std::uint_fast32_t>(rngSeed))
    {
    }

    void AiPlayerController::tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands)
    {
        // Times a pass when RWE_AI_PROFILE is set, and simply runs it otherwise.
        // Nothing measured here feeds back into the sim, so this is invisible
        // to determinism.
        const bool profiling = AiProfiler::enabled();
        auto timed = [&](const char* name, auto&& pass) {
            if (!profiling)
            {
                pass();
                return;
            }
            auto start = std::chrono::steady_clock::now();
            pass();
            auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            profiler.record(name, elapsed, playerId, sim.gameTime);
        };

        // 1. What do we own, and how is the economy doing?
        timed("economy", [&] { economy.refresh(sim, playerId, profile, blackboard); });

        // 2. What do we know about the enemy?
        timed("perception", [&] { perception.refresh(sim, playerId, profile, blackboard); });

        // 3. Influence map, once a second.
        ++ticksSinceThreatRebuild;
        if (threatMap.isEmpty() || ticksSinceThreatRebuild >= profile.threatMapTickInterval)
        {
            ticksSinceThreatRebuild = 0;
            timed("threatMap", [&] { threatMap.rebuild(sim, playerId, blackboard, profile.cheatModeOmniscient); });
        }

        // 3b. Where can our ground units walk to? Rebuilt now and then; the ground does not change.
        ++ticksSinceReachabilityRebuild;
        if (blackboard.baseAnchor && blackboard.sideUnitsResolved && (!reachability.isValid() || ticksSinceReachabilityRebuild >= 20 * SimTicksPerSecond))
        {
            ticksSinceReachabilityRebuild = 0;
            std::string mover = blackboard.sideUnits.constructor.empty() ? (blackboard.commanderUnitId ? sim.getUnitState(*blackboard.commanderUnitId).unitType : std::string()) : blackboard.sideUnits.constructor;
            auto moverDef = sim.unitDefinitions.find(mover);
            if (moverDef != sim.unitDefinitions.end())
            {
                timed("reachability", [&] {
                    reachability.rebuild(sim, moverDef->second.movementCollisionInfo, *blackboard.baseAnchor);
                    blackboard.groundReachabilityValid = reachability.isValid();
                    blackboard.hasUnreachableGround = reachability.walkableTileCount() > reachability.reachableTileCount() + 64;
                });
            }
        }

        // 4. Which phase of the game are we in?
        auto previousPhase = blackboard.phase;
        strategic.update(profile, blackboard);
        if (blackboard.phase != previousPhase)
        {
            LOG_INFO << "AI player " << playerId.value << " (" << profile.name << "): " << gamePhaseName(previousPhase) << " -> " << gamePhaseName(blackboard.phase)
                     << " at tick " << sim.gameTime.value << ", army " << blackboard.armySize << ", known enemies " << blackboard.knownEnemies.size();
        }

        // Periodic status line for play-test logs.
        if (sim.gameTime.value % (30u * SimTicksPerSecond) == 0)
        {
            std::string counts;
            for (const auto& [type, count] : blackboard.ownedTotalCounts)
            {
                counts += " " + type + "x" + std::to_string(count);
            }
            std::string commanderDoing = "no commander";
            if (blackboard.commanderUnitId)
            {
                const auto& commander = sim.getUnitState(*blackboard.commanderUnitId);
                commanderDoing = "commander at " + std::to_string(static_cast<int>(commander.position.x.value)) + "," + std::to_string(static_cast<int>(commander.position.z.value));
                if (commander.orders.empty())
                {
                    commanderDoing += " idle";
                }
                else
                {
                    commanderDoing += match(
                        commander.orders.front(),
                        [](const BuildOrder& o) { return " building " + o.unitType + " at " + std::to_string(static_cast<int>(o.position.x.value)) + "," + std::to_string(static_cast<int>(o.position.z.value)); },
                        [](const MoveOrder&) { return std::string(" moving"); },
                        [](const GuardOrder&) { return std::string(" guarding"); },
                        [](const auto&) { return std::string(" on another order"); });
                    commanderDoing += std::holds_alternative<NavigationStateMoving>(commander.navigationState.state) ? " (walking)" : " (not walking)";
                    commanderDoing += commander.buildOrderUnitId ? " nanoframe placed" : " no nanoframe";
                }
            }
            const auto& player = sim.getPlayer(playerId);
            LOG_INFO << "AI player " << playerId.value << " status: phase " << gamePhaseName(blackboard.phase)
                     << ", metal " << blackboard.currentMetal.value << (blackboard.metalStalled ? "(stalled)" : "")
                     << " (+" << player.metalProductionBuffer.value << "/-" << player.previousDesiredMetalConsumptionBuffer.value << " per s)"
                     << ", energy " << blackboard.currentEnergy.value << (blackboard.energyStalled ? "(stalled)" : "")
                     << " (+" << player.energyProductionBuffer.value << "/-" << player.previousDesiredEnergyConsumptionBuffer.value << " per s)"
                     << ", idle builders " << blackboard.idleBuilderCount << ", army " << blackboard.armySize
                     << ", known enemies " << blackboard.knownEnemies.size() << ", units:" << counts << "; " << commanderDoing;
        }

        // 5. Economy and production.
        timed("build", [&] { build.update(sim, playerId, profile, blackboard, reachability, rng, outCommands); });

        // 6. Eyes, lift and fists.
        timed("scout", [&] { scout.update(sim, profile, threatMap, reachability, blackboard, outCommands); });
        timed("transport", [&] { transport.update(sim, playerId, profile, reachability, build, blackboard, rng, outCommands); });
        timed("army", [&] { army.update(sim, playerId, profile, threatMap, blackboard, outCommands); });

        if (profiling && sim.gameTime.value % AiProfileReportIntervalTicks == 0)
        {
            profiler.report(playerId, sim.gameTime);
        }
    }
}
