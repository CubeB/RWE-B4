#pragma once

#include <map>
#include <optional>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/BuildManager.h>
#include <rwe/ai/ReachabilityMap.h>
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
            const BuildManager& build,
            AiBlackboard& bb,
            std::minstd_rand& rng,
            std::vector<PlayerCommand>& outCommands);

        /** A trip in progress: who is aboard (or being fetched) and where they are going. */
        struct Ferry
        {
            std::vector<UnitId> passengers;
            SimVector destination;
            GameTime startedAt;
            bool loaded{false};
        };

        const std::map<unsigned int, Ferry>& getFerries() const { return ferries; }

    private:
        int ticksSinceLastUpdate{0};

        /** Trips under way, keyed by the transport's raw id. */
        std::map<unsigned int, Ferry> ferries;

        /** A metal patch across the water worth sending a builder to, refreshed now and then. */
        std::optional<SimVector> expansionSite;
        GameTime expansionSiteCheckedAt{0};

        void tendFerries(const GameSimulation& sim, const AiTuningProfile& profile, AiBlackboard& bb, std::vector<PlayerCommand>& outCommands);

        void refreshExpansionSite(const GameSimulation& sim, const ReachabilityMap& reachability, const BuildManager& build, const AiBlackboard& bb, std::minstd_rand& rng);

        std::optional<SimVector> landingNear(const GameSimulation& sim, const ReachabilityMap& reachability, const SimVector& target, const SimVector& from) const;

        void bookPassengers(AiBlackboard& bb, const Ferry& ferry);
    };
}
