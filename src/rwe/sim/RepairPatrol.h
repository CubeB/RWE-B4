#pragma once

#include <optional>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitInfo.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * What a constructor on patrol does with its spare moments: the original's
     * RepairPatrol (0x405980 on the ground, VTOL_RepairPatrol 0x4152F0 in the
     * air), TOTALA-EXE-ECONOMY.md S:131. These are its two scans and nothing
     * else; the orders they lead to are planted by the caller.
     */

    /** Seconds-to-ticks cadence of the decision: 60 ticks on the ground, 45 in the air. */
    unsigned int repairPatrolPeriod(bool flies);

    /**
     * A whole number in [0, n) drawn the way the original's rand helper does:
     * nothing is drawn, and the generator is not advanced, when n is below two
     * (0x4B6C30). Every peer has to draw the same number of times.
     */
    unsigned int drawIndexBelow(GameSimulation& sim, unsigned int n);

    /**
     * The repair stage's pick (the query 0x47E890 and its callbacks 0x4158D0
     * and 0x405D90). Nothing at all unless the player's energy is at least a
     * fifth of its cap. Candidates are own or allied, standing on the ground,
     * short of health or still a frame, not already being reclaimed by this
     * player, and within the unit's SightDistance on the flat; one is chosen
     * with a single draw.
     */
    std::optional<UnitId> pickRepairPatrolRepairTarget(GameSimulation& sim, ConstUnitInfo unitInfo);

    /**
     * The reclaim stage (the scan 0x47EA40 and the decision ladder). `side` is
     * a side length, not a radius: samples are taken every 48 units over a
     * square of that side centred on the unit, one map square each, and the
     * feature found is kept in a metal list and an energy list; the best of
     * three draws is taken from each, and the ladder picks between them.
     */
    std::optional<FeatureId> pickRepairPatrolReclaimTarget(GameSimulation& sim, ConstUnitInfo unitInfo, SimScalar side);
}
