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
#include <rwe/sim/FeatureId.h>
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
         * How many metal deposits are free for the taking on our side: no
         * extractor of ours or of theirs that we know of on them, nearer our
         * base than theirs, within expansionMexSearchRadius, and out of reach
         * of a known gun. See AiTuningProfile::expansionConstructors.
         */
        int freeDepositsOnOurSide(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb) const;

        /** Whether a remembered enemy gun covers this ground. See AiTuningProfile::enemyGunRangeFromWeapon. */
        bool siteUnderEnemyGuns(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, const SimVector& site) const;

        /**
         * Whether the metal coming in is more than the running jobs can draw:
         * metalDemand * capacityIncomeRatio below metalIncome. update() counts
         * how long that stays true before acting on it.
         */
        static bool incomeOutrunsSpending(const AiTuningProfile& profile, const AiBlackboard& bb);

        /** The kbot lab's shares, leaned towards what answers the enemy we have seen. See AiTuningProfile::counterEnemyComposition. */
        struct LabShares
        {
            int raider;
            int rocketKbot;
            int artilleryKbot;
        };
        static LabShares counterShares(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb);

        /** The next piece of a laser tower's fortification, and where it goes. */
        struct FortificationPlan
        {
            std::string unitType;
            SimVector site;
            UnitId tower;
        };

        /**
         * What a finished laser tower of ours still lacks of its
         * fortification (AiTuningProfile::fortifyTowers): the first tooth of
         * its line not yet up, going up or ordered, from the middle out, and
         * then a missile tower behind it if none covers it. Towers are
         * walked in id order, so the oldest is finished first, and the
         * teeth sites are exact rather than searched for, because a line is
         * only a line if its teeth touch. A tooth site the ground will not
         * take is passed over rather than moved.
         *
         * Draws on the RNG only for the missile tower's site, and only when
         * the knob is on, so with it off nothing here changes a game.
         */
        std::optional<FortificationPlan> planFortification(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            std::minstd_rand& rng) const;

        /**
         * Looks at our defences' hit points, at most every
         * DefenceWatchIntervalTicks, and keeps each one's recent attacks and
         * the side they came from (AiTuningProfile::fortifyWhereAttacked).
         * Cheap by construction: a pass reads the hit points of the armed
         * buildings and nothing else, and the enemies are walked only for a
         * defence that has just been hit. Every walk is in id order.
         */
        void watchDefences(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb);

        /** A defence of ours that was destroyed, and where it stood (AiTuningProfile::rebuildLostDefences). */
        struct LostDefenceSite
        {
            std::string unitType;
            SimVector position;
            GameTime lostAt;
            int timesLost{0};
        };

        /**
         * Reads the defences lost since the last look out of bb.recentLosses
         * into lostDefenceSites, one entry per place, counting how often each
         * has been lost, and forgets those older than lostDefenceMemorySeconds
         * or lost more than maxDefenceRebuilds times.
         */
        void recordLostDefences(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb);

        const std::vector<LostDefenceSite>& getLostDefenceSites() const { return lostDefenceSites; }

        struct DefenceRebuildPlan
        {
            std::string unitType;
            SimVector site;
            /** Its wreck, where that is still lying on the spot: reclaimed first. */
            std::optional<FeatureId> wreck;
        };

        /**
         * The first lost defence due to be put back: past rebuildDelaySeconds,
         * nothing of ours there or ordered there, no armed enemy near, and
         * ground that takes it once its wreck -- if any -- is gone.
         */
        std::optional<DefenceRebuildPlan> planDefenceRebuild(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb) const;

        /**
         * Where the next of an energy building goes when it is laid out in
         * rows (AiTuningProfile::energyInRows): beside one of its kind already
         * standing, near the base, behind it, near the builder.
         */
        std::optional<SimVector> chooseEnergyRowSite(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const std::string& unitType,
            const SimVector& anchor,
            const SimVector& builderPosition,
            std::minstd_rand& rng,
            const std::function<bool(const SimVector&)>& accept) const;

        /**
         * The damaged structure this builder should mend, if any: a defence
         * before a factory, the nearest of the more important kind, within
         * repairSearchRadius of `from`, accepted by `reachable` when that is
         * set, and not already held by repairersPerStructure of ours.
         */
        /**
         * Sends every construction unit caught in a fight it is not covered
         * in back out of it. See AiTuningProfile::builderSafety.
         */
        void keepBuildersOutOfFights(const GameSimulation& sim, PlayerId aiOwner, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands);

        std::optional<UnitId> chooseRepairTarget(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            const SimVector& from,
            const std::function<bool(const SimVector&)>& reachable) const;

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
         * The best placement on the nearest metal deposit around the anchor,
         * within the radius. A deposit is a run of patch cells touching at
         * edges or corners, and it is taken at its heart -- the placement
         * with the most metal under it, nearest first among equals -- or not
         * at all.
         *
         * The two predicates are asked different questions. `admit` is about
         * ground: may this deposit be taken at all (the commander's leash,
         * our side of the map, somewhere the builder can walk)? A deposit is
         * admitted if any of its cells passes, so a rule whose boundary runs
         * through a deposit no longer pushes the extractor onto whichever
         * edge is inside it. `accept` is about one site: is the placement
         * the deposit would be given free to use (not dropped lately, not
         * kept for a moho)? A deposit whose best placement is refused is
         * passed over, rather than settled with a lesser placement beside it.
         */
        /**
         * Whether one of our other builders has already been told to build
         * within `radius` of this site. A build order is invisible on the
         * map until the frame goes down, so without asking this two builders
         * plan the same metal patch in the same pass and one of them makes
         * the walk for nothing. Orders being walked to count, and so does a
         * frame one of them has been sent back to finish.
         */
        bool siteClaimedByAnother(
            const GameSimulation& sim,
            PlayerId aiOwner,
            UnitId builder,
            const SimVector& site,
            SimScalar radius) const;

        std::optional<SimVector> chooseMexSite(
            const GameSimulation& sim,
            const std::string& unitType,
            const SimVector& anchor,
            SimScalar radius,
            std::minstd_rand& rng,
            const std::function<bool(const SimVector&)>& accept = nullptr,
            const std::function<bool(const SimVector&)>& admit = nullptr) const;
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

        /** One attack on a defence of ours: hits no more than fortifyAttackGapSeconds apart. */
        struct AttackEpisode
        {
            /** Unit vectors towards the attackers seen at each hit, summed. */
            SimVector direction;
            GameTime lastHitAt;
        };

        /** What watchDefences knows about one defence. */
        struct DefenceWatch
        {
            SimVector position;
            unsigned int hitPoints{0};
            GameTime seenAt{0};
            /** Oldest first, and never more than a handful. */
            std::vector<AttackEpisode> episodes;
            /** Set while the recent attacks agree on a side: the way the teeth should face. */
            std::optional<SimVector> teethToward;
        };

        static constexpr unsigned int DefenceWatchIntervalTicks = 15;
        static constexpr std::size_t MaxAttackEpisodes = 6;

        /** Keyed by raw unit id, so it is walked in id order. */
        std::map<unsigned int, DefenceWatch> defenceWatch;
        std::optional<GameTime> defenceWatchedAt;
        /** Whether any defence's attacks agree on a side, so planFortification can say no without walking anything. */
        bool defenceTeethOwed{false};

        /** When sendRepairersToCommander last looked. */
        std::optional<GameTime> commanderRepairCheckedAt;
        /**
         * Builders that backed off from a fight, and until when they are
         * given no new job; and when that was last looked at. See
         * AiTuningProfile::builderSafety.
         */
        std::map<unsigned int, GameTime> builderShelteredUntil;
        std::optional<GameTime> builderSafetyCheckedAt;
        /** Whether the commander's being too exposed to mend has been logged this spell. */
        bool commanderExposedLogged{false};

        /** See recordLostDefences. In the order first lost. */
        std::vector<LostDefenceSite> lostDefenceSites;
        /** The newest loss already read, so each is counted once. */
        std::optional<GameTime> lostDefencesReadUpTo;
        /**
         * Whether the commander's current spell below the repair line has
         * been logged. Written to the log and read by nothing else, so it
         * cannot change an outcome.
         */
        bool commanderHurtLogged{false};

        /**
         * Takes the nearest construction unit, busy or not, off its job to
         * mend a damaged commander, until commanderRepairers of them are on
         * it (AiTuningProfile::repairCommander).
         */
        void sendRepairersToCommander(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const AiBlackboard& bb,
            std::vector<PlayerCommand>& outCommands);

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
         * How long the AI has been unable to spend what it earns, in ticks,
         * and whether that has gone on long enough to buy more capacity with.
         * See AiTuningProfile::spendSurplusOnCapacity.
         */
        unsigned int capacityShortTicks{0};
        bool spendingCapacityShort{false};

        /** The middle of each deposit, numbered as metalPatchDeposit numbers them. */
        mutable std::vector<SimVector> depositCentres;

        /** Which deposit each of metalPatches belongs to, numbered from 0; the same length as metalPatches. */
        mutable std::vector<int> metalPatchDeposit;
        mutable int metalDepositCount{0};
        /**
         * Deposits whose heart was last found with a unit standing on it, and
         * since when; see chooseMexSite. Mutable for the same reason as the
         * patch index: the search is const.
         */
        mutable std::map<int, GameTime> depositHeartBlockedSince;

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
        std::vector<std::string> buildPriorities(const AiTuningProfile& profile, const AiBlackboard& bb, bool builderAtBase, const std::optional<OutpostDefencePlan>& outpost, const std::optional<FortificationPlan>& fortify, const std::optional<DefenceRebuildPlan>& rebuild, const std::string& builderType, bool enemyNavalSeen) const;

        void planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const;
    };
}
