#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameScene_util.h>
#include <rwe/game/Particle.h>

namespace rwe
{
    // A frame draws every moving thing as lerp(previous, current, frac), so
    // it shows the world between the end of the tick before the last one and
    // the end of the last one. An animation keyed on the simulation's own
    // clock is therefore a tick ahead of the units it belongs to -- upstream
    // #82, up to a thirtieth of a second. renderTimeFor is the clock the
    // drawing runs on instead.
    TEST_CASE("renderTimeFor")
    {
        SECTION("is the tick the frame actually depicts")
        {
            REQUIRE(renderTimeFor(GameTime(1)) == GameTime(0));
            REQUIRE(renderTimeFor(GameTime(2)) == GameTime(1));
            REQUIRE(renderTimeFor(GameTime(900)) == GameTime(899));
        }

        SECTION("stays at zero on the one frame with nothing behind it")
        {
            // Before a tick has run, previousPosition is the spawn position
            // and the lerp is a no-op, so that frame does depict tick 0.
            // Unsigned arithmetic makes getting this wrong spectacular.
            REQUIRE(renderTimeFor(GameTime(0)) == GameTime(0));
        }

        SECTION("moves a frame that changes every other tick a tick later")
        {
            // The frame index a two-tick sequence lands on, under the old
            // clock and under this one: the same sequence, one tick later.
            auto oldFrame = [](unsigned int t) { return (t / 2) % 4; };
            auto newFrame = [&](unsigned int t) { return oldFrame(renderTimeFor(GameTime(t)).value); };

            for (unsigned int t = 1; t < 40; ++t)
            {
                REQUIRE(newFrame(t) == oldFrame(t - 1));
            }
        }
    }

    TEST_CASE("a particle on the render clock")
    {
        Particle particle;
        particle.startTime = GameTime(100);
        particle.renderType = ParticleRenderTypeSprite{"FX", "smoke 1", ParticleFinishTimeEndOfFrames{}, GameTime(2), false};
        const auto& sprite = std::get<ParticleRenderTypeSprite>(particle.renderType);

        SECTION("is not drawn on the frame after the tick that made it")
        {
            // It belongs to a tick the frame has not reached yet. One frame
            // later it is there, in step with whatever emitted it rather
            // than a tick ahead of it.
            REQUIRE_FALSE(particle.isStarted(renderTimeFor(GameTime(100))));
            REQUIRE(particle.isStarted(renderTimeFor(GameTime(101))));
        }

        SECTION("shows its first frame for as long as any other")
        {
            REQUIRE(particle.getFrameIndex(renderTimeFor(GameTime(101)), sprite, 4) == 0);
            REQUIRE(particle.getFrameIndex(renderTimeFor(GameTime(102)), sprite, 4) == 0);
            REQUIRE(particle.getFrameIndex(renderTimeFor(GameTime(103)), sprite, 4) == 1);
        }

        SECTION("is kept until the frame that last draws it")
        {
            // Four frames of two ticks each, so it is done at age 8 -- and
            // the removal pass has to ask on the same clock as the drawing,
            // or the last frame is thrown away before it is shown.
            REQUIRE_FALSE(particle.isFinished(renderTimeFor(GameTime(108)), sprite, 4));
            REQUIRE(particle.isFinished(renderTimeFor(GameTime(109)), sprite, 4));
        }
    }
}
