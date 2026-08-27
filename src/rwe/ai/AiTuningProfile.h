#pragma once

#include <rwe/sim/SimScalar.h>
#include <string>

namespace rwe
{
    enum class AiDifficulty
    {
        Easy,
        Standard,
        Hard,
        Brutal,
    };

    /**
     * Every knob the AI reads. One instance per AI player; the four
     * difficulty tiers are just different sets of values.
     */
    struct AiTuningProfile
    {
        std::string name{"DEFAULT"};
        AiDifficulty difficulty{AiDifficulty::Standard};

        // --- Opening (commander only) ---
        int openingMetalExtractorCount{3};
        int openingSolarCount{4};

        // --- Expansion targets once a factory is up ---
        int targetSolarCount{10};
        int targetMetalExtractorCount{8};
        int targetConstructorCount{2};
        int targetDefenceCount{2};
        int targetRadarCount{1};

        // --- Site search ---
        SimScalar maxMexSearchRadius{512_ss};
        SimScalar expansionMexSearchRadius{2048_ss};
        SimScalar buildSiteGridSpacing{16_ss};
        SimScalar defenceDistanceFromBase{160_ss};

        // --- Cadence (ticks) ---
        int buildPlannerTickInterval{30};
        int threatMapTickInterval{30};
        int scoutTickInterval{60};
        int tacticalTickInterval{15};

        // --- Army ---
        int scoutCount{1};
        /** Attack once this many combat units are at the rally point. */
        int attackArmySize{8};
        /** Fall back to the rally point when the attacking army drops below this. */
        int retreatArmySize{3};
        /** Enemies this close to the base anchor trigger a defence. */
        SimScalar defendRadius{900_ss};
        /** How far from a known enemy an army unit will pick a fight. */
        SimScalar engageRadius{450_ss};
        /** Rally point sits this far from the base anchor, towards the enemy. */
        SimScalar rallyDistance{220_ss};
        /** Weighting of enemy anti-ground threat against economic value when choosing targets. */
        SimScalar threatAversion{1_ss};

        // --- Cheats (Brutal) ---
        bool cheatModeOmniscient{false};
        SimScalar resourceCheatMultiplier{1_ss};
    };

    AiTuningProfile makeDefaultStandardProfile();
    AiTuningProfile makeDefaultBrutalProfile();
    AiTuningProfile makeProfileForDifficulty(AiDifficulty difficulty);
    const char* aiDifficultyName(AiDifficulty difficulty);
}
