#include "AiPlayerController.h"
#include <rwe/sim/GameSimulation.h>

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
        strategic.update(profile, blackboard);

        // 5. Economy and production.
        build.update(sim, playerId, profile, blackboard, rng, outCommands);

        // 6. Eyes and fists.
        scout.update(sim, profile, threatMap, blackboard, outCommands);
        army.update(sim, playerId, profile, threatMap, blackboard, outCommands);
    }
}
