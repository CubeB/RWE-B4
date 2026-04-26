#include <catch2/catch_test_macros.hpp>
#include <rwe/game/GameSpeed.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/proto/serialization.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <variant>

namespace rwe
{
    // Mirrors GameScene's tick-rate scaling so we can unit test the
    // dispatch behavior without instantiating a full GameScene.
    // - paused == true halts the accumulator entirely.
    // - perMille scales real elapsed ms into "scaled ms" via integer math.
    // - ticks dispatched per frame are capped at maxTicksPerFrame; any
    //   leftover scaled ms beyond the cap is dropped to prevent spirals.
    static int simulateTickDispatch(int millisecondsElapsed, int& buffer, int perMille, bool paused, int maxTicksPerFrame = 10)
    {
        if (!paused)
        {
            buffer += (millisecondsElapsed * perMille) / 1000;
        }
        int ticks = 0;
        while (buffer >= SimMillisecondsPerTick && ticks < maxTicksPerFrame)
        {
            buffer -= SimMillisecondsPerTick;
            ++ticks;
        }
        if (ticks >= maxTicksPerFrame)
        {
            buffer = 0;
        }
        return ticks;
    }

    TEST_CASE("PlayerSetGameSpeedCommand protobuf round-trip", "[speed]")
    {
        SECTION("default-speed index round-trips")
        {
            PlayerCommand original = PlayerSetGameSpeedCommand{GameSpeed::DefaultIndex};
            proto::PlayerCommand wire;
            serializePlayerCommand(original, wire);
            REQUIRE(wire.has_set_game_speed());
            REQUIRE(wire.set_game_speed().speed_index() == GameSpeed::DefaultIndex);

            PlayerCommand restored = deserializeCommand(wire);
            REQUIRE(std::holds_alternative<PlayerSetGameSpeedCommand>(restored));
            REQUIRE(std::get<PlayerSetGameSpeedCommand>(restored).speedIndex == GameSpeed::DefaultIndex);
        }

        SECTION("min-speed index round-trips")
        {
            PlayerCommand original = PlayerSetGameSpeedCommand{GameSpeed::MinIndex};
            proto::PlayerCommand wire;
            serializePlayerCommand(original, wire);
            PlayerCommand restored = deserializeCommand(wire);
            REQUIRE(std::get<PlayerSetGameSpeedCommand>(restored).speedIndex == GameSpeed::MinIndex);
        }

        SECTION("max-speed index round-trips")
        {
            PlayerCommand original = PlayerSetGameSpeedCommand{GameSpeed::MaxIndex};
            proto::PlayerCommand wire;
            serializePlayerCommand(original, wire);
            PlayerCommand restored = deserializeCommand(wire);
            REQUIRE(std::get<PlayerSetGameSpeedCommand>(restored).speedIndex == GameSpeed::MaxIndex);
        }

        SECTION("pause and unpause still serialize")
        {
            // Sanity check that we didn't break the existing variants.
            proto::PlayerCommand wirePause;
            serializePlayerCommand(PlayerCommand(PlayerPauseGameCommand{}), wirePause);
            REQUIRE(wirePause.has_pause());
            auto restoredPause = deserializeCommand(wirePause);
            REQUIRE(std::holds_alternative<PlayerPauseGameCommand>(restoredPause));

            proto::PlayerCommand wireUnpause;
            serializePlayerCommand(PlayerCommand(PlayerUnpauseGameCommand{}), wireUnpause);
            REQUIRE(wireUnpause.has_unpause());
            auto restoredUnpause = deserializeCommand(wireUnpause);
            REQUIRE(std::holds_alternative<PlayerUnpauseGameCommand>(restoredUnpause));
        }
    }

    TEST_CASE("Tick dispatch rate scales with GameSpeed.perMille", "[speed]")
    {
        // SimMillisecondsPerTick is 33 (30 ticks/sec at 1.0x).
        SECTION("at 1.0x, ~one tick per 33ms")
        {
            int buffer = 0;
            int ticks = simulateTickDispatch(33, buffer, 1000, false);
            REQUIRE(ticks == 1);
        }

        SECTION("at 1.0x over 1 second produces 30 ticks")
        {
            int buffer = 0;
            int totalTicks = 0;
            // Cap is 10 ticks/frame; spread across many frames.
            for (int i = 0; i < 60; ++i)
            {
                totalTicks += simulateTickDispatch(1000 / 60, buffer, 1000, false);
            }
            // Per-frame integer truncation costs us at most 60ms of scaled
            // time over 60 frames, so the tick count should be 29-30.
            REQUIRE(totalTicks >= 29);
            REQUIRE(totalTicks <= 30);
        }

        SECTION("at 0.1x dispatches roughly 1/10 the ticks")
        {
            int buffer = 0;
            int totalTicks = 0;
            // Simulate 1 real second in 33ms slices.
            for (int i = 0; i < 30; ++i)
            {
                totalTicks += simulateTickDispatch(33, buffer, 100, false);
            }
            // Expected: (33 * 100 / 1000) * 30 = 99 scaled ms => ~3 ticks
            REQUIRE(totalTicks >= 2);
            REQUIRE(totalTicks <= 3);
        }

        SECTION("at 5.0x dispatches 5x the ticks")
        {
            int buffer = 0;
            int totalTicks = 0;
            // 60 frames of ~16ms each = 960ms wall time; at 5x that's 4800
            // scaled ms; expect ~145 ticks.
            for (int i = 0; i < 60; ++i)
            {
                totalTicks += simulateTickDispatch(16, buffer, 5000, false);
            }
            REQUIRE(totalTicks >= 140);
            REQUIRE(totalTicks <= 150);
        }
    }

    TEST_CASE("Pause halts tick dispatch", "[speed]")
    {
        SECTION("paused frames dispatch zero ticks")
        {
            int buffer = 0;
            for (int i = 0; i < 100; ++i)
            {
                int ticks = simulateTickDispatch(33, buffer, 1000, true);
                REQUIRE(ticks == 0);
            }
            // Buffer must not advance while paused.
            REQUIRE(buffer == 0);
        }

        SECTION("unpausing resumes from where we left off")
        {
            int buffer = 20; // partial tick already accumulated
            // pause for many frames
            for (int i = 0; i < 10; ++i)
            {
                simulateTickDispatch(33, buffer, 1000, true);
            }
            // buffer unchanged
            REQUIRE(buffer == 20);
            // unpause and tick
            int ticks = simulateTickDispatch(33, buffer, 1000, false);
            REQUIRE(ticks == 1);
        }
    }

    TEST_CASE("Tick dispatch caps at maxTicksPerFrame", "[speed]")
    {
        SECTION("a huge frame at 5x cannot exceed cap of 10")
        {
            int buffer = 0;
            // 1 second of wall time in a single frame at 5x: 5000 scaled ms
            // would be 151 ticks unbounded.
            int ticks = simulateTickDispatch(1000, buffer, 5000, false, 10);
            REQUIRE(ticks == 10);
            // After hitting cap, buffer is drained so backlog doesn't carry over.
            REQUIRE(buffer == 0);
        }

        SECTION("subsequent normal frames still dispatch normally")
        {
            int buffer = 0;
            simulateTickDispatch(1000, buffer, 5000, false, 10);
            // Next tiny frame should produce few ticks, not catch up to spiral.
            int ticks = simulateTickDispatch(33, buffer, 1000, false, 10);
            REQUIRE(ticks == 1);
        }
    }
}
