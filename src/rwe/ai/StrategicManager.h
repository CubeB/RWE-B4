#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>

namespace rwe
{
    // The Sorian-style StrategicManager would choose the current GamePhase,
    // pick an opening book, and emit attack triggers (per docs §2). This
    // one publishes the current phase to the blackboard so BuildManager's
    // opening conditions have somewhere to read from.
    class StrategicManager
    {
    public:
        StrategicManager() = default;

        void update(const AiTuningProfile& profile, AiBlackboard& bb) const;
    };
}
