#pragma once

#include <map>
#include <optional>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/BuildManager.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Puts the transports to work. When the map has ground the base cannot
     * walk to, a constructor is flown out to take the metal there, and when
     * the enemy sits across the water the army is ferried over to it.
     */
    class TransportManager
    {
    public:
        void update(
            const GameSimulation& sim,
            PlayerId aiOwner,
            const AiTuningProfile& profile,
            const ReachabilityMap& reachability,
            const ThreatMap& threatMap,
            const BuildManager& build,
            AiBlackboard& bb,
            std::minstd_rand& rng,
            std::vector<PlayerCommand>& outCommands);

        /**
         * Why a landing search came back with nothing, counted per gate.
         *
         * "No landing near the attack target" was the loudest counter in an
         * arena game -- between 0 and 1365 ticks of it in ten seeds on Coast
         * To Coast -- and said nothing at all about which test was doing the
         * refusing, so the first fix aimed at it (widening the band from 576
         * to 1152 units) turned out to change the count in one seed of ten.
         * A tally is cheap and settles that in one run.
         */
        struct LandingSearchTally
        {
            /** Steps taken before the ray left the map. */
            int stepsTried{0};
            /** Candidates that were under water. */
            int wet{0};
            /** Dry candidates the caller's own test refused. */
            int refused{0};
        };

        /** A trip in progress: who is aboard (or being fetched) and where they are going. */
        struct Ferry
        {
            std::vector<UnitId> passengers;
            SimVector destination;
            GameTime startedAt;
            bool loaded{false};
            /**
             * For a sea ferry that musters: the shore point the passengers
             * walk to, and the water beside it the hull loads from. Unset for
             * an air ferry and for one sent with seaFerryMuster off.
             */
            std::optional<SimVector> muster;
            std::optional<SimVector> station;
        };

        /** A shore point to gather a sea lift at, and the water beside it. */
        struct Muster
        {
            SimVector shore;
            SimVector water;
        };

        const std::map<unsigned int, Ferry>& getFerries() const { return ferries; }

    private:
        int ticksSinceLastUpdate{0};

        /** Trips under way, keyed by the transport's raw id. */
        std::map<unsigned int, Ferry> ferries;

        /** A metal patch across the water worth sending a builder to, refreshed now and then. */
        std::optional<SimVector> expansionSite;
        GameTime expansionSiteCheckedAt{0};
        /** Whether we have looked at all yet; coming up empty still counts as a look. */
        bool expansionSiteSearched{false};

        /**
         * The last answer "is the enemy somewhere we cannot walk to" had
         * evidence for -- that is, the last pass on which there was an attack
         * target to ask it about. Unset until there has been one.
         *
         * This exists because the question is asked of two decisions with
         * very different horizons. Sending a ferry wants the answer for the
         * target it is being sent to, now. BUILDING a ferry wants to know
         * whether one will be needed at all, and a 919-metal hull takes about
         * 223 seconds to make -- measured -- while the target it would be
         * asked about is rebuilt from scratch every tactical pass and is unset
         * outside the Attack phase and whenever nothing is currently
         * remembered. So the build decision reads the latch and the dispatch
         * decision reads the live answer. See
         * AiTuningProfile::armyFerryWantFromMap.
         */
        std::optional<bool> lastArmyFerryAnswer;

        /**
         * The muster found for the last origin it was asked about. The search
         * walks rings out from the shipyard and probes the water round each
         * dry candidate, which is too much to repeat every pass for an answer
         * that only changes when the yard does; the same reason
         * navalRallyMemo exists.
         */
        struct MusterMemo
        {
            SimVector origin;
            std::optional<Muster> muster;
        };
        std::optional<MusterMemo> musterMemo;

        /**
         * Where to gather a sea lift: the nearest dry cell to our shipyard (or
         * the ground anchor, lacking one) that is on home ground and has water
         * our navy can reach within a few steps of it. Nothing without naval
         * labelling, or when no such cell lies within reach.
         */
        std::optional<Muster> seaMuster(const GameSimulation& sim, const ReachabilityMap& reachability, const AiBlackboard& bb);

        void tendFerries(const GameSimulation& sim, const AiTuningProfile& profile, AiBlackboard& bb, std::vector<PlayerCommand>& outCommands);

        void refreshExpansionSite(const GameSimulation& sim, const ReachabilityMap& reachability, const BuildManager& build, const AiBlackboard& bb, std::minstd_rand& rng);

        std::optional<SimVector> landingNear(const GameSimulation& sim, const ReachabilityMap& reachability, const AiTuningProfile& profile, const ThreatMap& threatMap, const SimVector& target, const SimVector& from, LandingSearchTally& tally) const;

        /**
         * The hull counterpart of landingNear(): a ship cannot come ashore
         * anywhere along the way, so the drop point it returns must also
         * have water alongside it that the naval layer marks reachable from
         * our own base -- not merely dry and walkable for the cargo, which
         * is all landingNear asks. See issue #26.
         */
        std::optional<SimVector> navalLandingNear(const GameSimulation& sim, const ReachabilityMap& reachability, const AiTuningProfile& profile, const ThreatMap& threatMap, const SimVector& target, const SimVector& from, LandingSearchTally& tally) const;

        void bookPassengers(AiBlackboard& bb, const Ferry& ferry);
    };
}
