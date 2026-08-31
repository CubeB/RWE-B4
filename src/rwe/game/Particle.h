#pragma once

#include <memory>
#include <rwe/math/Vector3f.h>
#include <rwe/render/SpriteSeries.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/SimVector.h>
#include <variant>

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
    };

    struct ParticleRenderTypeWake
    {
        GameTime finishTime;
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
        unsigned int getFrameIndex(GameTime currentTime, GameTime frameDuration, int totalFrames) const;
        bool isFinished(GameTime currentTime, const ParticleFinishTime& finishTime, GameTime frameDuration, int numberOfFrames) const;
    };
}
