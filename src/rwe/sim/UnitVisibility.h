#pragma once

#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;
    struct PlayerVisibility;

    /**
     * The original's unit draw/target predicate over a supplied visibility
     * picture (PLAYTEST-CONSTRUCTION-VISIBILITY-FINDINGS.md CV-02).
     * Presentation may supply its combined watching view or revealed grid;
     * simulation callers use GameSimulation::canSeeUnit and their own grid.
     */
    bool canSeeUnitInVisibility(const GameSimulation& sim, PlayerId viewer, UnitId unitId, const PlayerVisibility& visibility);
}
