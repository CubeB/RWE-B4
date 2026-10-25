#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/io/tdf/tdf.h>

namespace rwe
{
    TEST_CASE("download AI options accept records without human button coordinates", "[ai][mods][download]")
    {
        // Actual GOLD10.2.0 armaaa.tdf relationships, DOWNLOAD-LINK-01.
        AiBuildTree tree;
        tree.buildableBy["ARMACV"] = {"ARMMEX"};
        tree.buildableBy["ARMACH"] = {"ARMMEX"};
        const std::set<std::string> types{"ARMACV", "ARMACH", "ARMAAA", "ARMMEX"};
        const auto blocks = parseListTdfFromString(
            "[MENUENTRY1]{UNITMENU=BLANK;UNITNAME=ARMAAA;}"
            "[MENUENTRY2]{UNITMENU=BLANK;UNITNAME=ARMAAA;}"
            "[MENUENTRY3]{UNITMENU=armacv;UNITNAME=armaaa;}"
            "[MENUENTRY4]{UNITMENU=ARMACH;UNITNAME=ARMAAA;}");
        REQUIRE(addDownloadBuildOptions(tree, blocks, types) == 2);
        CHECK(tree.canBuild("ARMACV", "ARMAAA"));
        CHECK(tree.canBuild("ARMACH", "ARMAAA"));
        CHECK(tree.canBuild("ARMACV", "ARMMEX"));
        CHECK_FALSE(tree.knows("BLANK"));
        CHECK(addDownloadBuildOptions(tree, blocks, types) == 0);

        const auto missing = parseListTdfFromString("[X]{UNITMENU=ARMACV;UNITNAME=MISSING;}");
        CHECK(addDownloadBuildOptions(tree, missing, types) == 0);
        CHECK_FALSE(tree.canBuild("ARMACV", "MISSING"));
    }

    TEST_CASE("mission caps constrain even unknown builders and zero weights forbid a type", "[ai][mission-profile]")
    {
        AiBuildTree tree;
        tree.missionRules["ARMCK"] = ClassicTypeEntry{100, 1};
        tree.missionRules["ARMBRTHA"] = ClassicTypeEntry{100, 0};
        tree.missionRules["ARMZEUS"] = ClassicTypeEntry{25, -1};
        CHECK(tree.canBuild("ARMLAB", "armck"));
        CHECK_FALSE(tree.canBuild("UNKNOWN", "ARMBRTHA"));
        tree.missionCommitted["ARMCK"] = 1;
        CHECK_FALSE(tree.canBuild("ARMLAB", "ARMCK"));
        tree.missionCommitted.clear();
        CHECK(tree.canBuild("ARMLAB", "ARMCK"));
        CHECK(tree.missionWeight("ARMZEUS") == 25);
        tree.missionRules["ARMZEUS"].multiplier = 0;
        CHECK_FALSE(tree.missionAllows("ARMZEUS"));
        tree.missionRules["ARMZEUS"] = ClassicTypeEntry{100, -2};
        CHECK_FALSE(tree.missionAllows("ARMZEUS"));
    }
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
