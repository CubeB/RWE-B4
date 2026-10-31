#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/cob/CobEnvironment.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/FactoryConstruction.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/Veterancy.h>
#include <rwe/sim/cob.h>
#include <rwe/sim/sim_test_util.h>
#include <string>
#include <vector>

/**
 * Community Patch (#934), TADR's per-unit veterancy keys.
 *
 * `VeterancyThresholds` sets the kills needed for levels 1..N and
 * `VeterancyAccuracyBuffRate` sets how fast the accuracy divisor climbs. The
 * defaults are the original's own, so a unit that names neither must keep the
 * old arithmetic to the bit in both modes; the keys are read only under
 * `--community-patch`, and they are data rather than per-unit state.
 */
namespace rwe
{
    namespace
    {
        UnitFbi veterancyFbi(const std::string& keys)
        {
            return parseUnitFbi(parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=ARMSOLAR;\nObjectname=ARMSOLAR;\nSoundCategory=NOSOUND;\n" + keys + "\n}\n"));
        }

        /** TADR's own example: 6 12 18 24 30. */
        VeterancyDefinition customThresholds()
        {
            VeterancyDefinition rules{};
            REQUIRE(parseVeterancyThresholds("6 12 18 24 30", rules));
            return rules;
        }

        VeterancyDefinition withRate(unsigned int rate)
        {
            VeterancyDefinition rules{};
            rules.accuracyBuffRate = rate;
            return rules;
        }

        /** A unit type that just stands there and takes the hit. */
        UnitDefinition makeTargetDefinition(unsigned int hitPoints)
        {
            UnitDefinition d{};
            d.objectName = "model";
            d.isMobile = true;
            d.canMove = false;
            d.maxHitPoints = hitPoints;
            d.buildTime = 0u;
            d.movementCollisionInfo = UnitDefinition::AdHocMovementClass{1u, 1u, 255u, 255u, 0u, 0u};
            return d;
        }

        UnitId spawnTarget(GameSimulation& sim, const std::string& unitType, PlayerId owner, const SimVector& position)
        {
            auto id = addUnitOfType(sim, unitType, owner, position, makeEmptyCobScript());
            sim.getUnitState(id).hitPoints = sim.unitDefinitions.at(unitType).maxHitPoints;
            return id;
        }

        unsigned int damageTaken(GameSimulation& sim, UnitId unitId)
        {
            const auto& unit = sim.getUnitState(unitId);
            return sim.unitDefinitions.at(unit.unitType).maxHitPoints - unit.hitPoints;
        }
    }

    TEST_CASE("VeterancyThresholds: an FBI with no keys keeps the original's rules", "[fbi][veterancy]")
    {
        auto fbi = veterancyFbi("");
        REQUIRE(fbi.veterancy.thresholdCount == 5u);
        REQUIRE(fbi.veterancy.thresholds[0] == 5u);
        REQUIRE(fbi.veterancy.thresholds[1] == 10u);
        REQUIRE(fbi.veterancy.thresholds[2] == 15u);
        REQUIRE(fbi.veterancy.thresholds[3] == 20u);
        REQUIRE(fbi.veterancy.thresholds[4] == 25u);
        REQUIRE(fbi.veterancy.accuracyBuffRate == 12u);

        MovementClassDatabase movementClasses;
        auto definition = parseUnitDefinition(fbi, movementClasses);
        REQUIRE(definition.veterancy.thresholdCount == 5u);
        REQUIRE(definition.veterancy.thresholds[4] == 25u);
        REQUIRE(definition.veterancy.accuracyBuffRate == 12u);

        // Nothing else about the unit moves.
        REQUIRE(definition.objectName == "ARMSOLAR");
        REQUIRE(definition.maxHitPoints == fbi.maxDamage);
    }

    TEST_CASE("VeterancyThresholds: both keys are parsed and copied to the definition", "[fbi][veterancy]")
    {
        auto fbi = veterancyFbi("VeterancyThresholds=6 12 18 24 30;\nVeterancyAccuracyBuffRate=3;");
        REQUIRE(fbi.veterancy.thresholdCount == 5u);
        REQUIRE(fbi.veterancy.thresholds[0] == 6u);
        REQUIRE(fbi.veterancy.thresholds[4] == 30u);
        REQUIRE(fbi.veterancy.accuracyBuffRate == 3u);

        MovementClassDatabase movementClasses;
        auto definition = parseUnitDefinition(fbi, movementClasses);
        REQUIRE(definition.veterancy.thresholdCount == 5u);
        REQUIRE(definition.veterancy.thresholds[2] == 18u);
        REQUIRE(definition.veterancy.accuracyBuffRate == 3u);
    }

    TEST_CASE("VeterancyThresholds: a malformed value falls back to the default", "[fbi][veterancy]")
    {
        struct Case
        {
            const char* keys;
            const char* why;
        };
        const Case cases[] = {
            {"VeterancyThresholds=;", "empty"},
            {"VeterancyThresholds=6 6 12;", "not strictly increasing"},
            {"VeterancyThresholds=6 12 12;", "repeated"},
            {"VeterancyThresholds=6 twelve;", "not a number"},
            {"VeterancyThresholds=-6 12;", "signed"},
            {"VeterancyThresholds=6 12 99999999999;", "overflows a word"},
        };

        for (const auto& c : cases)
        {
            CAPTURE(c.why);
            auto fbi = veterancyFbi(c.keys);
            REQUIRE(fbi.veterancy.thresholdCount == 5u);
            REQUIRE(fbi.veterancy.thresholds[0] == 5u);
            REQUIRE(fbi.veterancy.thresholds[4] == 25u);
            REQUIRE(fbi.veterancy.accuracyBuffRate == 12u);
        }

        // More thresholds than RWE will hold are refused as a whole rather
        // than truncated, because truncating would move the extrapolation
        // step as well.
        std::string tooMany = "VeterancyThresholds=";
        for (unsigned int i = 1; i <= VeterancyDefinition::MaxThresholds + 1; ++i)
        {
            tooMany += std::to_string(i) + " ";
        }
        tooMany += ";";
        auto fbi = veterancyFbi(tooMany);
        REQUIRE(fbi.veterancy.thresholdCount == 5u);
        REQUIRE(fbi.veterancy.thresholds[4] == 25u);
    }

    TEST_CASE("VeterancyAccuracyBuffRate: zero is a value, malformed is not", "[fbi][veterancy]")
    {
        auto zero = veterancyFbi("VeterancyAccuracyBuffRate=0;");
        REQUIRE(zero.veterancy.accuracyBuffRate == 0u);

        auto garbage = veterancyFbi("VeterancyAccuracyBuffRate=fast;");
        REQUIRE(garbage.veterancy.accuracyBuffRate == 12u);

        auto empty = veterancyFbi("VeterancyAccuracyBuffRate=;");
        REQUIRE(empty.veterancy.accuracyBuffRate == 12u);

        // A bad rate does not take the thresholds down with it.
        auto both = veterancyFbi("VeterancyThresholds=6 12 18 24 30;\nVeterancyAccuracyBuffRate=x;");
        REQUIRE(both.veterancy.thresholdCount == 5u);
        REQUIRE(both.veterancy.thresholds[0] == 6u);
        REQUIRE(both.veterancy.accuracyBuffRate == 12u);
    }

    TEST_CASE("veterancyLevel: the default ladder is the original's kills / 5", "[veterancy]")
    {
        const auto& rules = defaultVeterancy();
        REQUIRE(veterancyLevel(rules, 0) == 0u);
        REQUIRE(veterancyLevel(rules, 4) == 0u);
        REQUIRE(veterancyLevel(rules, 5) == 1u);
        REQUIRE(veterancyLevel(rules, 9) == 1u);
        REQUIRE(veterancyLevel(rules, 10) == 2u);
        REQUIRE(veterancyLevel(rules, 24) == 4u);
        REQUIRE(veterancyLevel(rules, 25) == 5u);
        REQUIRE(veterancyLevel(rules, 100) == 5u);
    }

    TEST_CASE("veterancyLevel: 6 12 18 24 30 moves every boundary", "[veterancy]")
    {
        auto rules = customThresholds();
        REQUIRE(veterancyLevel(rules, 0) == 0u);
        REQUIRE(veterancyLevel(rules, 5) == 0u);
        REQUIRE(veterancyLevel(rules, 6) == 1u);
        REQUIRE(veterancyLevel(rules, 11) == 1u);
        REQUIRE(veterancyLevel(rules, 12) == 2u);
        REQUIRE(veterancyLevel(rules, 17) == 2u);
        REQUIRE(veterancyLevel(rules, 18) == 3u);
        REQUIRE(veterancyLevel(rules, 24) == 4u);
        REQUIRE(veterancyLevel(rules, 30) == 5u);
        REQUIRE(veterancyLevel(rules, 1000) == 5u);
    }

    TEST_CASE("veterancyLevelUnbounded: capture cost extrapolates past the last threshold", "[veterancy]")
    {
        // The default list still gives kills / 5, which is the original's
        // capture arithmetic, and it has no ceiling.
        const auto& rules = defaultVeterancy();
        REQUIRE(veterancyLevelUnbounded(rules, 24) == 4u);
        REQUIRE(veterancyLevelUnbounded(rules, 25) == 5u);
        REQUIRE(veterancyLevelUnbounded(rules, 29) == 5u);
        REQUIRE(veterancyLevelUnbounded(rules, 30) == 6u);
        REQUIRE(veterancyLevelUnbounded(rules, 100) == 20u);

        // 6 12 18 24 30 continues in sixes.
        auto custom = customThresholds();
        REQUIRE(veterancyLevelUnbounded(custom, 29) == 4u);
        REQUIRE(veterancyLevelUnbounded(custom, 30) == 5u);
        REQUIRE(veterancyLevelUnbounded(custom, 36) == 6u);
        REQUIRE(veterancyLevelUnbounded(custom, 60) == 10u);

        // A single threshold extrapolates with itself.
        VeterancyDefinition single{};
        REQUIRE(parseVeterancyThresholds("10", single));
        REQUIRE(veterancyLevelUnbounded(single, 9) == 0u);
        REQUIRE(veterancyLevelUnbounded(single, 10) == 1u);
        REQUIRE(veterancyLevelUnbounded(single, 20) == 2u);
        REQUIRE(veterancyLevelUnbounded(single, 30) == 3u);
    }

    TEST_CASE("veterancyAccuracyDivisor: the rate replaces twelve, and zero disables it", "[veterancy][accuracy]")
    {
        const auto& original = defaultVeterancy();
        REQUIRE(veterancyAccuracyDivisor(original, 23) == 1u);
        REQUIRE(veterancyAccuracyDivisor(original, 24) == 2u);
        REQUIRE(veterancyAccuracyDivisor(original, 36) == 3u);
        // The count is read as a word, as the original reads it.
        REQUIRE(veterancyAccuracyDivisor(original, 65548) == 1u);

        auto rate3 = withRate(3);
        REQUIRE(veterancyAccuracyDivisor(rate3, 2) == 0u);
        REQUIRE(veterancyAccuracyDivisor(rate3, 3) == 1u);
        REQUIRE(veterancyAccuracyDivisor(rate3, 6) == 2u);
        REQUIRE(veterancyAccuracyDivisor(rate3, 30) == 10u);

        auto off = withRate(0);
        REQUIRE(veterancyAccuracyDivisor(off, 0) == 0u);
        REQUIRE(veterancyAccuracyDivisor(off, 1000) == 0u);
    }

    TEST_CASE("the accuracy cone follows the unit's rate and its default is unchanged", "[veterancy][accuracy]")
    {
        const SimAngle accuracy(800);

        // No keys: twenty-three kills buy nothing, twenty-four halve it.
        for (unsigned int kills = 0; kills < 24; ++kills)
        {
            REQUIRE(computeAccuracyCone(accuracy, 1400, 1400, kills, defaultVeterancy()) == accuracy);
        }
        REQUIRE(computeAccuracyCone(accuracy, 1400, 1400, 24, defaultVeterancy()) == SimAngle(400));

        // Rate 3 halves at six kills, not at twenty-four.
        auto rate3 = withRate(3);
        REQUIRE(computeAccuracyCone(accuracy, 1400, 1400, 3, rate3) == accuracy);
        REQUIRE(computeAccuracyCone(accuracy, 1400, 1400, 6, rate3) == SimAngle(400));
        REQUIRE(computeAccuracyCone(accuracy, 1400, 1400, 9, rate3) == SimAngle(266));

        // Zero disables the buff however many kills the unit has.
        auto off = withRate(0);
        REQUIRE(computeAccuracyCone(accuracy, 1400, 1400, 1000, off) == accuracy);
    }

    TEST_CASE("target tracking is one level, one kill past the first threshold", "[veterancy][aimlead]")
    {
        const SimVector origin(0_ss, 0_ss, 0_ss);
        const SimVector spot(0_ss, 0_ss, 300_ss);
        const SimVector velocity(2_ss, 0_ss, 0_ss);
        const SimScalar weaponVelocity(10.0f);

        // The original: five kills is still a rookie, six leads.
        REQUIRE(computeAimLead(origin, spot, velocity, weaponVelocity, false, 5, defaultVeterancy()).x == 0_ss);
        REQUIRE(computeAimLead(origin, spot, velocity, weaponVelocity, false, 6, defaultVeterancy()).x > 0_ss);

        // With a first threshold of six the step moves to seven, which is
        // TADR's own `kills > thresholds[0]` comparison.
        auto rules = customThresholds();
        REQUIRE(computeAimLead(origin, spot, velocity, weaponVelocity, false, 6, rules).x == 0_ss);
        REQUIRE(computeAimLead(origin, spot, velocity, weaponVelocity, false, 7, rules).x > 0_ss);
    }

    TEST_CASE("damage dealt and taken use the unit's own thresholds", "[veterancy][damage]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto us = addPlayer(sim, "us");
        auto them = addPlayer(sim, "them");

        auto custom = makeTargetDefinition(1000000u);
        custom.veterancy = customThresholds();
        sim.unitDefinitions["custom"] = custom;
        sim.unitDefinitions["original"] = makeTargetDefinition(1000000u);

        auto attacker = spawnTarget(sim, "custom", us, SimVector(32_ss, 0_ss, 32_ss));
        sim.communityPatchMode = true;

        SECTION("damage dealt stops at the number of thresholds")
        {
            struct Case
            {
                unsigned int kills;
                unsigned int expected;
            };
            const Case cases[] = {
                {5, 1000},
                {6, 1060},
                {11, 1060},
                {12, 1120},
                {30, 1300},
                {1000, 1300},
            };
            for (const auto& c : cases)
            {
                sim.getUnitState(attacker).kills = c.kills;
                auto victim = spawnTarget(sim, "original", them, SimVector(0_ss, 0_ss, 0_ss));
                sim.applyDamage(victim, 1000, attacker);
                REQUIRE(damageTaken(sim, victim) == c.expected);
                sim.quietlyKillUnit(victim);
                sim.deleteDeadUnits();
            }
        }

        SECTION("damage taken stops at the number of thresholds")
        {
            struct Case
            {
                unsigned int kills;
                unsigned int expected;
            };
            const Case cases[] = {
                {5, 1000},
                {6, 960},
                {12, 920},
                {30, 800},
                {1000, 800},
            };
            for (const auto& c : cases)
            {
                auto victim = spawnTarget(sim, "custom", them, SimVector(0_ss, 0_ss, 0_ss));
                sim.getUnitState(victim).kills = c.kills;
                sim.applyDamage(victim, 1000);
                REQUIRE(damageTaken(sim, victim) == c.expected);
                sim.quietlyKillUnit(victim);
                sim.deleteDeadUnits();
            }
        }

        SECTION("mode off keeps the original's five-kill ladder")
        {
            sim.communityPatchMode = false;

            // Ten kills is level two for the original and level one for 6 12.
            sim.getUnitState(attacker).kills = 10;
            auto victim = spawnTarget(sim, "original", them, SimVector(0_ss, 0_ss, 0_ss));
            sim.applyDamage(victim, 1000, attacker);
            REQUIRE(damageTaken(sim, victim) == 1120);

            auto customVictim = spawnTarget(sim, "custom", them, SimVector(0_ss, 0_ss, 0_ss));
            sim.getUnitState(customVictim).kills = 10;
            sim.applyDamage(customVictim, 1000);
            REQUIRE(damageTaken(sim, customVictim) == 920);
        }
    }

    TEST_CASE("reload time uses the unit's own thresholds, capped at sixteen levels", "[veterancy][reload]")
    {
        const auto reload = SimScalar(1.2f);
        const unsigned int hitPoints = 3000;

        auto custom = customThresholds();
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 25, custom) == GameTime(27));
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 30, custom) == GameTime(25));
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 1000, custom) == GameTime(25));

        // The original's ladder is untouched.
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 25, defaultVeterancy()) == GameTime(25));
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 100, defaultVeterancy()) == GameTime(25));

        // Mode off resolves a custom unit back to the original's rules.
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 25, veterancyRules(custom, false)) == GameTime(25));

        // TADR caps reload at sixteen levels even when the list is longer.
        VeterancyDefinition many{};
        std::string text;
        for (unsigned int i = 1; i <= 17; ++i)
        {
            text += std::to_string(i) + " ";
        }
        REQUIRE(parseVeterancyThresholds(text, many));
        REQUIRE(computeReloadTicks(reload, hitPoints, hitPoints, 17, many) == GameTime(1));
    }

    TEST_CASE("capture time uses the target's own thresholds with no ceiling", "[veterancy][capture]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto them = addPlayer(sim, "them");

        // Zero build cost gives the 150-tick floor, so the veterancy factor
        // is the only thing that moves.
        auto definition = makeTargetDefinition(1000u);
        definition.buildCostEnergy = Energy(0.0f);
        definition.buildCostMetal = Metal(0.0f);
        definition.veterancy = customThresholds();
        sim.unitDefinitions["custom"] = definition;
        sim.communityPatchMode = true;

        auto target = spawnTarget(sim, "custom", them, SimVector(0_ss, 0_ss, 0_ss));

        // Ten kills is level one for 6 12, level two for the original.
        sim.getUnitState(target).kills = 10;
        REQUIRE(sim.computeCaptureTime(sim.getUnitState(target)) == 165u);

        sim.getUnitState(target).kills = 12;
        REQUIRE(sim.computeCaptureTime(sim.getUnitState(target)) == 180u);

        // Past the last threshold, six more kills are one more level.
        sim.getUnitState(target).kills = 36;
        REQUIRE(sim.computeCaptureTime(sim.getUnitState(target)) == 240u);

        // The default list at ten kills is still the original's.
        auto original = makeTargetDefinition(1000u);
        original.buildCostEnergy = Energy(0.0f);
        original.buildCostMetal = Metal(0.0f);
        sim.unitDefinitions["original"] = original;
        auto originalTarget = spawnTarget(sim, "original", them, SimVector(0_ss, 0_ss, 0_ss));
        sim.getUnitState(originalTarget).kills = 10;
        REQUIRE(sim.computeCaptureTime(sim.getUnitState(originalTarget)) == 180u);

        // Mode off ignores the custom unit's keys.
        sim.communityPatchMode = false;
        sim.getUnitState(target).kills = 10;
        REQUIRE(sim.computeCaptureTime(sim.getUnitState(target)) == 180u);
    }

    TEST_CASE("reclaim work uses the reclaimer's own thresholds", "[veterancy][reclaim]")
    {
        auto custom = customThresholds();

        // Ten kills is level one for 6 12 (factor two), level two for the
        // original (factor three).
        REQUIRE(computeUnitReclaimStep(300u, 10u, 1000u, Metal(150.0f), custom) == 200u);
        REQUIRE(computeUnitReclaimStep(300u, 10u, 1000u, Metal(150.0f), defaultVeterancy()) == 300u);

        // Unbounded: thirty-six kills is level six, so the factor is seven.
        REQUIRE(computeUnitReclaimStep(300u, 36u, 1000u, Metal(150.0f), custom) == 700u);

        // Mode off resolves back to the original's ladder.
        REQUIRE(computeUnitReclaimStep(300u, 10u, 1000u, Metal(150.0f), veterancyRules(custom, false)) == 300u);
    }

    TEST_CASE("COB VeteranLevel answers with the unit's own thresholds", "[veterancy][cob]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto owner = addPlayer(sim, "owner");

        auto definition = makeTargetDefinition(1000u);
        definition.veterancy = customThresholds();
        sim.unitDefinitions["custom"] = definition;
        sim.unitDefinitions["original"] = makeTargetDefinition(1000u);
        sim.communityPatchMode = true;

        auto customUnit = spawnTarget(sim, "custom", owner, SimVector(0_ss, 0_ss, 0_ss));
        auto& unit = sim.getUnitState(customUnit);
        const auto& env = *unit.cobEnvironment;
        auto query = [&]() {
            return handleQuery(sim, env, customUnit, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}});
        };

        unit.kills = 5;
        REQUIRE(query() == 0);
        unit.kills = 6;
        REQUIRE(query() == 1);
        unit.kills = 12;
        REQUIRE(query() == 2);
        unit.kills = 30;
        REQUIRE(query() == 5);
        unit.kills = 1000;
        REQUIRE(query() == 5);

        // The original's ladder is unchanged.
        auto originalUnit = spawnTarget(sim, "original", owner, SimVector(0_ss, 0_ss, 0_ss));
        auto& original = sim.getUnitState(originalUnit);
        original.kills = 5;
        REQUIRE(handleQuery(sim, *original.cobEnvironment, originalUnit, CobEnvironment::QueryStatus{CobEnvironment::QueryStatus::VeteranLevel{}}) == 1);

        // Mode off ignores the custom unit's keys.
        sim.communityPatchMode = false;
        unit.kills = 10;
        REQUIRE(query() == 2);
    }

    TEST_CASE("endgame damage keeps the victim's own damage-taken tier", "[veterancy][endgame]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "player");

        auto definition = makeTargetDefinition(50000u);
        definition.veterancy = customThresholds();
        sim.unitDefinitions["custom"] = definition;
        sim.unitDefinitions["original"] = makeTargetDefinition(50000u);
        sim.communityPatchMode = true;

        auto wipe = [&](const std::string& type, unsigned int kills, bool mode) {
            auto id = spawnTarget(sim, type, player, SimVector(0_ss, 0_ss, 0_ss));
            sim.getUnitState(id).kills = kills;
            sim.communityPatchMode = mode;
            sim.wipeCommanderArmy(player);
            auto taken = damageTaken(sim, id);
            sim.quietlyKillUnit(id);
            sim.deleteDeadUnits();
            return taken;
        };

        // Ten kills: level one for 6 12 (24 levels left), level two for the
        // original (23 left). Each level is 1200 points.
        REQUIRE(wipe("custom", 10u, true) == 28800u);
        REQUIRE(wipe("original", 10u, true) == 27600u);
        REQUIRE(wipe("custom", 10u, false) == 27600u);
    }

    TEST_CASE("a cancelled factory product takes its own damage-taken tier", "[veterancy][factory-death]")
    {
        GameSimulation sim(makeFlatTerrain(64, 64), 0u, 0, 0);
        auto player = addPlayer(sim, "player");

        auto factoryDefinition = makeTargetDefinition(1000u);
        factoryDefinition.buildTime = 1000u;
        sim.unitDefinitions["factory"] = factoryDefinition;

        auto productDefinition = makeTargetDefinition(50000u);
        productDefinition.buildTime = 1000u;
        productDefinition.veterancy = customThresholds();
        sim.unitDefinitions["product"] = productDefinition;

        sim.communityPatchMode = true;

        auto factory = spawnTarget(sim, "factory", player, SimVector(0_ss, 0_ss, 0_ss));
        auto product = spawnTarget(sim, "product", player, SimVector(0_ss, 0_ss, 0_ss));
        sim.getUnitState(product).buildTimeCompleted = 500u;
        sim.getUnitState(product).kills = 10u;
        sim.getUnitState(factory).factoryState = FactoryBehaviorStateBuilding{std::make_pair(product, std::nullopt)};

        cancelFactoryProductOnDeath(sim, factory);

        // Level one for 6 12: 24 levels left, 28800 points.
        REQUIRE(damageTaken(sim, product) == 28800u);
    }
}
