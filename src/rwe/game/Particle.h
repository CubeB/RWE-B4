#pragma once

#include <functional>
#include <memory>
#include <rwe/math/Vector3f.h>
#include <rwe/render/SpriteSeries.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/SimVector.h>
#include <variant>
#include <vector>

namespace rwe
{
    struct ParticleFinishTimeEndOfFrames
    {
    };
    struct ParticleFinishTimeFixedTime
    {
        GameTime time;
    };
    using ParticleFinishTime = std::variant<ParticleFinishTimeEndOfFrames, ParticleFinishTimeFixedTime>;

    struct ParticleRenderTypeSprite
    {
        std::string gafName;
        std::string animName;
        ParticleFinishTime finishTime;
        GameTime frameDuration{4};
        bool translucent{false};

        /**
         * Draw this one inside the world, tested against the depth buffer,
         * rather than over the finished frame. Explosions and smoke want to
         * be seen whatever is in front of them; an aircraft's exhaust wants
         * the aircraft to hide it, since it comes out underneath.
         */
        bool inWorld{false};

        /**
         * When this is not empty it replaces frameDuration and finishTime.
         * Entry i is the age in ticks at which frame i comes up, and the last
         * entry is the age at which the particle goes away, so the sequence
         * shows one frame fewer than the schedule has entries. Smoke needs it
         * because the original neither holds every frame for the same time nor
         * plays the sequence to the end; see makeSmokePuffFrameSchedule.
         */
        std::vector<GameTime> frameStartTimes;
    };

    /**
     * A single dot of water foam. The original draws one screen pixel filled
     * with a palette index and walks that index up entries 97..103, one step
     * every `rampPeriod` ticks, over a life of exactly six steps -- so the dot
     * reaches the deepest blue on the tick it dies.
     */
    struct ParticleRenderTypeWake
    {
        GameTime finishTime;

        /** Ticks between colour steps: 16 for Wake1, 8 for the faster Wake2. */
        unsigned int rampPeriod{16};
    };

    /**
     * A small flat square of one colour: the nanolathe spray.
     *
     * The original animates each particle's colour, stepping it one place
     * along palette entries 161..167 every tick and wrapping back to 161.
     * Rather than rewrite the particle every tick we keep where it started
     * and work out where it has got to from its age.
     */
    struct ParticleRenderTypeNano
    {
        GameTime finishTime;

        /** Offset into the seven-colour cycle at spawn, 0..6. */
        unsigned char colorPhase{0};

        /** Half the side of the square, in world units. */
        float halfSize{1.0f};

        /**
         * How far to push the square towards the camera in the depth buffer,
         * in world units, without moving it on screen. Nanolathe spray uses
         * this so the structure it is being poured into cannot swallow it.
         */
        float depthNudge{0.0f};
    };

    using ParticleRenderType = std::variant<ParticleRenderTypeSprite, ParticleRenderTypeWake, ParticleRenderTypeNano>;

    struct Particle
    {
        Vector3f position;
        Vector3f velocity;
        ParticleRenderType renderType;
        GameTime startTime;

        bool isStarted(GameTime currentTime) const;
        unsigned int getFrameIndex(GameTime currentTime, const ParticleRenderTypeSprite& renderType, int totalFrames) const;
        bool isFinished(GameTime currentTime, const ParticleRenderTypeSprite& renderType, int numberOfFrames) const;
    };

    /** The first frame of a puff of smoke is held for this many ticks. */
    const unsigned int smokePuffFirstFrameTicks = 7;

    /** Every frame after the first is held for this many ticks plus a roll of the same again. */
    const unsigned int smokePuffLaterFrameTicks = 3;

    /**
     * How one puff of smoke plays in the original.
     *
     * A puff does not run its sequence out. It picks, when it is born, the
     * frame it will stop on -- uniformly between two and two short of the last
     * one -- so most puffs die while they are still small blobs and only the
     * occasional one lives long enough to reach the fat frames at the end.
     * That spread is what keeps a damaged unit's smoke from reading as a
     * column of identical clouds.
     *
     * The timing is uneven too. The first frame gets the emitter's own seven
     * tick period, and every frame after it gets half of that plus a fresh
     * roll of the same, so three to five ticks each. Rolling every hold up
     * front is equivalent to the original rolling one at a time, since each
     * roll is independent of everything before it.
     *
     * randomBelow must return a uniform value in [0, n) for n > 0.
     */
    std::vector<GameTime> makeSmokePuffFrameSchedule(int numberOfFrames, const std::function<int(int)>& randomBelow);
}
