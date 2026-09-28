#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_util.h>

namespace rwe
{
    class TadPuppetDriver;

    /** How much of a live stream the receiver is willing to hold. */
    struct TaLiveReceiverOptions
    {
        /**
         * How far the local clock runs behind the sender's, in ticks, and so
         * how far out of order the stream may be and still be put right. A
         * packet naming tick T arrives up to this many ticks early and is held
         * until T; a packet later than that cannot be applied on the tick it
         * names and is applied late instead.
         */
        uint32_t jitterTicks{3};

        /** Packets one sender may have held at once. */
        uint32_t maxHeldPerSender{64};

        /** Packets every sender together may have held at once. */
        uint32_t maxHeld{512};

        /**
         * Ticks a serial may name past the local tick. Beyond it the packet is
         * refused, because a peer that names a tick a quarter of an hour away
         * is not one whose stream can be waited for.
         */
        uint32_t maxSerialLeadTicks{300};
    };

    /**
     * How far TA's tick and RWE's tick have parted company, in ticks.
     *
     * The offset is sampled once per packet the receiver accepts, as the local
     * tick less the tick the serial maps to. Two clocks running at the same
     * rate hold one offset, so the trend is the drift: `driftTicks` is where
     * the run finished against where it started, and `driftPer1000Ticks` is
     * the rate it moved at, per thousand ticks of the local clock.
     */
    struct TaLiveClockStats
    {
        uint64_t samples{0};

        int64_t minOffsetTicks{0};
        int64_t maxOffsetTicks{0};
        double meanOffsetTicks{0.0};

        int64_t firstOffsetTicks{0};
        int64_t lastOffsetTicks{0};
        int64_t driftTicks{0};
        double driftPer1000Ticks{0.0};

        /** The latest tick any accepted packet has named: where the sender is at least up to. */
        int64_t latestTaTick{0};

        /** The serial this receiver takes for tick 0, once it has one. */
        std::optional<uint32_t> originSerial;

        /** How often a serial below the origin moved it back, before it froze. */
        uint32_t originCorrections{0};
    };

    /** What a live receiver did with the stream it was given, for the overlay. */
    struct TaLiveReceiverStats
    {
        uint64_t packetsReceived{0};

        /** Handed to the driver, whether held first or passed straight through. */
        uint64_t packetsApplied{0};

        /**
         * A packet whose serial named a tick already played, so it went out at
         * once rather than waiting for a tick that has been.
         */
        uint64_t packetsLate{0};

        /** A marker already held or already handed over. Dropped, never applied twice. */
        uint64_t packetsDuplicate{0};

        /**
         * A packet that arrived after one its sender sent later, and was
         * applied in the order that sender sent it. Arriving early is what a
         * receive buffer is for and is not counted here.
         */
        uint64_t packetsOutOfOrder{0};

        /** A packet with no 0x2c to key on, released on the tick of the packet before it. */
        uint64_t packetsWithoutSerial{0};

        /**
         * A packet with no sequence: a reply, which wears 0xffffffff on the
         * wire and counts for nothing in its sender's order.
         */
        uint64_t packetsUnsequenced{0};

        /**
         * A serial naming a tick before the clock started or further ahead than
         * `maxSerialLeadTicks`, refused before anything is held for it.
         */
        uint64_t packetsDroppedOutOfRange{0};

        /** A packet that found the buffer at its bound. */
        uint64_t packetsDroppedBufferFull{0};

        /**
         * Packets a sender sent, between one this receiver had already
         * released and one that had not arrived, and that had not turned up
         * within `jitterTicks + 1` ticks. Waited on once and given up on, so
         * the packet behind them is not held for ever.
         */
        uint64_t packetsDroppedGap{0};

        /** Packets held right now. */
        uint32_t held{0};

        TaLiveClockStats clock;
    };

    /**
     * A jitter buffer between a live TA peer's packets and the puppet driver.
     *
     * A demo arrives in order, so the driver could take a packet the moment it
     * was read. A game does not: the same packets come over UDP with jitter,
     * loss and reordering, and a packet that names tick 900 with the driver
     * already at tick 903 can never be applied at 900. So each sender's packets
     * are held and handed over in **that sender's own order**, which the
     * stream carries as a marker falling by one per packet, and not in the
     * order they arrived: arrival cannot place a record that has no clock of
     * its own, and a `0x0c` that overtakes the `0x09` two packets behind it is
     * a death for a unit the driver has not heard of.
     *
     * The marker says *which*; the `0x2c` serial says *when*. A packet is held
     * until the local tick reaches the tick its serial names and goes out
     * there, never before, so a record lands on the tick the stream recorded
     * it on. One with no serial of its own has no tick to wait for, so it goes
     * on the tick of the packet before it in its sender's order, which is the
     * tick the driver's own per-sender clock would have stamped it with.
     *
     * The local clock lags the sender's by `jitterTicks`: a packet that names
     * tick T arrives up to that many ticks early and waits here for T, and
     * that lag is the whole of the reordering tolerance. Handing a packet over
     * early instead would be a receive buffer that applies records before the
     * tick they name, which puts a death before the full-state record it
     * belongs behind and leaves a unit that should have died alive.
     *
     * It maps TA's serial onto RWE's tick from the first packets of the run --
     * the lowest serial seen before the first packet is applied, so a first
     * packet that arrives out of order does not move the whole clock -- and
     * reports how far the two clocks then drift apart. The anchor is fixed
     * once a packet has been applied, because a moving one would leave the
     * drift figures nothing to measure; a peer whose serials then go backwards
     * is a fault, and it shows as `packetsDroppedOutOfRange` climbing at the
     * packet rate. A marker already handed over is never applied twice, and a
     * short history of them is what says so -- the buffer bound is on packets
     * held, not on that.
     *
     * The driver must be on its external clock: this hands packets over, and
     * the caller's `applyTick` on the driver is what puts them into the world.
     */
    class TaLiveReceiver
    {
    public:
        TaLiveReceiver(TadPuppetDriver& driver, TaLiveReceiverOptions options = {});
        ~TaLiveReceiver();

        TaLiveReceiver(const TaLiveReceiver&) = delete;
        TaLiveReceiver& operator=(const TaLiveReceiver&) = delete;

        /**
         * Takes one packet's subpackets, having arrived at `localTick`.
         *
         * `sequence` is the packet's marker as the network layer read it, where
         * a **higher marker is a packet sent earlier** -- the count falls by one
         * per packet, and the wire's values go below zero over a session, which
         * is why it is ordered as a signed count. Nothing means a reply, which
         * counts for nothing in its sender's order and goes straight through.
         *
         * `TadPacket::time` is carried through untouched; the driver reads the
         * `0x2c` serial and never that field, and this receiver does not use
         * it either.
         */
        void onPacket(
            const TadPacket& packet,
            const std::vector<TadBytes>& subPackets,
            uint32_t localTick,
            std::optional<uint32_t> sequence);

        /**
         * Releases every packet of a sender whose turn has come: the tick it
         * names is here, and every packet that sender sent before it has gone
         * or been given up on. Call once a tick, before the driver's own
         * `applyTick` for it.
         */
        void onTick(uint32_t localTick);

        const TaLiveReceiverOptions& options() const;

        const TaLiveReceiverStats& stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
