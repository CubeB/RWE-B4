#pragma once

#include <array>
#include <functional>
#include <optional>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/game/PlayerCommand.h>
#include <map>
#include <rwe/grid/Point.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;
    struct UnitDefinition;

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

        /** What is still to be paid for a unit, and how long that takes a given builder. */
        struct BuildEstimate
        {
            /** Metal still to be paid. */
            float metal{0.0f};
            /** Build time at the builder's own rate, unstalled, in seconds. */
            float seconds{0.0f};
        };

        /** `alreadyBuilt` is the frame's buildTimeCompleted; zero for something not yet started. */
        static BuildEstimate estimateBuild(const UnitDefinition& target, const UnitDefinition& builder, unsigned int alreadyBuilt = 0);

        /**
         * Whether the stockpile lasts the build, at income less what our
         * builders are already committed to, given `extraSeconds` of saving
         * beforehand. A stockpile near the cap is always affordable: income
         * over the cap is thrown away, and the one thing worse than a stall
         * is metal nobody spends.
         *
         * Conservative on purpose. The commitment counts builds that will
         * finish before this one does, and income leaves out extractors
         * still going up; both errors make it say no when the answer was
         * yes, never the other way round.
         */
        static bool canAfford(const AiBlackboard& bb, const BuildEstimate& estimate, int extraSeconds = 0);

        /**
         * An extractor cluster of ours that nothing defends, and where a
         * tower should go to cover it. See planOutpostDefence.
         */
        struct OutpostDefencePlan
        {
            /** Middle of the cluster; the tower is sited around it. */
            SimVector anchor;
            /** Uncovered extractors the tower would reach from there. */
            int extractors{0};
            /** An extractor was lost there lately, which outranks the cluster minimum. */
            bool raided{false};
        };

        /**
         * Where an outpost tower is wanted, if anywhere: the cluster of our
         * extractors, beyond defendRadius of the base and outside the reach
         * of every tower we have or are putting up, that a tower would cover
         * most of -- or the place one of them was just destroyed.
         *
         * Nothing here draws on the RNG and every walk is in id order, so it
         * is the same on every peer. Empty when the profile's outpost tower
         * count is already standing.
         */
        std::optional<OutpostDefencePlan> planOutpostDefence(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb) const;

        std::optional<SimVector> chooseBuildSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng) const;

        /**
         * A site's worth, compared lexicographically: the first element
         * decides, the second breaks its ties, the third the second's.
         */
        using SiteScore = std::array<float, 3>;

        /**
         * Every buildable site on the rings around the anchor, out to the
         * base radius, scored by the caller; the best wins and the RNG
         * breaks exact ties. chooseBuildSite is the special case that stops
         * at the nearest ring with room, which is right for a solar
         * collector and wrong for a tower, whose worth is where it stands.
         */
        std::optional<SimVector> chooseScoredBuildSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng,
            const std::function<SiteScore(const SimVector&, int ring)>& score,
            const std::function<bool(const SimVector&)>& accept = nullptr) const;

        /**
         * Where the next tower of a type goes, given the ones already
         * standing, going up, or on a builder's way. Anti-air is placed for
         * coverage: the site whose range takes in the most of our buildings
         * that no tower of the type already covers, then as far from the
         * others as its range, then nearest the base. A laser tower is
         * placed for the approaches: as far from the others as its range,
         * then on the side that faces the enemy, then nearest the base --
         * so successive towers form a line across the front rather than a
         * knot at one post. The range is the weapon's own.
         *
         * Only ground the base can walk to is considered. Scoring the whole
         * base radius rather than the nearest ring means the winner can sit
         * on a mesa the builder cannot climb; the order then dies on the
         * spot and the same site wins again next pass, and a commander
         * spent four minutes of one game doing exactly that.
         */
        std::optional<SimVector> chooseDefenceSite(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const ReachabilityMap& reachability,
            const std::string& unitType,
            std::minstd_rand& rng) const;

        /**
         * A radar goes on the edge of the base facing the enemy -- the
         * middle sees what the buildings already see -- and on the highest
         * ground the nearest free ring there offers.
         */
        std::optional<SimVector> chooseRadarSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const ReachabilityMap& reachability,
            const std::string& unitType,
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

        /** The last build order handed to each builder, by raw unit id, so a dropped one can be noticed. */
        struct IssuedOrder
        {
            std::string unitType;
            SimVector site;
            GameTime at{0};
        };
        std::map<unsigned int, IssuedOrder> issuedOrders;

        /**
         * Sites whose orders were dropped, by heightmap cell, with when.
         * A builder handed a site it cannot reach, or that is occupied when
         * it arrives, drops the order and is idle again at the next pass --
         * and the site is still the best one by every test the planner
         * applies, so it is handed out again. Measured on Crystal Maze, a
         * commander was given the same extractor site fifty-four times and
         * built nothing for twenty minutes on a full store. Kept for
         * failedSiteMemorySeconds. Ordered, and walked in order, so it is
         * the same on every peer.
         */
        std::map<std::pair<int, int>, GameTime> failedSites;

        /** Whether an order to this site was dropped lately. */
        bool siteFailedLately(const GameSimulation& sim, const SimVector& site) const;

        /**
         * Where extractors of ours were destroyed, by heightmap cell, with
         * when: the blackboard's loss memory copied out and kept for
         * outpostRaidMemorySeconds, because a minute is not long enough for
         * a builder to come free and answer it. Ordered, so the plan that
         * reads it is the same on every peer.
         */
        std::map<std::pair<int, int>, GameTime> raidedSites;

        /** What the builder is waiting for the metal for, so the log says so once rather than every second. */
        std::string savingFor;

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
        /**
         * What the base wants built, most wanted first, filtered to what
         * `builderType` actually has a button for. Every rule reads as a need
         * of the base; the filter is what turns that into this builder's job.
         */
        std::vector<std::string> buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase, const std::optional<OutpostDefencePlan>& outpost, const std::string& builderType) const;

        void planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const;
    };
}
