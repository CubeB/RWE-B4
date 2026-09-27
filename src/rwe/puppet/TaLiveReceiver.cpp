#include <rwe/puppet/TaLiveReceiver.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <rwe/io/tad/tad_events.h>
#include <rwe/puppet/TadPuppetDriver.h>

namespace rwe
{
    struct TaLiveReceiver::Impl
    {
        /** How many handed-over serials per sender are remembered as repeats. */
        static constexpr std::size_t duplicateWindow = 64;

        struct Held
        {
            TadPacket packet;
            std::vector<TadBytes> subPackets;

            /** False for a packet with no 0x2c, which rides behind the one it arrived after. */
            bool hasSerial{true};
        };

        struct SenderState
        {
            /**
             * Held packets by the serial they name, in order, so the front is
             * always the next one due and a release cannot be out of serial
             * order. Keyed on the serial rather than on the tick it maps to, so
             * that moving the origin does not have to move every key with it.
             *
             * A multimap because a packet with no 0x2c of its own takes the key
             * of the last packet held for that sender, and a multimap keeps it
             * behind the packet it arrived after rather than in front of it.
             */
            std::multimap<uint32_t, Held> held;

            std::optional<uint32_t> highestSeen;

            /**
             * The last few serials handed to the driver, oldest first, so a
             * repeat of one is recognised rather than applied twice. Capped,
             * because a receiver runs for hours and a serial that comes back
             * further behind than the window is a late packet, not a duplicate
             * worth remembering.
             */
            std::vector<uint32_t> handed;

            bool hasHanded(uint32_t serial) const
            {
                return std::find(handed.begin(), handed.end(), serial) != handed.end();
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
            }
            else
            {
                clock.minOffsetTicks = std::min(clock.minOffsetTicks, offset);
                clock.maxOffsetTicks = std::max(clock.maxOffsetTicks, offset);
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

        void apply(uint8_t sender, uint32_t serial, const Held& held)
        {
            originFrozen = true;
            driver.onPacket(held.packet, held.subPackets);
            ++stats.packetsApplied;
            if (held.hasSerial)
            {
                remember(sender, serial);
            }
        }

        /** Keeps a serial on the short list of the ones already handed over. */
        void remember(uint8_t sender, uint32_t serial)
        {
            auto& state = senders[sender];
            state.handed.push_back(serial);
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
    };

    TaLiveReceiver::TaLiveReceiver(TadPuppetDriver& driver, TaLiveReceiverOptions options)
        : impl(std::make_unique<Impl>(driver, options))
    {
    }

    TaLiveReceiver::~TaLiveReceiver() = default;

    void TaLiveReceiver::onPacket(const TadPacket& packet, const std::vector<TadBytes>& subPackets, uint32_t localTick)
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

        if (!serial)
        {
            ++i.stats.packetsWithoutSerial;

            // A record with no clock of its own belongs to its sender's current
            // tick, so it cannot go past what that sender has already sent: a
            // 0x0c that overtakes the 0x09 two packets behind it is a death for
            // a unit the driver has not heard of, and the stream's own order is
            // the only thing that says otherwise. Anything still held for this
            // sender goes first, and this rides behind the last of it.
            auto& sender = i.senders[packet.sender];
            if (sender.held.empty())
            {
                i.driver.onPacket(packet, subPackets);
                ++i.stats.packetsApplied;
                return;
            }

            if (sender.held.size() >= i.options.maxHeldPerSender || i.totalHeld >= i.options.maxHeld)
            {
                ++i.stats.packetsDroppedBufferFull;
                return;
            }

            sender.held.emplace(sender.held.rbegin()->first, Impl::Held{packet, subPackets, false});
            ++i.totalHeld;
            i.stats.held = i.totalHeld;
            return;
        }

        auto taTick = i.tickOf(*serial);
        if (!taTick || *taTick > static_cast<int64_t>(localTick) + i.options.maxSerialLeadTicks)
        {
            ++i.stats.packetsDroppedOutOfRange;
            return;
        }

        auto& sender = i.senders[packet.sender];
        // A key is only present while the packet that put it there is: the two
        // are inserted together and released together, so a serial-less entry
        // under this key means this packet is still held too.
        if (sender.held.count(*serial) != 0 || sender.hasHanded(*serial))
        {
            ++i.stats.packetsDuplicate;
            return;
        }
        auto outOfOrder = sender.highestSeen && *serial < *sender.highestSeen;
        if (!sender.highestSeen || *serial > *sender.highestSeen)
        {
            sender.highestSeen = *serial;
        }
        i.sampleClock(localTick, *taTick);

        if (*taTick < static_cast<int64_t>(localTick))
        {
            ++i.stats.packetsLate;
            i.originFrozen = true;
            i.driver.onPacket(packet, subPackets);
            ++i.stats.packetsApplied;
            i.remember(packet.sender, *serial);
            return;
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
        sender.held.emplace(*serial, Impl::Held{packet, subPackets});
        ++i.totalHeld;
        i.stats.held = i.totalHeld;
    }

    void TaLiveReceiver::onTick(uint32_t localTick)
    {
        auto& i = *impl;
        auto due = static_cast<int64_t>(localTick) + i.options.jitterTicks;
        for (auto& [sender, state] : i.senders)
        {
            // The front is the next one due, and for two entries on the same
            // key it is the one that arrived first, so a packet with no 0x2c
            // goes out after the packet it arrived behind.
            while (!state.held.empty() && i.tickFor(state.held.begin()->first) <= due)
            {
                auto it = state.held.begin();
                i.apply(sender, it->first, it->second);
                state.held.erase(it);
                --i.totalHeld;
            }
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
