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
         * Ticks a packet is held ahead of the tick its serial names, which is
         * also how far out of order the stream may be and still be put right.
         * A packet has to be in the driver's hand before that tick arrives or
         * it can only be applied late.
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

        /** The serial this receiver takes for tick 0, once it has one. */
        std::optional<uint32_t> originSerial;

        /** How often a serial below the origin moved it back, before it froze. */
        uint32_t originCorrections{0};
    };

    /** What a live receiver did with the stream it was given, for the overlay. */
    struct TaLiveReceiverStats
    {
        uint64_t packetsReceived{0};

        /** Handed to the driver, whether held first or passed through late. */
        uint64_t packetsApplied{0};

        /**
         * A packet whose serial named a tick already played, applied at once
         * because waiting would only make it later.
         */
        uint64_t packetsLate{0};

        /** A serial already held or already applied. Dropped, never applied twice. */
        uint64_t packetsDuplicate{0};

        /**
         * An accepted packet that arrived after a higher serial from the same
         * sender, and was applied in the order it was sent. The late ones are
         * counted above instead, since they are not put back in order.
         */
        uint64_t packetsOutOfOrder{0};

        /** A packet with no 0x2c to key on, passed through for the driver to resolve. */
        uint64_t packetsWithoutSerial{0};

        /**
         * A serial naming a tick before the clock started or further ahead than
         * `maxSerialLeadTicks`, refused before anything is held for it.
         */
        uint64_t packetsDroppedOutOfRange{0};

        /** A packet that found the buffer at its bound. */
        uint64_t packetsDroppedBufferFull{0};

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
     * already at tick 903 can never be applied at 900. The receiver holds each
     * sender's packets until the local tick is within `jitterTicks` of the tick
     * the serial names, so the driver has them in hand by the tick that names
     * them and applies them there.
     *
     * It maps TA's serial onto RWE's tick from the first packets of the run --
     * the lowest serial seen before the first packet is applied, so a first
     * packet that arrives out of order does not move the whole clock -- and
     * reports how far the two clocks then drift apart. The anchor is fixed
     * once a packet has been applied, because a moving one would leave the
     * drift figures nothing to measure; a peer whose serials then go backwards
     * is a fault, and it shows as `packetsDroppedOutOfRange` climbing at the
     * packet rate. A serial already applied is never applied twice.
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
         * `TadPacket::time` is carried through untouched; the driver reads the
         * `0x2c` serial and never that field, and this receiver does not use
         * it either.
         */
        void onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, uint32_t localTick);

        /**
         * Releases every packet whose serial is within `jitterTicks` of
         * `localTick`, in serial order per sender. Call once a tick, before the
         * driver's own `applyTick` for it.
         */
        void onTick(uint32_t localTick);

        const TaLiveReceiverOptions& options() const;

        const TaLiveReceiverStats& stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
