#include "AiTuningProfile.h"

namespace rwe
{
    AiTuningProfile makeDefaultStandardProfile()
    {
        AiTuningProfile p;
        p.name = "STANDARD";
        p.difficulty = AiDifficulty::Standard;
        return p;
    }

    AiTuningProfile makeDefaultBrutalProfile()
    {
        auto p = makeDefaultStandardProfile();
        p.name = "BRUTAL";
        p.difficulty = AiDifficulty::Brutal;
        p.cheatModeOmniscient = true;
        p.resourceCheatMultiplier = 1.25_ssf;
        p.attackArmySize = 6;
        p.targetConstructorCount = 3;
        p.targetDefenceCount = 3;
        p.buildPlannerTickInterval = 20;
        p.tacticalTickInterval = 10;
        return p;
    }

    AiTuningProfile makeProfileForDifficulty(AiDifficulty difficulty)
    {
        switch (difficulty)
        {
            case AiDifficulty::Easy:
            {
                auto p = makeDefaultStandardProfile();
                p.name = "EASY";
                p.difficulty = AiDifficulty::Easy;
                // Slow to expand, slow to react, and only attacks with a big, late army.
                p.targetSolarCount = 6;
                p.targetMetalExtractorCount = 5;
                p.targetConstructorCount = 1;
                p.targetDefenceCount = 1;
                p.attackArmySize = 14;
                p.retreatArmySize = 5;
                p.buildPlannerTickInterval = 60;
                p.tacticalTickInterval = 30;
                p.scoutTickInterval = 120;
                return p;
            }
            case AiDifficulty::Standard:
                return makeDefaultStandardProfile();
            case AiDifficulty::Hard:
            {
                auto p = makeDefaultStandardProfile();
                p.name = "HARD";
                p.difficulty = AiDifficulty::Hard;
                p.targetSolarCount = 12;
                p.targetMetalExtractorCount = 10;
                p.targetConstructorCount = 3;
                p.targetDefenceCount = 3;
                p.attackArmySize = 7;
                p.retreatArmySize = 2;
                p.buildPlannerTickInterval = 20;
                p.tacticalTickInterval = 10;
                p.scoutTickInterval = 45;
                return p;
            }
            case AiDifficulty::Brutal:
                return makeDefaultBrutalProfile();
        }
        return makeDefaultStandardProfile();
    }

    const char* aiDifficultyName(AiDifficulty difficulty)
    {
        switch (difficulty)
        {
            case AiDifficulty::Easy: return "Easy";
            case AiDifficulty::Standard: return "Standard";
            case AiDifficulty::Hard: return "Hard";
            case AiDifficulty::Brutal: return "Brutal";
        }
        return "Standard";
    }
}
