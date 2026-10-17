#pragma once

#include <rwe/sim/PlayerId.h>

namespace rwe
{
    /**
     * Small decisions the interface makes before it sends anything to the
     * simulation, kept apart from GameScene so each can be tested without a
     * scene and so the scene's translation units stay under the COFF section
     * ceiling (CLAUDE.md). Nothing here reads or writes simulation state.
     */

    /**
     * Whether an armed Capture click on a unit makes an order (BT-107). The
     * original's order builder (0x43F6D1) and cursor chooser (0x43E797) test
     * only that the selected unit can capture, that there is a target, and
     * that the target's owner record differs from the unit's own. Alliance is
     * never asked, so a teammate's unit is accepted from the armed button.
     * The default-action ladder is another matter and keeps its own alliance
     * test.
     */
    bool armedCaptureAccepts(bool ordererCanCapture, PlayerId ordererOwner, PlayerId targetOwner);

    /**
     * Whether a selected unit receives the order when an armed Build cursor is
     * clicked on a site (BT-108). The original's issuing routine (0x419670)
     * walks every selected unit and skips those without the builder flag; it
     * has no lead-builder rule, so ten selected constructors all get the site.
     *
     * RWE also asks for a mobile unit, which is its reading and not a decoded
     * test: a factory selected alongside the constructors cannot walk to a
     * site, and a placement order on one would only sit in its queue.
     */
    bool buildClickTakesUnit(bool isBuilder, bool isMobile);

    /** A builder's menu as the panel shows it: whether the build menu is up, and which page of it. */
    struct BuildMenuPosition
    {
        bool open;
        unsigned int page;
    };

    /**
     * One step of the build menu's paging (BT-104). `toggle` is the original's
     * argument of the same name: the NEXT and PREV buttons pass it clear, so
     * the page wraps and the menu stays open; the period and comma keys pass it
     * set, so stepping off either end closes the menu and a step from closed
     * opens page one (forward) or the last page (backward). Backward closing at
     * the first page is the mirror of the decoded forward case, not a decoded
     * one. A unit with no pages is left as it was.
     */
    BuildMenuPosition stepBuildMenu(BuildMenuPosition position, unsigned int pageCount, bool forward, bool toggle);
}
