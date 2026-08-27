#include "AiTuningProfile.h"

namespace rwe
{
    AiTuningProfile makeDefaultStandardProfile()
    {
        // Hardcoded for Phase 1. A follow-up agent task will load these values
        // from a TDF in data/ai/profiles/ so non-engineers can tune the AI
        // without recompiling.
        AiTuningProfile p;
        p.name = "DEFAULT";
        p.difficulty = AiDifficulty::Standard;
        p.openingMetalExtractorCount = 3;
        p.openingSolarCount = 1;
        p.maxMexSearchRadius = 512_ss;
        p.buildSiteGridSpacing = 16_ss;
        p.buildPlannerTickInterval = 30;
        p.cheatModeOmniscient = false;
        p.resourceCheatMultiplier = 1_ss;
        return p;
    }

    AiTuningProfile makeDefaultBrutalProfile()
    {
        auto p = makeDefaultStandardProfile();
        p.name = "BRUTAL";
        p.difficulty = AiDifficulty::Brutal;
        // Mirror the proposal's recommended Brutal settings.
        p.cheatModeOmniscient = true;
        // 1.25x income matches the table in docs §1.
        p.resourceCheatMultiplier = 1.25_ssf;
        return p;
    }
}
