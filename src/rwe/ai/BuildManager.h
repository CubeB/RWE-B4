#pragma once

#include <functional>
#include <optional>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Turns the blackboard's picture of the economy into build orders for
     * idle builders and production for factories.
     */
    class BuildManager
    {
    public:
        BuildManager() = default;

        void update(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const ReachabilityMap& reachability,
            std::minstd_rand& rng,
            std::vector<PlayerCommand>& outCommands);

        std::optional<SimVector> chooseBuildSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng) const;

        /**
         * The richest buildable metal patch on the nearest ring around the
         * anchor, within the radius. The optional predicate can rule sites
         * out (for example, only ground the base cannot walk to).
         */
        std::optional<SimVector> chooseMexSite(
            const GameSimulation& sim,
            const std::string& unitType,
            const SimVector& anchor,
            SimScalar radius,
            std::minstd_rand& rng,
            const std::function<bool(const SimVector&)>& accept = nullptr) const;

    private:
        int ticksSinceLastPlanning{0};

        /**
         * Every heightmap cell that sits on a metal patch, in scan order.
         *
         * The metal grid is only ever written when the map's permanent
         * features are placed as the game loads, so this is worked out once
         * and reused: looking for a patch then means walking a few hundred
         * known cells rather than every cell on the map.
         *
         * Mutable because the site searches are const and this is a cache of
         * something that cannot change.
         */
        mutable std::vector<Point> metalPatches;
        mutable bool metalPatchesIndexed{false};

        void indexMetalPatches(const GameSimulation& sim) const;

        /** What the next idle builder should build, most wanted first. */
        std::vector<std::string> buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase) const;

        void planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const;
    };
}
