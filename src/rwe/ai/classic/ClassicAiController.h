#pragma once

#include <cstdint>
#include <memory>
#include <rwe/ai/AiController.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * What a ClassicAiController is built from, all of it read before the
     * game starts so that two peers given the same files play the same game.
     */
    struct ClassicAiConfig
    {
        /**
         * The seat's difficulty. Easy plays the profile's `easy` plan with a
         * 0.5 income handicap, Standard its `medium` plan at 0.7, and Hard and
         * Brutal its `hard` plan at 1.0. Idle thinks about nothing.
         */
        AiDifficulty difficulty{AiDifficulty::Standard};

        /**
         * The text of the profile file: the mission's `aiprofile` if the
         * schema names one and it exists, otherwise `ai/default.txt`. Empty
         * when neither does, which plays with every multiplier at 100 and no
         * limits -- the original does the same.
         */
        std::string profileText;

        /**
         * Playing back a replay: construct, draw the seed, and do nothing.
         * The loader sets this on the same condition it sets
         * AiTuningProfile::idle.
         */
        bool idle{false};
    };

    /**
     * The original's computer player (TOTALA-EXE-COMPUTER-PLAYER.md,
     * sections 119 to 123): ten timers, a classifier that sorts every unit
     * into a group every 30 ticks, a weighted draw over what a builder may
     * build, two randomised placement lattices, and assault waves that attack
     * the enemy nearest their own centroid.
     *
     * It is deliberately NOT a better AI. It has no transport policy, never
     * sends an aircraft at anything, never guards or captures, and waits at
     * six units. That is the original's, and it is what this class is for.
     *
     * Everything it decides comes from the simulation it is handed and from
     * its own generator, seeded once at construction from the simulation's.
     * It holds no raw pointers across ticks.
     */
    class ClassicAiController final : public AiController
    {
    public:
        ClassicAiController(PlayerId playerId, ClassicAiConfig config, std::uint64_t rngSeed);

        /** Out of line for the reason AiController's is. */
        ~ClassicAiController() override;

        void tick(const GameSimulation& sim, std::vector<PlayerCommand>& outCommands) override;

        PlayerId getPlayerId() const override { return playerId; }
        const AiTuningProfile& getProfile() const override { return profile; }
        AiEngine engine() const override { return AiEngine::Classic; }

    private:
        struct Impl;

        PlayerId playerId;
        AiTuningProfile profile;
        std::unique_ptr<Impl> impl;
    };
}
