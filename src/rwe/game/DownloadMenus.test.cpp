#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/game/DownloadMenus.h>
#include <rwe/io/tdf/tdf.h>

namespace rwe
{
    namespace
    {
        GuiEntry gadget(GuiElementType id, const std::string& name, int x, int y, int w, int h, int attribs)
        {
            GuiEntry entry{};
            entry.common.id = id;
            entry.common.name = name;
            entry.common.xpos = x;
            entry.common.ypos = y;
            entry.common.width = w;
            entry.common.height = h;
            entry.common.attribs = attribs;
            return entry;
        }

        /**
         * A build page as the shipped GUI files lay one out -- ARMACK2.GUI's
         * gadgets, positions and attributes: the header, the side's font
         * gadget, PREV and NEXT, the six 64x64 slots in two columns, ORDERS
         * and BUILD, then the order strip.
         */
        std::vector<GuiEntry> buildPage(const std::string& side, const std::vector<std::string>& slots, int emptySlotAttribs = 32)
        {
            std::vector<GuiEntry> page;
            page.push_back(gadget(GuiElementType::Panel, "HEADER", 0, 128, 128, 352, 0));
            page.push_back(gadget(GuiElementType::Font, side + "BUTT", 0, 0, 0, 0, 0));
            page.push_back(gadget(GuiElementType::Button, side + "PREV", 8, 222, 44, 16, 2));
            page.push_back(gadget(GuiElementType::Button, side + "NEXT", 72, 222, 44, 16, 2));
            const int xs[] = {0, 64, 0, 64, 0, 64};
            const int ys[] = {27, 27, 91, 91, 155, 155};
            for (std::size_t i = 0; i < 6; ++i)
            {
                const auto& name = slots.at(i);
                auto attribs = name == "IGPATCH" ? emptySlotAttribs : 32;
                page.push_back(gadget(GuiElementType::Button, name, xs[i], ys[i], 64, 64, attribs));
            }
            page.push_back(gadget(GuiElementType::Button, side + "ORDERS", 3, 4, 59, 19, 16));
            page.push_back(gadget(GuiElementType::Button, side + "BUILD", 65, 4, 59, 19, 16));
            page.push_back(gadget(GuiElementType::Button, side + "MOVE", 5, 247, 55, 31, 64));
            page.push_back(gadget(GuiElementType::Button, side + "STOP", 64, 247, 55, 31, 0));
            return page;
        }

        std::vector<std::string> slotNames(const std::vector<GuiEntry>& page)
        {
            std::vector<std::string> names;
            for (const auto& g : page)
            {
                if (isBuildSlot(g))
                {
                    names.push_back(g.common.name);
                }
            }
            return names;
        }

        bool hasGadget(const std::vector<GuiEntry>& page, const std::string& name)
        {
            return std::any_of(page.begin(), page.end(), [&](const auto& g) { return g.common.name == name; });
        }

        const GuiEntry& gadgetNamed(const std::vector<GuiEntry>& page, const std::string& name)
        {
            return *std::find_if(page.begin(), page.end(), [&](const auto& g) { return g.common.name == name; });
        }
    }

    TEST_CASE("download menu entries are read from a download TDF", "[download]")
    {
        // Shaped as the shipped download/armuwmex.tdf and armfdrag.tdf are,
        // the second in mixed case, as several shipped files are. The third
        // has no BUTTON and is passed over rather than failing the file.
        auto blocks = parseListTdfFromString(
            "[MENUENTRY1]\n{\nUNITMENU=ARMCS;\nMENU=3;\nBUTTON=5;\nUNITNAME=ARMUWMEX;\n}\n"
            "[MenuEntry2]\n{\nUnitMenu=ARMCS;\nMenu=4;\nButton=0;\nUnitName=ARMFDRAG;\n}\n"
            "[MENUENTRY3]\n{\nUNITMENU=ARMCS;\nMENU=4;\nUNITNAME=ARMFRT;\n}\n");

        auto entries = parseDownloadMenuEntries(blocks);

        REQUIRE(entries.size() == 2);
        REQUIRE(entries[0].builder == "ARMCS");
        REQUIRE(entries[0].unitName == "ARMUWMEX");
        // MENU is the page counted from one, plus one: MENU=3 is the second page.
        REQUIRE(entries[0].page == 1);
        REQUIRE(entries[0].slot == 5);
        REQUIRE(entries[1].unitName == "ARMFDRAG");
        REQUIRE(entries[1].page == 2);
        REQUIRE(entries[1].slot == 0);
    }

    TEST_CASE("a download button fills an empty slot on a page the builder ships", "[download]")
    {
        // ARMACK's second page: three buttons, then three IGPATCH. The v3.1
        // patch's Flakker, fortification wall and Ambusher land on exactly
        // those three at MENU=3, BUTTON=3 to 5.
        std::vector<std::vector<GuiEntry>> pages{
            buildPage("ARM", {"ARMSOLAR", "ARMMEX", "ARMLAB", "ARMVP", "ARMAP", "ARMRAD"}),
            buildPage("ARM", {"ARMANNI", "ARMAMD", "ARMASP", "IGPATCH", "IGPATCH", "IGPATCH"}),
        };

        SECTION("each goes in the slot it names, and the page keeps its own")
        {
            auto result = applyDownloadMenuEntries(pages, {
                {"ARMACK", 1, 3, "ARMFLAK"},
                {"ARMACK", 1, 4, "ARMFORT"},
                {"ARMACK", 1, 5, "ARMAMB"},
            });

            REQUIRE(result.placed == 3);
            REQUIRE(result.skipped == 0);
            REQUIRE(pages.size() == 2);
            REQUIRE(slotNames(pages[1]) == std::vector<std::string>{"ARMANNI", "ARMAMD", "ARMASP", "ARMFLAK", "ARMFORT", "ARMAMB"});
            // Where IGPATCH was: the bottom-right slot.
            REQUIRE(gadgetNamed(pages[1], "ARMAMB").common.xpos == 64);
            REQUIRE(gadgetNamed(pages[1], "ARMAMB").common.ypos == 155);
        }

        SECTION("a slot the page itself fills is not taken from it")
        {
            auto result = applyDownloadMenuEntries(pages, {{"ARMACK", 1, 0, "ARMFLAK"}});

            REQUIRE(result.placed == 0);
            REQUIRE(result.skipped == 1);
            REQUIRE(slotNames(pages[1])[0] == "ARMANNI");
            REQUIRE_FALSE(hasGadget(pages[1], "ARMFLAK"));
        }

        SECTION("of two entries for one slot, the first keeps it")
        {
            auto result = applyDownloadMenuEntries(pages, {
                {"ARMACK", 1, 3, "ARMFLAK"},
                {"ARMACK", 1, 3, "ARMFORT"},
            });

            REQUIRE(result.placed == 1);
            REQUIRE(result.skipped == 1);
            REQUIRE(slotNames(pages[1])[3] == "ARMFLAK");
        }
    }

    TEST_CASE("CORACA2's empty slots are slots, although the file does not mark them", "[download]")
    {
        // The one shipped page whose IGPATCH gadgets carry attribs=0 instead
        // of the BuildButton attribute. Asking only for the attribute found
        // three slots here and left CORFLAK, CORFORT and CORTOAST nowhere.
        std::vector<std::vector<GuiEntry>> pages{
            buildPage("COR", {"CORSOLAR", "CORMEX", "CORLAB", "CORVP", "CORAP", "CORRAD"}),
            buildPage("COR", {"CORSILO", "CORFMD", "CORASP", "IGPATCH", "IGPATCH", "IGPATCH"}, 0),
        };
        REQUIRE(slotNames(pages[1]).size() == 6);

        auto result = applyDownloadMenuEntries(pages, {
            {"CORACA", 1, 3, "CORFLAK"},
            {"CORACA", 1, 4, "CORFORT"},
            {"CORACA", 1, 5, "CORTOAST"},
        });

        REQUIRE(result.placed == 3);
        // And each takes the attribute a build button draws its queue count by.
        REQUIRE(gadgetNamed(pages[1], "CORTOAST").common.attribs == 32);
    }

    TEST_CASE("download buttons past a builder's last page get pages of their own", "[download]")
    {
        // The construction ship: ARMCS1.GUI is the only page it ships, and
        // the rest of its menu -- the floating defences, the underwater
        // storage and the underwater metal extractor -- is download entries
        // at MENU=3 and MENU=4. Three pages, where RWE showed one.
        std::vector<std::vector<GuiEntry>> pages{
            buildPage("ARM", {"ARMASY", "ARMSY", "ARMTIDE", "ARMSONAR", "ARMTL", "ARMLLT"}),
        };

        auto result = applyDownloadMenuEntries(pages, {
            {"ARMCS", 1, 0, "ARMFRT"},
            {"ARMCS", 1, 1, "ARMUWES"},
            {"ARMCS", 1, 2, "ARMFMKR"},
            {"ARMCS", 1, 3, "ARMUWMS"},
            {"ARMCS", 1, 4, "ARMFHLT"},
            {"ARMCS", 1, 5, "ARMUWMEX"},
            {"ARMCS", 2, 0, "ARMFDRAG"},
        });

        REQUIRE(result.placed == 7);
        REQUIRE(pages.size() == 3);
        REQUIRE(slotNames(pages[0]) == std::vector<std::string>{"ARMASY", "ARMSY", "ARMTIDE", "ARMSONAR", "ARMTL", "ARMLLT"});
        REQUIRE(slotNames(pages[1]) == std::vector<std::string>{"ARMFRT", "ARMUWES", "ARMFMKR", "ARMUWMS", "ARMFHLT", "ARMUWMEX"});
        REQUIRE(slotNames(pages[2]) == std::vector<std::string>{"ARMFDRAG", "IGPATCH", "IGPATCH", "IGPATCH", "IGPATCH", "IGPATCH"});

        SECTION("a new page is the first page's layout, controls and all")
        {
            for (std::size_t i = 1; i < pages.size(); ++i)
            {
                REQUIRE(pages[i].front().common.name == "HEADER");
                REQUIRE(pages[i].front().common.height == 352);
                REQUIRE(hasGadget(pages[i], "ARMPREV"));
                REQUIRE(hasGadget(pages[i], "ARMNEXT"));
                REQUIRE(hasGadget(pages[i], "ARMORDERS"));
                REQUIRE(hasGadget(pages[i], "ARMMOVE"));
                REQUIRE(pages[i].size() == pages[0].size());
            }
        }

        SECTION("and does not carry the first page's units over")
        {
            for (const auto& unit : {"ARMASY", "ARMSY", "ARMTIDE", "ARMSONAR", "ARMTL", "ARMLLT"})
            {
                REQUIRE_FALSE(hasGadget(pages[1], unit));
                REQUIRE_FALSE(hasGadget(pages[2], unit));
            }
        }
    }

    TEST_CASE("download entries find their builder whatever the case, and the AI can then build what they add", "[download]")
    {
        BuilderGuisDatabase db;
        db.addBuilderGui("ARMCS", {buildPage("ARM", {"ARMASY", "ARMSY", "ARMTIDE", "ARMSONAR", "ARMTL", "ARMLLT"})});
        const std::set<std::string> units{"ARMCS", "ARMASY", "ARMSY", "ARMTIDE", "ARMSONAR", "ARMTL", "ARMLLT", "ARMUWMEX", "ARMFARK"};

        REQUIRE_FALSE(buildTreeFromBuilderGuis(db, units).canBuild("ARMCS", "ARMUWMEX"));

        auto result = applyDownloadMenuEntries(db, {
            {"armcs", 1, 5, "ARMUWMEX"},
            // A builder that ships no pages has no layout to put a page in.
            {"ARMFARK", 0, 0, "ARMSOLAR"},
        });

        REQUIRE(result.placed == 1);
        REQUIRE(result.skipped == 1);
        // The construction ship could never build an underwater extractor in
        // RWE, which is what the arena measured, and why its ships sat idle.
        REQUIRE(buildTreeFromBuilderGuis(db, units).canBuild("ARMCS", "ARMUWMEX"));
    }
}
