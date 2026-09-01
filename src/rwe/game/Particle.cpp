#include "Particle.h"
#include <algorithm>
#include <rwe/util/match.h>

namespace rwe
{
    bool Particle::isStarted(GameTime currentTime) const
    {
        return currentTime >= startTime;
    }

    unsigned int Particle::getFrameIndex(GameTime currentTime, const ParticleRenderTypeSprite& renderType, int totalFrames) const
    {
        assert(currentTime >= startTime);
        auto deltaTime = currentTime - startTime;

        if (!renderType.frameStartTimes.empty())
        {
            // The last entry is when the particle goes away rather than the
            // start of a frame, so it is not a candidate to land on.
            auto last = renderType.frameStartTimes.end() - 1;
            auto it = std::upper_bound(renderType.frameStartTimes.begin(), last, deltaTime);
            return static_cast<unsigned int>(std::distance(renderType.frameStartTimes.begin(), it) - 1);
        }

        auto frameIndex = (deltaTime.value / renderType.frameDuration.value) % totalFrames;
        return frameIndex;
    }

    bool Particle::isFinished(GameTime currentTime, const ParticleRenderTypeSprite& renderType, int numberOfFrames) const
    {
        if (!renderType.frameStartTimes.empty())
        {
            return currentTime >= startTime + renderType.frameStartTimes.back();
        }

        auto concreteFinishTime = match(
            renderType.finishTime,
            [&](const ParticleFinishTimeFixedTime& t) { return t.time; },
            [&](const ParticleFinishTimeEndOfFrames&) { return startTime + (GameTime(numberOfFrames * renderType.frameDuration.value)); });

        return currentTime >= concreteFinishTime;
    }

    std::vector<GameTime> makeSmokePuffFrameSchedule(int numberOfFrames, const std::function<int(int)>& randomBelow)
    {
        // The original draws the frame to stop on as two plus a roll over
        // (frameCount - 1) - 2, so a sequence has to be four frames long
        // before there is anything to choose between. Both of the smoke
        // sequences it uses are much longer than that, but a mod's need not be.
        auto framesToShow = 2;
        if (numberOfFrames > 3)
        {
            framesToShow += randomBelow(numberOfFrames - 3);
        }
        framesToShow = std::min(framesToShow, numberOfFrames);

        std::vector<GameTime> schedule;
        schedule.reserve(framesToShow + 1);

        auto age = 0u;
        schedule.push_back(GameTime(age));
        for (int frame = 1; frame <= framesToShow; ++frame)
        {
            // The first frame is held for the emitter's own period; every one
            // after it gets half of that plus a roll of the same again.
            age += frame == 1 ? smokePuffFirstFrameTicks : smokePuffLaterFrameTicks + randomBelow(smokePuffLaterFrameTicks);
            schedule.push_back(GameTime(age));
        }

        return schedule;
    }
}
