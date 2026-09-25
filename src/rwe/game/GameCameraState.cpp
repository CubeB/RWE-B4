#include "GameCameraState.h"

#include <cmath>

namespace rwe
{
    Vector3f GameCameraState::getRoundedPosition() const
    {
        return Vector3f(
            std::round(position.x),
            std::round(position.y),
            std::round(position.z));
    }

    float GameCameraState::scaleDimension(float dimension) const
    {
        return dimension / zoom;
    }

    float advanceCameraZoom(float current, float target, unsigned int millisecondsElapsed)
    {
        const float tau = 0.1f;
        const float epsilon = 1e-4f;

        auto seconds = static_cast<float>(millisecondsElapsed) / 1000.0f;
        auto alpha = 1.0f - std::exp(-seconds / tau);
        auto next = current + (target - current) * alpha;

        if (std::abs(target - next) <= epsilon)
        {
            return target;
        }
        return next;
    }
}
