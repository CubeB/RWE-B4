#include <catch2/catch_test_macros.hpp>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <string>
#include <vector>

/** The framing a live host puts around the subpackets it queues. */
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

        std::string toHex(const TadBytes& bytes)
        {
            static const char* digits = "0123456789abcdef";
            std::string out;
            out.reserve(bytes.size() * 2);
            for (std::uint8_t b : bytes)
            {
                out.push_back(digits[b >> 4]);
                out.push_back(digits[b & 0xF]);
            }
            return out;
        }

        TadBytes subpacket(std::uint8_t code, std::size_t size)
        {
            TadBytes out(size, 0x5A);
            out[0] = code;
            return out;
        }
    }

    TEST_CASE("queued subpackets go out as one packet per peer and transport", "[net][ta]")
    {
        std::vector<Sent> sent;
        TaOutboundBatcher batcher(
            [&](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
            },
            TadPacketCompressed);

        batcher.queue(1, subpacket(0x20, 186), TaTransport::Tcp);
        batcher.queue(1, subpacket(0x24, 6), TaTransport::Tcp);
        batcher.queue(1, subpacket(0x02, 13), TaTransport::Udp);
        batcher.queue(2, subpacket(0x07, 1), TaTransport::Tcp);
        REQUIRE(batcher.queued(1, TaTransport::Tcp) == 2);
        REQUIRE(batcher.queued(1, TaTransport::Udp) == 1);
        REQUIRE(batcher.queued(2, TaTransport::Udp) == 0);
        REQUIRE(sent.empty());

        batcher.flush();
        REQUIRE(sent.size() == 3);

        std::vector<std::uint8_t> codes;
        for (const Sent& one : sent)
        {
            auto parsed = taParsePacket(one.bytes);
            REQUIRE(parsed);
            REQUIRE(parsed->checksumValid);
            for (const auto& sub : parsed->packet.subpackets)
            {
                codes.push_back(sub[0]);
            }
        }
        REQUIRE(codes == std::vector<std::uint8_t>{0x20, 0x24, 0x02, 0x07});

        // A batcher asked for compressed packets gives one where compression
        // pays, and a plain one where it does not: a 0x07 is a single byte, so
        // framing it costs more than it saves.
        REQUIRE(sent[0].bytes[0] == TadPacketCompressed);
        REQUIRE(sent[2].bytes[0] == TadPacketUncompressed);

        REQUIRE(batcher.queued(1, TaTransport::Tcp) == 0);
        batcher.flush();
        REQUIRE(sent.size() == 3);
    }

    TEST_CASE("a packet takes the u32 of the first subpacket queued into it", "[net][ta]")
    {
        std::vector<Sent> sent;
        TaOutboundBatcher batcher(
            [&](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
            },
            TadPacketUncompressed);

        batcher.queueReply(1, subpacket(0x02, 13), TaTransport::Udp);
        batcher.queue(1, subpacket(0x07, 1), TaTransport::Udp);
        batcher.flush();

        REQUIRE(sent.size() == 1);
        auto parsed = taParsePacket(sent[0].bytes);
        REQUIRE(parsed);
        REQUIRE(parsed->packet.marker == TaReplyMarker);
        REQUIRE(toHex(parsed->packet.subpackets[0]).substr(0, 2) == "02");
    }

    TEST_CASE("a sender's own traffic takes a count and a reply does not", "[net][ta]")
    {
        std::vector<Sent> sent;
        TaOutboundBatcher batcher(
            [&](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
            },
            TadPacketUncompressed);

        batcher.queue(1, subpacket(0x06, 1), TaTransport::Udp);
        batcher.queue(1, subpacket(0x06, 1), TaTransport::Udp);
        batcher.flush();
        batcher.queue(1, subpacket(0x06, 1), TaTransport::Udp);
        batcher.queue(1, subpacket(0x06, 1), TaTransport::Udp);
        batcher.flush();

        REQUIRE(sent.size() == 2);
        auto first = taParsePacket(sent[0].bytes);
        auto second = taParsePacket(sent[1].bytes);
        REQUIRE(first);
        REQUIRE(second);
        REQUIRE(first->packet.marker == 0xfffffffe);
        REQUIRE(second->packet.marker == 0xfffffffc);
    }

    TEST_CASE("queueing for everyone reaches every peer and costs one packet each", "[net][ta]")
    {
        std::vector<Sent> sent;
        TaOutboundBatcher batcher(
            [&](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
            },
            TadPacketCompressed);

        const TaOutboundBatcher::PeerId peers[] = {3, 5};
        batcher.queueForAll(peers, subpacket(0x07, 1), TaTransport::Tcp);
        batcher.flush();

        REQUIRE(sent.size() == 2);
        REQUIRE(sent[0].peer == 3);
        REQUIRE(sent[1].peer == 5);
        for (const Sent& one : sent)
        {
            auto parsed = taParsePacket(one.bytes);
            REQUIRE(parsed);
            REQUIRE(parsed->packet.subpackets.size() == 1);
            REQUIRE(parsed->packet.subpackets[0][0] == 0x07);
        }
    }

    TEST_CASE("a batcher frames one way", "[net][ta]")
    {
        std::vector<Sent> sent;
        TaOutboundBatcher batcher(
            [&](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
            },
            TadPacketUncompressed);

        batcher.queue(1, subpacket(0x20, 186), TaTransport::Tcp);
        batcher.flush();

        auto parsed = taParsePacket(sent[0].bytes);
        REQUIRE(parsed);
        REQUIRE(parsed->packet.type == TadPacketUncompressed);
        REQUIRE(parsed->packet.subpackets.size() == 1);
    }
}
