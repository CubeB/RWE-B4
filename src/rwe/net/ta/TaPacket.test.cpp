#include <catch2/catch_test_macros.hpp>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/net/ta/TaPacket.h>
#include <rwe/net/ta/TaPinger.h>
#include <rwe/net/ta/ta_packet_captures.h>
#include <string>
#include <vector>

/**
 * The live envelope over packets a real Total Annihilation sent, and the
 * transforms it reuses from the demo reader.
 */
namespace rwe
{
    namespace
    {
        TadBytes fromHex(const char* hex)
        {
            TadBytes out;
            for (const char* p = hex; p[0] != '\0' && p[1] != '\0'; p += 2)
            {
                int value = 0;
                for (int i = 0; i < 2; ++i)
                {
                    const char c = p[i];
                    value <<= 4;
                    if (c >= '0' && c <= '9')
                    {
                        value |= c - '0';
                    }
                    else if (c >= 'a' && c <= 'f')
                    {
                        value |= c - 'a' + 10;
                    }
                    else
                    {
                        value |= c - 'A' + 10;
                    }
                }
                out.push_back(static_cast<std::uint8_t>(value));
            }
            return out;
        }

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

        /** What the transforms leave behind: a packet's plain form. */
        TadBytes plainForm(const TadBytes& wire)
        {
            TadBytes copy = wire;
            tadDecrypt(copy, 0);
            bool ok = true;
            return tadDecompress(copy, 3, ok);
        }

        /**
         * The marker and the subpackets, without the first three bytes. A
         * compressed packet's checksum covers the bytes that compression
         * replaced, so it does not survive a round trip and is not compared.
         */
        TadBytes bodyOf(const TadBytes& wire)
        {
            TadBytes plain = plainForm(wire);
            return TadBytes(plain.begin() + 3, plain.end());
        }

        std::string codesOf(const std::vector<TadBytes>& subpackets)
        {
            static const char* digits = "0123456789abcdef";
            std::string out;
            for (const auto& subpacket : subpackets)
            {
                out.push_back(digits[subpacket[0] >> 4]);
                out.push_back(digits[subpacket[0] & 0xF]);
            }
            return out;
        }

        // A real host's reply, ta-baseline.pcap at 20.72s, and the request it
        // answers at the same moment. Both are whole TA packets, so the envelope
        // and the 0x02 are checked at once.
        const char* hostRequestPacket = "038605b5fbfaf9055913d3030c0d0e0f660ed908";
        const char* hostRequestSubpacket = "02511ad90800000000760ed908";
        const char* hostReplyPacket = "03e106fcfbfaf9055811d3035c15d707640ed908";
        const char* hostReplySubpacket = "025018d9085018d908740ed908";
    }

    TEST_CASE("every TA packet in the spike's captures decodes to what the reference found", "[net][ta]")
    {
        std::size_t plain = 0;
        std::size_t compressed = 0;

        for (const auto& fixture : ta_test::capturedPackets())
        {
            CAPTURE(fixture.source, fixture.wire);

            auto parsed = taParsePacket(fromHex(fixture.wire));
            REQUIRE(parsed);
            REQUIRE(parsed->checksumValid);
            REQUIRE(parsed->stats.total() == 0);
            REQUIRE(parsed->packet.marker == fixture.marker);
            REQUIRE(codesOf(parsed->packet.subpackets) == fixture.codes);

            if (parsed->packet.type == TadPacketUncompressed)
            {
                ++plain;
            }
            else
            {
                ++compressed;
            }
        }

        // Both framings and both shapes of the marker have to be in there, or
        // the corpus is not the one the envelope was written against.
        REQUIRE(plain > 0);
        REQUIRE(compressed > 0);
        REQUIRE(ta_test::capturedPackets().size() == 1497);
    }

    TEST_CASE("a plain TA packet from the captures re-encodes byte for byte", "[net][ta]")
    {
        std::size_t checked = 0;
        for (const auto& fixture : ta_test::capturedPackets())
        {
            TadBytes wire = fromHex(fixture.wire);
            if (wire[0] != TadPacketUncompressed)
            {
                continue;
            }

            CAPTURE(fixture.source, fixture.wire);
            auto parsed = taParsePacket(wire);
            REQUIRE(parsed);
            REQUIRE(toHex(parsed->packet.build()) == fixture.wire);
            ++checked;
        }
        REQUIRE(checked == 340);
    }

    TEST_CASE("a compressed TA packet from the captures re-encodes to the same bytes inside", "[net][ta]")
    {
        std::size_t checked = 0;
        std::size_t exact = 0;
        for (const auto& fixture : ta_test::capturedPackets())
        {
            TadBytes wire = fromHex(fixture.wire);
            if (wire[0] != TadPacketCompressed)
            {
                continue;
            }

            CAPTURE(fixture.source, fixture.wire);
            auto parsed = taParsePacket(wire);
            REQUIRE(parsed);

            TadBytes rebuilt = parsed->packet.build();
            REQUIRE(bodyOf(rebuilt) == bodyOf(wire));
            if (rebuilt == wire)
            {
                ++exact;
            }
            ++checked;
        }

        REQUIRE(checked > 0);
        // tadCompress is gpgnet4ta's compressor, not TA's, so the two agree on
        // what the packet says and not always on how it is packed.
        REQUIRE(exact == 0);
    }

    TEST_CASE("a ping reply RWE builds is the one a real host sent, byte for byte", "[net][ta]")
    {
        auto request = TaPing{148445264, 148445264, 0x08D90E74};
        CAPTURE(TaPinger::buildPing(request));

        TaPacket packet;
        packet.type = TadPacketUncompressed;
        packet.marker = TaReplyMarker;
        packet.subpackets.push_back(TaPinger::buildPing(request));
        REQUIRE(toHex(TaPinger::buildPing(request)) == hostReplySubpacket);
        REQUIRE(toHex(packet.build()) == hostReplyPacket);
    }

    TEST_CASE("a ping request RWE builds is the one a real host sent, byte for byte", "[net][ta]")
    {
        TaPing request{148445777, 0, 0x08D90E76};

        TaPacket packet;
        packet.type = TadPacketUncompressed;
        packet.marker = 0xffffffb6;
        packet.subpackets.push_back(TaPinger::buildPing(request));
        REQUIRE(toHex(TaPinger::buildPing(request)) == hostRequestSubpacket);
        REQUIRE(toHex(packet.build()) == hostRequestPacket);
    }

    TEST_CASE("the u32 on a sender's own traffic counts down from 0xffffff", "[net][ta]")
    {
        TaMarkerCounter counter;
        REQUIRE(counter.take() == 0xfffffffe);
        REQUIRE(counter.take() == 0xfffffffd);
        REQUIRE(counter.take() == 0xfffffffc);
    }

    TEST_CASE("a packet built from nothing is a header and nothing else", "[net][ta]")
    {
        TaPacket packet;
        REQUIRE(packet.marker == TaReplyMarker);
        REQUIRE(packet.subpackets.empty());
        auto parsed = taParsePacket(packet.build());
        REQUIRE(parsed);
        REQUIRE(parsed->checksumValid);
        REQUIRE(parsed->packet.marker == TaReplyMarker);
        REQUIRE(parsed->packet.subpackets.empty());
    }

    TEST_CASE("a packet of fewer than seven bytes is not a packet", "[net][ta]")
    {
        for (std::size_t size = 0; size < TaPacketHeaderSize; ++size)
        {
            TadBytes tooShort(size, 0x03);
            CAPTURE(size);
            REQUIRE_FALSE(taParsePacket(tooShort));
        }
    }
}
