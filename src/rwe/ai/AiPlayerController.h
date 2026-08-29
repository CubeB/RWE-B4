#pragma once

#include <cstdint>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/ArmyManager.h>
#include <rwe/ai/BuildManager.h>
#include <rwe/ai/EconomyManager.h>
#include <rwe/ai/PerceptionManager.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/ai/ScoutManager.h>
#include <rwe/ai/StrategicManager.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/ai/TransportManager.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * One computer player. Runs inside the deterministic simulation and only
     * ever talks to it through the same PlayerCommand stream a human uses.
     */
    class AiPlayerController
    {
    public:
        AiPlayerController(
            PlayerId playerId,
            AiTuningProfile profile,
            std::uint64_t rngSeed);

        void tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands);

        PlayerId getPlayerId() const { return playerId; }
        const AiTuningProfile& getProfile() const { return profile; }
        const AiBlackboard& getBlackboard() const { return blackboard; }
        const ThreatMap& getThreatMap() const { return threatMap; }
        const ReachabilityMap& getReachabilityMap() const { return reachability; }
        const TransportManager& getTransportManager() const { return transport; }
        const std::minstd_rand& getRng() const { return rng; }

    private:
        PlayerId playerId;
        AiTuningProfile profile;
        std::minstd_rand rng;
        AiBlackboard blackboard;
        ThreatMap threatMap;
        int ticksSinceThreatRebuild{0};
        ReachabilityMap reachability;
        int ticksSinceReachabilityRebuild{0};

        PerceptionManager perception;
        EconomyManager economy;
        StrategicManager strategic;
        BuildManager build;
        ScoutManager scout;
        TransportManager transport;
        ArmyManager army;
    };
}
