#pragma once

#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct AiBlackboard;

    /**
     * The tuning knobs assessExposure and its callers read.
     *
     * Kept out of AiTuningProfile deliberately: this module only computes
     * whether a position is safe and where to retreat to, and it is for
     * whoever wires it into BuildManager/ArmyManager to decide how those
     * numbers are exposed to the rest of the AI's tuning.
     */
    struct BuilderSafetyParams
    {
        /** How long a sighting counts, in ticks (compare bb.now.value <= enemy.lastSeen.value + memoryTicks). */
        unsigned int memoryTicks{150};
        /** Added to a MOBILE enemy's weapon range: it can walk this far before we react. */
        SimScalar mobileThreatMargin{150_ss};
        /** Added to a STATIC enemy's (isBuilding) weapon range. */
        SimScalar staticThreatMargin{32_ss};
        /** Our mobile armed units count as cover within this distance of the position. */
        SimScalar coverRadius{500_ss};
        /** A mobile unit up to this far on the far side of the position (away from the threat) still counts. */
        SimScalar behindSlack{64_ss};
        /** Cover must reach threatMetal * protectionRatio for the position to be safe. */
        float protectionRatio{1.0f};
        /** What our commander counts for as cover, instead of its build cost. */
        float commanderCoverMetal{600.0f};
        /** How far beyond the longest threat's range a retreating builder goes. */
        SimScalar retreatExtraDistance{150_ss};
    };

    /**
     * What assessExposure found at one position: how much danger stands
     * near it, how much of our own strength stands between it and that
     * danger, and whether the balance says it is safe to be there.
     */
    struct BuilderExposure
    {
        float threatMetal{0.0f};
        float protectionMetal{0.0f};
        SimScalar maxThreatRange{0_ss};
        std::optional<SimVector> threatCentre;
        bool exposed{false};
    };

    /**
     * Weighs the remembered armed enemies within reach of `position` against
     * our own armed cover standing near it, and reports whether the balance
     * favours the enemy.
     *
     * `exclude`, when given, is left out of the cover count -- for asking
     * whether a builder is safe where it stands, its own (unarmed, and so
     * uncounted anyway) presence should not be double-counted as its own
     * protection.
     */
    BuilderExposure assessExposure(const GameSimulation& sim, PlayerId aiOwner, const AiBlackboard& bb, const BuilderSafetyParams& params, const SimVector& position, std::optional<UnitId> exclude = std::nullopt);

    /**
     * Where a builder standing at `from`, exposed as described by
     * `exposure`, should retreat to: away from the threat and, if the
     * remembered base anchor lies on the safe side, toward home.
     *
     * Returns `from` unchanged if the exposure carries no threat centre.
     */
    SimVector retreatPoint(const GameSimulation& sim, const AiBlackboard& bb, const BuilderSafetyParams& params, const SimVector& from, const BuilderExposure& exposure);

    /** One builder that should move to safety, and where to. */
    struct BuilderRetreat
    {
        UnitId builder;
        SimVector destination;
        BuilderExposure exposure;
    };

    /**
     * Surveys every mobile, non-commander builder of ours and reports the
     * ones that should retreat right now: exposed by assessExposure, and
     * not already under way to (near enough) the retreat point this would
     * choose.
     */
    std::vector<BuilderRetreat> planBuilderRetreats(const GameSimulation& sim, PlayerId aiOwner, const AiBlackboard& bb, const BuilderSafetyParams& params);
}
