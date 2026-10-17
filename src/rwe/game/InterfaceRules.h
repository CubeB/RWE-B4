#pragma once

#include <SDL3/SDL_keycode.h>
#include <cstddef>
#include <rwe/sim/PlayerId.h>
#include <vector>

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
    /**
     * Whether a left-button release ends a click rather than a drag (BT-102).
     * The press records the clock and the world point; the release is a click
     * only if the clock advanced by fewer than 25 units and the X and Z of the
     * point each moved by fewer than 32 world units (0x4995AE). The clock
     * (0x4B6340) is the operating system's millisecond tick times 30 over
     * 1000, so the units are wall-clock thirtieths of a second -- not
     * simulation ticks, which stop when the game is paused -- and the distance
     * is on the map, so it shrinks on screen when zoomed out.
     */
    bool pressIsClick(unsigned int pressMillis, unsigned int releaseMillis, float worldDeltaX, float worldDeltaZ);

    /**
     * What the select-everything sweep asks of a unit beyond being the local
     * player's, alive and not held by a mission script (BT-180). The service
     * (0x48BD50) marks each own unit that is selectable, finished (its build
     * fraction is zero, so no nanoframes) and not under the owner-transfer
     * lock, and either not carried or carried by an air base. The squad recall
     * (0x48D9A0) uses the same test. RWE has no transfer lock to ask about, and
     * a unit riding in a transport is the only carried unit it models, so
     * carried is simply refused.
     */
    bool sweepSelectsUnit(bool underConstruction, bool carried);

    /**
     * Which of the selected units Ctrl+D sends a self-destruct toggle to
     * (BT-181). The key looks for an existing self-destruct order on each
     * selected unit (0x439E30) and removes it (0x439F80); only when none of
     * them had one does it issue the order to the whole selection (0x48CF30).
     * So a mixed selection cancels the units already counting and arms none of
     * the rest. `counting[i]` says whether unit i has a countdown running; the
     * result is the indices to toggle.
     */
    std::vector<std::size_t> selfDestructRecipients(const std::vector<bool>& counting);

    /** Steps in the end-of-game screen fade, and the ticks between one and the next (BT-197). */
    constexpr unsigned int EndGameFadeSteps = 10;
    constexpr unsigned int EndGameFadeTicksPerStep = 2;

    /**
     * How many of the fade's steps have been taken `elapsedTicks` after it was
     * armed. The original steps once every second tick (0x41FA8F compares the
     * deadline strictly), the first two ticks after arming, so ten steps take
     * twenty ticks.
     */
    unsigned int endGameFadeStep(unsigned int elapsedTicks);

    /**
     * Whether a key press is one the original's key ring would hand to a
     * screen waiting for "a key" (BT-199, 0x4C1AB0): a character or a
     * function key, not a modifier or a lock. The glamour picture and the
     * results chart both ask the ring. That the ring takes no modifier alone
     * is RWE's reading; the ruling says only that the pop returns the next
     * queued key.
     */
    bool keyDismissesEndScreen(SDL_Keycode key);

    BuildMenuPosition stepBuildMenu(BuildMenuPosition position, unsigned int pageCount, bool forward, bool toggle);
}
