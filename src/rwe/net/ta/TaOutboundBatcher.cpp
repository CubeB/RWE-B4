#include "TaOutboundBatcher.h"

#include <utility>

namespace rwe
{
    TaOutboundBatcher::TaOutboundBatcher(Sender send, std::uint8_t packetType)
        : send(std::move(send)),
          packetType(packetType)
    {
    }

    void TaOutboundBatcher::append(PeerId to, TadBytes subpacket, TaTransport transport, std::uint32_t marker)
    {
        Queue& queue = queues[Key{to, transport}];
        if (!queue.markerSet)
        {
            queue.marker = marker;
            queue.markerSet = true;
        }
        queue.subpackets.push_back(std::move(subpacket));
    }

    void TaOutboundBatcher::queue(PeerId to, TadBytes subpacket, TaTransport transport)
    {
        append(to, std::move(subpacket), transport, markerCounter.take());
    }

    void TaOutboundBatcher::queueReply(PeerId to, TadBytes subpacket, TaTransport transport)
    {
        append(to, std::move(subpacket), transport, TaReplyMarker);
    }

    void TaOutboundBatcher::queueForAll(std::span<const PeerId> peers, TadBytes subpacket, TaTransport transport)
    {
        for (PeerId peer : peers)
        {
            append(peer, subpacket, transport, markerCounter.take());
        }
    }

    void TaOutboundBatcher::flush()
    {
        for (auto& [key, queue] : queues)
        {
            TaPacket packet;
            packet.type = packetType;
            packet.marker = queue.marker;
            packet.subpackets = std::move(queue.subpackets);
            TadBytes bytes = packet.build();
            send(key.peer, bytes, key.transport);
        }
        queues.clear();
    }

    std::size_t TaOutboundBatcher::queued(PeerId to, TaTransport transport) const
    {
        auto it = queues.find(Key{to, transport});
        return it == queues.end() ? 0 : it->second.subpackets.size();
    }
}
