#include <catch2/catch_test_macros.hpp>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/net/ta/TaPinger.h>
#include <string>
#include <vector>

/** Answering a peer live, and noticing that a peer answered. */
namespace rwe
{
    namespace
    {
        struct Sent
        {
            TaOutboundBatcher::PeerId peer;
            TaTransport transport;
            TadBytes bytes;
        };

        struct Harness
        {
            std::vector<Sent> sent;
            std::uint32_t now{1000};
            std::uint32_t ourPlayerId{0x08D90E76};

            TaOutboundBatcher batcher{
                [this](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                    sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
                },
                TadPacketUncompressed};

            TaPinger pinger{
                batcher,
                ourPlayerId,
                [this] { return now; }};

            /** The last packet sent to a peer, decoded. */
            std::optional<TaPacketParse> lastTo(TaOutboundBatcher::PeerId peer)
            {
                for (auto it = sent.rbegin(); it != sent.rend(); ++it)
                {
                    if (it->peer == peer)
                    {
                        return taParsePacket(it->bytes);
                    }
                }
                return std::nullopt;
            }
        };

        std::uint32_t requesterTickOf(const TaPacket& packet)
        {
            auto ping = TaPinger::parsePing(packet.subpackets[0]);
            return ping->requesterTick;
        }
    }

    TEST_CASE("a 0x02 request is answered live with our tick and their id", "[net][ta]")
    {
        Harness h;
        h.pinger.handle(0x08D90E74, TaPinger::buildPing(TaPing{148445264, 0, 0x08D90E74}), TaTransport::Udp);
        h.batcher.flush();

        REQUIRE(h.sent.size() == 1);
        REQUIRE(h.sent[0].peer == 0x08D90E74);
        REQUIRE(h.sent[0].transport == TaTransport::Udp);

        auto parsed = h.lastTo(0x08D90E74);
        REQUIRE(parsed);
        REQUIRE(parsed->packet.subpackets.size() == 1);

        auto ping = TaPinger::parsePing(parsed->packet.subpackets[0]);
        REQUIRE(ping);
        REQUIRE(ping->requesterTick == 148445264);
        REQUIRE(ping->responderTick == h.now);
        REQUIRE(ping->requesterPlayerId == 0x08D90E74);
        REQUIRE(parsed->packet.marker == TaReplyMarker);
        REQUIRE(parsed->packet.type == TadPacketUncompressed);
    }

    TEST_CASE("a request we send carries our own tick and player id, and goes over UDP", "[net][ta]")
    {
        Harness h;
        const TaOutboundBatcher::PeerId peers[] = {0x08D90E74, 0x08D90E77};
        h.pinger.sendRequests(peers);
        h.batcher.flush();

        REQUIRE(h.sent.size() == 2);
        for (const Sent& one : h.sent)
        {
            REQUIRE(one.transport == TaTransport::Udp);
            auto parsed = taParsePacket(one.bytes);
            REQUIRE(parsed);
            auto ping = TaPinger::parsePing(parsed->packet.subpackets[0]);
            REQUIRE(ping);
            REQUIRE(ping->requesterTick == h.now);
            REQUIRE(ping->responderTick == 0);
            REQUIRE(ping->requesterPlayerId == h.ourPlayerId);
        }
    }

    TEST_CASE("a reply records the round trip from our own clock", "[net][ta]")
    {
        Harness h;
        const TaOutboundBatcher::PeerId peers[] = {9};
        h.pinger.sendRequests(peers);
        std::uint32_t sentAt = h.now;
        h.batcher.flush();

        REQUIRE_FALSE(h.pinger.roundTripTicks(9));
        REQUIRE(h.pinger.outstandingTick(9) == sentAt);

        h.now += 37;
        h.pinger.handle(9, TaPinger::buildPing(TaPing{sentAt, h.now, 9}), TaTransport::Udp);
        REQUIRE(h.pinger.roundTripTicks(9) == 37);
        REQUIRE_FALSE(h.pinger.outstandingTick(9));

        // One answer is all it takes; the next request is timed from scratch.
        h.pinger.sendRequests(peers);
        h.batcher.flush();
        REQUIRE(h.pinger.outstandingTick(9) == sentAt + 37);
        REQUIRE(h.pinger.roundTripTicks(9) == 37);
    }

    TEST_CASE("a replayed ping is not a round trip", "[net][ta]")
    {
        Harness h;
        const TaOutboundBatcher::PeerId peers[] = {9};
        h.pinger.sendRequests(peers);
        std::uint32_t sentAt = h.now;
        h.batcher.flush();

        // A tick from another session: the elapsed time it would give is the
        // absurd ping a replay produces, so it is thrown away.
        h.pinger.handle(9, TaPinger::buildPing(TaPing{sentAt - 900000, 12345, 9}), TaTransport::Udp);
        REQUIRE_FALSE(h.pinger.roundTripTicks(9));
        REQUIRE(h.pinger.outstandingTick(9) == sentAt);

        // A peer that never had a request from us is not timed either.
        h.pinger.handle(11, TaPinger::buildPing(TaPing{sentAt, 1, 11}), TaTransport::Udp);
        REQUIRE_FALSE(h.pinger.roundTripTicks(11));
    }

    TEST_CASE("only a whole 0x02 is a ping", "[net][ta]")
    {
        REQUIRE(TaPinger::parsePing(TaPinger::buildPing(TaPing{1, 2, 3})));
        REQUIRE_FALSE(TaPinger::parsePing({}));
        REQUIRE_FALSE(TaPinger::parsePing({0x02}));

        TadBytes shortOne = TaPinger::buildPing(TaPing{1, 2, 3});
        shortOne.pop_back();
        REQUIRE_FALSE(TaPinger::parsePing(shortOne));

        TadBytes wrongCode = TaPinger::buildPing(TaPing{1, 2, 3});
        wrongCode[0] = 0x07;
        REQUIRE_FALSE(TaPinger::parsePing(wrongCode));

        TadBytes longer = TaPinger::buildPing(TaPing{1, 2, 3});
        longer.push_back(0);
        REQUIRE_FALSE(TaPinger::parsePing(longer));
    }

    TEST_CASE("a subpacket that is not a ping is left alone", "[net][ta]")
    {
        Harness h;
        h.pinger.handle(9, {0x20, 0x00}, TaTransport::Tcp);
        h.pinger.handle(9, TaPinger::buildPing(TaPing{1, 2, 3}), TaTransport::Udp);
        h.batcher.flush();

        // The 0x20 is not a ping and neither is a reply to a request we never
        // sent, so nothing goes back out.
        REQUIRE(h.sent.empty());
    }

    TEST_CASE("a ping travels inside a packet the envelope can read back", "[net][ta]")
    {
        Harness h;
        const TaOutboundBatcher::PeerId peers[] = {7};
        h.pinger.sendRequests(peers);
        h.batcher.flush();

        auto parsed = h.lastTo(7);
        REQUIRE(parsed);
        REQUIRE(parsed->checksumValid);
        REQUIRE(requesterTickOf(parsed->packet) == h.now);
        REQUIRE(parsed->packet.marker == 0xfffffffe);
    }
}
