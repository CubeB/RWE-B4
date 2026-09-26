#pragma once

namespace rwe
{
    /**
     * Whether the players' list should be out this frame.
     *
     * TOTALA-EXE.md 76 and 108: a display-word bit that only F4 toggles holds
     * it out, and so does Space while the cursor is off the side panel's own
     * gadget. The side panel follows the F4 bit alone.
     */
    bool playerListWantsOut(bool f4Latch, bool spaceHeld, bool cursorOnPanel);

    /**
     * Steps the slide position toward its target without overshooting it.
     *
     * The rate is RWE's own. 76 gives the endpoints and the sounds but not
     * how fast the original crosses between them.
     */
    float advancePanelSlide(float current, float target, float pixelsPerSecond, int millisecondsElapsed);

    /**
     * How far the camera moves so the world does not jump when the panel's
     * slide changes the world viewport's left inset.
     *
     * The camera is the centre of the orthographic view, so widening the
     * viewport to the left moves that centre by half the change -- and every
     * world pixel would slide with it. Moving the camera by the same
     * half-change in world units cancels the slide exactly, leaving the
     * ground where it was and revealing new ground only in the strip the
     * panel has vacated. `zoom` is GameCameraState's own; one screen pixel
     * is `1 / zoom` world units.
     */
    float panelSlideCameraShift(int insetBefore, int insetAfter, float zoom);
}
