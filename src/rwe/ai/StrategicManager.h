#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>

namespace rwe
{
    // Phase 1 StrategicManager: phase tracker only.
    //
    // The full Sorian-style StrategicManager (per docs §2) chooses the
    // current GamePhase, picks an opening book, and emits attack triggers.
    // Phase 1 only needs to publish the current phase to the blackboard so
    // BuildManager opening conditions have somewhere to read from.
    class StrategicManager
    {
    public:
        StrategicManager() = default;

        // Decide the current GamePhase given the blackboard state.
        // In Phase 1 this is always Opening; the function exists so callers
        // do not have to change when phase transitions land.
        void update(const AiTuningProfile& profile, AiBlackboard& bb) const;
    };
}
