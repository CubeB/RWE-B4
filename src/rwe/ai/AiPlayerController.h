#pragma once

#include <cstdint>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/BuildManager.h>
#include <rwe/ai/EconomyManager.h>
#include <rwe/ai/StrategicManager.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    // One AiPlayerController per AI player. Owned by GameSimulation
    // (locked decision per docs §12-Q4) so that AI state participates in
    // GameHash and replays without separate plumbing.
    //
    // Update flow per sim tick:
    //   1. EconomyManager::refresh — read sim, fill blackboard.
    //   2. StrategicManager::update — pick GamePhase.
    //   3. BuildManager::update    — emit BuildOrder commands as needed.
    //   4. (future) ArmyManager / TacticalLayer — emit attack orders.
    //
    // The controller never mutates `sim` directly. Everything it wants
    // done is expressed as PlayerCommands appended to `pendingCommands`,
    // which GameScene drains once per scene tick into the same
    // PlayerCommandService channel humans use.
    //
    // Determinism:
    //   - All RNG goes through the per-controller `rng`, sub-seeded once
    //     from `simulation.rng` at construction (docs §12-Q6).
    //   - No std::chrono, no std::random_device, no float math anywhere
    //     in the call chain.
    //   - Blackboard maps use std::map for stable iteration order.
    class AiPlayerController
    {
    public:
        AiPlayerController(
            PlayerId playerId,
            AiTuningProfile profile,
            std::uint64_t rngSeed);

        // Run one sim-tick of AI work. Reads `sim`, appends commands to
        // `outCommands`. Idempotent: calling tick() with no changed inputs
        // produces no new commands beyond what the rate-limit allows.
        void tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands);

        PlayerId getPlayerId() const
        {
            return playerId;
        }

        const AiTuningProfile& getProfile() const
        {
            return profile;
        }

        const AiBlackboard& getBlackboard() const
        {
            return blackboard;
        }

        // Test/debug accessor for the AI's RNG state. Not used by sim code.
        const std::minstd_rand& getRng() const
        {
            return rng;
        }

    private:
        PlayerId playerId;
        AiTuningProfile profile;

        // Per-AI sub-RNG. Seeded once at construction from a value pulled
        // from simulation.rng so the AI's choices ride along with the seeded
        // sim and survive replay/dump round-trips.
        std::minstd_rand rng;

        AiBlackboard blackboard;

        EconomyManager economy;
        StrategicManager strategic;
        BuildManager build;
    };
}
