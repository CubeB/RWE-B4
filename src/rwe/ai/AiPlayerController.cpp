#include "AiPlayerController.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>

namespace rwe
{
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
        // 1. What do we own, and how is the economy doing?
        economy.refresh(sim, playerId, profile, blackboard);

        // 2. What do we know about the enemy?
        perception.refresh(sim, playerId, profile, blackboard);

        // 3. Influence map, once a second.
        ++ticksSinceThreatRebuild;
        if (threatMap.isEmpty() || ticksSinceThreatRebuild >= profile.threatMapTickInterval)
        {
            ticksSinceThreatRebuild = 0;
            threatMap.rebuild(sim, playerId, blackboard, profile.cheatModeOmniscient);
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
            LOG_INFO << "AI player " << playerId.value << " status: phase " << gamePhaseName(blackboard.phase)
                     << ", metal " << blackboard.currentMetal.value << (blackboard.metalStalled ? "(stalled)" : "")
                     << ", energy " << blackboard.currentEnergy.value << (blackboard.energyStalled ? "(stalled)" : "")
                     << ", idle builders " << blackboard.idleBuilderCount << ", army " << blackboard.armySize
                     << ", known enemies " << blackboard.knownEnemies.size() << ", units:" << counts << "; " << commanderDoing;
        }

        // 5. Economy and production.
        build.update(sim, playerId, profile, blackboard, rng, outCommands);

        // 6. Eyes and fists.
        scout.update(sim, profile, threatMap, blackboard, outCommands);
        army.update(sim, playerId, profile, threatMap, blackboard, outCommands);
    }
}
