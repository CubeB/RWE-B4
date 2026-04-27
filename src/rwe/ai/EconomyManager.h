#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/sim/PlayerId.h>

namespace rwe
{
    struct GameSimulation;

    // Phase 1 EconomyManager: pure read-only helper that refreshes the
    // blackboard with current resource state and unit counts. Future phases
    // will add BP allocation, buffer targeting, and reclaim prioritisation.
    //
    // Sorian's EconomyManager is responsible for telling the BuildManager
    // *what* fraction of build power to spend on each category; in Phase 1
    // we have no build power at all, so the responsibilities are minimal.
    class EconomyManager
    {
    public:
        EconomyManager() = default;

        // Refill `bb` with everything Phase 1 cares about: resource levels,
        // owned counts, the commander pointer, the base anchor.
        // The simulation argument is const so the EconomyManager has no way
        // to accidentally mutate sim state — all mutations must flow back
        // through PlayerCommand emission.
        void refresh(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            AiBlackboard& bb) const;
    };
}
