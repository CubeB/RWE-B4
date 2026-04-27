#pragma once

#include <map>
#include <optional>
#include <rwe/game/SceneTime.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <string>

namespace rwe
{
    // Coarse phase the StrategicManager believes the match is in.
    // Phase 1 only uses Opening; later phases will drive transitions
    // through this enum.
    enum class GamePhase
    {
        Opening,
        Boom,
        Attack,
        Defend,
        Tech,
        Endgame,
    };

    // Read-mostly snapshot of "what the AI knows about itself" at the top of
    // each controller tick. Managers read from here rather than poking each
    // other's internals — this keeps the Sorian-style layering loose enough
    // that the BuildManager can be swapped without touching the EconomyManager.
    //
    // POD-ish struct; the only nontrivial member is the count map, which we
    // intentionally use std::map (not unordered_map) for deterministic
    // iteration order — see the project's determinism notes in
    // docs/ai-architecture-proposal.md §4.
    struct AiBlackboard
    {
        GamePhase phase{GamePhase::Opening};

        Metal currentMetal{0};
        Energy currentEnergy{0};
        Metal metalStorage{0};
        Energy energyStorage{0};

        // Set whenever the per-player simulation flags `metalStalled` /
        // `energyStalled` go true. Cheaper than recomputing income.
        bool metalStalled{false};
        bool energyStalled{false};

        // Counts of each completed unitType currently owned by the AI.
        // Keyed in upper-case to match `simulation.unitDefinitions`.
        std::map<std::string, int> ownedCompletedCounts;

        // Counts of *any* unit (in-progress or completed). Used to gate "have
        // I already started building this?" decisions so the BuildManager
        // doesn't double-issue while a builder is en route.
        std::map<std::string, int> ownedTotalCounts;

        // Number of friendly builder units that are currently idle (no orders
        // queued). Phase 1 builders never get a second job — they idle —
        // but the count is still useful for the opening logic.
        int idleBuilderCount{0};

        // Convenience handle to "the commander", since most Phase 1 reasoning
        // is rooted at the commander's location. Empty if no commander unit
        // has been spotted (commander dead, unusual map, etc.).
        std::optional<UnitId> commanderUnitId;
        std::optional<SimVector> commanderPosition;

        // Anchor point used as the centre of the AI's base. In Phase 1 this
        // is just the commander's position, but the indirection lets later
        // phases pick a different anchor (e.g. main lab) once one exists.
        std::optional<SimVector> baseAnchor;
    };
}
