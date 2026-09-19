#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/ai/ThreatMap.h>
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

        /**
         * threatMap is here because a builder guard triggered on raw distance
         * measurably costs games -- 69 of 81 guards ended with the builder
         * simply finished and only 6 with it lost, so units were taken off the
         * army to escort jobs nothing was threatening. Danger is what the
         * trigger should read, and this was the one manager never given an
         * influence map to read it from.
         */
        void update(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const ThreatMap& threatMap,
            AiBlackboard& bb,
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
         * What a combat unit is worth for what it costs: hit points times
         * damage a second, over its metal price. Zero for anything unarmed
         * or free.
         *
         * Both halves matter and neither alone will do. Judged on hit points
         * a Zeus is worse than a Peewee; judged on damage it is better; the
         * product is what says whether a fight between equal metal goes one
         * way or the other, because a unit that lives twice as long also
         * fires twice as many times.
         *
         * Deliberately crude. It ignores range, speed, and what the damage
         * lands on, so it cannot tell a siege gun from a brawler. It is
         * asked only one question -- is this tier worth its factory -- and
         * for that the answer is not close enough for the details to matter:
         * see the ratios in §15.7.
         */
        static float unitCombatValuePerMetal(const GameSimulation& sim, const std::string& unitType);

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
         * Whether a tower's cost is proportionate to what it protects,
         * judged in seconds of the base's current metal income --
         * profile.defenceValueMaxPaybackSeconds, extended by
         * profile.outpostDefenceValueSecondsPerExtractor for every
         * extractor `extractorsCovered` says the site would additionally
         * cover. This is a separate question from canAfford: that asks
         * whether the stockpile lasts the build, this asks whether the
         * build is worth having at all.
         *
         * A reading of zero income is treated as unmeasured rather than as
         * "no income" -- most often the very first tick, or a fixture that
         * never modelled any -- so the gate does not fire before there is
         * anything to judge a tower against. defenceValueMaxPaybackSeconds
         * of zero switches the whole test off and always answers yes: the
         * count thresholds and metalShort are all that gate a tower then,
         * which is every behaviour before this existed.
         */
        static bool towerCostJustified(const AiTuningProfile& profile, const AiBlackboard& bb, const UnitDefinition& towerDef, int extractorsCovered = 0);

        /**
         * Whether a build site wanting a guard should take the one guard slot
         * off whatever is already holding it.
         *
         * There is a single `buildSiteGuardRequest`, and it used to go to
         * whichever qualifying build order came last. Measured over ten games
         * at hard difficulty, 346 requests produced 55 guards -- most were
         * overwritten before anybody stood anywhere -- and in the same run the
         * four builders that actually died were at sites the influence map
         * read as 14988, 13560, 0 and 0. Recency was discarding precisely the
         * requests worth keeping, so the slot goes to the most threatened site
         * instead.
         *
         * The comparison is >= rather than > on purpose. With
         * buildSiteGuardThreat switched off every site reads zero, every
         * request ties, and recency decides exactly as it did before, so the
         * kill switch still restores the old behaviour whole. With > the slot
         * would freeze on the first request until it timed out, which is a
         * behaviour change nobody asked for.
         */
        static bool guardRequestDisplaces(const std::optional<AiBlackboard::BuildSiteGuardRequest>& held, float siteThreat);

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

        /**
         * `accept`, when set, is asked about every candidate before the ring
         * walk counts it -- which is the whole point of passing it down
         * rather than filtering the result. nearestRingOnly stops at the
         * first ring that has room, so a filter applied afterwards would
         * empty that ring and give up, where this one walks outward until it
         * finds a ring with somewhere the builder can actually get to.
         */
        std::optional<SimVector> chooseBuildSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng,
            const std::function<bool(const SimVector&)>& accept = {}) const;

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
            const AiBlackboard& bb,
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
         * A shipyard site, from MapIntel::shipyardSites -- nearest the base
         * of the ones still valid, rather than a ring search around the
         * anchor: chooseBuildSite's ring walk asks canBeBuiltAt about
         * ordinary dry ground, and an 8x8 footprint needing
         * MinWaterDepth=30 would refuse every candidate it ever offered.
         * MapIntel's list already answers the depth question; this only
         * re-checks canBeBuiltAt (occupancy, the live yard map) and the
         * site's own failedSites memory, exactly as every other site choice
         * does.
         *
         * Doesn't itself verify a builder can walk close enough to work the
         * site -- that is what createNewUnit's own "Target area was
         * blocked" refusal is for, which drops the order and lands the site
         * in failedSites the same way an unreachable metal patch does. A
         * site nobody can reach is tried once and then left alone for
         * failedSiteMemorySeconds, not filtered out ahead of time.
         */
        std::optional<SimVector> chooseShipyardSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const std::string& unitType,
            std::minstd_rand& rng) const;

        /**
         * The richest buildable metal patch on the nearest ring around the
         * anchor that has one, walking on inward while each further ring does
         * strictly better, so a deposit is taken at its heart rather than its
         * near edge. Within the radius. The optional predicate can rule sites
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
         * Which of the available builders is planned for this pass. In id
         * order the commander is first and was always the one served; with
         * builders assisting a factory back in the pool the same low id would
         * win every pass and the ones behind it would never be planned for at
         * all. This is why the advanced constructor gets a turn.
         */
        std::size_t plannerCursor{0};

        /** The last build order handed to each builder, by raw unit id, so a dropped one can be noticed. */
        struct IssuedOrder
        {
            std::string unitType;
            SimVector site;
            GameTime at{0};
        };
        std::map<unsigned int, IssuedOrder> issuedOrders;

        /**
         * The one extractor being replaced by a moho, if any.
         *
         * The original refuses a building placed over a standing unit of any
         * kind (TOTALA-EXE.md, the footprint test at 0x47D547), so an upgrade
         * is two jobs: reclaim the old extractor, then build on the patch it
         * leaves. Between the two the patch earns nothing, which is why there
         * is only ever one of these: a base that reclaimed its extractors
         * together would have no income to build their replacements with.
         *
         * While it stands, the patch is kept for the moho -- no builder is
         * offered it for an ordinary extractor -- and it is given up after
         * extractorUpgradeTimeoutSeconds, or if the builder dies, so a job
         * that went wrong costs one patch for a while and not for good.
         */
        struct ExtractorUpgrade
        {
            UnitId builder;
            UnitId oldExtractor;
            SimVector site;
            GameTime at{0};
        };
        std::optional<ExtractorUpgrade> extractorUpgrade;

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
         * Whether an armed enemy is sitting close enough to this site to
         * shoot whatever we put there.
         *
         * The same question the extractor search has always asked about a
         * metal patch (mexAvoidsEnemyGunsRadius, and the measurement in its
         * own comment: a commander ordered one site 232 times in five
         * hundred seconds, each frame living a second or two), asked of
         * every site instead of only that one. A frame is born with no hit
         * points whatever it is going to become, so nothing about that
         * pathology was ever specific to extractors -- and on an all-water
         * map what the commander actually feeds to the gun is tidal
         * generators, which the extractor rule never covered. Gated by
         * noticeProductionHarassment and measured by productionHarassRadius.
         */
        bool siteUnderEnemyGuns(const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& site) const;

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

        /**
         * How many of those patches have water over them, counted on the same
         * pass. Zero is the ordinary answer and the reason this is kept: a
         * Total Annihilation map puts its metal on land, so a map can be 98%
         * water and still have every one of its 315 patches dry. Without this
         * the underwater extractor is wanted on every water map and its site
         * search fails every time -- measured on Hundred Isles, 4473 mentions
         * and about 3500 failed searches in a single game.
         */
        mutable int submergedMetalPatches{0};

        /**
         * Where the map's geothermal vents are, found once. A vent is a
         * feature and an indestructible one, so the list never changes;
         * whether one is free is asked of the simulation each time.
         */
        mutable std::vector<SimVector> geothermalVents;
        mutable bool geothermalVentsIndexed{false};

        /** When the factories were first held for the tier-two economy; see tierTwoEconomyReserve. */
        mutable std::optional<GameTime> tierTwoReserveStarted;
        void indexGeothermalVents(const GameSimulation& sim) const;

        void indexMetalPatches(const GameSimulation& sim) const;

        /** What the next idle builder should build, most wanted first. */
        /**
         * What the base wants built, most wanted first, filtered to what
         * `builderType` actually has a button for. Every rule reads as a need
         * of the base; the filter is what turns that into this builder's job.
         */
        std::vector<std::string> buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase, const std::optional<OutpostDefencePlan>& outpost, const std::string& builderType, bool enemyNavalSeen) const;

        void planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const;
    };
}
