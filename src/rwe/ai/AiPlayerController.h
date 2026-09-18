#pragma once

#include <cstdint>
#include <map>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/ai/AirManager.h>
#include <rwe/ai/ArmyManager.h>
#include <rwe/ai/BuildManager.h>
#include <rwe/ai/EconomyManager.h>
#include <rwe/ai/MetalMakerManager.h>
#include <rwe/ai/PerceptionManager.h>
#include <rwe/ai/ReachabilityMap.h>
#include <rwe/ai/ScoutManager.h>
#include <rwe/ai/StrategicManager.h>
#include <rwe/ai/ThreatMap.h>
#include <rwe/ai/TransportManager.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Wall-clock timing of the AI's passes, for hunting frame stalls.
     *
     * Off unless the RWE_AI_PROFILE environment variable is set. It is a
     * pure observer: nothing it measures is ever read back into the
     * simulation, so switching it on cannot change what the AI does.
     */
    class AiProfiler
    {
    public:
        /** Whether RWE_AI_PROFILE was set when the process started. */
        static bool enabled();

        struct PassStats
        {
            double totalMs{0.0};
            double worstMs{0.0};
            unsigned int calls{0};
            unsigned int spikes{0};
        };

        /** Times one pass, logging straight away if it blew the spike budget. */
        void record(const char* pass, double milliseconds, PlayerId player, GameTime now);

        /** Logs the accumulated totals since the last report, then clears them. */
        void report(PlayerId player, GameTime now);

    private:
        // std::map so the report comes out in a stable order.
        std::map<std::string, PassStats> passes;
        double windowMs{0.0};
    };

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
            std::uint64_t rngSeed,
            MapIntel mapIntel,
            AiBuildTree buildTree = AiBuildTree{});

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
        /** So the air-threat line is logged when it changes rather than every tick. */
        bool loggedAirThreat{false};

        PerceptionManager perception;
        EconomyManager economy;
        MetalMakerManager metalMakers;
        StrategicManager strategic;
        BuildManager build;
        ScoutManager scout;
        TransportManager transport;
        ArmyManager army;
        AirManager air;

        AiProfiler profiler;
    };
}
