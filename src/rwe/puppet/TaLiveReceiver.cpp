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
        struct Held
        {
            TadPacket packet;
            std::vector<TadBytes> subPackets;
        };

        struct SenderState
        {
            /**
             * Held packets by the serial they name, in order, so the front is
             * always the next one due and a release cannot be out of serial
             * order. Keyed on the serial rather than on the tick it maps to, so
             * that moving the origin does not have to move every key with it.
             */
            std::map<uint32_t, Held> held;

            std::optional<uint32_t> highestSeen;
            std::optional<uint32_t> highestApplied;
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

        void apply(const Held& held)
        {
            originFrozen = true;
            driver.onPacket(held.packet, held.subPackets);
            ++stats.packetsApplied;
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
            // Nothing to key on, so the driver resolves the tick itself from
            // the sender's last serial, exactly as a demo's playback does.
            ++i.stats.packetsWithoutSerial;
            i.driver.onPacket(packet, subPackets);
            ++i.stats.packetsApplied;
            return;
        }

        auto taTick = i.tickOf(*serial);
        if (!taTick || *taTick > static_cast<int64_t>(localTick) + i.options.maxSerialLeadTicks)
        {
            ++i.stats.packetsDroppedOutOfRange;
            return;
        }

        auto& sender = i.senders[packet.sender];
        if (sender.held.count(*serial) != 0 || (sender.highestApplied && *serial <= *sender.highestApplied))
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
            sender.highestApplied = *serial;
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
            while (!state.held.empty() && i.tickFor(state.held.begin()->first) <= due)
            {
                auto it = state.held.begin();
                state.highestApplied = it->first;
                i.apply(it->second);
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
