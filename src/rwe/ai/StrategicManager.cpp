#include "StrategicManager.h"

namespace rwe
{
    void StrategicManager::update(const AiTuningProfile& profile, AiBlackboard& bb) const
    {
        (void)profile;
        // Phase 1: AI never leaves Opening. Phase 2 will introduce the
        // Opening -> Boom transition once a lab is up.
        bb.phase = GamePhase::Opening;
    }
}
