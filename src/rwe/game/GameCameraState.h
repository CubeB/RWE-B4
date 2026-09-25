#pragma once

#include <rwe/math/Vector3f.h>
namespace rwe
{
    struct GameCameraState
    {
        /** Visible extent as a reciprocal: larger zoom shows less world. */
        float zoom{1.0f};

        /**
         * Frame pixels per logical point, from SceneManager::frameDensity.
         * Folding it into scaleDimension keeps physical parity: with zoom
         * fixed, a sharper display spans the same world extent in logical
         * points rather than showing more battlefield.
         */
        float density{1.0f};
        Vector3f position{0.0f, 0.0f, 0.0f};

        Vector3f getRoundedPosition() const;

        float scaleDimension(float dimension) const;
    };

    /**
     * Eases a zoom value toward a target over roughly 100 ms, as an exact
     * exponential approach: alpha = 1 - exp(-dt / tau). Composing two steps
     * equals one step of the summed time, so the result does not depend on
     * the frame rate. Snaps to the target once it is within epsilon.
     *
     * Scene state only -- the camera is not part of the simulation.
     */
    float advanceCameraZoom(float current, float target, unsigned int millisecondsElapsed);
}
