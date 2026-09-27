#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/puppet/TaLiveReceiver.h>
#include <rwe/puppet/TadPuppetDriver.h>
#include <rwe/puppet/puppet_test_util.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitState.h>
#include <rwe/sim/sim_test_util.h>
#include <vector>

namespace rwe
{
    namespace
    {
        // Serials a whole number of maxUnits apart name the same slot, so a
        // full-state record at each lands on the same puppet and its health
        // says which of them was applied last. The first record for a slot
        // spawns the unit complete and does not carry its health over, so a
        // test that reads health back needs two of them at least.
        constexpr uint16_t maxUnits = 8;

        TadUnitStateLayout testLayout()
        {
            return tadUnitStateLayout(std::vector<bool>{false, false, false}, maxUnits);
        }

        TadBytes fullStateRecord(uint32_t serial, uint16_t health)
        {
            TadUnitState state;
            state.tick = serial;
            state.sync = TadUnitSync{0, 3, health, 0, 0, 0, std::nullopt, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}, std::nullopt};
            return tadEncodeUnitState(state, testLayout());
        }

        /**
         * The driver reads which types fly out of the simulation's table, so
         * the world has to exist before the driver is built -- hence its own
         * struct rather than a line in the fixture's constructor.
         */
        struct World
        {
            GameSimulation sim{makeFlatTerrain(64, 64), 0u, 0, 0};

            World()
            {
                definePuppetTestWorld(sim);
                addWellStockedPlayer(sim, "ARM");
            }
        };

        struct Fixture
        {
            World world;
            TadPuppetDriver driver{world.sim, maxUnits, puppetTestLoadOrder()};
            TaLiveReceiver receiver{driver};

            Fixture()
            {
                driver.addPlayer(1, PlayerId(0));
                driver.setExternalClock(true);
            }

            void feed(uint32_t serial, uint16_t health, uint32_t localTick)
            {
                receiver.onPacket(TadPacket{0, 1}, {fullStateRecord(serial, health)}, localTick);
            }

            /** One turn of the live loop: release, apply, then run the tick. */
            void onTick(uint32_t localTick)
            {
                receiver.onTick(localTick);
                driver.applyTick(localTick);
                world.sim.tick();
            }

            /** Runs ticks `from` to `to`, the last one included. */
            void onTicks(uint32_t from, uint32_t to)
            {
                for (uint32_t t = from; t <= to; ++t)
                {
                    onTick(t);
                }
            }

            /** The puppet's health, or -1 while no record has placed it. */
            int health() const
            {
                auto unit = findUnitOfType(world.sim, "TANK");
                if (!unit)
                {
                    return -1;
                }
                return static_cast<int>(world.sim.getUnitState(*unit).hitPoints);
            }
        };
    }

    TEST_CASE("a live receiver applies a packet at the tick its serial names", "[puppet]")
    {
        Fixture f;
        f.feed(8, 100, 0);
        f.feed(16, 22, 0);
        f.feed(24, 33, 0);

        // The first is due now, so there is nothing to wait for: it places the
        // puppet complete, at the tick it named.
        f.onTick(0);
        REQUIRE(f.health() == 100);
        REQUIRE(f.receiver.stats().packetsLate == 0);

        f.onTicks(1, 7);
        REQUIRE(f.health() == 100);

        // Tick 8 is the next one named, and 16 the one after it.
        f.onTick(8);
        REQUIRE(f.health() == 22);
        f.onTicks(9, 15);
        REQUIRE(f.health() == 22);
        f.onTick(16);
        REQUIRE(f.health() == 33);
        REQUIRE(f.receiver.stats().held == 0);
    }

    TEST_CASE("a live receiver puts an out-of-order packet back in serial order", "[puppet]")
    {
        Fixture f;
        f.feed(8, 100, 0);
        f.feed(32, 44, 0);
        f.feed(16, 22, 0);
        f.feed(24, 33, 0);

        REQUIRE(f.receiver.stats().packetsOutOfOrder == 2);
        REQUIRE(f.receiver.stats().packetsLate == 0);

        // Tick 0 places the puppet, and the three that arrived out of order
        // are still applied in the ticks they name, so the last of them wins.
        f.onTick(0);
        REQUIRE(f.health() == 100);
        f.onTick(8);
        REQUIRE(f.health() == 22);
        f.onTick(16);
        REQUIRE(f.health() == 33);
        f.onTick(24);
        REQUIRE(f.health() == 44);
    }

    TEST_CASE("a packet whose tick has passed is applied at once and counted", "[puppet]")
    {
        Fixture f;
        f.feed(8, 100, 0);
        f.onTicks(0, 7);
        REQUIRE(f.health() == 100);

        // Serial 16 names tick 8 and tick 10 is here, so no amount of holding
        // would put it back.
        f.feed(16, 22, 10);
        REQUIRE(f.receiver.stats().packetsLate == 1);
        f.onTick(10);
        REQUIRE(f.health() == 22);

        // A serial that named a tick before the receiver's clock started is
        // refused once the clock has settled, rather than wrapping to a tick
        // a long way in the future.
        f.feed(0, 33, 10);
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 1);
        REQUIRE(f.health() == 22);
    }

    TEST_CASE("a live receiver drops a serial it has already had", "[puppet]")
    {
        Fixture f;
        f.feed(8, 100, 0);
        f.feed(8, 33, 0);
        REQUIRE(f.receiver.stats().packetsDuplicate == 1);
        REQUIRE(f.receiver.stats().held == 1);

        f.onTick(0);
        REQUIRE(f.health() == 100);

        // The same serial again, now that it has been applied.
        f.feed(8, 44, 4);
        REQUIRE(f.receiver.stats().packetsDuplicate == 2);
        REQUIRE(f.health() == 100);
    }

    TEST_CASE("a live receiver refuses a serial far past the tick it names", "[puppet]")
    {
        Fixture f;
        f.feed(8, 100, 0);
        f.feed(0xffffffffu, 22, 0);
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 1);
        REQUIRE(f.receiver.stats().held == 1);

        f.onTick(0);
        REQUIRE(f.health() == 100);
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 1);
    }

    TEST_CASE("a live receiver keeps each peer's serials to itself", "[puppet]")
    {
        Fixture f;
        addWellStockedPlayer(f.world.sim, "CORE");
        f.driver.addPlayer(2, PlayerId(1));

        auto feed = [&](uint8_t sender, uint32_t serial, uint16_t health, uint32_t localTick) {
            f.receiver.onPacket(TadPacket{0, sender}, {fullStateRecord(serial, health)}, localTick);
        };

        // One peer on a later clock, with a serial of its own and a serial the
        // other peer has already used: neither may be taken for a repeat.
        feed(1, 8, 100, 0);
        feed(2, 108, 100, 0);
        feed(1, 8, 100, 0);
        feed(2, 116, 100, 0);

        REQUIRE(f.receiver.stats().packetsDuplicate == 1);
        REQUIRE(f.receiver.stats().held == 3);
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 0);
    }

    TEST_CASE("a live receiver's buffer is bounded, and says what it refused", "[puppet]")
    {
        Fixture f;
        TaLiveReceiver bounded(f.driver, TaLiveReceiverOptions{3, 4, 6, 300});

        // Eleven serials, none of them due and every one inside the lead, so
        // nothing is ever released and the only bound that can save us is the
        // buffer's own. One sender, so the per-sender bound is the one that
        // bites first.
        for (uint32_t i = 1; i <= 11; ++i)
        {
            bounded.onPacket(TadPacket{0, 1}, {fullStateRecord(i * maxUnits, 100)}, 0);
        }

        REQUIRE(bounded.stats().packetsReceived == 11);
        REQUIRE(bounded.stats().held == 4);
        REQUIRE(bounded.stats().packetsDroppedBufferFull == 7);
    }

    TEST_CASE("a live receiver's clock settles on the lowest serial of the run", "[puppet]")
    {
        Fixture f;
        f.feed(24, 33, 0);
        f.feed(8, 22, 0);

        REQUIRE(f.receiver.stats().clock.originSerial.value_or(0) == 8u);
        REQUIRE(f.receiver.stats().clock.originCorrections == 1);

        // Neither was refused as being before the clock started, and both are
        // applied in the order they were sent rather than the order they came.
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 0);
        REQUIRE(f.receiver.stats().packetsLate == 0);
        f.onTick(0);
        REQUIRE(f.health() == 100);
        f.onTick(16);
        REQUIRE(f.health() == 33);
    }

    TEST_CASE("a live receiver reports the two clocks holding and then parting", "[puppet]")
    {
        SECTION("a sender keeping our time does not drift")
        {
            Fixture f;
            for (uint32_t t = 0; t < 100; ++t)
            {
                f.feed(8 + t, 100, t);
                f.onTick(t);
            }

            auto clock = f.receiver.stats().clock;
            REQUIRE(clock.samples == 100);
            REQUIRE(clock.originSerial.value_or(0) == 8u);
            REQUIRE(clock.driftTicks == 0);
            REQUIRE(clock.minOffsetTicks == 0);
            REQUIRE(clock.maxOffsetTicks == 0);
            REQUIRE(clock.meanOffsetTicks == Catch::Approx(0.0));
            REQUIRE(clock.driftPer1000Ticks == Catch::Approx(0.0));
        }

        SECTION("a sender running at half our rate falls behind, and says so")
        {
            Fixture f;
            for (uint32_t t = 0; t < 200; ++t)
            {
                if (t % 2 == 0)
                {
                    f.feed(8 + t / 2, 100, t);
                }
                f.onTick(t);
            }

            auto clock = f.receiver.stats().clock;
            REQUIRE(clock.samples == 100);
            REQUIRE(clock.driftTicks > 80);
            REQUIRE(clock.driftPer1000Ticks == Catch::Approx(500.0).epsilon(0.05));
            REQUIRE(clock.meanOffsetTicks > 0.0);
            REQUIRE(clock.maxOffsetTicks > clock.minOffsetTicks);
        }
    }

    TEST_CASE("a packet with no clock to key on is passed through", "[puppet]")
    {
        Fixture f;
        TadBytes chat(65, 0);
        chat[0] = static_cast<uint8_t>(TadSubPacketCode::Chat);
        chat[1] = 'h';
        chat[2] = 'i';

        f.receiver.onPacket(TadPacket{0, 1}, {chat}, 0);
        REQUIRE(f.receiver.stats().packetsWithoutSerial == 1);
        REQUIRE(f.receiver.stats().packetsApplied == 1);

        f.onTick(0);
        REQUIRE(f.driver.stats().chatLines == 1);
        REQUIRE(f.driver.takeChat().size() == 1);
    }
}
