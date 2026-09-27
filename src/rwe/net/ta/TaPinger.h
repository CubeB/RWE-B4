#pragma once

// 0x02, the ping. A request carries the requester's tick and player id; a reply
// adds the responder's tick and goes back to the requester alone. TA shows the
// difference of the two ticks as the peer's latency, so a reply has to be built
// live: a replayed one carries another session's clock and shows as an absurd
// ping, and a host that stops answering is offered for rejection.
// docs/TA-NETWORK.md, "The battleroom".

#include <rwe/io/tad/tad_util.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>

namespace rwe
{
    /** The three fields a 0x02 carries, all little-endian after the code. */
    struct TaPing
    {
        /** The clock of whoever sent the ping; a reply echoes it unchanged. */
        std::uint32_t requesterTick;

        /** The responder's clock. Zero in a request, which is how the two are told apart. */
        std::uint32_t responderTick;

        /** The player id to answer: the sender's in a request, the requester's in a reply. */
        std::uint32_t requesterPlayerId;
    };

    /**
     * Answers every 0x02 request live and sends its own on a timer the caller
     * owns.
     */
    class TaPinger
    {
    public:
        using PeerId = TaHostSession::PeerId;

        /**
         * Reads the caller's clock. The captures show milliseconds, near
         * GetTickCount: two peers a few seconds apart in uptime agree to the
         * millisecond, and the gap between the two ticks in a reply is the round
         * trip. It is a wall clock, not the game tick.
         */
        using TickSource = std::function<std::uint32_t()>;

        /**
         * @param batcher where a ping goes when it is queued. It should be
         *        framing uncompressed, which is how the captures carry a 0x02,
         *        though a peer reads either.
         * @param ourPlayerId the id a request we send carries.
         */
        TaPinger(TaOutboundBatcher& batcher, std::uint32_t ourPlayerId, TickSource tick);

        /**
         * Answers a 0x02 request from `from`, and records the round trip to a
         * reply. Anything that is not a 0x02 is left alone: the caller decides
         * what else to do with a subpacket.
         */
        void handle(PeerId from, const TadBytes& subpacket, TaTransport transport);

        /**
         * Sends a request to each peer, and remembers the tick for it. Call on a
         * timer; a request goes to everyone, so pass everyone.
         */
        void sendRequests(std::span<const PeerId> peers);

        /**
         * Ticks between sending the request a peer last answered and answering
         * it, from this side's own clock. Nothing for a peer that has not
         * answered, and nothing for a reply that echoes a tick we never sent:
         * that is a replay, and taking its clock for an elapsed time is what
         * makes a replayed ping read as an absurd latency.
         */
        std::optional<std::uint32_t> roundTripTicks(PeerId peer) const;

        /** The tick a request last sent to a peer carried, for a caller logging latency. */
        std::optional<std::uint32_t> outstandingTick(PeerId peer) const;

        /** The 0x02 subpacket: code, requester tick, responder tick, requester id. */
        static TadBytes buildPing(const TaPing& ping);

        /** Nothing for a subpacket that is not a whole 0x02. */
        static std::optional<TaPing> parsePing(const TadBytes& subpacket);

    private:
        TaOutboundBatcher& batcher;
        std::uint32_t ourPlayerId;
        TickSource tick;
        std::map<PeerId, std::uint32_t> outstanding;
        std::map<PeerId, std::uint32_t> lastRoundTrip;
    };
}
