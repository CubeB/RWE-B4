#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/sim/PlayerId.h>
#include <functional>
#include <optional>

namespace rwe
{
    struct GameSimulation;
    struct UnitState;

    /**
     * The live unit behind a remembered contact, if it is still standing.
     *
     * Thirteen sites across ArmyManager, AirManager and BuilderSafety spelled
     * this out by hand as `!ref || ref->get().isDead()`. It is one rule and it
     * belongs in one place, beside the memory it reads.
     *
     * Note what it does, because it is not what PerceptionManager::refresh
     * does. This asks the *live* simulation whether the unit is dead, which
     * is more than the AI is entitled to know: refresh deliberately keeps a
     * contact it cannot see, so that a unit blowing up out of sight does not
     * vanish off the AI's map the instant it dies. Every caller of this
     * function learns about that death immediately anyway. The behaviour is
     * unchanged from when it was written out thirteen times; stating it once
     * is what makes it one edit to change.
     */
    std::optional<std::reference_wrapper<const UnitState>>
        contactStillStanding(const GameSimulation& sim, const KnownEnemy& enemy);

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
