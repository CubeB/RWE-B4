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
}
