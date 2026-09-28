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
         * A packet carrying both a 0x09 that makes the unit and the 0x2c that
         * clocks it, which is what makes the packet held rather than passed
         * through. Unit 1 is block 0, index 0, the slot the 0x2c names.
         */
        std::vector<TadBytes> buildAndState(uint32_t serial)
        {
            return {tadEncodeBuildStarted(TadBuildStarted{3, 1, TadPosition{0, 0, 0}, TadRotation{0, 0, 0}}), fullStateRecord(serial, 100)};
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

            /** A packet under the next marker down, as a sender's would be. */
            void feed(uint32_t serial, uint16_t health, uint32_t localTick)
            {
                feedAt(nextSequence--, {fullStateRecord(serial, health)}, localTick);
            }

            /** Delivers a packet under a chosen marker, as a network layer would. */
            void feedAt(uint32_t sequence, const std::vector<TadBytes>& subPackets, uint32_t localTick)
            {
                receiver.onPacket(TadPacket{0, 1}, subPackets, localTick, sequence);
            }

            void feedTo(uint8_t sender, uint32_t sequence, const std::vector<TadBytes>& subPackets, uint32_t localTick)
            {
                receiver.onPacket(TadPacket{0, sender}, subPackets, localTick, sequence);
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

            uint32_t nextSequence{1000};
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

    TEST_CASE("a live receiver hands packets over in the order a sender sent them", "[puppet]")
    {
        Fixture f;

        // A marker falls by one per packet, so the packet a sender sent first
        // has the highest: 4, 3, 2, 1 for serials 8, 16, 24, 32. They arrive
        // 4, 2, 3, 1, which is the only thing a network can change.
        f.feedAt(4, {fullStateRecord(8, 100)}, 0);
        f.feedAt(2, {fullStateRecord(24, 33)}, 0);
        f.feedAt(3, {fullStateRecord(16, 22)}, 0);
        f.feedAt(1, {fullStateRecord(32, 44)}, 0);

        REQUIRE(f.receiver.stats().packetsOutOfOrder == 1);
        REQUIRE(f.receiver.stats().packetsLate == 0);
        REQUIRE(f.receiver.stats().packetsDroppedGap == 0);

        // Tick 0 places the puppet, and the two that arrived out of order are
        // still applied in the ticks they name, so the last of them wins.
        f.onTick(0);
        REQUIRE(f.health() == 100);
        f.onTick(8);
        REQUIRE(f.health() == 22);
        f.onTick(16);
        REQUIRE(f.health() == 33);
        f.onTick(24);
        REQUIRE(f.health() == 44);
    }

    TEST_CASE("a live receiver reorders across a packet with no clock of its own", "[puppet]")
    {
        Fixture f;

        // A build and the state record that clocks it, then a state record for a
        // later tick, then damage that belongs between them. Arrival order
        // would put the damage after the later record, which is a different
        // health; the marker puts it back where the sender sent it.
        f.feedAt(12, buildAndState(8), 0);
        f.feedAt(10, {fullStateRecord(24, 50)}, 0);
        f.feedAt(11, {tadEncodeDamage(TadDamage{1, 0, 30, 0})}, 0);

        REQUIRE(f.receiver.stats().packetsWithoutSerial == 1);
        REQUIRE(f.receiver.stats().packetsOutOfOrder == 1);

        // The damage goes on the tick the build is on, and the state record for
        // tick 16 corrects afterwards. Applied the other way round, the record
        // would be setting 50 and the damage would take 30 off it.
        f.onTick(0);
        REQUIRE(f.health() == 70);
        f.onTick(16);
        REQUIRE(f.health() == 50);
    }

    TEST_CASE("a packet that arrived early waits for the tick it names", "[puppet]")
    {
        Fixture f;

        f.feedAt(2, {fullStateRecord(8, 100)}, 0);

        // Three ticks early, which is the whole of the buffer's depth, so it is
        // in hand with room to spare -- and still has not been applied. A
        // record handed over before the tick it names lands a death before the
        // full-state record it belongs behind, and a unit that should have died
        // stays alive.
        f.feedAt(1, {fullStateRecord(24, 50)}, 13);
        f.onTick(13);
        REQUIRE(f.health() == 100);
        REQUIRE(f.receiver.stats().held == 1);
        REQUIRE(f.receiver.stats().packetsLate == 0);

        f.onTicks(14, 15);
        REQUIRE(f.health() == 100);
        REQUIRE(f.receiver.stats().held == 1);

        f.onTick(16);
        REQUIRE(f.health() == 50);
        REQUIRE(f.receiver.stats().held == 0);
    }

    TEST_CASE("a live receiver gives up on a packet its sender did not send", "[puppet]")
    {
        Fixture f;

        // Markers 10 and 8 with 9 missing: 8 cannot go out before 9, and 9 is
        // never coming.
        f.feedAt(10, buildAndState(8), 0);
        f.feedAt(8, {fullStateRecord(24, 50)}, 0);
        f.onTick(0);
        REQUIRE(f.health() == 100);
        REQUIRE(f.receiver.stats().packetsDroppedGap == 0);
        REQUIRE(f.receiver.stats().held == 1);

        // The gap is waited on for the buffer's depth plus the one tick, and no
        // longer: a packet cannot arrive later than the buffer is deep.
        f.onTicks(1, 3);
        REQUIRE(f.receiver.stats().packetsDroppedGap == 0);
        f.onTick(4);
        REQUIRE(f.receiver.stats().packetsDroppedGap == 1);
        REQUIRE(f.receiver.stats().held == 1);

        // And then it waits for the tick it names like anything else.
        f.onTick(15);
        REQUIRE(f.health() == 100);
        f.onTick(16);
        REQUIRE(f.health() == 50);
        REQUIRE(f.receiver.stats().held == 0);
    }

    TEST_CASE("a live receiver passes a reply through, sequence or not", "[puppet]")
    {
        Fixture f;
        f.feedAt(5, buildAndState(8), 0);
        REQUIRE(f.receiver.stats().held == 1);

        // A reply wears 0xffffffff on the wire and counts for nothing in its
        // sender's order, so there is no place to hold it behind.
        TadBytes chat(65, 0);
        chat[0] = static_cast<uint8_t>(TadSubPacketCode::Chat);
        chat[1] = 'h';
        chat[2] = 'i';
        f.receiver.onPacket(TadPacket{0, 1}, {chat}, 0, std::nullopt);

        REQUIRE(f.receiver.stats().packetsUnsequenced == 1);
        REQUIRE(f.receiver.stats().packetsApplied == 1);
        REQUIRE(f.receiver.stats().held == 1);

        f.onTick(0);
        REQUIRE(f.driver.stats().chatLines == 1);
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

    TEST_CASE("a live receiver drops a marker it has already had", "[puppet]")
    {
        Fixture f;
        f.feedAt(7, {fullStateRecord(8, 100)}, 0);
        f.feedAt(7, {fullStateRecord(8, 33)}, 0);
        REQUIRE(f.receiver.stats().packetsDuplicate == 1);
        REQUIRE(f.receiver.stats().held == 1);

        f.onTick(0);
        REQUIRE(f.health() == 100);

        // The same marker again, now that its packet has been handed over.
        f.feedAt(7, {fullStateRecord(8, 44)}, 4);
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

    TEST_CASE("a live receiver keeps each peer's markers to itself", "[puppet]")
    {
        Fixture f;
        addWellStockedPlayer(f.world.sim, "CORE");
        f.driver.addPlayer(2, PlayerId(1));

        auto feed = [&](uint8_t sender, uint32_t sequence, uint32_t serial, uint16_t health, uint32_t localTick) {
            f.receiver.onPacket(TadPacket{0, sender}, {fullStateRecord(serial, health)}, localTick, sequence);
        };

        // One peer's marker is no relation to the other's, though both count
        // down from the same number, and a serial the other peer has already
        // used is not a repeat of anything here. Only the third packet repeats
        // one, and it repeats its own sender's.
        feed(1, 3, 8, 100, 0);
        feed(2, 3, 108, 100, 0);
        feed(1, 3, 8, 100, 0);
        feed(2, 2, 116, 100, 0);

        REQUIRE(f.receiver.stats().packetsDuplicate == 1);
        REQUIRE(f.receiver.stats().held == 3);
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 0);
    }

    TEST_CASE("a live receiver's buffer is bounded, and says what it refused", "[puppet]")
    {
        Fixture f;
        TaLiveReceiver bounded(f.driver, TaLiveReceiverOptions{3, 4, 6, 300});

        // Eleven packets, none of them due and every one inside the lead, so
        // nothing is ever released and the only bound that can save us is the
        // buffer's own. One sender, so the per-sender bound is the one that
        // bites first.
        for (uint32_t i = 1; i <= 11; ++i)
        {
            bounded.onPacket(TadPacket{0, 1}, {fullStateRecord(i * maxUnits, 100)}, 0, 12 - i);
        }

        REQUIRE(bounded.stats().packetsReceived == 11);
        REQUIRE(bounded.stats().held == 4);
        REQUIRE(bounded.stats().packetsDroppedBufferFull == 7);
    }

    TEST_CASE("a live receiver's clock settles on the lowest serial of the run", "[puppet]")
    {
        Fixture f;

        // The second packet of this sender's stream turns up first, and names a
        // later tick. Nothing has been applied yet, so the clock moves back to
        // the tick the first packet named rather than leaving it unplayed.
        f.feedAt(1, {fullStateRecord(16, 33)}, 0);
        f.feedAt(2, {fullStateRecord(8, 100)}, 0);

        REQUIRE(f.receiver.stats().clock.originSerial.value_or(0) == 8u);
        REQUIRE(f.receiver.stats().clock.originCorrections == 1);

        // Neither was refused as being before the clock started, and they are
        // applied in the order that sender sent them rather than the order they
        // came.
        REQUIRE(f.receiver.stats().packetsDroppedOutOfRange == 0);
        REQUIRE(f.receiver.stats().packetsLate == 0);
        f.onTick(0);
        REQUIRE(f.health() == 100);
        f.onTick(8);
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

    TEST_CASE("a packet with no clock of its own goes on its sender's tick", "[puppet]")
    {
        Fixture f;
        TadBytes chat(65, 0);
        chat[0] = static_cast<uint8_t>(TadSubPacketCode::Chat);
        chat[1] = 'h';
        chat[2] = 'i';

        f.feedAt(1, {chat}, 0);
        REQUIRE(f.receiver.stats().packetsWithoutSerial == 1);
        REQUIRE(f.receiver.stats().held == 1);

        f.onTick(0);
        REQUIRE(f.driver.stats().chatLines == 1);
        REQUIRE(f.driver.takeChat().size() == 1);
    }

    TEST_CASE("a death with no clock of its own does not overtake the unit it names", "[puppet]")
    {
        Fixture f;

        // The 0x09 and the 0x2c that clocks it, held because the 0x2c is three
        // ticks ahead of the buffer's depth...
        f.feedAt(2, buildAndState(8), 0);
        REQUIRE(f.receiver.stats().held == 1);

        // ...and a death in a packet with no 0x2c, which has no tick of its own
        // and so must not go past what its own sender has already sent.
        f.feedAt(1, {tadEncodeDeath(TadDeath{1, 0xffffffffu, 0, 100, 0})}, 0);
        REQUIRE(f.receiver.stats().held == 2);

        f.onTick(3);
        REQUIRE(f.driver.stats().unitsSpawned == 1);
        REQUIRE(f.driver.stats().recordsDroppedBadBlock == 0);
        REQUIRE(f.driver.stats().recordsDroppedUnknownUnit == 0);
        REQUIRE(f.driver.stats().deathsDroppedNotLive == 0);
        REQUIRE(f.driver.stats().unitsKilled == 1);
        REQUIRE(f.receiver.stats().packetsWithoutSerial == 1);
        REQUIRE(f.receiver.stats().held == 0);
    }
}
