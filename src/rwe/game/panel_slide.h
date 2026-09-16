#pragma once

namespace rwe
{
    /**
     * Whether the side panel should be getting out of the way this frame.
     *
     * TOTALA-EXE.md 76: the original keeps a display-word bit that only F4
     * toggles, and the slide runs to the hidden endpoint while that bit is
     * set. With it clear the slide runs back, except while Space is held and
     * the cursor is off the panel's own gadget -- holding Space is a peek at
     * the map underneath, so pointing at the panel cancels it.
     */
    bool panelWantsHiding(bool f4Latch, bool spaceHeld, bool cursorOnPanel);

    /**
     * Steps the slide position toward its target without overshooting it.
     *
     * The rate is RWE's own. 76 gives the endpoints and the sounds but not
     * how fast the original crosses between them.
     */
    float advancePanelSlide(float current, float target, float pixelsPerSecond, int millisecondsElapsed);
}
