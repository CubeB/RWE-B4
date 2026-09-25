#pragma once

#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/sim/PlayerId.h>
#include <functional>
#include <optional>

namespace rwe
{
    struct GameSimulation;
    class UnitState;

    /**
     * A remembered contact the AI is entitled to act on.
     *
     * `unit` is the live unit when the AI can see it now. It is null when the
     * contact has gone quiet -- last seen somewhere we are not watching -- and
     * the caller then has only the remembered fields of the KnownEnemy it
     * asked about: where it was and what it was when seen. Reading live state
     * in that case would be knowledge the AI does not have.
     */
    struct StandingContact
    {
        const UnitState* unit{nullptr};
    };

    /**
     * Whether the AI may still act on a remembered contact, and the live unit
     * if it can see it.
     *
     * A contact is gone only once we have looked where we last saw it and it
     * was not there -- the same rule PerceptionManager::refresh uses to forget
     * one -- so a unit that dies out of sight stays on the AI's map until
     * something of ours goes and looks. An omniscient profile sees everything
     * and drops a dead contact at once.
     */
    std::optional<StandingContact>
        contactStillStanding(const GameSimulation& sim, PlayerId aiOwner, bool omniscient, const KnownEnemy& enemy);

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
