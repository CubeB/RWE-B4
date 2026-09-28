#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <rwe/game/GameScene_util.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/MapTerrain.h>
#include <vector>

namespace rwe
{
    // The two periods the original passes: 16 for Wake1 and ReverseWake1, 8
    // for Wake2 and ReverseWake2.
    static const unsigned int Wake1Period = 16;
    static const unsigned int Wake2Period = 8;

    TEST_CASE("computeWakeEmission: a dot starts at the first vertex and drifts towards the second", "[wake]")
    {
        Vector3f first(10.0f, 0.0f, 0.0f);
        Vector3f second(110.0f, 0.0f, 0.0f);

        auto emission = computeWakeEmission(first, second, false, Wake1Period);

        REQUIRE(emission.spawnPosition.x == 10.0f);
        REQUIRE(emission.velocity.x == 0.5f);
        REQUIRE(emission.velocity.y == 0.0f);
        REQUIRE(emission.velocity.z == 0.0f);
    }

    TEST_CASE("computeWakeEmission: reverse swaps the two vertices and nothing else", "[wake]")
    {
        // That really is the whole of the difference between Wake1 and
        // ReverseWake1 in the original -- there is no test on the unit's
        // speed or direction of travel anywhere.
        Vector3f first(10.0f, 0.0f, 0.0f);
        Vector3f second(110.0f, 0.0f, 0.0f);

        auto forward = computeWakeEmission(first, second, false, Wake1Period);
        auto backward = computeWakeEmission(first, second, true, Wake1Period);

        REQUIRE(backward.spawnPosition.x == 110.0f);
        REQUIRE(backward.velocity.x == -forward.velocity.x);
        REQUIRE(backward.duration == forward.duration);
    }

    TEST_CASE("computeWakeEmission: the speed does not depend on how far apart the vertices are", "[wake]")
    {
        // The original normalises before scaling, so a modeller putting the
        // two vertices a long way apart does not make a faster wake.
        auto near = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(2.0f, 0.0f, 0.0f), false, Wake1Period);
        auto far = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(2000.0f, 0.0f, 0.0f), false, Wake1Period);

        REQUIRE(near.velocity.x == 0.5f);
        REQUIRE(far.velocity.x == 0.5f);
    }

    TEST_CASE("computeWakeEmission: coincident vertices give a dot that sits still", "[wake]")
    {
        // Normalising a zero vector would be a division by zero, and some
        // pieces really are degenerate.
        auto emission = computeWakeEmission(Vector3f(5.0f, 5.0f, 5.0f), Vector3f(5.0f, 5.0f, 5.0f), false, Wake1Period);

        REQUIRE(emission.velocity.x == 0.0f);
        REQUIRE(emission.velocity.y == 0.0f);
        REQUIRE(emission.velocity.z == 0.0f);
    }

    TEST_CASE("computeWakeEmission: the ramp period sets the life at six steps", "[wake]")
    {
        Vector3f first(0.0f, 0.0f, 0.0f);
        Vector3f second(1.0f, 0.0f, 0.0f);

        REQUIRE(computeWakeEmission(first, second, false, Wake1Period).duration == GameTime(96));
        REQUIRE(computeWakeEmission(first, second, false, Wake2Period).duration == GameTime(48));
    }

    TEST_CASE("computeWakeEmission: a wake travels half a unit a tick for its whole life", "[wake]")
    {
        // Wake1 lives 96 ticks at half a unit a tick, so it covers 48 world
        // units; Wake2 covers half that.
        auto wake1 = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(1.0f, 0.0f, 0.0f), false, Wake1Period);
        REQUIRE(wake1.velocity.x * static_cast<float>(wake1.duration.value) == 48.0f);

        auto wake2 = computeWakeEmission(Vector3f(0.0f, 0.0f, 0.0f), Vector3f(1.0f, 0.0f, 0.0f), false, Wake2Period);
        REQUIRE(wake2.velocity.x * static_cast<float>(wake2.duration.value) == 24.0f);
    }

    TEST_CASE("wakeColorIndex: the colour steps once every ramp period", "[wake]")
    {
        REQUIRE(wakeColorIndex(0, Wake1Period) == 0);
        REQUIRE(wakeColorIndex(15, Wake1Period) == 0);
        REQUIRE(wakeColorIndex(16, Wake1Period) == 1);
        REQUIRE(wakeColorIndex(31, Wake1Period) == 1);
        REQUIRE(wakeColorIndex(32, Wake1Period) == 2);

        // Wake2 walks the same seven colours twice as fast.
        REQUIRE(wakeColorIndex(8, Wake2Period) == 1);
        REQUIRE(wakeColorIndex(16, Wake2Period) == 2);
    }

    TEST_CASE("wakeColorIndex: the last colour comes up as the dot dies", "[wake]")
    {
        // Six steps over a life of 6 * period, so the deepest blue is reached
        // on the final tick and the ramp never has to wrap.
        REQUIRE(wakeColorIndex(6 * Wake1Period - 1, Wake1Period) == 5);
        REQUIRE(wakeColorIndex(6 * Wake1Period, Wake1Period) == 6);
        REQUIRE(wakeColorIndex(6 * Wake2Period, Wake2Period) == 6);
    }

    TEST_CASE("wakeColorIndex: it clamps rather than wrapping", "[wake]")
    {
        // The original would wrap back to the palest blue, but its life is
        // arranged so that it never gets the chance. Clamping keeps a dot
        // that outlives its schedule from flashing white.
        REQUIRE(wakeColorIndex(10000, Wake1Period) == 6);
        REQUIRE(wakeColorIndex(5, 0) == 0);
    }

    namespace
    {
        // Sea on the west half of a 64-wide map, land on the east. World x 0
        // is heightmap column 32, sixteen world units a column, so the shore
        // runs down world x 0.
        MapTerrain makeShoreTerrain()
        {
            Grid<unsigned char> heights(64, 64, static_cast<unsigned char>(0));
            for (int y = 0; y < 64; ++y)
            {
                for (int x = 32; x < 64; ++x)
                {
                    heights.set(x, y, static_cast<unsigned char>(60));
                }
            }
            return MapTerrain(std::move(heights), 30_ss);
        }
    }

    TEST_CASE("a wake dot laid far out at sea skips the shore test for its whole life", "[wake]")
    {
        // Issue #10: the per-tick shore test was most of what a wake cost,
        // and over open water it can only ever answer "still at sea".
        auto terrain = makeShoreTerrain();
        auto farOut = spawnWakeDot(terrain, Vector3f(-300.0f, 30.0f, 0.0f), Vector3f(0.5f, 0.0f, 0.0f), GameTime(0), GameTime(96), Wake1Period, false);
        REQUIRE(farOut.overOpenWaterForLife);

        // One whose drift over its life reaches the shore is still asked.
        auto nearShore = spawnWakeDot(terrain, Vector3f(-40.0f, 30.0f, 0.0f), Vector3f(0.5f, 0.0f, 0.0f), GameTime(0), GameTime(96), Wake1Period, false);
        REQUIRE_FALSE(nearShore.overOpenWaterForLife);
    }

    TEST_CASE("a wake dot still dies where the shore comes up under it", "[wake]")
    {
        auto terrain = makeShoreTerrain();
        std::vector<WakeDot> dots{
            spawnWakeDot(terrain, Vector3f(-40.0f, 30.0f, 0.0f), Vector3f(1.0f, 0.0f, 0.0f), GameTime(0), GameTime(96), Wake1Period, false),
        };
        int ticks = 0;
        while (!dots.empty() && ticks < 96)
        {
            updateWakeDots(terrain, GameTime(static_cast<unsigned int>(ticks)), dots);
            ++ticks;
        }
        // Gone well before its life ran out, at the waterline rather than on
        // the beach: the ground here is sea until the column at world x -16
        // starts rising towards the land.
        REQUIRE(dots.empty());
        REQUIRE(ticks < 96);
        REQUIRE(ticks > 20);
    }

    TEST_CASE("the wake batch is the quads of the dots the view can see, and nothing else", "[wake]")
    {
        // The walk over the dot list is the expensive half of the wake pass,
        // so it is one function now rather than a call a dot (issue #10). What
        // it puts in the batch is what this pins down: six vertices a dot, the
        // two-unit square the original draws, and nothing for a dot that is
        // finished, not started yet, or off screen.
        auto terrain = makeShoreTerrain();
        std::vector<WakeDot> dots{
            spawnWakeDot(terrain, Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), GameTime(0), GameTime(96), Wake1Period, false),
            spawnWakeDot(terrain, Vector3f(100.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), GameTime(0), GameTime(96), Wake1Period, false),
            spawnWakeDot(terrain, Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), GameTime(0), GameTime(4), Wake1Period, false),
            spawnWakeDot(terrain, Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), GameTime(20), GameTime(96), Wake1Period, false),
        };

        ColoredMeshBatch batch;
        // Left over from a previous frame: the batch is emptied first, so a
        // dot that is dropped cannot leave last frame's quad behind.
        batch.triangles.emplace_back(Vector3f(9.0f, 9.0f, 9.0f), Vector3f(1.0f, 0.0f, 0.0f));

        // An identity view is its own inverse, so a dot is on screen exactly
        // when it is within a unit or so of the origin.
        buildWakeDotBatch(GameTime(10), Matrix4f::identity(), dots, batch);

        REQUIRE(batch.triangles.size() == 6);
        REQUIRE(batch.triangles[0].x == -1.0f);
        REQUIRE(batch.triangles[0].y == 0.0f);
        REQUIRE(batch.triangles[0].z == -1.0f);
        REQUIRE(batch.triangles[2].x == 1.0f);
        REQUIRE(batch.triangles[2].z == 1.0f);
        REQUIRE(batch.triangles[5].x == 1.0f);
        REQUIRE(batch.triangles[5].z == -1.0f);

        // One dot, one colour: the six vertices of a quad are the same flat
        // patch of foam, not a gradient across it.
        for (const auto& vertex : batch.triangles)
        {
            REQUIRE(vertex.r == batch.triangles[0].r);
            REQUIRE(vertex.g == batch.triangles[0].g);
            REQUIRE(vertex.b == batch.triangles[0].b);
        }
    }

    TEST_CASE("a dot's age reaches the colour in the batch, so the ramp is still there", "[wake]")
    {
        auto terrain = makeShoreTerrain();
        // Both on screen at tick 40 -- a dot has to be within about a unit of
        // the origin for an identity view to keep it -- one laid at the start
        // of its life and one twenty ticks in, so they sit at different
        // points of the six-step ramp and the batch has to say which is which.
        std::vector<WakeDot> dots{
            spawnWakeDot(terrain, Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), GameTime(0), GameTime(96), Wake1Period, false),
            spawnWakeDot(terrain, Vector3f(0.5f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.0f), GameTime(20), GameTime(96), Wake1Period, false),
        };

        ColoredMeshBatch batch;
        buildWakeDotBatch(GameTime(40), Matrix4f::identity(), dots, batch);

        REQUIRE(batch.triangles.size() == 12);
        // Ages 40 and 20 on a sixteen-tick ramp are steps two and one of the
        // seven water blues, which are palette entries 99 and 98.
        REQUIRE(batch.triangles[0].r == 151 / 255.0f);
        REQUIRE(batch.triangles[0].g == 179 / 255.0f);
        REQUIRE(batch.triangles[0].b == 255 / 255.0f);
        REQUIRE(batch.triangles[6].r == 175 / 255.0f);
        REQUIRE(batch.triangles[6].g == 207 / 255.0f);
        REQUIRE(batch.triangles[6].b == 255 / 255.0f);
    }

    TEST_CASE("wake dots that are left keep the order they were laid in", "[wake]")
    {
        auto terrain = makeShoreTerrain();
        std::vector<WakeDot> dots;
        for (int i = 0; i < 6; ++i)
        {
            // Every other one is short-lived, so the update removes from the
            // middle of the list as well as the front.
            auto life = GameTime(i % 2 == 0 ? 10u : 96u);
            dots.push_back(spawnWakeDot(terrain, Vector3f(-300.0f + static_cast<float>(i), 30.0f, 0.0f), Vector3f(0.0f, 0.0f, 0.5f), GameTime(0), life, Wake1Period, false));
        }
        updateWakeDots(terrain, GameTime(10), dots);
        REQUIRE(dots.size() == 3);
        for (std::size_t i = 0; i < dots.size(); ++i)
        {
            // The survivors are the odd ones, 1, 3 and 5, in that order, and
            // each has taken its step.
            REQUIRE(dots[i].position.x == -300.0f + static_cast<float>((i * 2) + 1));
            REQUIRE(dots[i].position.z == 0.5f);
        }
    }
}
