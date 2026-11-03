#include "WindProvider.h"

#include <rwe/sim/SimRandom.h>
#include <rwe/sim/SimTicksPerSecond.h>

namespace rwe
{
    std::string NativeWindProvider::id() const
    {
        return "native.wind@1";
    }

    WindState NativeWindProvider::update(const WindInput& input, std::minstd_rand& rng) const
    {
        WindState state;
        state.nextChange = input.nextChange;
        state.direction = input.direction;
        state.generationFactor = input.generationFactor;
        state.vector = input.vector;

        if (input.now < input.nextChange)
        {
            return state;
        }

        // The body is the native updateWind, moved here unchanged.
        state.changed = true;
        state.nextChange = input.now + GameTime(randomBetween(rng, 5, 14) * SimTicksPerSecond);

        const int windSpan = input.maxSpeed - input.minSpeed;
        state.speed = input.minSpeed
            + (windSpan < 2 ? 0 : static_cast<int>(randomBelow(rng, static_cast<unsigned int>(windSpan))));

        if (state.speed != 0)
        {
            state.direction = SimAngle(static_cast<uint16_t>(randomBetween(rng, MinAngle.value, MaxAngle.value)));
        }

        state.generationFactor = rweMin(1_ss, SimScalar(state.speed) / SimScalar(MaxUtilizableWindSpeed));
        state.vector = computeWindVector(state.direction, state.speed);
        return state;
    }
}
