#pragma once

// One TA packet per send: subpackets queued for a peer on a transport are
// gathered into a single packet when the queue is flushed, so a reply and the
// keepalive that travels with it cost one packet rather than two.
// docs/TA-NETWORK.md, "TA packets".

#include <rwe/io/tad/tad_util.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/net/ta/TaPacket.h>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

namespace rwe
{
    /**
     * Collects subpackets per peer and transport and flushes one TA packet per
     * send. It knows nothing about what a subpacket means.
     */
    class TaOutboundBatcher
    {
    public:
        using PeerId = TaHostSession::PeerId;
        using Sender = std::function<void(PeerId, std::span<const std::uint8_t>, TaTransport)>;

        /**
         * @param send sends to one peer; TaHostSession::send fits.
         * @param packetType TadPacketUncompressed or TadPacketCompressed. One
         *        batcher frames one way: a 0x02 ping and the load packets go
         *        uncompressed, everything else compressed.
         */
        TaOutboundBatcher(Sender send, std::uint8_t packetType);

        /** Queues one subpacket on a sender's own traffic, which takes the next count. */
        void queue(PeerId to, TadBytes subpacket, TaTransport transport);

        /** Queues one subpacket as a reply, which carries TaReplyMarker. */
        void queueReply(PeerId to, TadBytes subpacket, TaTransport transport);

        /** Queues one subpacket for every peer, as a host's own keepalive. */
        void queueForAll(std::span<const PeerId> peers, TadBytes subpacket, TaTransport transport);

        /** Sends one packet per peer and transport with anything queued, and clears the queues. */
        void flush();

        /** Subpackets waiting for a peer on a transport. */
        std::size_t queued(PeerId to, TaTransport transport) const;

    private:
        struct Key
        {
            PeerId peer;
            TaTransport transport;

            bool operator<(const Key& other) const
            {
                return peer != other.peer ? peer < other.peer : transport < other.transport;
            }
        };

        struct Queue
        {
            std::vector<TadBytes> subpackets;

            /**
             * A packet carries one u32, so whichever subpacket was queued first
             * decides it. Mixing replies and own traffic in one packet is the
             * caller's business.
             */
            std::uint32_t marker{TaReplyMarker};
            bool markerSet{false};
        };

        Sender send;
        std::uint8_t packetType;
        TaMarkerCounter markerCounter;
        std::map<Key, Queue> queues;

        void append(PeerId to, TadBytes subpacket, TaTransport transport, std::uint32_t marker);
    };
}
