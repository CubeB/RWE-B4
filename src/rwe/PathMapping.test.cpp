#include <catch2/catch_test_macros.hpp>
#include <rwe/PathMapping.h>

namespace rwe
{
    TEST_CASE("manifest resource directories override PathMapping fields", "[setup][modsdk]")
    {
        PathMapping mapping;
        CHECK(applyPathMappingDirectory(mapping, "weapons", "weaponP"));
        CHECK(mapping.weapons == "weaponP");
        CHECK(applyPathMappingDirectory(mapping, "guis", "guiP"));
        CHECK(mapping.guis == "guiP");
        CHECK(applyPathMappingDirectory(mapping, "builderGuis", "guiP"));
        CHECK(mapping.builderGuis == "guiP");
        CHECK(applyPathMappingDirectory(mapping, "units", "UNITSE"));
        CHECK(mapping.units == "UNITSE");

        // The base field the manifest does not name keeps its default.
        CHECK(mapping.bitmaps.empty());

        CHECK_FALSE(applyPathMappingDirectory(mapping, "not-a-root", "x"));
        CHECK(mapping.weapons == "weaponP");
    }
}
