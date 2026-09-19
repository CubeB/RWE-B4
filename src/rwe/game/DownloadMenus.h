#pragma once

#include <rwe/io/gui/gui.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <string>
#include <vector>

namespace rwe
{
    class BuilderGuisDatabase;

    /**
     * One button a TDF in the `download` directory adds to a builder's build menu.
     *
     * The expansions and patches extend the menus of builders that already
     * ship GUI pages this way, instead of replacing the pages:
     *
     *     [MENUENTRY1] { UNITMENU=ARMCS; MENU=3; BUTTON=5; UNITNAME=ARMUWMEX; }
     *
     * There are 70 such files in the shipped data -- the v3.1 patch, both
     * expansions and six of the downloadable units -- adding 111 buttons to
     * 26 builders, and for 53 units they are the only way to build them at
     * all: the Vulcan, the Buzzsaw, the Krogoth gantry, the Flakker, the
     * fortification wall, the construction ship's second and third pages.
     * RWE did not read them, so none of those could be built.
     */
    struct DownloadMenuEntry
    {
        /** The builder whose menu gets the button (UNITMENU), spelt as the file spells it. */
        std::string builder;

        /**
         * The page, counted from zero. The file's MENU is this plus two, so
         * MENU=2 is a builder's first page. That offset is read off the data
         * twice over: the construction ship ships one page and takes entries
         * at MENU=3 and 4, which is its three pages; and the advanced
         * construction kbot's download buttons at MENU=3 land on exactly the
         * three empty slots of its second page.
         */
        int page;

        /** The slot on that page, 0 to 5: left to right, then top to bottom. */
        int slot;

        /** The unit the button builds (UNITNAME), spelt as the file spells it. */
        std::string unitName;
    };

    /**
     * The name of an empty build slot's gadget. It is an ordinary 64x64
     * button on the page whose art, in commongui.gaf, is the blank patch.
     */
    inline constexpr const char* EmptyBuildSlotName = "IGPATCH";

    struct DownloadMenuResult
    {
        int placed{0};
        /** Entries that found no empty slot to go in, or no builder pages to lay a new page out from. */
        int skipped{0};
    };

    /**
     * The entries in one parsed download file. A block missing any of the
     * four keys is passed over rather than failing the file, since several
     * of these files come from third-party downloadable units.
     */
    std::vector<DownloadMenuEntry> parseDownloadMenuEntries(const std::vector<TdfBlock>& blocks);

    /**
     * Whether a page gadget is one of its six build slots: a button that
     * builds something, or an empty slot. Both are needed. Every page in the
     * shipped data marks its slots with the BuildButton attribute, except
     * CORACA2.GUI, whose three IGPATCH gadgets carry attribs=0 -- so a rule
     * that asked only for the attribute would find three slots on that page
     * and nowhere to put the three buttons meant for them. Checked against
     * all 49 pages a download entry touches: six slots on every one, in
     * file order left to right and top to bottom.
     */
    bool isBuildSlot(const GuiEntry& entry);

    /**
     * Places the entries on one builder's pages. An entry for an existing
     * page fills the empty slot it names; every one in the shipped data lands
     * on an IGPATCH, and one that finds its slot taken is skipped rather than
     * displacing what the page itself put there. An entry beyond the last
     * page adds pages, each laid out as the builder's first page with every
     * slot emptied.
     */
    DownloadMenuResult applyDownloadMenuEntries(std::vector<std::vector<GuiEntry>>& pages, const std::vector<DownloadMenuEntry>& entries);

    /**
     * The same over every builder, matching builder names regardless of
     * case: the pages are filed under the name as the FBI spells it, and the
     * download files spell names their own way.
     */
    DownloadMenuResult applyDownloadMenuEntries(BuilderGuisDatabase& db, const std::vector<DownloadMenuEntry>& entries);
}
