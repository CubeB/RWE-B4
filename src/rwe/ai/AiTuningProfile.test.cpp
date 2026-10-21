#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiTuningProfile.h>
#include <set>
#include <string>

namespace rwe
{
    // Every knob listAiKnobs names has to be one applyAiTuning will accept,
    // and by the same value it lists as the default -- the two are read off
    // one table (AiTuningProfile.cpp), and this is what would catch them
    // drifting apart if they ever did.
    TEST_CASE("every listed AI knob can be set by name")
    {
        auto knobs = listAiKnobs();
        REQUIRE(knobs.size() > 100);

        for (const auto& knob : knobs)
        {
            AiTuningProfile p;
            INFO("knob: " << knob.name << " (" << knob.type << ") default " << knob.defaultValue);
            REQUIRE(applyAiTuning(p, knob.name, knob.defaultValue));
        }
    }

    TEST_CASE("listAiKnobs is sorted by name and has no duplicates")
    {
        auto knobs = listAiKnobs();
        for (std::size_t i = 1; i < knobs.size(); ++i)
        {
            REQUIRE(knobs[i - 1].name < knobs[i].name);
        }
    }

    TEST_CASE("listAiKnobs only names int, bool, float or scalar")
    {
        auto knobs = listAiKnobs();
        for (const auto& knob : knobs)
        {
            REQUIRE((knob.type == "int" || knob.type == "bool" || knob.type == "float" || knob.type == "scalar"));
        }
    }

    TEST_CASE("applyAiTuning refuses an unknown knob")
    {
        AiTuningProfile p;
        REQUIRE_FALSE(applyAiTuning(p, "notARealKnobName", "1"));
    }

    TEST_CASE("applyAiTuning sets an int knob by name")
    {
        AiTuningProfile p;
        REQUIRE(applyAiTuning(p, "targetSolarCount", "42"));
        REQUIRE(p.targetSolarCount == 42);
    }

    TEST_CASE("surplus metal maker control is available for paired comparisons")
    {
        AiTuningProfile p;
        CHECK_FALSE(p.metalMakerUseSurplus);
        REQUIRE(applyAiTuning(p, "metalMakerUseSurplus", "true"));
        CHECK(p.metalMakerUseSurplus);
    }

    TEST_CASE("applyAiTuning sets a bool knob by name")
    {
        AiTuningProfile p;
        REQUIRE(applyAiTuning(p, "dgunByValue", "false"));
        REQUIRE(p.dgunByValue == false);
        REQUIRE(applyAiTuning(p, "dgunByValue", "true"));
        REQUIRE(p.dgunByValue == true);
    }

    TEST_CASE("applyAiTuning sets a float knob by name")
    {
        AiTuningProfile p;
        REQUIRE(applyAiTuning(p, "techMinArmyValueRatio", "2.5"));
        REQUIRE(p.techMinArmyValueRatio == 2.5f);
    }

    TEST_CASE("applyAiTuning sets a scalar knob by name")
    {
        AiTuningProfile p;
        REQUIRE(applyAiTuning(p, "commanderLeashRadius", "999"));
        REQUIRE(p.commanderLeashRadius == SimScalar(999.0f));
    }

    // #389: the outpost-raid response's engagement-predictor knobs. Default
    // off/zero so an un-tuned game is unchanged; both have to be settable by
    // name like every other knob for --ai-tune and the arena scripts to
    // reach them.
    TEST_CASE("applyAiTuning sets outpostResponseUsesPredictor by name")
    {
        AiTuningProfile p;
        REQUIRE(p.outpostResponseUsesPredictor == false);
        REQUIRE(applyAiTuning(p, "outpostResponseUsesPredictor", "true"));
        REQUIRE(p.outpostResponseUsesPredictor == true);
    }

    TEST_CASE("applyAiTuning sets outpostResponseMarginThreshold by name")
    {
        AiTuningProfile p;
        REQUIRE(p.outpostResponseMarginThreshold == 0.0f);
        REQUIRE(applyAiTuning(p, "outpostResponseMarginThreshold", "5000"));
        REQUIRE(p.outpostResponseMarginThreshold == 5000.0f);
    }

    // The acquisition-radius comparison for the arena: on by default, and
    // settable by name so a paired run can play the flat engageRadius as its
    // own control.
    TEST_CASE("applyAiTuning sets acquireWeaponRange by name")
    {
        AiTuningProfile p;
        REQUIRE(p.acquireWeaponRange);
        REQUIRE(applyAiTuning(p, "acquireWeaponRange", "false"));
        REQUIRE_FALSE(p.acquireWeaponRange);
        REQUIRE(applyAiTuning(p, "acquireWeaponRange", "true"));
        REQUIRE(p.acquireWeaponRange);
    }

    // The second strength batch: queue depth and the stalled-head drop, and
    // the anti-air escort. All four are on by default and every one restores
    // the old behaviour when set to its off value, which is what a paired
    // arena run uses as its control arm.
    TEST_CASE("the factory queue and anti-air escort knobs are listed and settable")
    {
        AiTuningProfile p;
        CHECK(p.factoryQueueDepth == 1);
        CHECK(p.factoryQueueDropStalled);
        CHECK(p.factoryQueueStallSeconds == 60);
        CHECK(p.antiAirEscortCount == 1);

        REQUIRE(applyAiTuning(p, "factoryQueueDepth", "1"));
        CHECK(p.factoryQueueDepth == 1);
        REQUIRE(applyAiTuning(p, "factoryQueueDropStalled", "false"));
        CHECK_FALSE(p.factoryQueueDropStalled);
        REQUIRE(applyAiTuning(p, "factoryQueueStallSeconds", "0"));
        CHECK(p.factoryQueueStallSeconds == 0);
        REQUIRE(applyAiTuning(p, "antiAirEscortCount", "0"));
        CHECK(p.antiAirEscortCount == 0);

        // And they are in the listed set -- the test at the top of this file
        // would catch a name that applyAiTuning accepted but listAiKnobs did
        // not, but not one that was never added to either.
        std::set<std::string> names;
        for (const auto& knob : listAiKnobs())
        {
            names.insert(knob.name);
        }
        CHECK(names.count("factoryQueueDepth") == 1);
        CHECK(names.count("factoryQueueDropStalled") == 1);
        CHECK(names.count("factoryQueueStallSeconds") == 1);
        CHECK(names.count("antiAirEscortCount") == 1);
    }

    // The third strength batch: the decayed composition memory and the
    // value-odds attack gate. Both are on by default and each has an off
    // value that restores the old command stream, which is the control arm
    // for a paired arena run.
    TEST_CASE("the composition memory and value-odds knobs are listed and settable")
    {
        AiTuningProfile p;
        CHECK(p.enemyCompositionMemory);
        CHECK(p.compositionDecaySeconds == 30);
        CHECK(p.compositionCommitSeconds == 60);
        CHECK(p.compositionLeanMax == 2);
        CHECK(p.attackUsesValueOdds);
        CHECK(p.attackValueRatio == 1.1f);
        CHECK(p.retreatValueRatio == 0.7f);

        REQUIRE(applyAiTuning(p, "enemyCompositionMemory", "false"));
        CHECK_FALSE(p.enemyCompositionMemory);
        REQUIRE(applyAiTuning(p, "compositionDecaySeconds", "45"));
        CHECK(p.compositionDecaySeconds == 45);
        REQUIRE(applyAiTuning(p, "compositionCommitSeconds", "90"));
        CHECK(p.compositionCommitSeconds == 90);
        REQUIRE(applyAiTuning(p, "compositionLeanMax", "3"));
        CHECK(p.compositionLeanMax == 3);
        REQUIRE(applyAiTuning(p, "attackUsesValueOdds", "false"));
        CHECK_FALSE(p.attackUsesValueOdds);
        REQUIRE(applyAiTuning(p, "attackValueRatio", "1.25"));
        CHECK(p.attackValueRatio == 1.25f);
        REQUIRE(applyAiTuning(p, "retreatValueRatio", "0.8"));
        CHECK(p.retreatValueRatio == 0.8f);

        std::set<std::string> names;
        for (const auto& knob : listAiKnobs())
        {
            names.insert(knob.name);
        }
        CHECK(names.count("enemyCompositionMemory") == 1);
        CHECK(names.count("compositionDecaySeconds") == 1);
        CHECK(names.count("compositionCommitSeconds") == 1);
        CHECK(names.count("compositionLeanMax") == 1);
        CHECK(names.count("attackUsesValueOdds") == 1);
        CHECK(names.count("attackValueRatio") == 1);
        CHECK(names.count("retreatValueRatio") == 1);
    }

    // The fourth batch: the wave reserve and attack-move, and the replay-
    // derived opening posture. The reserve and attack-move are on by default
    // and share one off combination (reserveWave false, waveCommitFraction
    // 1.0, waveAttackMove false). The opening posture is off by default so it
    // can be A/B'd against the ordinary plan.
    TEST_CASE("the wave reserve and opening knobs are listed and settable")
    {
        AiTuningProfile p;
        CHECK(p.reserveWave);
        CHECK(p.waveCommitFraction == 0.7f);
        CHECK(p.reserveReleaseSeconds == 20);
        CHECK(p.waveAttackMove);
        CHECK_FALSE(p.humanlikeOpening);
        CHECK(p.openingFactoryBySeconds == 60);
        CHECK(p.openingConstructorBySeconds == 150);
        CHECK(p.openingExpansionBySeconds == 213);
        CHECK(p.openingExpansionSites == 10);
        CHECK(p.openingAttackArmySize == 7);
        CHECK(p.openingAttackBySeconds == 450);

        REQUIRE(applyAiTuning(p, "reserveWave", "false"));
        CHECK_FALSE(p.reserveWave);
        REQUIRE(applyAiTuning(p, "waveCommitFraction", "1.0"));
        CHECK(p.waveCommitFraction == 1.0f);
        REQUIRE(applyAiTuning(p, "reserveReleaseSeconds", "30"));
        CHECK(p.reserveReleaseSeconds == 30);
        REQUIRE(applyAiTuning(p, "waveAttackMove", "false"));
        CHECK_FALSE(p.waveAttackMove);
        REQUIRE(applyAiTuning(p, "humanlikeOpening", "true"));
        CHECK(p.humanlikeOpening);
        REQUIRE(applyAiTuning(p, "openingFactoryBySeconds", "45"));
        CHECK(p.openingFactoryBySeconds == 45);
        REQUIRE(applyAiTuning(p, "openingConstructorBySeconds", "120"));
        CHECK(p.openingConstructorBySeconds == 120);
        REQUIRE(applyAiTuning(p, "openingExpansionBySeconds", "210"));
        CHECK(p.openingExpansionBySeconds == 210);
        REQUIRE(applyAiTuning(p, "openingExpansionSites", "12"));
        CHECK(p.openingExpansionSites == 12);
        REQUIRE(applyAiTuning(p, "openingAttackArmySize", "6"));
        CHECK(p.openingAttackArmySize == 6);
        REQUIRE(applyAiTuning(p, "openingAttackBySeconds", "420"));
        CHECK(p.openingAttackBySeconds == 420);

        std::set<std::string> names;
        for (const auto& knob : listAiKnobs())
        {
            names.insert(knob.name);
        }
        CHECK(names.count("reserveWave") == 1);
        CHECK(names.count("waveCommitFraction") == 1);
        CHECK(names.count("reserveReleaseSeconds") == 1);
        CHECK(names.count("waveAttackMove") == 1);
        CHECK(names.count("humanlikeOpening") == 1);
        CHECK(names.count("openingFactoryBySeconds") == 1);
        CHECK(names.count("openingConstructorBySeconds") == 1);
        CHECK(names.count("openingExpansionBySeconds") == 1);
        CHECK(names.count("openingExpansionSites") == 1);
        CHECK(names.count("openingAttackArmySize") == 1);
        CHECK(names.count("openingAttackBySeconds") == 1);
    }

    // The sixth batch: directional threat memory and proportional defence.
    // The memory, the proportional response and threat-directed siting are
    // on by default; each off value restores the old single-direction /
    // send-everyone behaviour, which is the control arm for a paired run.
    TEST_CASE("the threat memory and proportional defence knobs are listed and settable")
    {
        AiTuningProfile p;
        CHECK(p.threatMemory);
        CHECK(p.threatMemoryDecaySeconds == 60);
        CHECK(p.proportionalDefence);
        CHECK(p.defenceResponseRatio == 1.3f);
        CHECK(p.defenceResponseMaxShare == 1.0f);
        CHECK(p.defenceResponseMinUnits == 1);
        CHECK(p.recallThreatRatio == 1.5f);
        CHECK(p.threatDirectedDefence);

        REQUIRE(applyAiTuning(p, "threatMemory", "false"));
        CHECK_FALSE(p.threatMemory);
        REQUIRE(applyAiTuning(p, "threatMemoryDecaySeconds", "90"));
        CHECK(p.threatMemoryDecaySeconds == 90);
        REQUIRE(applyAiTuning(p, "proportionalDefence", "false"));
        CHECK_FALSE(p.proportionalDefence);
        REQUIRE(applyAiTuning(p, "defenceResponseRatio", "2.0"));
        CHECK(p.defenceResponseRatio == 2.0f);
        REQUIRE(applyAiTuning(p, "defenceResponseMaxShare", "0.5"));
        CHECK(p.defenceResponseMaxShare == 0.5f);
        REQUIRE(applyAiTuning(p, "defenceResponseMinUnits", "2"));
        CHECK(p.defenceResponseMinUnits == 2);
        REQUIRE(applyAiTuning(p, "recallThreatRatio", "2.5"));
        CHECK(p.recallThreatRatio == 2.5f);
        REQUIRE(applyAiTuning(p, "threatDirectedDefence", "false"));
        CHECK_FALSE(p.threatDirectedDefence);

        std::set<std::string> names;
        for (const auto& knob : listAiKnobs())
        {
            names.insert(knob.name);
        }
        CHECK(names.count("threatMemory") == 1);
        CHECK(names.count("threatMemoryDecaySeconds") == 1);
        CHECK(names.count("proportionalDefence") == 1);
        CHECK(names.count("defenceResponseRatio") == 1);
        CHECK(names.count("defenceResponseMaxShare") == 1);
        CHECK(names.count("defenceResponseMinUnits") == 1);
        CHECK(names.count("recallThreatRatio") == 1);
        CHECK(names.count("threatDirectedDefence") == 1);
    }

    // The seventh batch: the synchronized second axis and the replay-derived
    // outside-economy target. The raid sync is on by default and its off value
    // restores immediate dispatch exactly; the site count is only read with
    // humanlikeOpening on.
    TEST_CASE("the synchronized raid and opening expansion-site knobs are listed and settable")
    {
        AiTuningProfile p;
        CHECK(p.syncRaidWithWave);
        CHECK(p.syncWindowSeconds == 15);
        CHECK(p.openingExpansionSites == 10);

        REQUIRE(applyAiTuning(p, "syncRaidWithWave", "false"));
        CHECK_FALSE(p.syncRaidWithWave);
        REQUIRE(applyAiTuning(p, "syncRaidWithWave", "true"));
        CHECK(p.syncRaidWithWave);
        REQUIRE(applyAiTuning(p, "syncWindowSeconds", "0"));
        CHECK(p.syncWindowSeconds == 0);
        REQUIRE(applyAiTuning(p, "syncWindowSeconds", "30"));
        CHECK(p.syncWindowSeconds == 30);
        REQUIRE(applyAiTuning(p, "openingExpansionSites", "0"));
        CHECK(p.openingExpansionSites == 0);

        std::set<std::string> names;
        for (const auto& knob : listAiKnobs())
        {
            names.insert(knob.name);
        }
        CHECK(names.count("syncRaidWithWave") == 1);
        CHECK(names.count("syncWindowSeconds") == 1);
        CHECK(names.count("openingExpansionSites") == 1);
    }
}
