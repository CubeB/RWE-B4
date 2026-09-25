#include "AiTuningProfile.h"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        struct IntKnob
        {
            const char* name;
            int AiTuningProfile::*field;
        };
        struct BoolKnob
        {
            const char* name;
            bool AiTuningProfile::*field;
        };
        struct FloatKnob
        {
            const char* name;
            float AiTuningProfile::*field;
        };
        struct ScalarKnob
        {
            const char* name;
            SimScalar AiTuningProfile::*field;
        };

        bool parseAiTuningBool(const std::string& value)
        {
            return value == "1" || value == "true" || value == "on" || value == "yes";
        }

        /**
         * One knob, one entry, in one of the four tables below by its field's
         * type -- this is the single table applyAiTuning matches against and
         * listAiKnobs reads back, so the two cannot drift apart. Order is the
         * order --ai-tune has always matched in (largely declaration order);
         * listAiKnobs sorts its own output rather than relying on it.
         */
        constexpr IntKnob intKnobs[] = {
            {"openingMetalExtractorCount", &AiTuningProfile::openingMetalExtractorCount},
            {"openingSolarCount", &AiTuningProfile::openingSolarCount},
            {"targetSolarCount", &AiTuningProfile::targetSolarCount},
            {"targetMetalExtractorCount", &AiTuningProfile::targetMetalExtractorCount},
            {"targetConstructorCount", &AiTuningProfile::targetConstructorCount},
            {"expansionConstructors", &AiTuningProfile::expansionConstructors},
            {"seaAirFactoriesWhenIsolated", &AiTuningProfile::seaAirFactoriesWhenIsolated},
            {"stalledAttackSeconds", &AiTuningProfile::stalledAttackSeconds},
            {"stalledAttackForgetSeconds", &AiTuningProfile::stalledAttackForgetSeconds},
            {"stalledAttackRepositionTries", &AiTuningProfile::stalledAttackRepositionTries},
            {"counterShareBonus", &AiTuningProfile::counterShareBonus},
            {"capacitySurplusSeconds", &AiTuningProfile::capacitySurplusSeconds},
            {"surplusConstructors", &AiTuningProfile::surplusConstructors},
            {"surplusFactories", &AiTuningProfile::surplusFactories},
            {"freeDepositsPerExpansionConstructor", &AiTuningProfile::freeDepositsPerExpansionConstructor},
            {"targetDefenceCount", &AiTuningProfile::targetDefenceCount},
            {"outpostDefenceCount", &AiTuningProfile::outpostDefenceCount},
            {"outpostTowerIncomeStep", &AiTuningProfile::outpostTowerIncomeStep},
            {"outpostDefenceMax", &AiTuningProfile::outpostDefenceMax},
            {"outpostDefenceMinExtractors", &AiTuningProfile::outpostDefenceMinExtractors},
            {"outpostRaidMemorySeconds", &AiTuningProfile::outpostRaidMemorySeconds},
            {"baseAntiAirTowerCount", &AiTuningProfile::baseAntiAirTowerCount},
            {"reactiveAntiAirTowerCount", &AiTuningProfile::reactiveAntiAirTowerCount},
            {"antiAirMobileCount", &AiTuningProfile::antiAirMobileCount},
            {"targetRadarCount", &AiTuningProfile::targetRadarCount},
            {"targetMetalMakerCount", &AiTuningProfile::targetMetalMakerCount},
            {"starvedMetalMakerCount", &AiTuningProfile::starvedMetalMakerCount},
            {"starvedMetalMakerPasses", &AiTuningProfile::starvedMetalMakerPasses},
            {"targetAirPlantCount", &AiTuningProfile::targetAirPlantCount},
            {"targetVehiclePlantCount", &AiTuningProfile::targetVehiclePlantCount},
            {"targetTidalCount", &AiTuningProfile::targetTidalCount},
            {"targetSonarCount", &AiTuningProfile::targetSonarCount},
            {"targetTorpedoLauncherCount", &AiTuningProfile::targetTorpedoLauncherCount},
            {"surplusLabCount", &AiTuningProfile::surplusLabCount},
            {"techMinMetalIncome", &AiTuningProfile::techMinMetalIncome},
            {"labRaiderShare", &AiTuningProfile::labRaiderShare},
            {"labRocketKbotShare", &AiTuningProfile::labRocketKbotShare},
            {"labArtilleryKbotShare", &AiTuningProfile::labArtilleryKbotShare},
            {"vehicleTankShare", &AiTuningProfile::vehicleTankShare},
            {"vehicleMissileTruckShare", &AiTuningProfile::vehicleMissileTruckShare},
            {"vehicleMediumTankShare", &AiTuningProfile::vehicleMediumTankShare},
            {"fortifyTeethPerTower", &AiTuningProfile::fortifyTeethPerTower},
            {"fortifyWrapGapTiles", &AiTuningProfile::fortifyWrapGapTiles},
            {"fortifyWrapTeeth", &AiTuningProfile::fortifyWrapTeeth},
            {"fortifyExtraConstructors", &AiTuningProfile::fortifyExtraConstructors},
            {"fortifyRepeatAttacks", &AiTuningProfile::fortifyRepeatAttacks},
            {"fortifyReactiveTeeth", &AiTuningProfile::fortifyReactiveTeeth},
            {"fortifyAttackGapSeconds", &AiTuningProfile::fortifyAttackGapSeconds},
            {"fortifyAttackMemorySeconds", &AiTuningProfile::fortifyAttackMemorySeconds},
            {"rebuildDelaySeconds", &AiTuningProfile::rebuildDelaySeconds},
            {"maxDefenceRebuilds", &AiTuningProfile::maxDefenceRebuilds},
            {"lostDefenceMemorySeconds", &AiTuningProfile::lostDefenceMemorySeconds},
            {"repairStructuresBelowPercent", &AiTuningProfile::repairStructuresBelowPercent},
            {"repairersPerStructure", &AiTuningProfile::repairersPerStructure},
            {"repairCommanderBelowPercent", &AiTuningProfile::repairCommanderBelowPercent},
            {"commanderRepairers", &AiTuningProfile::commanderRepairers},
            {"techSaveUpSeconds", &AiTuningProfile::techSaveUpSeconds},
            {"targetAdvancedLabCount", &AiTuningProfile::targetAdvancedLabCount},
            {"targetAdvancedConstructorCount", &AiTuningProfile::targetAdvancedConstructorCount},
            {"targetAdvancedRadarCount", &AiTuningProfile::targetAdvancedRadarCount},
            {"heavyDefenceCount", &AiTuningProfile::heavyDefenceCount},
            {"targetFusionCount", &AiTuningProfile::targetFusionCount},
            {"targetScoutPlaneCount", &AiTuningProfile::targetScoutPlaneCount},
            {"targetScoutVehicleCount", &AiTuningProfile::targetScoutVehicleCount},
            {"navalFleetSize", &AiTuningProfile::navalFleetSize},
            {"targetShipyardCount", &AiTuningProfile::targetShipyardCount},
            {"isolatedLandArmyCap", &AiTuningProfile::isolatedLandArmyCap},
            {"targetScoutShipCount", &AiTuningProfile::targetScoutShipCount},
            {"targetSeaTransportCount", &AiTuningProfile::targetSeaTransportCount},
            {"ferryArmyRuns", &AiTuningProfile::ferryArmyRuns},
            {"seaTransportReserve", &AiTuningProfile::seaTransportReserve},
            {"maxSeaTransportCount", &AiTuningProfile::maxSeaTransportCount},
            {"targetConstructionShipCount", &AiTuningProfile::targetConstructionShipCount},
            {"targetSubmarineCount", &AiTuningProfile::targetSubmarineCount},
            {"submarineMinDestroyerCount", &AiTuningProfile::submarineMinDestroyerCount},
            {"targetAdvancedShipyardCount", &AiTuningProfile::targetAdvancedShipyardCount},
            {"navalTechMinMetalIncome", &AiTuningProfile::navalTechMinMetalIncome},
            {"targetCruiserCount", &AiTuningProfile::targetCruiserCount},
            {"targetBattleshipCount", &AiTuningProfile::targetBattleshipCount},
            {"targetAntiAirShipCount", &AiTuningProfile::targetAntiAirShipCount},
            {"targetSeaplanePlatformCount", &AiTuningProfile::targetSeaplanePlatformCount},
            {"maxSurplusMetalMakerCount", &AiTuningProfile::maxSurplusMetalMakerCount},
            {"tierTwoReserveMinArmySize", &AiTuningProfile::tierTwoReserveMinArmySize},
            {"tierTwoReserveCoversLabAfterSeconds", &AiTuningProfile::tierTwoReserveCoversLabAfterSeconds},
            {"tierTwoReserveMaxSeconds", &AiTuningProfile::tierTwoReserveMaxSeconds},
            {"targetUnderwaterFusionCount", &AiTuningProfile::targetUnderwaterFusionCount},
            {"targetMetalStorageCount", &AiTuningProfile::targetMetalStorageCount},
            {"targetEnergyStorageCount", &AiTuningProfile::targetEnergyStorageCount},
            {"maxReactiveFighterCount", &AiTuningProfile::maxReactiveFighterCount},
            {"targetTorpedoSeaplaneCount", &AiTuningProfile::targetTorpedoSeaplaneCount},
            {"attackNavalSize", &AiTuningProfile::attackNavalSize},
            {"navalAttackFleetSize", &AiTuningProfile::navalAttackFleetSize},
            {"saveUpSeconds", &AiTuningProfile::saveUpSeconds},
            {"failedSiteMemorySeconds", &AiTuningProfile::failedSiteMemorySeconds},
            {"commanderFightsUpToMetal", &AiTuningProfile::commanderFightsUpToMetal},
            {"commanderRetreatBelowPercent", &AiTuningProfile::commanderRetreatBelowPercent},
            {"retreatRaiderBelowPercent", &AiTuningProfile::retreatRaiderBelowPercent},
            {"retreatLineBelowPercent", &AiTuningProfile::retreatLineBelowPercent},
            {"rejoinAbovePercent", &AiTuningProfile::rejoinAbovePercent},
            {"mendWaitSeconds", &AiTuningProfile::mendWaitSeconds},
            {"finishBuildAbovePercent", &AiTuningProfile::finishBuildAbovePercent},
            {"mendBelowPercent", &AiTuningProfile::mendBelowPercent},
            {"builderShelterSeconds", &AiTuningProfile::builderShelterSeconds},
            {"commanderFrameAbsenceSeconds", &AiTuningProfile::commanderFrameAbsenceSeconds},
            {"perimeterDefenceCount", &AiTuningProfile::perimeterDefenceCount},
            {"radarWarningMinContacts", &AiTuningProfile::radarWarningMinContacts},
            {"navalStalledAttackSeconds", &AiTuningProfile::navalStalledAttackSeconds},
            {"surplusFactoryIncomeStep", &AiTuningProfile::surplusFactoryIncomeStep},
            {"surplusFactoryCap", &AiTuningProfile::surplusFactoryCap},
            {"surplusFleetMultiplier", &AiTuningProfile::surplusFleetMultiplier},
            {"surplusBomberMultiplier", &AiTuningProfile::surplusBomberMultiplier},
            {"extractorUpgradeTimeoutSeconds", &AiTuningProfile::extractorUpgradeTimeoutSeconds},
            {"buildPlannerTickInterval", &AiTuningProfile::buildPlannerTickInterval},
            {"scoutTickInterval", &AiTuningProfile::scoutTickInterval},
            {"tacticalTickInterval", &AiTuningProfile::tacticalTickInterval},
            {"attackArmySize", &AiTuningProfile::attackArmySize},
            {"retreatArmySize", &AiTuningProfile::retreatArmySize},
            {"attackPatienceSeconds", &AiTuningProfile::attackPatienceSeconds},
            {"commanderDefendsAloneMaxIntruders", &AiTuningProfile::commanderDefendsAloneMaxIntruders},
            {"reinforcementGroupSize", &AiTuningProfile::reinforcementGroupSize},
            {"raidPartySize", &AiTuningProfile::raidPartySize},
            {"targetFighterCount", &AiTuningProfile::targetFighterCount},
            {"targetBomberCount", &AiTuningProfile::targetBomberCount},
            {"airWorthItEnemyDefences", &AiTuningProfile::airWorthItEnemyDefences},
            {"targetAirRepairPadCount", &AiTuningProfile::targetAirRepairPadCount},
            {"targetAdvancedAirPlantCount", &AiTuningProfile::targetAdvancedAirPlantCount},
            {"targetGunshipCount", &AiTuningProfile::targetGunshipCount},
            {"gunshipPackSize", &AiTuningProfile::gunshipPackSize},
            {"bomberMaxAntiAirCover", &AiTuningProfile::bomberMaxAntiAirCover},
            {"bomberMinClusterSize", &AiTuningProfile::bomberMinClusterSize},
            {"targetAirConstructorCount", &AiTuningProfile::targetAirConstructorCount},
            {"battlefieldReclaimEscortCount", &AiTuningProfile::battlefieldReclaimEscortCount},
            {"battlefieldReclaimBatch", &AiTuningProfile::battlefieldReclaimBatch},
            {"waveMeetEnemyCount", &AiTuningProfile::waveMeetEnemyCount},
            {"defenceValueMaxPaybackSeconds", &AiTuningProfile::defenceValueMaxPaybackSeconds},
            {"outpostDefenceValueSecondsPerExtractor", &AiTuningProfile::outpostDefenceValueSecondsPerExtractor},
            {"buildSiteGuardSize", &AiTuningProfile::buildSiteGuardSize},
            {"buildSiteGuardTimeoutSeconds", &AiTuningProfile::buildSiteGuardTimeoutSeconds},
            {"targetMemoryTicks", &AiTuningProfile::targetMemoryTicks},
            {"ferryLandingSearchSteps", &AiTuningProfile::ferryLandingSearchSteps},
        };

        constexpr BoolKnob boolKnobs[] = {
            {"expansionContestsMiddleWhenBoxedIn", &AiTuningProfile::expansionContestsMiddleWhenBoxedIn},
            {"dgunByValue", &AiTuningProfile::dgunByValue},
            {"kiteWithLongerRange", &AiTuningProfile::kiteWithLongerRange},
            {"answerStalledAttacks", &AiTuningProfile::answerStalledAttacks},
            {"answerBlockedShots", &AiTuningProfile::answerBlockedShots},
            {"enemyGunRangeFromWeapon", &AiTuningProfile::enemyGunRangeFromWeapon},
            {"counterEnemyComposition", &AiTuningProfile::counterEnemyComposition},
            {"spendSurplusOnCapacity", &AiTuningProfile::spendSurplusOnCapacity},
            {"answerOutpostRaids", &AiTuningProfile::answerOutpostRaids},
            {"techLevelTwo", &AiTuningProfile::techLevelTwo},
            {"fortifyTowers", &AiTuningProfile::fortifyTowers},
            {"fortifyAtTierTwo", &AiTuningProfile::fortifyAtTierTwo},
            {"fortifyTeethWrap", &AiTuningProfile::fortifyTeethWrap},
            {"fortifyMissileTower", &AiTuningProfile::fortifyMissileTower},
            {"fortifyWhereAttacked", &AiTuningProfile::fortifyWhereAttacked},
            {"rebuildLostDefences", &AiTuningProfile::rebuildLostDefences},
            {"fortifyRebuiltDefences", &AiTuningProfile::fortifyRebuiltDefences},
            {"reinforceTwiceLostDefences", &AiTuningProfile::reinforceTwiceLostDefences},
            {"energyInRows", &AiTuningProfile::energyInRows},
            {"repairStructures", &AiTuningProfile::repairStructures},
            {"repairUnderFire", &AiTuningProfile::repairUnderFire},
            {"repairCommander", &AiTuningProfile::repairCommander},
            {"earlyShipyard", &AiTuningProfile::earlyShipyard},
            {"navalBuildersPlanForBase", &AiTuningProfile::navalBuildersPlanForBase},
            {"solarOnDemand", &AiTuningProfile::solarOnDemand},
            {"vehiclePlantFirst", &AiTuningProfile::vehiclePlantFirst},
            {"tierTwoEconomyReserve", &AiTuningProfile::tierTwoEconomyReserve},
            {"surplusExpansion", &AiTuningProfile::surplusExpansion},
            {"commanderStandsItsGround", &AiTuningProfile::commanderStandsItsGround},
            {"retreatDamagedUnits", &AiTuningProfile::retreatDamagedUnits},
            {"mendNeedsMender", &AiTuningProfile::mendNeedsMender},
            {"resumeAfterBackingOff", &AiTuningProfile::resumeAfterBackingOff},
            {"mendDamagedUnits", &AiTuningProfile::mendDamagedUnits},
            {"commanderUsesDgun", &AiTuningProfile::commanderUsesDgun},
            {"commanderKeepsFrames", &AiTuningProfile::commanderKeepsFrames},
            {"builderSafety", &AiTuningProfile::builderSafety},
            {"commanderPrefersNearSites", &AiTuningProfile::commanderPrefersNearSites},
            {"commanderMends", &AiTuningProfile::commanderMends},
            {"firstDefencesOnPerimeter", &AiTuningProfile::firstDefencesOnPerimeter},
            {"outpostTowersLeftToConstructors", &AiTuningProfile::outpostTowersLeftToConstructors},
            {"extractorUpgrades", &AiTuningProfile::extractorUpgrades},
            {"attackInWaves", &AiTuningProfile::attackInWaves},
            {"holdWhenOutnumbered", &AiTuningProfile::holdWhenOutnumbered},
            {"reserveAnswersIntruders", &AiTuningProfile::reserveAnswersIntruders},
            {"noticeProductionHarassment", &AiTuningProfile::noticeProductionHarassment},
            {"commanderAnswersHarassment", &AiTuningProfile::commanderAnswersHarassment},
            {"raidingParties", &AiTuningProfile::raidingParties},
            {"huntEnemyCommander", &AiTuningProfile::huntEnemyCommander},
            {"focusOneEnemy", &AiTuningProfile::focusOneEnemy},
            {"spreadDefences", &AiTuningProfile::spreadDefences},
            {"commanderUsesOwnReachability", &AiTuningProfile::commanderUsesOwnReachability},
            {"navalScouting", &AiTuningProfile::navalScouting},
            {"defenceFacesRecentLosses", &AiTuningProfile::defenceFacesRecentLosses},
            {"cheatModeOmniscient", &AiTuningProfile::cheatModeOmniscient},
            {"expansionNeedsExploredGround", &AiTuningProfile::expansionNeedsExploredGround},
            {"expansionStaysOnOurSide", &AiTuningProfile::expansionStaysOnOurSide},
            {"armyFerryWantFromMap", &AiTuningProfile::armyFerryWantFromMap},
            {"ferryLandingFan", &AiTuningProfile::ferryLandingFan},
            {"ferryLandingAvoidsThreat", &AiTuningProfile::ferryLandingAvoidsThreat},
            {"ferryBoundArmyCap", &AiTuningProfile::ferryBoundArmyCap},
        };

        constexpr FloatKnob floatKnobs[] = {
            {"counterShareTrigger", &AiTuningProfile::counterShareTrigger},
            {"capacityIncomeRatio", &AiTuningProfile::capacityIncomeRatio},
            {"outpostResponseStrength", &AiTuningProfile::outpostResponseStrength},
            {"isolatedLandArmyCapMinWaterFraction", &AiTuningProfile::isolatedLandArmyCapMinWaterFraction},
            {"techMinArmyValueRatio", &AiTuningProfile::techMinArmyValueRatio},
            {"buildSiteGuardThreat", &AiTuningProfile::buildSiteGuardThreat},
            {"buildSiteGuardThreatRadius", &AiTuningProfile::buildSiteGuardThreatRadius},
            {"builderSafetyProtectionRatio", &AiTuningProfile::builderSafetyProtectionRatio},
            {"radarWarningRings", &AiTuningProfile::radarWarningRings},
            {"extractorUpgradeMinMetalFraction", &AiTuningProfile::extractorUpgradeMinMetalFraction},
            {"bomberFactoryWeight", &AiTuningProfile::bomberFactoryWeight},
            {"bomberCoverPenalty", &AiTuningProfile::bomberCoverPenalty},
            {"bomberArmyReachDiscount", &AiTuningProfile::bomberArmyReachDiscount},
            {"ferryLandingThreatRadius", &AiTuningProfile::ferryLandingThreatRadius},
        };

        constexpr ScalarKnob scalarKnobs[] = {
            {"stalledAttackSidestep", &AiTuningProfile::stalledAttackSidestep},
            {"kiteRangeMargin", &AiTuningProfile::kiteRangeMargin},
            {"enemyGunRangeMargin", &AiTuningProfile::enemyGunRangeMargin},
            {"outpostRaidRadius", &AiTuningProfile::outpostRaidRadius},
            {"outpostResponseRadius", &AiTuningProfile::outpostResponseRadius},
            {"fortifyTeethDistance", &AiTuningProfile::fortifyTeethDistance},
            {"fortifyMissileDistance", &AiTuningProfile::fortifyMissileDistance},
            {"fortifyMissileCoverRadius", &AiTuningProfile::fortifyMissileCoverRadius},
            {"fortifyAttackerRadius", &AiTuningProfile::fortifyAttackerRadius},
            {"repairSearchRadius", &AiTuningProfile::repairSearchRadius},
            {"repairCommanderRadius", &AiTuningProfile::repairCommanderRadius},
            {"commanderDangerRadius", &AiTuningProfile::commanderDangerRadius},
            {"mendMaxWalkHome", &AiTuningProfile::mendMaxWalkHome},
            {"mendStandRadius", &AiTuningProfile::mendStandRadius},
            {"claimedSiteRadius", &AiTuningProfile::claimedSiteRadius},
            {"mendHavenRadius", &AiTuningProfile::mendHavenRadius},
            {"mendRadius", &AiTuningProfile::mendRadius},
            {"builderSafetyMargin", &AiTuningProfile::builderSafetyMargin},
            {"builderSafetyCoverRadius", &AiTuningProfile::builderSafetyCoverRadius},
            {"commanderFrameCoverRadius", &AiTuningProfile::commanderFrameCoverRadius},
            {"commanderFrameHandoverRadius", &AiTuningProfile::commanderFrameHandoverRadius},
            {"commanderAssistRadius", &AiTuningProfile::commanderAssistRadius},
            {"commanderLeashRadius", &AiTuningProfile::commanderLeashRadius},
            {"perimeterDefenceMargin", &AiTuningProfile::perimeterDefenceMargin},
            {"commanderGuardRadius", &AiTuningProfile::commanderGuardRadius},
            {"builderAvoidsContestedRadius", &AiTuningProfile::builderAvoidsContestedRadius},
            {"shipyardSpacing", &AiTuningProfile::shipyardSpacing},
            {"productionHarassRadius", &AiTuningProfile::productionHarassRadius},
            {"raidAvoidBaseRadius", &AiTuningProfile::raidAvoidBaseRadius},
            {"attackBaseRadius", &AiTuningProfile::attackBaseRadius},
            {"focusSwitchMargin", &AiTuningProfile::focusSwitchMargin},
            {"gunshipSupportRadius", &AiTuningProfile::gunshipSupportRadius},
            {"fighterLeash", &AiTuningProfile::fighterLeash},
            {"bomberHomeDefenseRadius", &AiTuningProfile::bomberHomeDefenseRadius},
            {"bomberClusterRadius", &AiTuningProfile::bomberClusterRadius},
            {"bomberSortieScale", &AiTuningProfile::bomberSortieScale},
            {"bomberLeaveToArmyRadius", &AiTuningProfile::bomberLeaveToArmyRadius},
            {"battlefieldReclaimRadius", &AiTuningProfile::battlefieldReclaimRadius},
            {"waveCohesionRadius", &AiTuningProfile::waveCohesionRadius},
            {"waveMeetEnemyRadius", &AiTuningProfile::waveMeetEnemyRadius},
            {"buildSiteGuardMinDistance", &AiTuningProfile::buildSiteGuardMinDistance},
            {"defenceDistanceFromBase", &AiTuningProfile::defenceDistanceFromBase},
            {"radarDistanceFromBase", &AiTuningProfile::radarDistanceFromBase},
            {"nearMexSearchRadius", &AiTuningProfile::nearMexSearchRadius},
            {"maxMexSearchRadius", &AiTuningProfile::maxMexSearchRadius},
            {"buildSiteFallbackRadius", &AiTuningProfile::buildSiteFallbackRadius},
            {"expansionMexSearchRadius", &AiTuningProfile::expansionMexSearchRadius},
            {"commanderMexSearchRadius", &AiTuningProfile::commanderMexSearchRadius},
            {"mexAvoidsEnemyGunsRadius", &AiTuningProfile::mexAvoidsEnemyGunsRadius},
            {"defendRadius", &AiTuningProfile::defendRadius},
            {"engageRadius", &AiTuningProfile::engageRadius},
            {"rallyDistance", &AiTuningProfile::rallyDistance},
            {"navalRallyDistance", &AiTuningProfile::navalRallyDistance},
            {"threatAversion", &AiTuningProfile::threatAversion},
            {"resourceCheatMultiplier", &AiTuningProfile::resourceCheatMultiplier},
        };

        template <typename Value>
        std::string formatKnobDefault(Value value)
        {
            std::ostringstream out;
            out << value;
            return out.str();
        }
    }

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
        p.defenceValueMaxPaybackSeconds = 45;
        p.outpostDefenceValueSecondsPerExtractor = 20;
        p.buildSiteGuardSize = 3;
        p.navalFleetSize = 12;
        p.targetSubmarineCount = 4;
        p.submarineMinDestroyerCount = 2;
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
                // Builds a tower wherever the count thresholds say to and
                // never second-guesses the metal, faces it at the enemy's
                // base (or the map's middle, not knowing that either) rather
                // than reading where it has actually been hit, and a
                // builder sent somewhere alone stays alone -- the weaker AI
                // is meant to be, among other things, worse at judging
                // value, worse at reading a raid, and less careful with its
                // army.
                p.defenceFacesRecentLosses = false;
                p.defenceValueMaxPaybackSeconds = 0;
                p.buildSiteGuardSize = 0;
                // A small fleet, late, and no submarines: teching into a
                // specialist hull is not the kind of judgement Easy is meant
                // to show.
                p.navalFleetSize = 3;
                p.targetSubmarineCount = 0;
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
                p.defenceValueMaxPaybackSeconds = 60;
                p.outpostDefenceValueSecondsPerExtractor = 30;
                p.buildSiteGuardSize = 3;
                p.navalFleetSize = 9;
                p.targetSubmarineCount = 3;
                p.submarineMinDestroyerCount = 2;
                return p;
            }
            case AiDifficulty::Brutal:
                return makeDefaultBrutalProfile();
        }
        return makeDefaultStandardProfile();
    }

    BuilderSafetyParams builderSafetyParams(const AiTuningProfile& p)
    {
        BuilderSafetyParams params;
        params.memoryTicks = static_cast<unsigned int>(std::max(0, p.targetMemoryTicks));
        params.mobileThreatMargin = p.builderSafetyMargin;
        params.coverRadius = p.builderSafetyCoverRadius;
        params.protectionRatio = p.builderSafetyProtectionRatio;
        params.commanderCoverMetal = static_cast<float>(p.commanderFightsUpToMetal);
        return params;
    }

    void applyFactionDefaults(AiTuningProfile& p, const std::string& side)
    {
        std::string upper;
        for (auto c : side)
        {
            upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        if (upper == "CORE")
        {
            // CORE does not win the raider fight and should not try to. At the
            // same price and weight the Peewee's EMG does about 60 damage a
            // second to the A.K. laser's 35, and the Flash 60 to the
            // Instigator's 44. So the damage comes from Storms, which outrange
            // both of ARM's raiders, and Thuds lobbing into the crowd, with one
            // raider to each Thud as the screen in front of them, and the
            // waves are big enough to stay together. Measured against the
            // shared defaults on Great Divide, CORE only, three batches of
            // eight seeds: CORE finished ahead in 6 of 8 against 2, in 5 of 8
            // against 1 once PR #82 was in, and in 6 of 8 against 2 on seeds
            // it was never tuned on (ROADMAP, 2026-09-19).
            p.labRaiderShare = 1;
            p.labRocketKbotShare = 2;
            p.labArtilleryKbotShare = 1;
            p.attackArmySize = 14;
        }
    }

    bool applyAiTuning(AiTuningProfile& p, const std::string& knob, const std::string& value)
    {
        for (const auto& k : intKnobs)
        {
            if (knob == k.name)
            {
                p.*(k.field) = std::stoi(value);
                return true;
            }
        }
        for (const auto& k : boolKnobs)
        {
            if (knob == k.name)
            {
                p.*(k.field) = parseAiTuningBool(value);
                return true;
            }
        }
        for (const auto& k : floatKnobs)
        {
            if (knob == k.name)
            {
                p.*(k.field) = std::stof(value);
                return true;
            }
        }
        for (const auto& k : scalarKnobs)
        {
            if (knob == k.name)
            {
                p.*(k.field) = SimScalar(std::stof(value));
                return true;
            }
        }
        return false;
    }

    std::vector<AiKnobInfo> listAiKnobs()
    {
        // A default-constructed profile, not makeDefaultStandardProfile's:
        // --ai-tune reads and writes AiTuningProfile's own fields, and those
        // are the defaults a knob's value is relative to. The STANDARD name
        // and a couple of derived fields are the only difference and neither
        // is a knob.
        AiTuningProfile p;

        std::vector<AiKnobInfo> result;
        result.reserve(std::size(intKnobs) + std::size(boolKnobs) + std::size(floatKnobs) + std::size(scalarKnobs));

        for (const auto& k : intKnobs)
        {
            result.push_back({k.name, "int", std::to_string(p.*(k.field))});
        }
        for (const auto& k : boolKnobs)
        {
            result.push_back({k.name, "bool", (p.*(k.field)) ? "true" : "false"});
        }
        for (const auto& k : floatKnobs)
        {
            result.push_back({k.name, "float", formatKnobDefault(p.*(k.field))});
        }
        for (const auto& k : scalarKnobs)
        {
            result.push_back({k.name, "scalar", formatKnobDefault(simScalarToFloat(p.*(k.field)))});
        }

        std::sort(result.begin(), result.end(), [](const AiKnobInfo& a, const AiKnobInfo& b) {
            return a.name < b.name;
        });
        return result;
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
