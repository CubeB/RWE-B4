#pragma once

#include <optional>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <random>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    // Phase 1 BuildManager: hardcoded opening book.
    //
    // Behaviour:
    // 1. Determine the AI's side prefix ("ARM" or "CORE") from the
    //    commander's unit type.
    // 2. While `ownedTotalCounts[mex] < profile.openingMetalExtractorCount`,
    //    pick a metal patch reachable to the commander, find a buildable
    //    spot on/near it, and issue a BuildOrder to the commander.
    // 3. Once the mex quota is met, do the same with solar collectors near
    //    the base anchor.
    // 4. After both quotas are met, emit nothing — Phase 1 deliberately
    //    stops short of factories, units, or attack logic.
    //
    // Determinism: the BuildManager is allowed to consult `rng` for
    // tiebreaking but must not call `std::random_device` or any wall-clock.
    // All world-coordinate math goes through SimScalar.
    //
    // Future phases will replace this with a conditional BuildOrderTemplate
    // system (docs §6); the API is intentionally narrow so the swap is
    // a drop-in replacement.
    class BuildManager
    {
    public:
        BuildManager() = default;

        // Run one BuildManager update. Appends any newly-decided commands
        // to `outCommands` rather than returning them, so the controller
        // can collect commands across multiple managers in one buffer.
        //
        // `rng` must be a deterministic generator owned by the
        // AiPlayerController — never `std::random_device`.
        void update(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            std::minstd_rand& rng,
            std::vector<PlayerCommand>& outCommands);

    private:
        // Cached per-match data, computed lazily on first update().
        bool sideResolved{false};
        std::string mexUnitType;
        std::string solarUnitType;

        // Tick counter (not gameTime, so the BuildManager runs at its own
        // cadence regardless of when in the sim the controller is invoked).
        int ticksSinceLastPlanning{0};

        // Try to resolve the AI's side prefix using the commander unit type.
        // Sets `sideResolved`, `mexUnitType`, `solarUnitType` on success.
        // Falls back to ARM names if no commander can be found.
        void resolveSide(const GameSimulation& sim, const AiBlackboard& bb);

        // Pick a build position for `unitType` near `anchor`, returning
        // nullopt if no valid site can be found within
        // `profile.maxMexSearchRadius`.
        std::optional<SimVector> chooseBuildSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng) const;

        // Pick a build site for a metal extractor: looks for the nearest
        // metal-bearing cell first, then defers to chooseBuildSite for
        // placement validity.
        std::optional<SimVector> chooseMexSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng) const;
    };
}
