#include <catch2/catch_test_macros.hpp>
#include <rwe/LoadingScene_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/io/fbi/io.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitDefinition.h>
#include <string>

/**
 * Community Patch (#934), TADR `PreviewObject3D`.
 *
 * The key names an alternative 3DO for the build-cursor ghost. It is
 * presentation only: the unit's real Objectname, footprint and build
 * behaviour must not move, and an absent or unreadable preview model must
 * fall back to the ordinary model rather than leaving the cursor bare.
 */
namespace rwe
{
    namespace
    {
        UnitFbi previewFbi(const std::string& extraKeys)
        {
            return parseUnitFbi(parseTdfFromString(
                "[UNITINFO]\n{\nUnitName=ARMSOLAR;\nObjectname=ARMSOLAR;\nSoundCategory=NOSOUND;\n" + extraKeys + "\n}\n"));
        }

        std::unordered_map<std::string, UnitModelDefinition> modelDatabase(const std::vector<std::string>& names)
        {
            std::unordered_map<std::string, UnitModelDefinition> models;
            for (const auto& name : names)
            {
                models.insert({name, UnitModelDefinition{}});
            }
            return models;
        }
    }

    TEST_CASE("PreviewObject3D is parsed and copied to the unit definition", "[fbi][previewobject]")
    {
        auto fbi = previewFbi("PreviewObject3D=corarmvp_preview;");
        REQUIRE(fbi.previewObjectName == "corarmvp_preview");

        MovementClassDatabase movementClasses;
        auto definition = parseUnitDefinition(fbi, movementClasses);

        // The preview key is carried, but the real model is still the real
        // model: no other part of the definition is allowed to move.
        REQUIRE(definition.previewObjectName == "corarmvp_preview");
        REQUIRE(definition.objectName == "ARMSOLAR");
    }

    TEST_CASE("an FBI with no PreviewObject3D leaves the key empty", "[fbi][previewobject]")
    {
        auto fbi = previewFbi("");
        REQUIRE(fbi.previewObjectName.empty());

        MovementClassDatabase movementClasses;
        auto definition = parseUnitDefinition(fbi, movementClasses);
        REQUIRE(definition.previewObjectName.empty());
        REQUIRE(definition.objectName == "ARMSOLAR");
    }

    TEST_CASE("the build-cursor ghost honours PreviewObject3D only under the Community Patch", "[game][previewobject]")
    {
        MovementClassDatabase movementClasses;
        auto definition = parseUnitDefinition(previewFbi("PreviewObject3D=corarmvp_preview;"), movementClasses);
        auto models = modelDatabase({"ARMSOLAR", "CORARMVP_PREVIEW"});

        SECTION("a loaded preview model is used while the mode is on")
        {
            REQUIRE(buildGhostObjectName(definition, models, true) == "CORARMVP_PREVIEW");
        }

        SECTION("the mode off keeps the ordinary model")
        {
            REQUIRE(buildGhostObjectName(definition, models, false) == "ARMSOLAR");
        }

        SECTION("a missing preview model falls back to the ordinary model")
        {
            auto withoutPreview = modelDatabase({"ARMSOLAR"});
            REQUIRE(buildGhostObjectName(definition, withoutPreview, true) == "ARMSOLAR");
        }

        SECTION("no key at all keeps the ordinary model")
        {
            auto withoutKey = parseUnitDefinition(previewFbi(""), movementClasses);
            REQUIRE(buildGhostObjectName(withoutKey, models, true) == "ARMSOLAR");
        }

        SECTION("the real model name is untouched wherever the ghost points")
        {
            REQUIRE(definition.objectName == "ARMSOLAR");
            REQUIRE(buildGhostObjectName(definition, models, true) != definition.objectName);
            REQUIRE(buildGhostObjectName(definition, models, false) == definition.objectName);
        }
    }
}
