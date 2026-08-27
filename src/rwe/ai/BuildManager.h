#pragma once

#include <optional>
#include <random>
#include <rwe/ai/AiBlackboard.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /** The unit types one side's AI builds, resolved once from the player's side. */
    struct AiSideUnits
    {
        std::string metalExtractor;
        std::string solar;
        std::string lab;
        std::string constructor;
        std::string raider;
        std::string rocketKbot;
        std::string lightLaserTower;
        std::string radar;
    };

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
            std::minstd_rand& rng,
            std::vector<PlayerCommand>& outCommands);

        const AiSideUnits& getSideUnits() const { return sideUnits; }

        std::optional<SimVector> chooseBuildSite(
            const GameSimulation& sim,
            const AiTuningProfile& profile,
            const std::string& unitType,
            const SimVector& anchor,
            std::minstd_rand& rng) const;

        std::optional<SimVector> chooseMexSite(
            const GameSimulation& sim,
            const std::string& unitType,
            const SimVector& anchor,
            SimScalar radius,
            std::minstd_rand& rng) const;

    private:
        bool sideResolved{false};
        AiSideUnits sideUnits;
        int ticksSinceLastPlanning{0};

        void resolveSide(const GameSimulation& sim, PlayerId aiOwner);

        /** What the next idle builder should build, if anything. */
        std::optional<std::string> chooseNextBuilding(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb) const;

        void planFactories(const GameSimulation& sim, const AiTuningProfile& profile, const AiBlackboard& bb, std::vector<PlayerCommand>& outCommands) const;
    };
}
