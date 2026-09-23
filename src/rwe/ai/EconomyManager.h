#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/sim/PlayerId.h>

namespace rwe
{
    struct GameSimulation;

    // Refreshes the blackboard with the current resource state and unit
    // counts, and nothing else.
    //
    // Sorian's EconomyManager is responsible for telling the BuildManager
    // *what* fraction of build power to spend on each category; here there
    // is no build power at all, so the responsibilities are minimal.
    class EconomyManager
    {
    public:
        EconomyManager() = default;

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
