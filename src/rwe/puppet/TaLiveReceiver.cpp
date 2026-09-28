#include <rwe/puppet/TaLiveReceiver.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <rwe/io/tad/tad_events.h>
#include <rwe/puppet/TadPuppetDriver.h>

namespace rwe
{
    struct TaLiveReceiver::Impl
    {
        /** How many markers per sender are remembered as repeats. */
        static constexpr std::size_t duplicateWindow = 64;

        struct Held
        {
            TadPacket packet;
            std::vector<TadBytes> subPackets;

            /** The 0x2c's serial, or nothing for a packet carrying no clock. */
            std::optional<uint32_t> serial;
        };

        struct SenderState
        {
            /**
             * Held packets by their marker, highest first, so the front is the
             * next one this sender sent whatever order the network delivered
             * them in.
             */
            std::map<int32_t, Held, std::greater<int32_t>> held;

            /** The marker the next packet in this sender's stream should carry. */
            std::optional<int32_t> expectedMarker;

            /** The local tick at which a gap at the front was first waited on. */
            std::optional<uint32_t> gapSince;

            std::optional<int32_t> lastArrived;

            /**
             * The tick the last packet of this sender's stream went out on,
             * which is where a packet with no serial of its own goes: the
             * packet before it in the sender's order carried that tick.
             */
            std::optional<uint32_t> lastReleasedTick;

            /**
             * The last few markers handed to the driver, so a repeat of one is
             * recognised rather than applied twice. Capped, because a receiver
             * runs for hours and a marker that comes back further behind than
             * the window is a late packet, not a duplicate worth remembering.
             */
            std::vector<int32_t> handed;

            bool hasHanded(int32_t marker) const
            {
                return std::find(handed.begin(), handed.end(), marker) != handed.end();
            }
        };

        TadPuppetDriver& driver;
        TaLiveReceiverOptions options;
        TaLiveReceiverStats stats;

        std::map<uint8_t, SenderState> senders;

        /** The serial taken for tick 0. Signed, so a serial below it is expressible. */
        int64_t originSerial{0};

        /**
         * Set once a packet has been applied. Until then a lower serial moves
         * the origin back, so a first packet that arrives out of order does not
         * put the whole clock a tick out.
         */
        bool originFrozen{false};

        uint32_t totalHeld{0};

        /** Least squares over (tick, offset), for the rate the clocks drift at. */
        double sumX{0.0};
        double sumY{0.0};
        double sumXY{0.0};
        double sumXX{0.0};

        void resetClock()
        {
            auto corrections = stats.clock.originCorrections;
            stats.clock = TaLiveClockStats{};
            stats.clock.originCorrections = corrections;
            stats.clock.originSerial = static_cast<uint32_t>(originSerial);
            sumX = 0.0;
            sumY = 0.0;
            sumXY = 0.0;
            sumXX = 0.0;
        }

        /** Records one observation of the gap between the two clocks. */
        void sampleClock(uint32_t localTick, int64_t taTick)
        {
            auto offset = static_cast<int64_t>(localTick) - taTick;
            auto& clock = stats.clock;
            if (clock.samples == 0)
            {
                clock.firstOffsetTicks = offset;
                clock.minOffsetTicks = offset;
                clock.maxOffsetTicks = offset;
                clock.latestTaTick = taTick;
            }
            else
            {
                clock.minOffsetTicks = std::min(clock.minOffsetTicks, offset);
                clock.maxOffsetTicks = std::max(clock.maxOffsetTicks, offset);
                clock.latestTaTick = std::max(clock.latestTaTick, taTick);
            }
            ++clock.samples;
            clock.lastOffsetTicks = offset;
            clock.driftTicks = clock.lastOffsetTicks - clock.firstOffsetTicks;

            auto x = static_cast<double>(localTick);
            sumX += x;
            sumY += static_cast<double>(offset);
            sumXY += x * static_cast<double>(offset);
            sumXX += x * x;

            auto n = static_cast<double>(clock.samples);
            auto variance = n * sumXX - sumX * sumX;
            clock.driftPer1000Ticks = variance > 0.0 ? (n * sumXY - sumX * sumY) / variance * 1000.0 : 0.0;
            clock.meanOffsetTicks = sumY / n;
        }

        void apply(uint8_t sender, int32_t marker, const Held& held, uint32_t tick)
        {
            originFrozen = true;
            driver.onPacketAt(held.packet, held.subPackets, tick);
            ++stats.packetsApplied;
            remember(sender, marker);
        }

        /** Keeps a marker on the short list of the ones already handed over. */
        void remember(uint8_t sender, int32_t marker)
        {
            auto& state = senders[sender];
            state.handed.push_back(marker);
            if (state.handed.size() > duplicateWindow)
            {
                state.handed.erase(state.handed.begin());
            }
        }

        /** The tick a serial names, negative for one before the clock started. */
        int64_t tickFor(uint32_t serial) const
        {
            return static_cast<int64_t>(serial) - originSerial;
        }

        /**
         * The tick a serial names, or nothing when it is outside what this
         * receiver will wait for. A serial below the origin is the clock still
         * settling rather than a fault, and moves the origin while that is
         * still possible.
         */
        std::optional<int64_t> tickOf(uint32_t serial)
        {
            if (!stats.clock.originSerial)
            {
                originSerial = serial;
                stats.clock.originSerial = serial;
                return 0;
            }

            auto tick = tickFor(serial);
            if (tick < 0)
            {
                if (originFrozen)
                {
                    return std::nullopt;
                }
                originSerial = serial;
                ++stats.clock.originCorrections;

                // Every offset measured so far was taken against the old
                // origin, so the drift figures start again from the settled one.
                if (stats.clock.samples > 0)
                {
                    resetClock();
                }
                else
                {
                    stats.clock.originSerial = serial;
                }
                tick = 0;
            }
            return tick;
        }

        /**
         * Hands one sender's due packets over, in the order that sender sent
         * them and each on the tick it belongs to.
         *
         * Two things hold a packet back: a packet this sender sent before it
         * that has not turned up, and its own tick, which has not come. The
         * first is waited on for `jitterTicks + 1` ticks and then given up on,
         * because a packet cannot arrive later than the buffer is deep and a
         * lost one never arrives at all.
         */
        void release(uint8_t sender, SenderState& state, uint32_t localTick)
        {
            while (!state.held.empty())
            {
                auto it = state.held.begin();
                auto marker = it->first;

                if (state.expectedMarker && marker < *state.expectedMarker)
                {
                    if (!state.gapSince)
                    {
                        state.gapSince = localTick;
                    }
                    if (static_cast<int64_t>(localTick) < static_cast<int64_t>(*state.gapSince) + options.jitterTicks + 1)
                    {
                        return;
                    }

                    // Given up on, and the wait ends there: the gap is closed so
                    // the packet in hand is the sender's next one as far as
                    // anyone here can tell.
                    stats.packetsDroppedGap += static_cast<uint64_t>(static_cast<int64_t>(*state.expectedMarker) - marker);
                    state.expectedMarker = marker;
                    state.gapSince.reset();
                }

                // A packet with a 0x2c goes out on the tick that one names, and
                // not before: the driver applies a record on the tick it names,
                // so a record handed over early lands a death before the
                // full-state record it belongs behind, and a unit that should
                // have died stays alive. One with no serial has no tick of its
                // own, so it goes on the tick of the packet before it in the
                // sender's order, which is the tick the driver would have
                // stamped it with and the one the sender's own records put it
                // on. With nothing before it, it goes on the tick it is being
                // handed over, which is the only tick there is.
                auto tick = static_cast<int64_t>(localTick);
                if (it->second.serial)
                {
                    tick = tickFor(*it->second.serial);
                }
                else if (state.lastReleasedTick)
                {
                    tick = state.lastReleasedTick.value();
                }
                if (tick > static_cast<int64_t>(localTick))
                {
                    return;
                }

                // A marker that has run out of range stops the check rather
                // than asking for a packet that cannot exist.
                if (marker != std::numeric_limits<int32_t>::min())
                {
                    state.expectedMarker = marker - 1;
                }
                state.gapSince.reset();
                state.lastReleasedTick = static_cast<uint32_t>(std::max<int64_t>(tick, 0));
                apply(sender, marker, it->second, state.lastReleasedTick.value());
                state.held.erase(it);
                --totalHeld;
            }
        }
    };

    TaLiveReceiver::TaLiveReceiver(TadPuppetDriver& driver, TaLiveReceiverOptions options)
        : impl(std::make_unique<Impl>(driver, options))
    {
    }

    TaLiveReceiver::~TaLiveReceiver() = default;

    void TaLiveReceiver::onPacket(
        const TadPacket& packet,
        const std::vector<TadBytes>& subPackets,
        uint32_t localTick,
        std::optional<uint32_t> sequence)
    {
        auto& i = *impl;
        ++i.stats.packetsReceived;

        std::optional<uint32_t> serial;
        for (const auto& subPacket : subPackets)
        {
            serial = tadSerialOfUnitState(subPacket);
            if (serial)
            {
                break;
            }
        }

        if (!sequence)
        {
            // A reply wears 0xffffffff on the wire and counts for nothing in
            // its sender's order, so there is nowhere to put it and nothing to
            // put it behind. It goes on the tick it landed, which keeps the
            // driver on this receiver's clock rather than a second one of its
            // own; a reply carries no record of the world either way.
            ++i.stats.packetsUnsequenced;
            i.driver.onPacketAt(packet, subPackets, localTick);
            ++i.stats.packetsApplied;
            return;
        }

        // The marker falls by one per packet, so the one a sender sent first is
        // the highest, and reading it as signed keeps the order right when a
        // session's count goes below zero.
        auto marker = static_cast<int32_t>(*sequence);
        auto& sender = i.senders[packet.sender];
        if (sender.held.count(marker) != 0 || sender.hasHanded(marker))
        {
            ++i.stats.packetsDuplicate;
            return;
        }
        // Arriving after a packet this sender sent *later* is the reordering the
        // buffer puts right; arriving early is what a receive buffer is for and
        // is not a fault.
        auto outOfOrder = sender.lastArrived && marker > *sender.lastArrived;
        sender.lastArrived = marker;

        if (serial)
        {
            auto taTick = i.tickOf(*serial);
            // Noted even for a packet too far ahead to hold, so a local clock
            // that has fallen that far behind still knows how far to catch up;
            // not for one so far ahead it is a bad serial rather than a lead.
            if (taTick && i.stats.clock.samples > 0
                && *taTick <= static_cast<int64_t>(localTick) + 4 * static_cast<int64_t>(i.options.maxSerialLeadTicks))
            {
                i.stats.clock.latestTaTick = std::max(i.stats.clock.latestTaTick, *taTick);
            }
            if (!taTick || *taTick > static_cast<int64_t>(localTick) + i.options.maxSerialLeadTicks)
            {
                ++i.stats.packetsDroppedOutOfRange;
                return;
            }
            i.sampleClock(localTick, *taTick);
            if (*taTick < static_cast<int64_t>(localTick))
            {
                ++i.stats.packetsLate;
            }
        }
        else
        {
            ++i.stats.packetsWithoutSerial;
        }

        if (sender.held.size() >= i.options.maxHeldPerSender || i.totalHeld >= i.options.maxHeld)
        {
            ++i.stats.packetsDroppedBufferFull;
            return;
        }

        if (outOfOrder)
        {
            ++i.stats.packetsOutOfOrder;
        }
        sender.held.emplace(marker, Impl::Held{packet, subPackets, serial});
        ++i.totalHeld;
        i.stats.held = i.totalHeld;
    }

    void TaLiveReceiver::onTick(uint32_t localTick)
    {
        auto& i = *impl;
        for (auto& [sender, state] : i.senders)
        {
            i.release(sender, state, localTick);
        }
        i.stats.held = i.totalHeld;
    }

    const TaLiveReceiverOptions& TaLiveReceiver::options() const
    {
        return impl->options;
    }

    const TaLiveReceiverStats& TaLiveReceiver::stats() const
    {
        return impl->stats;
    }
}
