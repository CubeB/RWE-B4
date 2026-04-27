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
        // 1. Refresh blackboard from current sim truth.
        economy.refresh(sim, playerId, profile, blackboard);

        // 2. Strategic phase decision (Phase 1: always Opening).
        strategic.update(profile, blackboard);

        // 3. BuildManager: opening book — mexes, then solar.
        build.update(sim, playerId, profile, blackboard, rng, outCommands);

        // 4..N: ArmyManager, TacticalLayer, ScoutManager — Phase 3+ deferred.
    }
}
