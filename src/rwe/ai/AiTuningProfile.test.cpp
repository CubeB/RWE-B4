#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiTuningProfile.h>

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
}
