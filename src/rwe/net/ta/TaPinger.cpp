#include "TaPinger.h"

#include <rwe/io/tad/tad_headers.h>
#include <utility>

namespace rwe
{
    namespace
    {
        constexpr std::size_t PingSize = 13;

        std::uint32_t readU32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0])
                | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16)
                | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        void writeU32(TadBytes& out, std::uint32_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
            out.push_back(static_cast<std::uint8_t>(value >> 16));
            out.push_back(static_cast<std::uint8_t>(value >> 24));
        }
    }

    TaPinger::TaPinger(TaOutboundBatcher& batcher, std::uint32_t ourPlayerId, TickSource tick)
        : batcher(batcher),
          ourPlayerId(ourPlayerId),
          tick(std::move(tick))
    {
    }

    void TaPinger::handle(PeerId from, const TadBytes& subpacket, TaTransport transport)
    {
        auto ping = parsePing(subpacket);
        if (!ping)
        {
            return;
        }

        if (ping->responderTick == 0)
        {
            batcher.queueReply(
                from,
                buildPing(TaPing{ping->requesterTick, tick(), ping->requesterPlayerId}),
                transport);
            return;
        }

        auto sent = outstanding.find(from);
        if (sent == outstanding.end() || sent->second != ping->requesterTick)
        {
            return;
        }

        outstanding.erase(sent);
        lastRoundTrip[from] = tick() - ping->requesterTick;
    }

    void TaPinger::sendRequests(std::span<const PeerId> peers)
    {
        for (PeerId peer : peers)
        {
            std::uint32_t now = tick();
            outstanding[peer] = now;
            batcher.queue(peer, buildPing(TaPing{now, 0, ourPlayerId}), TaTransport::Udp);
        }
    }

    std::optional<std::uint32_t> TaPinger::roundTripTicks(PeerId peer) const
    {
        auto it = lastRoundTrip.find(peer);
        if (it == lastRoundTrip.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::uint32_t> TaPinger::outstandingTick(PeerId peer) const
    {
        auto it = outstanding.find(peer);
        if (it == outstanding.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    TadBytes TaPinger::buildPing(const TaPing& ping)
    {
        TadBytes out;
        out.reserve(PingSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::Ping));
        writeU32(out, ping.requesterTick);
        writeU32(out, ping.responderTick);
        writeU32(out, ping.requesterPlayerId);
        return out;
    }

    std::optional<TaPing> TaPinger::parsePing(const TadBytes& subpacket)
    {
        if (subpacket.size() != PingSize || subpacket[0] != static_cast<std::uint8_t>(TadSubPacketCode::Ping))
        {
            return std::nullopt;
        }

        return TaPing{
            readU32(&subpacket[1]),
            readU32(&subpacket[5]),
            readU32(&subpacket[9])};
    }
}
