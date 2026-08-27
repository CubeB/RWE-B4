#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/sim/PlayerId.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * Keeps the blackboard's memory of enemy units in step with what the AI
     * is allowed to know: everything, for an omniscient profile; otherwise
     * only units in line of sight or on radar, remembered where they were
     * last seen until we look there again and find them gone.
     */
    class PerceptionManager
    {
    public:
        void refresh(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, AiBlackboard& bb) const;
    };
}
