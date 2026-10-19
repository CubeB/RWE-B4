#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    // BT-64, static reference 97d7ac59. Relevant keys transcribed from the
    // shipped totala1.hpi FBI files, not derived from the water-yard letters.
    TEST_CASE("shipped shipyards keep water yards without the Floater flag", "[floater][fbi]")
    {
        MovementClassDatabase movementClasses;
        for (const auto* keys : {
                 "UnitName=ARMSY; WaterLine=1;",
                 "UnitName=CORSY; WaterLine=22;"})
        {
            auto fbi = parseUnitFbi(parseTdfFromString(
                std::string("[UNITINFO] { Objectname=model; SoundCategory=NOSOUND; ")
                + keys
                + "FootprintX=8; FootprintZ=8; BMcode=0;"
                  "YardMap=wCCCCCCw CCCCCCCC CCCCCCCC CCCCCCCC CCCCCCCC CCCCCCCC CCCCCCCC wCCCCCCw; }"));
            auto definition = parseUnitDefinition(fbi, movementClasses);
            INFO(fbi.unitName);
            REQUIRE_FALSE(definition.floater);
            REQUIRE_FALSE(definition.isMobile);
            REQUIRE(definition.waterLine == fbi.waterLine);
            REQUIRE(definition.yardMap.has_value());
            REQUIRE(definition.yardMap->any(isWater));
        }
    }

    TEST_CASE("the shipped Skeeter takes Floater from its explicit FBI key", "[floater][fbi]")
    {
        auto fbi = parseUnitFbi(parseTdfFromString(
            "[UNITINFO] { UnitName=ARMPT; Objectname=model; SoundCategory=NOSOUND;"
            "FootprintX=4; FootprintZ=4; BMcode=1; MovementClass=BOATS4; Floater=1; }"));
        MovementClassDatabase movementClasses;
        auto definition = parseUnitDefinition(fbi, movementClasses);
        REQUIRE(definition.floater);
        REQUIRE(definition.isMobile);
        REQUIRE(fbi.yardMap.empty());
    }
}
