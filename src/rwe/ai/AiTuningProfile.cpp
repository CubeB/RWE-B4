#include "AiTuningProfile.h"
#include <cctype>
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
        auto setFloat = [&](const char* name, float& field) {
            if (knob != name)
            {
                return false;
            }
            field = std::stof(value);
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
            || setBool("expansionContestsMiddleWhenBoxedIn", p.expansionContestsMiddleWhenBoxedIn)
            || setInt("targetConstructorCount", p.targetConstructorCount)
            || setInt("expansionConstructors", p.expansionConstructors)
            || setInt("seaAirFactoriesWhenIsolated", p.seaAirFactoriesWhenIsolated)
            || setBool("dgunByValue", p.dgunByValue)
            || setBool("kiteWithLongerRange", p.kiteWithLongerRange)
            || setBool("answerStalledAttacks", p.answerStalledAttacks)
            || setInt("stalledAttackSeconds", p.stalledAttackSeconds)
            || setInt("stalledAttackForgetSeconds", p.stalledAttackForgetSeconds)
            || setBool("answerBlockedShots", p.answerBlockedShots)
            || setInt("stalledAttackRepositionTries", p.stalledAttackRepositionTries)
            || setScalar("stalledAttackSidestep", p.stalledAttackSidestep)
            || setScalar("kiteRangeMargin", p.kiteRangeMargin)
            || setBool("enemyGunRangeFromWeapon", p.enemyGunRangeFromWeapon)
            || setScalar("enemyGunRangeMargin", p.enemyGunRangeMargin)
            || setBool("counterEnemyComposition", p.counterEnemyComposition)
            || setInt("counterShareBonus", p.counterShareBonus)
            || setFloat("counterShareTrigger", p.counterShareTrigger)
            || setBool("spendSurplusOnCapacity", p.spendSurplusOnCapacity)
            || setFloat("capacityIncomeRatio", p.capacityIncomeRatio)
            || setInt("capacitySurplusSeconds", p.capacitySurplusSeconds)
            || setInt("surplusConstructors", p.surplusConstructors)
            || setInt("surplusFactories", p.surplusFactories)
            || setInt("freeDepositsPerExpansionConstructor", p.freeDepositsPerExpansionConstructor)
            || setInt("targetDefenceCount", p.targetDefenceCount)
            || setInt("outpostDefenceCount", p.outpostDefenceCount)
            || setInt("outpostTowerIncomeStep", p.outpostTowerIncomeStep)
            || setInt("outpostDefenceMax", p.outpostDefenceMax)
            || setBool("answerOutpostRaids", p.answerOutpostRaids)
            || setScalar("outpostRaidRadius", p.outpostRaidRadius)
            || setScalar("outpostResponseRadius", p.outpostResponseRadius)
            || setFloat("outpostResponseStrength", p.outpostResponseStrength)
            || setInt("outpostDefenceMinExtractors", p.outpostDefenceMinExtractors)
            || setInt("outpostRaidMemorySeconds", p.outpostRaidMemorySeconds)
            || setInt("baseAntiAirTowerCount", p.baseAntiAirTowerCount)
            || setInt("reactiveAntiAirTowerCount", p.reactiveAntiAirTowerCount)
            || setInt("antiAirMobileCount", p.antiAirMobileCount)
            || setInt("targetRadarCount", p.targetRadarCount)
            || setInt("targetMetalMakerCount", p.targetMetalMakerCount)
            || setInt("starvedMetalMakerCount", p.starvedMetalMakerCount)
            || setInt("starvedMetalMakerPasses", p.starvedMetalMakerPasses)
            || setInt("targetAirPlantCount", p.targetAirPlantCount)
            || setInt("targetVehiclePlantCount", p.targetVehiclePlantCount)
            || setFloat("isolatedLandArmyCapMinWaterFraction", p.isolatedLandArmyCapMinWaterFraction)
            || setInt("targetTidalCount", p.targetTidalCount)
            || setInt("targetSonarCount", p.targetSonarCount)
            || setInt("targetTorpedoLauncherCount", p.targetTorpedoLauncherCount)
            || setInt("surplusLabCount", p.surplusLabCount)
            || setBool("techLevelTwo", p.techLevelTwo)
            || setInt("techMinMetalIncome", p.techMinMetalIncome)
            || setFloat("techMinArmyValueRatio", p.techMinArmyValueRatio)
            || setInt("labRaiderShare", p.labRaiderShare)
            || setInt("labRocketKbotShare", p.labRocketKbotShare)
            || setInt("labArtilleryKbotShare", p.labArtilleryKbotShare)
            || setInt("vehicleTankShare", p.vehicleTankShare)
            || setInt("vehicleMissileTruckShare", p.vehicleMissileTruckShare)
            || setInt("vehicleMediumTankShare", p.vehicleMediumTankShare)
            || setBool("fortifyTowers", p.fortifyTowers)
            || setBool("fortifyAtTierTwo", p.fortifyAtTierTwo)
            || setInt("fortifyTeethPerTower", p.fortifyTeethPerTower)
            || setScalar("fortifyTeethDistance", p.fortifyTeethDistance)
            || setBool("fortifyTeethWrap", p.fortifyTeethWrap)
            || setInt("fortifyWrapGapTiles", p.fortifyWrapGapTiles)
            || setInt("fortifyWrapTeeth", p.fortifyWrapTeeth)
            || setBool("fortifyMissileTower", p.fortifyMissileTower)
            || setScalar("fortifyMissileDistance", p.fortifyMissileDistance)
            || setScalar("fortifyMissileCoverRadius", p.fortifyMissileCoverRadius)
            || setInt("fortifyExtraConstructors", p.fortifyExtraConstructors)
            || setBool("fortifyWhereAttacked", p.fortifyWhereAttacked)
            || setInt("fortifyRepeatAttacks", p.fortifyRepeatAttacks)
            || setInt("fortifyReactiveTeeth", p.fortifyReactiveTeeth)
            || setInt("fortifyAttackGapSeconds", p.fortifyAttackGapSeconds)
            || setInt("fortifyAttackMemorySeconds", p.fortifyAttackMemorySeconds)
            || setScalar("fortifyAttackerRadius", p.fortifyAttackerRadius)
            || setBool("rebuildLostDefences", p.rebuildLostDefences)
            || setInt("rebuildDelaySeconds", p.rebuildDelaySeconds)
            || setInt("maxDefenceRebuilds", p.maxDefenceRebuilds)
            || setInt("lostDefenceMemorySeconds", p.lostDefenceMemorySeconds)
            || setBool("fortifyRebuiltDefences", p.fortifyRebuiltDefences)
            || setBool("reinforceTwiceLostDefences", p.reinforceTwiceLostDefences)
            || setBool("energyInRows", p.energyInRows)
            || setBool("repairStructures", p.repairStructures)
            || setInt("repairStructuresBelowPercent", p.repairStructuresBelowPercent)
            || setScalar("repairSearchRadius", p.repairSearchRadius)
            || setInt("repairersPerStructure", p.repairersPerStructure)
            || setBool("repairUnderFire", p.repairUnderFire)
            || setBool("repairCommander", p.repairCommander)
            || setInt("repairCommanderBelowPercent", p.repairCommanderBelowPercent)
            || setScalar("repairCommanderRadius", p.repairCommanderRadius)
            || setInt("commanderRepairers", p.commanderRepairers)
            || setInt("techSaveUpSeconds", p.techSaveUpSeconds)
            || setInt("targetAdvancedLabCount", p.targetAdvancedLabCount)
            || setInt("targetAdvancedConstructorCount", p.targetAdvancedConstructorCount)
            || setInt("targetAdvancedRadarCount", p.targetAdvancedRadarCount)
            || setInt("heavyDefenceCount", p.heavyDefenceCount)
            || setInt("targetFusionCount", p.targetFusionCount)
            || setInt("targetScoutPlaneCount", p.targetScoutPlaneCount)
            || setInt("targetScoutVehicleCount", p.targetScoutVehicleCount)
            || setInt("navalFleetSize", p.navalFleetSize)
            || setInt("targetShipyardCount", p.targetShipyardCount)
            || setBool("earlyShipyard", p.earlyShipyard)
            || setFloat("buildSiteGuardThreat", p.buildSiteGuardThreat)
            || setFloat("buildSiteGuardThreatRadius", p.buildSiteGuardThreatRadius)
            || setInt("isolatedLandArmyCap", p.isolatedLandArmyCap)
            || setInt("targetScoutShipCount", p.targetScoutShipCount)
            || setInt("targetSeaTransportCount", p.targetSeaTransportCount)
            || setInt("targetConstructionShipCount", p.targetConstructionShipCount)
            || setInt("targetSubmarineCount", p.targetSubmarineCount)
            || setInt("submarineMinDestroyerCount", p.submarineMinDestroyerCount)
            || setInt("targetAdvancedShipyardCount", p.targetAdvancedShipyardCount)
            || setBool("navalBuildersPlanForBase", p.navalBuildersPlanForBase)
            || setInt("navalTechMinMetalIncome", p.navalTechMinMetalIncome)
            || setInt("targetCruiserCount", p.targetCruiserCount)
            || setInt("targetBattleshipCount", p.targetBattleshipCount)
            || setInt("targetAntiAirShipCount", p.targetAntiAirShipCount)
            || setInt("targetSeaplanePlatformCount", p.targetSeaplanePlatformCount)
            || setInt("maxSurplusMetalMakerCount", p.maxSurplusMetalMakerCount)
            || setBool("solarOnDemand", p.solarOnDemand)
            || setBool("vehiclePlantFirst", p.vehiclePlantFirst)
            || setBool("tierTwoEconomyReserve", p.tierTwoEconomyReserve)
            || setInt("tierTwoReserveMinArmySize", p.tierTwoReserveMinArmySize)
            || setInt("tierTwoReserveCoversLabAfterSeconds", p.tierTwoReserveCoversLabAfterSeconds)
            || setInt("tierTwoReserveMaxSeconds", p.tierTwoReserveMaxSeconds)
            || setInt("targetUnderwaterFusionCount", p.targetUnderwaterFusionCount)
            || setInt("targetMetalStorageCount", p.targetMetalStorageCount)
            || setInt("targetEnergyStorageCount", p.targetEnergyStorageCount)
            || setInt("maxReactiveFighterCount", p.maxReactiveFighterCount)
            || setInt("targetTorpedoSeaplaneCount", p.targetTorpedoSeaplaneCount)
            || setInt("attackNavalSize", p.attackNavalSize)
            || setInt("navalAttackFleetSize", p.navalAttackFleetSize)
            || setInt("saveUpSeconds", p.saveUpSeconds)
            || setInt("failedSiteMemorySeconds", p.failedSiteMemorySeconds)
            || setBool("surplusExpansion", p.surplusExpansion)
            || setScalar("commanderDangerRadius", p.commanderDangerRadius)
            || setBool("commanderStandsItsGround", p.commanderStandsItsGround)
            || setInt("commanderFightsUpToMetal", p.commanderFightsUpToMetal)
            || setInt("commanderRetreatBelowPercent", p.commanderRetreatBelowPercent)
            || setBool("retreatDamagedUnits", p.retreatDamagedUnits)
            || setInt("retreatRaiderBelowPercent", p.retreatRaiderBelowPercent)
            || setInt("retreatLineBelowPercent", p.retreatLineBelowPercent)
            || setInt("rejoinAbovePercent", p.rejoinAbovePercent)
            || setInt("mendWaitSeconds", p.mendWaitSeconds)
            || setBool("mendNeedsMender", p.mendNeedsMender)
            || setScalar("mendMaxWalkHome", p.mendMaxWalkHome)
            || setScalar("mendStandRadius", p.mendStandRadius)
            || setScalar("claimedSiteRadius", p.claimedSiteRadius)
            || setInt("finishBuildAbovePercent", p.finishBuildAbovePercent)
            || setBool("resumeAfterBackingOff", p.resumeAfterBackingOff)
            || setScalar("mendHavenRadius", p.mendHavenRadius)
            || setBool("mendDamagedUnits", p.mendDamagedUnits)
            || setInt("mendBelowPercent", p.mendBelowPercent)
            || setScalar("mendRadius", p.mendRadius)
            || setBool("commanderUsesDgun", p.commanderUsesDgun)
            || setBool("commanderKeepsFrames", p.commanderKeepsFrames)
            || setBool("builderSafety", p.builderSafety)
            || setScalar("builderSafetyMargin", p.builderSafetyMargin)
            || setScalar("builderSafetyCoverRadius", p.builderSafetyCoverRadius)
            || setFloat("builderSafetyProtectionRatio", p.builderSafetyProtectionRatio)
            || setInt("builderShelterSeconds", p.builderShelterSeconds)
            || setInt("commanderFrameAbsenceSeconds", p.commanderFrameAbsenceSeconds)
            || setScalar("commanderFrameCoverRadius", p.commanderFrameCoverRadius)
            || setScalar("commanderFrameHandoverRadius", p.commanderFrameHandoverRadius)
            || setScalar("commanderAssistRadius", p.commanderAssistRadius)
            || setScalar("commanderLeashRadius", p.commanderLeashRadius)
            || setBool("commanderPrefersNearSites", p.commanderPrefersNearSites)
            || setBool("commanderMends", p.commanderMends)
            || setBool("firstDefencesOnPerimeter", p.firstDefencesOnPerimeter)
            || setInt("perimeterDefenceCount", p.perimeterDefenceCount)
            || setScalar("perimeterDefenceMargin", p.perimeterDefenceMargin)
            || setBool("outpostTowersLeftToConstructors", p.outpostTowersLeftToConstructors)
            || setScalar("commanderGuardRadius", p.commanderGuardRadius)
            || setFloat("radarWarningRings", p.radarWarningRings)
            || setInt("radarWarningMinContacts", p.radarWarningMinContacts)
            || setInt("navalStalledAttackSeconds", p.navalStalledAttackSeconds)
            || setScalar("builderAvoidsContestedRadius", p.builderAvoidsContestedRadius)
            || setScalar("shipyardSpacing", p.shipyardSpacing)
            || setInt("surplusFactoryIncomeStep", p.surplusFactoryIncomeStep)
            || setInt("surplusFactoryCap", p.surplusFactoryCap)
            || setInt("surplusFleetMultiplier", p.surplusFleetMultiplier)
            || setInt("surplusBomberMultiplier", p.surplusBomberMultiplier)
            || setBool("extractorUpgrades", p.extractorUpgrades)
            || setFloat("extractorUpgradeMinMetalFraction", p.extractorUpgradeMinMetalFraction)
            || setInt("extractorUpgradeTimeoutSeconds", p.extractorUpgradeTimeoutSeconds)
            || setInt("buildPlannerTickInterval", p.buildPlannerTickInterval)
            || setInt("scoutTickInterval", p.scoutTickInterval)
            || setInt("tacticalTickInterval", p.tacticalTickInterval)
            || setInt("attackArmySize", p.attackArmySize)
            || setInt("retreatArmySize", p.retreatArmySize)
            || setInt("attackPatienceSeconds", p.attackPatienceSeconds)
            || setBool("attackInWaves", p.attackInWaves)
            || setBool("holdWhenOutnumbered", p.holdWhenOutnumbered)
            || setInt("commanderDefendsAloneMaxIntruders", p.commanderDefendsAloneMaxIntruders)
            || setBool("reserveAnswersIntruders", p.reserveAnswersIntruders)
            || setBool("noticeProductionHarassment", p.noticeProductionHarassment)
            || setScalar("productionHarassRadius", p.productionHarassRadius)
            || setBool("commanderAnswersHarassment", p.commanderAnswersHarassment)
            || setInt("reinforcementGroupSize", p.reinforcementGroupSize)
            || setBool("raidingParties", p.raidingParties)
            || setInt("raidPartySize", p.raidPartySize)
            || setScalar("raidAvoidBaseRadius", p.raidAvoidBaseRadius)
            || setScalar("attackBaseRadius", p.attackBaseRadius)
            || setBool("huntEnemyCommander", p.huntEnemyCommander)
            || setBool("focusOneEnemy", p.focusOneEnemy)
            || setScalar("focusSwitchMargin", p.focusSwitchMargin)
            || setInt("targetFighterCount", p.targetFighterCount)
            || setInt("targetBomberCount", p.targetBomberCount)
            || setInt("airWorthItEnemyDefences", p.airWorthItEnemyDefences)
            || setInt("targetAirRepairPadCount", p.targetAirRepairPadCount)
            || setInt("targetAdvancedAirPlantCount", p.targetAdvancedAirPlantCount)
            || setInt("targetGunshipCount", p.targetGunshipCount)
            || setInt("gunshipPackSize", p.gunshipPackSize)
            || setScalar("gunshipSupportRadius", p.gunshipSupportRadius)
            || setScalar("fighterLeash", p.fighterLeash)
            || setInt("bomberMaxAntiAirCover", p.bomberMaxAntiAirCover)
            || setScalar("bomberHomeDefenseRadius", p.bomberHomeDefenseRadius)
            || setScalar("bomberClusterRadius", p.bomberClusterRadius)
            || setInt("bomberMinClusterSize", p.bomberMinClusterSize)
            || setFloat("bomberFactoryWeight", p.bomberFactoryWeight)
            || setFloat("bomberCoverPenalty", p.bomberCoverPenalty)
            || setScalar("bomberSortieScale", p.bomberSortieScale)
            || setScalar("bomberLeaveToArmyRadius", p.bomberLeaveToArmyRadius)
            || setFloat("bomberArmyReachDiscount", p.bomberArmyReachDiscount)
            || setInt("targetAirConstructorCount", p.targetAirConstructorCount)
            || setScalar("battlefieldReclaimRadius", p.battlefieldReclaimRadius)
            || setInt("battlefieldReclaimEscortCount", p.battlefieldReclaimEscortCount)
            || setInt("battlefieldReclaimBatch", p.battlefieldReclaimBatch)
            || setScalar("waveCohesionRadius", p.waveCohesionRadius)
            || setScalar("waveMeetEnemyRadius", p.waveMeetEnemyRadius)
            || setInt("waveMeetEnemyCount", p.waveMeetEnemyCount)
            || setBool("spreadDefences", p.spreadDefences)
            || setBool("commanderUsesOwnReachability", p.commanderUsesOwnReachability)
            || setBool("navalScouting", p.navalScouting)
            || setBool("defenceFacesRecentLosses", p.defenceFacesRecentLosses)
            || setInt("defenceValueMaxPaybackSeconds", p.defenceValueMaxPaybackSeconds)
            || setInt("outpostDefenceValueSecondsPerExtractor", p.outpostDefenceValueSecondsPerExtractor)
            || setInt("buildSiteGuardSize", p.buildSiteGuardSize)
            || setScalar("buildSiteGuardMinDistance", p.buildSiteGuardMinDistance)
            || setInt("buildSiteGuardTimeoutSeconds", p.buildSiteGuardTimeoutSeconds)
            || setBool("cheatModeOmniscient", p.cheatModeOmniscient)
            || setInt("targetMemoryTicks", p.targetMemoryTicks)
            || setScalar("defenceDistanceFromBase", p.defenceDistanceFromBase)
            || setScalar("radarDistanceFromBase", p.radarDistanceFromBase)
            || setScalar("nearMexSearchRadius", p.nearMexSearchRadius)
            || setScalar("maxMexSearchRadius", p.maxMexSearchRadius)
            || setScalar("buildSiteFallbackRadius", p.buildSiteFallbackRadius)
            || setScalar("expansionMexSearchRadius", p.expansionMexSearchRadius)
            || setScalar("commanderMexSearchRadius", p.commanderMexSearchRadius)
            || setScalar("mexAvoidsEnemyGunsRadius", p.mexAvoidsEnemyGunsRadius)
            || setBool("expansionNeedsExploredGround", p.expansionNeedsExploredGround)
            || setBool("expansionStaysOnOurSide", p.expansionStaysOnOurSide)
            || setScalar("defendRadius", p.defendRadius)
            || setScalar("engageRadius", p.engageRadius)
            || setScalar("rallyDistance", p.rallyDistance)
            || setScalar("navalRallyDistance", p.navalRallyDistance)
            || setBool("armyFerryWantFromMap", p.armyFerryWantFromMap)
            || setInt("ferryLandingSearchSteps", p.ferryLandingSearchSteps)
            || setBool("ferryLandingFan", p.ferryLandingFan)
            || setBool("ferryLandingAvoidsThreat", p.ferryLandingAvoidsThreat)
            || setFloat("ferryLandingThreatRadius", p.ferryLandingThreatRadius)
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
