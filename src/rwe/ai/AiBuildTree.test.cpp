#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/game/BuilderGuisDatabase.h>

namespace rwe
{
    namespace
    {
        /** One page of gadgets, named as the shipped GUI files name them. */
        std::vector<GuiEntry> page(const std::vector<std::string>& names)
        {
            std::vector<GuiEntry> entries;
            for (const auto& name : names)
            {
                GuiEntry entry{};
                entry.common.name = name;
                entries.push_back(entry);
            }
            return entries;
        }
    }

    TEST_CASE("the build tree is read from the shipped build menus", "[ai]")
    {
        // ARMCOM's real pages: the basic economy, then the level-one plants.
        // ARMCK's third page is the one that carries the tech step and the
        // towers the commander has no button for.
        BuilderGuisDatabase guis;
        guis.addBuilderGui(
            "ARMCOM",
            {page({"HEADER", "ARMBUTT", "ARMPREV", "ARMNEXT", "ARMSOLAR", "ARMMEX", "ARMORDERS"}),
                page({"HEADER", "ARMLAB", "ARMVP", "ARMLLT", "ARMRAD", "ARMBLAST"})});
        guis.addBuilderGui(
            "ARMCK",
            {page({"HEADER", "ARMSOLAR", "ARMMEX"}),
                page({"ARMLAB", "ARMLLT", "ARMRAD"}),
                page({"ARMALAB", "ARMHLT", "ARMRL", "ARMGUARD"})});
        // The expansion spells a few unit names in mixed case, and the loader
        // files a unit's pages under the raw name out of its FBI while
        // keying the definitions upper case.
        guis.addBuilderGui("Corckfus", {page({"Armvulc", "CORSOLAR"})});

        std::set<std::string> unitTypes{
            "ARMCOM", "ARMCK", "ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMLLT", "ARMRAD",
            "ARMALAB", "ARMHLT", "ARMRL", "ARMGUARD", "CORSOLAR", "ARMVULC", "CORCKFUS"};

        auto tree = buildTreeFromBuilderGuis(guis, unitTypes);

        SECTION("gadgets that name a unit are build buttons, and the rest are controls")
        {
            REQUIRE(tree.canBuild("ARMCOM", "ARMSOLAR"));
            REQUIRE(tree.canBuild("ARMCOM", "ARMLAB"));
            REQUIRE_FALSE(tree.canBuild("ARMCOM", "ARMORDERS"));
            REQUIRE_FALSE(tree.canBuild("ARMCOM", "HEADER"));
        }

        SECTION("the commander cannot build the tech step, the towers, or anti-air")
        {
            // The fault this table exists to fix: the AI had the commander
            // putting up Defenders, which no player can do.
            REQUIRE_FALSE(tree.canBuild("ARMCOM", "ARMRL"));
            REQUIRE_FALSE(tree.canBuild("ARMCOM", "ARMALAB"));
            REQUIRE_FALSE(tree.canBuild("ARMCOM", "ARMHLT"));
            REQUIRE_FALSE(tree.canBuild("ARMCOM", "ARMGUARD"));
        }

        SECTION("the construction kbot can, on its third page")
        {
            REQUIRE(tree.canBuild("ARMCK", "ARMALAB"));
            REQUIRE(tree.canBuild("ARMCK", "ARMRL"));
            REQUIRE(tree.canBuild("ARMCK", "ARMHLT"));
            REQUIRE(tree.canBuild("ARMCK", "ARMGUARD"));
        }

        SECTION("names are matched without regard to case, either side")
        {
            REQUIRE(tree.knows("CORCKFUS"));
            REQUIRE(tree.canBuild("CORCKFUS", "ARMVULC"));
            REQUIRE(tree.canBuild("corckfus", "armvulc"));
        }

        SECTION("a builder the data says nothing about is allowed anything")
        {
            // Missing GUI files must not produce an AI that sits still.
            REQUIRE_FALSE(tree.knows("MODCON"));
            REQUIRE(tree.canBuild("MODCON", "ARMFUS"));
        }
    }
}
