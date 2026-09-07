#include "AiTuningProfile.h"
#include <stdexcept>

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
        p.baseAntiAirTowerCount = 2;
        p.reactiveAntiAirTowerCount = 5;
        p.antiAirMobileCount = 3;
        p.buildPlannerTickInterval = 20;
        p.tacticalTickInterval = 10;
        return p;
    }

    AiTuningProfile makeIdleProfile()
    {
        AiTuningProfile p;
        p.name = "IDLE";
        p.difficulty = AiDifficulty::Idle;
        p.idle = true;
        return p;
    }

    AiTuningProfile makeProfileForDifficulty(AiDifficulty difficulty)
    {
        switch (difficulty)
        {
            case AiDifficulty::Idle:
                return makeIdleProfile();
            case AiDifficulty::Easy:
            {
                auto p = makeDefaultStandardProfile();
                p.name = "EASY";
                p.difficulty = AiDifficulty::Easy;
                // Slow to expand, slow to react, and only attacks with a big, late army.
                p.targetSolarCount = 6;
                p.targetMetalExtractorCount = 5;
                p.targetDefenceCount = 1;
                p.targetVehiclePlantCount = 0;
                p.surplusLabCount = 0;
                // Forgets about aircraft until they are overhead, and then
                // under-builds. Being slow to answer a bomber is a more
                // convincing weakness than owning fewer solar collectors.
                p.baseAntiAirTowerCount = 0;
                p.reactiveAntiAirTowerCount = 1;
                p.antiAirMobileCount = 0;
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
                p.targetConstructorCount = 2;
                p.surplusLabCount = 2;
                p.targetDefenceCount = 3;
                p.baseAntiAirTowerCount = 2;
                p.reactiveAntiAirTowerCount = 5;
                p.antiAirMobileCount = 3;
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

    bool applyAiTuning(AiTuningProfile& p, const std::string& knob, const std::string& value)
    {
        auto asInt = [&] { return std::stoi(value); };
        auto asBool = [&] { return value == "1" || value == "true" || value == "on" || value == "yes"; };
        auto asScalar = [&] { return SimScalar(std::stof(value)); };

        auto setInt = [&](const char* name, int& field) {
            if (knob != name)
            {
                return false;
            }
            field = asInt();
            return true;
        };
        auto setBool = [&](const char* name, bool& field) {
            if (knob != name)
            {
                return false;
            }
            field = asBool();
            return true;
        };
        auto setScalar = [&](const char* name, SimScalar& field) {
            if (knob != name)
            {
                return false;
            }
            field = asScalar();
            return true;
        };

        return setInt("openingMetalExtractorCount", p.openingMetalExtractorCount)
            || setInt("openingSolarCount", p.openingSolarCount)
            || setInt("targetSolarCount", p.targetSolarCount)
            || setInt("targetMetalExtractorCount", p.targetMetalExtractorCount)
            || setInt("targetConstructorCount", p.targetConstructorCount)
            || setInt("targetDefenceCount", p.targetDefenceCount)
            || setInt("baseAntiAirTowerCount", p.baseAntiAirTowerCount)
            || setInt("reactiveAntiAirTowerCount", p.reactiveAntiAirTowerCount)
            || setInt("antiAirMobileCount", p.antiAirMobileCount)
            || setInt("targetRadarCount", p.targetRadarCount)
            || setInt("targetMetalMakerCount", p.targetMetalMakerCount)
            || setInt("targetAirPlantCount", p.targetAirPlantCount)
            || setInt("targetVehiclePlantCount", p.targetVehiclePlantCount)
            || setInt("surplusLabCount", p.surplusLabCount)
            || setInt("targetScoutPlaneCount", p.targetScoutPlaneCount)
            || setInt("targetScoutVehicleCount", p.targetScoutVehicleCount)
            || setInt("saveUpSeconds", p.saveUpSeconds)
            || setInt("buildPlannerTickInterval", p.buildPlannerTickInterval)
            || setInt("scoutTickInterval", p.scoutTickInterval)
            || setInt("tacticalTickInterval", p.tacticalTickInterval)
            || setInt("attackArmySize", p.attackArmySize)
            || setInt("retreatArmySize", p.retreatArmySize)
            || setBool("attackInWaves", p.attackInWaves)
            || setBool("holdWhenOutnumbered", p.holdWhenOutnumbered)
            || setBool("cheatModeOmniscient", p.cheatModeOmniscient)
            || setScalar("nearMexSearchRadius", p.nearMexSearchRadius)
            || setScalar("maxMexSearchRadius", p.maxMexSearchRadius)
            || setScalar("expansionMexSearchRadius", p.expansionMexSearchRadius)
            || setScalar("defendRadius", p.defendRadius)
            || setScalar("engageRadius", p.engageRadius)
            || setScalar("rallyDistance", p.rallyDistance)
            || setScalar("threatAversion", p.threatAversion)
            || setScalar("resourceCheatMultiplier", p.resourceCheatMultiplier);
    }

    const char* aiDifficultyName(AiDifficulty difficulty)
    {
        switch (difficulty)
        {
            case AiDifficulty::Idle: return "Idle";
            case AiDifficulty::Easy: return "Easy";
            case AiDifficulty::Standard: return "Standard";
            case AiDifficulty::Hard: return "Hard";
            case AiDifficulty::Brutal: return "Brutal";
        }
        return "Standard";
    }
}
