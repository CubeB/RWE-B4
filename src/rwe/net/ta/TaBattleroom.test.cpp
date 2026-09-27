#include <catch2/catch_test_macros.hpp>
#include <asio.hpp>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/net/ta/TaBattleroom.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <rwe/net/ta/TaPacket.h>
#include <rwe/net/ta/TaPinger.h>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

/** The battleroom built from fields: a status against a recorded one, the unit
 * sync echo, the host's own rules, and the launch in the order the captures put it. */
namespace rwe
{
    namespace
    {
        constexpr std::uint32_t HostId = 0x08D80E77;
        constexpr std::uint32_t JoinerId = 0x08D90E74;

        /** The host's first status in ta-baseline.pcap, verbatim, as the template came from it. */
        const std::vector<std::uint8_t> CapturedHostStatus{
            0x20, 0x43, 0x61, 0x6e, 0x61, 0x6c, 0x20, 0x43, 0x72, 0x6f, 0x73, 0x73,
            0x69, 0x6e, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xc0, 0x03,
            0x00, 0x76, 0x0e, 0xd9, 0x08, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
            0x01, 0x4f, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0xfa, 0x00,
            0x03, 0x01, 0x29, 0x05, 0xbe, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,};

        /**
         * The joiner's status from ta-sides.pcap at 34.82 and the host's at
         * 33.66, once the two have exchanged their settings: of the 65 records
         * in that capture, those 57 are the only bytes that ever differ, and
         * every case below is one of these with 150, 151 and 156 set. The map
         * name in bytes 1-32 is the one thing taken out, because it is text.
         */
        const std::vector<std::uint8_t> SidesJoinerStatus{
            0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xc0, 0x03,
            0x00, 0xe8, 0xae, 0x3a, 0x0e, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00,
            0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0xfa, 0x00,
            0x03, 0x01, 0x29, 0x05, 0xbe, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,};

        const std::vector<std::uint8_t> SidesHostStatus{
            0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0xc0, 0x03,
            0x00, 0xea, 0xae, 0x3a, 0x0e, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
            0x02, 0x48, 0x04, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0xfa, 0x00,
            0x03, 0x01, 0x29, 0x05, 0xbe, 0x6e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,};

        /** One of those records with the side, the colour and the state the capture had. */
        TadBytes at(std::vector<std::uint8_t> record, std::uint8_t side, std::uint8_t colour, std::uint8_t state)
        {
            record[TaPlayerStatusSideOffset] = side;
            record[TaPlayerStatusColourOffset] = colour;
            record[TaPlayerStatusStateOffset] = state;
            return TadBytes(record.begin(), record.end());
        }

        struct Sent
        {
            TaOutboundBatcher::PeerId peer;
            TaTransport transport;
            TadBytes bytes;
        };

        /** A host with no sockets behind it: the battleroom only asks it for four things. */
        struct Session : TaBattleroomHost
        {
            /** Where the pings the battleroom's own batcher sends are recorded. */
            std::vector<Sent>* sent{nullptr};

            std::uint32_t hostPlayerId() const override { return HostId; }

            bool send(std::uint32_t peer, std::span<const std::uint8_t> bytes, TaTransport transport) override
            {
                if (sent)
                {
                    sent->push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
                }
                return true;
            }

            void sendSessionDescChanged() override { ++sessionDescChanged; }

            std::optional<TaPeerAddress> peerAddress(std::uint32_t peer) const override
            {
                return TaPeerAddress{
                    peer,
                    asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 34701),
                    asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 34751)};
            }

            std::string peerName(std::uint32_t) const override { return "rwe"; }

            int sessionDescChanged{0};

            explicit Session(std::vector<Sent>* where) : sent(where)
            {
            }
        };

        struct Harness
        {
            asio::io_context ioContext;
            std::vector<Sent> sent;
            Session session;
            std::uint32_t now{1000};

            TaOutboundBatcher traffic{
                [this](TaOutboundBatcher::PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                    sent.push_back(Sent{peer, transport, TadBytes(bytes.begin(), bytes.end())});
                },
                TadPacketCompressed};

            TaBattleroomConfig config;

            std::unique_ptr<TaBattleroom> room;

            Harness() : session(&sent)
            {
                config.mapName = "Canal Crossing";
                config.hostTeam = 0;
                config.keepaliveInterval = std::chrono::milliseconds(60000);
                config.pingInterval = std::chrono::milliseconds(60000);
                config.loadingStepInterval = std::chrono::milliseconds(1);
                room = std::make_unique<TaBattleroom>(
                    ioContext,
                    session,
                    traffic,
                    [this] { return now; },
                    config);
            }

            /** Every subpacket sent to a peer, in order, across both transports. */
            std::vector<std::pair<TaTransport, TadBytes>> subpacketsTo(TaOutboundBatcher::PeerId peer) const
            {
                std::vector<std::pair<TaTransport, TadBytes>> out;
                for (const auto& one : sent)
                {
                    if (one.peer != peer)
                    {
                        continue;
                    }
                    auto parsed = taParsePacket(one.bytes);
                    if (!parsed)
                    {
                        continue;
                    }
                    for (const auto& subpacket : parsed->packet.subpackets)
                    {
                        out.emplace_back(one.transport, subpacket);
                    }
                }
                return out;
            }

            std::vector<TadBytes> codesTo(TaOutboundBatcher::PeerId peer, TaTransport transport) const
            {
                std::vector<TadBytes> out;
                for (const auto& [sent, subpacket] : subpacketsTo(peer))
                {
                    if (sent == transport)
                    {
                        out.push_back(subpacket);
                    }
                }
                return out;
            }

            /** Feeds a packet built from subpackets to the room, as the session would. */
            void deliver(const TaPacket& packet, TaHostSession::PeerId from = JoinerId, TaTransport transport = TaTransport::Tcp)
            {
                room->handleAppData(from, packet.build(), transport);
            }

            void joinerStatus(std::uint8_t state, std::string mapName = {}, TaHostSession::PeerId from = JoinerId)
            {
                TaPlayerStatus status;
                status.playerId = from;
                status.mapName = std::move(mapName);
                status.state = state;
                TaPacket packet;
                packet.subpackets.push_back(taBuildPlayerStatus(status));
                deliver(packet, from);
            }

            /** A recorded 0x20 as it came off the wire, rather than one built here. */
            void deliverStatus(const TadBytes& status, TaHostSession::PeerId from = JoinerId)
            {
                TaPacket packet;
                packet.subpackets.push_back(status);
                deliver(packet, from);
            }
        };

        std::uint8_t codeOf(const TadBytes& subpacket)
        {
            return subpacket.empty() ? 0 : subpacket[0];
        }

        std::vector<std::uint8_t> codeList(const std::vector<TadBytes>& subpackets)
        {
            std::vector<std::uint8_t> out;
            for (const auto& one : subpackets)
            {
                out.push_back(codeOf(one));
            }
            return out;
        }

        TaBattleroom::LaunchParams launchParams()
        {
            TaBattleroom::LaunchParams params;
            params.commanderTypeIndex = 34;
            params.commanderUnitId = 251;
            params.commanderPosition = TadPosition{19922944, 5636096, 114294784};
            return params;
        }
    }

    TEST_CASE("a status built from fields is the recorded host's with its known bytes replaced", "[net][ta]")
    {
        TaPlayerStatus status;
        status.playerId = 0x08D90E76;
        status.mapName = "Canal Crossing";
        status.state = 0x01;
        status.options = 0x4F;

        REQUIRE(taBuildPlayerStatus(status) == CapturedHostStatus);
    }

    TEST_CASE("a side and a colour are read from bytes 150 and 151, and written back to them", "[net][ta]")
    {
        // What ta-sides.pcap recorded, in the order it happened: a joining
        // player has no colour until it is given one, and both players' sides
        // and colours move while the lobby is open.
        struct Case
        {
            std::uint8_t side;
            TadSide expected;
            std::uint8_t colour;
            std::uint8_t state;
        };

        SECTION("the joiner's status, at 32.8, 34.8, 43.0 and 44.5 seconds")
        {
            const Case cases[]{
                {0x00, TadSide::Arm, 0xFF, 0x00},
                {0x00, TadSide::Arm, 0x01, 0x02},
                {0x01, TadSide::Core, 0x01, 0x02},
                {0x01, TadSide::Core, 0x02, 0x02},
            };
            for (const auto& one : cases)
            {
                CAPTURE(one.side, one.colour, one.state);
                auto parsed = taParsePlayerStatus(at(SidesJoinerStatus, one.side, one.colour, one.state));
                REQUIRE(parsed);
                REQUIRE(parsed->side == one.expected);
                REQUIRE(parsed->colour == one.colour);
                REQUIRE(parsed->state == one.state);
            }
        }

        SECTION("the host's own status, at 33.7, 54.0, 56.0 and 66.2 seconds")
        {
            const Case cases[]{
                {0x00, TadSide::Arm, 0x00, 0x02},
                {0x01, TadSide::Core, 0x00, 0x02},
                {0x01, TadSide::Core, 0x01, 0x02},
                {0x00, TadSide::Arm, 0x01, 0x02},
            };
            for (const auto& one : cases)
            {
                CAPTURE(one.side, one.colour);
                auto parsed = taParsePlayerStatus(at(SidesHostStatus, one.side, one.colour, one.state));
                REQUIRE(parsed);
                REQUIRE(parsed->side == one.expected);
                REQUIRE(parsed->colour == one.colour);
                REQUIRE(parsed->options == 0x48);
            }
        }

        SECTION("the state byte as the two players readied and launched")
        {
            for (std::uint8_t state : {0x02, 0x22, 0x32})
            {
                CAPTURE(state);
                auto ready = taParsePlayerStatus(at(SidesHostStatus, 0x00, 0x01, state));
                REQUIRE(ready);
                REQUIRE(ready->side == TadSide::Arm);
                REQUIRE(ready->colour == 0x01);
                REQUIRE(ready->state == state);
            }
        }

        SECTION("a side byte that is neither ARM nor CORE names no side")
        {
            for (std::uint8_t side : {std::uint8_t{0x02}, std::uint8_t{0x7F}, std::uint8_t{0xFF}})
            {
                CAPTURE(side);
                REQUIRE(taParsePlayerStatus(at(SidesHostStatus, side, 0x00, 0x02))->side == TadSide::Watch);
            }
        }
    }

    TEST_CASE("the host's own status round trips, with its side and colour at the recorded offsets", "[net][ta]")
    {
        TaPlayerStatus status;
        status.playerId = 0x0E3AAEEA;
        status.side = TadSide::Core;
        status.colour = 0x01;
        status.state = 0x22;
        status.options = 0x48;

        REQUIRE(taBuildPlayerStatus(status) == at(SidesHostStatus, 0x01, 0x01, 0x22));

        auto parsed = taParsePlayerStatus(taBuildPlayerStatus(status));
        REQUIRE(parsed);
        REQUIRE(parsed->playerId == status.playerId);
        REQUIRE(parsed->side == TadSide::Core);
        REQUIRE(parsed->colour == 0x01);
        REQUIRE(parsed->state == 0x22);
        REQUIRE(parsed->options == 0x48);
    }

    TEST_CASE("a status carries the map name in bytes 1-32, padded with NULs", "[net][ta]")
    {
        TaPlayerStatus status;
        status.mapName = "Great Divide";
        auto bytes = taBuildPlayerStatus(status);

        REQUIRE(bytes.size() == TaPlayerStatusSize);
        REQUIRE(std::string(bytes.begin() + 1, bytes.begin() + 13) == "Great Divide");
        for (std::size_t i = 13; i < 33; ++i)
        {
            CAPTURE(i);
            REQUIRE(bytes[i] == 0);
        }
    }

    TEST_CASE("a map name longer than the field is truncated, not written over the id", "[net][ta]")
    {
        TaPlayerStatus status;
        status.playerId = 0x08D90E76;
        status.mapName = "a map name that will not fit in thirty-two bytes at all";

        auto bytes = taBuildPlayerStatus(status);
        REQUIRE(bytes.size() == TaPlayerStatusSize);
        REQUIRE(std::string(bytes.begin() + 1, bytes.begin() + 33) == "a map name that will not fit in ");
        REQUIRE(taParsePlayerStatus(bytes)->playerId == 0x08D90E76);
    }

    TEST_CASE("a status field that is not ASCII names no map, so no game is refused over it", "[net][ta]")
    {
        // The scripted joiner of #430 puts its own name in these bytes in
        // UTF-16, where a real capture has a map name in ASCII.
        TaPlayerStatus status;
        status.playerId = JoinerId;
        auto bytes = taBuildPlayerStatus(status);

        std::string const utf16Name = "rwe";
        std::fill(
            bytes.begin() + static_cast<std::ptrdiff_t>(TaPlayerStatusMapNameOffset),
            bytes.begin() + static_cast<std::ptrdiff_t>(TaPlayerStatusMapNameOffset + TaPlayerStatusMapNameLength),
            static_cast<std::uint8_t>(0));
        for (std::size_t i = 0; i < utf16Name.size(); ++i)
        {
            bytes[TaPlayerStatusMapNameOffset + 2 * i] = static_cast<std::uint8_t>(utf16Name[i]);
        }

        auto parsed = taParsePlayerStatus(bytes);
        REQUIRE(parsed);
        REQUIRE(parsed->mapName.empty());
        const std::vector<std::string> theirs{parsed->mapName};
        REQUIRE_FALSE(taMapNamesDiffer("Canal Crossing", theirs));
    }

    TEST_CASE("a status we build reads back as the fields it was built from", "[net][ta]")
    {
        TaPlayerStatus status;
        status.playerId = 0x08D90E74;
        status.mapName = "Coast To Coast";
        status.state = 0x22;
        status.options = 0x48;

        auto parsed = taParsePlayerStatus(taBuildPlayerStatus(status));
        REQUIRE(parsed);
        REQUIRE(parsed->playerId == status.playerId);
        REQUIRE(parsed->mapName == status.mapName);
        REQUIRE(parsed->state == status.state);
        REQUIRE(parsed->options == status.options);
    }

    TEST_CASE("a status short of its fields, or not a 0x20, is not one", "[net][ta]")
    {
        for (std::size_t size = 0; size < TaPlayerStatusSize; ++size)
        {
            TadBytes bytes(size, 0x20);
            CAPTURE(size);
            REQUIRE_FALSE(taParsePlayerStatus(bytes));
        }

        TadBytes full(CapturedHostStatus.begin(), CapturedHostStatus.end());
        full[0] = 0x21;
        REQUIRE_FALSE(taParsePlayerStatus(full));
    }

    TEST_CASE("a 0x1a sub-type 2 record's id, and nothing else, is read", "[net][ta]")
    {
        // The id at 6, and the checksum at 10 that a host never computes.
        TadBytes record{0x1A, 0x02, 0x00, 0x00, 0x00, 0x00, 0xb6, 0x78, 0xf8, 0xb8, 0x64, 0x86, 0xce, 0x1e};
        REQUIRE(taParseUnitSyncId(record) == 0xb8f878b6);

        for (std::uint8_t subType : {0x00, 0x01, 0x03, 0x04})
        {
            TadBytes other = record;
            other[1] = subType;
            CAPTURE(subType);
            REQUIRE_FALSE(taParseUnitSyncId(other));
        }

        for (std::size_t size = 0; size < 14; ++size)
        {
            TadBytes shortRecord(size, 0x00);
            if (size >= 2)
            {
                shortRecord[0] = 0x1A;
                shortRecord[1] = 0x02;
            }
            CAPTURE(size);
            REQUIRE_FALSE(taParseUnitSyncId(shortRecord));
        }
    }

    TEST_CASE("a joining peer is met with our status, our team and the unit table's header", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);

        auto sent = h.codesTo(JoinerId, TaTransport::Tcp);
        REQUIRE(codeList(sent) == std::vector<std::uint8_t>({
                                                         0x20,
                                                         0x24,
                                                         0x1A,
                                                     }));

        auto status = taParsePlayerStatus(sent[0]);
        REQUIRE(status);
        REQUIRE(status->playerId == HostId);
        REQUIRE(status->mapName == "Canal Crossing");
        REQUIRE(status->options == 0x4F);
        REQUIRE(sent[1][5] == 0);

        REQUIRE(sent[2] == taBuildUnitSyncHeader());
    }

    TEST_CASE("each batch of the joiner's ids is echoed in its order, both ways, uncompressed", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);

        TaPacket batch;
        for (std::uint32_t i = 0; i < 35; ++i)
        {
            TadBytes record{0x1A, 0x02, 0x00, 0x00, 0x00, 0x00};
            for (std::size_t b = 0; b < 4; ++b)
            {
                record.push_back(static_cast<std::uint8_t>((0x1000 + i) >> (8 * b)));
            }
            record.resize(14, 0);
            batch.subpackets.push_back(record);
        }
        h.deliver(batch);

        auto echoes = h.codesTo(JoinerId, TaTransport::Tcp);
        auto it = std::find_if(echoes.begin(), echoes.end(), [](const TadBytes& one) {
            return one.size() == 14 && one[1] == 0x03;
        });
        REQUIRE(it != echoes.end());

        // Two records per id, in the joiner's order: 0x0001 then 0x0101, the
        // second meaning in use, and a limit of 0xffff.
        std::vector<TadBytes> records;
        for (const auto& one : echoes)
        {
            if (one.size() == 14 && one[1] == 0x03)
            {
                records.push_back(one);
            }
        }
        REQUIRE(records.size() == 70);
        for (std::size_t i = 0; i < 35; ++i)
        {
            std::uint32_t expected = 0x1000 + static_cast<std::uint32_t>(i);
            REQUIRE(records[2 * i] == taBuildUnitSyncEcho(expected, false));
            REQUIRE(records[2 * i + 1] == taBuildUnitSyncEcho(expected, true));
            REQUIRE(records[2 * i][10] == 0x01);
            REQUIRE(records[2 * i][11] == 0x00);
            REQUIRE(records[2 * i + 1][10] == 0x01);
            REQUIRE(records[2 * i + 1][11] == 0x01);
        }
        REQUIRE(h.room->unitSyncEchoed() == 35);

        // Every echo is a reply, which is what a joiner counts.
        for (const auto& one : h.sent)
        {
            auto parsed = taParsePacket(one.bytes);
            REQUIRE(parsed);
            for (const auto& subpacket : parsed->packet.subpackets)
            {
                if (subpacket[0] == 0x1A && subpacket[1] == 0x03)
                {
                    REQUIRE(parsed->packet.marker == TaReplyMarker);
                }
            }
        }
    }

    TEST_CASE("a joiner's side and colour are read from its status and kept across beats", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);

        // A joining player has no colour until it is given one, and a side
        // neither 0 nor 1 is no side, so neither replaces what the peer is
        // already known to be.
        REQUIRE(h.room->peer(JoinerId)->side == TadSide::Watch);
        REQUIRE(h.room->peer(JoinerId)->colour == 0xFF);

        h.deliverStatus(at(SidesJoinerStatus, 0x00, 0xFF, 0x00));
        REQUIRE(h.room->peer(JoinerId)->side == TadSide::Arm);
        REQUIRE(h.room->peer(JoinerId)->colour == 0xFF);

        h.deliverStatus(at(SidesJoinerStatus, 0x00, 0x01, 0x02));
        REQUIRE(h.room->peer(JoinerId)->side == TadSide::Arm);
        REQUIRE(h.room->peer(JoinerId)->colour == 0x01);

        h.deliverStatus(at(SidesJoinerStatus, 0x01, 0x02, 0x02));
        REQUIRE(h.room->peer(JoinerId)->side == TadSide::Core);
        REQUIRE(h.room->peer(JoinerId)->colour == 0x02);
    }

    TEST_CASE("a launch reports the side and colour the joiner's status last said", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);
        h.deliverStatus(at(SidesJoinerStatus, 0x01, 0x02, 0x20));

        TaBattleroom::JoinerInfo launched;
        h.room->onLaunched([&](const TaBattleroom::JoinerInfo& info) { launched = info; });
        REQUIRE(h.room->launch(launchParams()));
        for (int i = 0; i < 50 && h.room->state() != TaBattleroomState::Launched; ++i)
        {
            h.ioContext.run_for(std::chrono::milliseconds(5));
        }

        REQUIRE(h.room->state() == TaBattleroomState::Launched);
        REQUIRE(launched.side == TadSide::Core);
        REQUIRE(launched.colour == 0x02);
    }

    TEST_CASE("the host's own status carries the side and colour it was configured with", "[net][ta]")
    {
        Harness h;
        h.config.side = TadSide::Core;
        h.config.colour = 7;
        h.room = std::make_unique<TaBattleroom>(
            h.ioContext,
            h.session,
            h.traffic,
            [&h] { return h.now; },
            h.config);
        h.room->peerJoined(JoinerId);

        auto status = taParsePlayerStatus(h.codesTo(JoinerId, TaTransport::Tcp)[0]);
        REQUIRE(status);
        REQUIRE(status->side == TadSide::Core);
        REQUIRE(status->colour == 7);
    }

    TEST_CASE("a joiner's status says which team it is on and whether it is ready", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);

        bool announced = false;
        h.room->onReady([&](const TaBattleroomPeer& peer) {
            announced = true;
            REQUIRE(peer.playerId == JoinerId);
        });

        h.joinerStatus(0x00);
        auto peer = h.room->peer(JoinerId);
        REQUIRE(peer);
        REQUIRE(peer->team == TaNoTeam);
        REQUIRE_FALSE(peer->ready);
        REQUIRE_FALSE(announced);

        TaPacket team;
        team.subpackets.push_back(taBuildTeam(JoinerId, 1));
        h.deliver(team);
        REQUIRE(h.room->peer(JoinerId)->team == 1);

        h.joinerStatus(0x20);
        REQUIRE(h.room->peer(JoinerId)->ready);
        REQUIRE(announced);

        announced = false;
        h.joinerStatus(0x20);
        REQUIRE_FALSE(announced);
    }

    TEST_CASE("a ping is answered live, from our own clock", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);

        h.now = 123456;
        TaPacket request;
        request.subpackets.push_back(TaPinger::buildPing(TaPing{99999, 0, JoinerId}));
        h.deliver(request, JoinerId, TaTransport::Udp);

        auto answers = h.codesTo(JoinerId, TaTransport::Udp);
        REQUIRE(answers.size() == 1);
        auto ping = TaPinger::parsePing(answers[0]);
        REQUIRE(ping);
        REQUIRE(ping->requesterTick == 99999);
        REQUIRE(ping->responderTick == 123456);
        REQUIRE(ping->requesterPlayerId == JoinerId);
    }

    TEST_CASE("a launch is refused when every player is on one team", "[net][ta]")
    {
        const std::uint8_t bothOnZero[]{0, 0};
        const std::uint8_t split[]{0, 1};
        const std::uint8_t oneAlone[]{TaNoTeam, TaNoTeam};
        const std::uint8_t oneRealOneNot[]{TaNoTeam, 0};
        const std::uint8_t alone[]{0};

        REQUIRE(taEveryPlayerOnOneTeam(bothOnZero));
        REQUIRE_FALSE(taEveryPlayerOnOneTeam(split));
        REQUIRE_FALSE(taEveryPlayerOnOneTeam(oneAlone));
        REQUIRE_FALSE(taEveryPlayerOnOneTeam(oneRealOneNot));
        REQUIRE_FALSE(taEveryPlayerOnOneTeam(alone));
        REQUIRE_FALSE(taEveryPlayerOnOneTeam({}));

        Harness h;
        h.config.hostTeam = 0;
        std::string reason;
        h.room->onRefused([&](const std::string& why) { reason = why; });
        h.room->peerJoined(JoinerId);

        TaPacket team;
        team.subpackets.push_back(taBuildTeam(JoinerId, 0));
        h.deliver(team);

        REQUIRE_FALSE(h.room->launch(launchParams()));
        REQUIRE(reason == "every player is on one team");
        REQUIRE(h.room->state() == TaBattleroomState::Waiting);
    }

    TEST_CASE("a launch is refused when a joiner says another map", "[net][ta]")
    {
        const std::vector<std::string> same{"Canal Crossing", ""};
        const std::vector<std::string> other{"Great Divide", ""};
        const std::vector<std::string> none{"", ""};

        REQUIRE_FALSE(taMapNamesDiffer("Canal Crossing", same));
        REQUIRE(taMapNamesDiffer("Canal Crossing", other));
        REQUIRE_FALSE(taMapNamesDiffer("Canal Crossing", none));

        Harness h;
        std::string reason;
        h.room->onRefused([&](const std::string& why) { reason = why; });
        h.room->peerJoined(JoinerId);
        h.joinerStatus(0x00, "Great Divide");

        REQUIRE_FALSE(h.room->launch(launchParams()));
        REQUIRE(reason == "a player is on another map");
    }

    TEST_CASE("a launch is refused when nobody has joined", "[net][ta]")
    {
        Harness h;
        std::string reason;
        h.room->onRefused([&](const std::string& why) { reason = why; });

        REQUIRE_FALSE(h.room->launch(launchParams()));
        REQUIRE(reason == "nobody has joined");
    }

    TEST_CASE("the launch sends the loading sequence, then the move to UDP, in that order", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);
        h.sent.clear();

        TaBattleroom::JoinerInfo launched;
        bool sawLaunch = false;
        h.room->onLaunched([&](const TaBattleroom::JoinerInfo& info) {
            launched = info;
            sawLaunch = true;
        });

        REQUIRE(h.room->launch(launchParams()));
        REQUIRE(h.room->state() == TaBattleroomState::Launching);

        for (int i = 0; i < 50 && h.room->state() != TaBattleroomState::Launched; ++i)
        {
            h.ioContext.run_for(std::chrono::milliseconds(5));
        }
        REQUIRE(h.room->state() == TaBattleroomState::Launched);

        // Over TCP: 0x08, the progress ladder to 0x64, and 0x1e.
        auto tcp = codeList(h.codesTo(JoinerId, TaTransport::Tcp));
        REQUIRE(tcp.front() == 0x08);
        REQUIRE(tcp.back() == 0x1E);
        REQUIRE(std::count(tcp.begin(), tcp.end(), 0x2A) == 4);

        std::vector<std::uint8_t> progress;
        for (const auto& subpacket : h.codesTo(JoinerId, TaTransport::Tcp))
        {
            if (subpacket[0] == 0x2A)
            {
                progress.push_back(subpacket[1]);
            }
        }
        REQUIRE(progress == std::vector<std::uint8_t>({0x00, 0x26, 0x36, 0x64}));

        REQUIRE(h.session.sessionDescChanged == 2);

        // Over UDP: 0x15, 0x07, then 0x2a, the status, the team, the 0x09 and the 0x11.
        auto udp = codeList(h.codesTo(JoinerId, TaTransport::Udp));
        REQUIRE(udp == std::vector<std::uint8_t>({
                               0x15,
                               0x07,
                               0x2A,
                               0x20,
                               0x24,
                               0x09,
                               0x11,
                           }));

        auto udpRecords = h.codesTo(JoinerId, TaTransport::Udp);
        auto build = std::find_if(udpRecords.begin(), udpRecords.end(), [](const TadBytes& one) {
            return one[0] == 0x09;
        });
        REQUIRE(build != udpRecords.end());
        REQUIRE((*build)[1] == 0x22);
        REQUIRE((*build)[2] == 0x00);
        REQUIRE((*build)[3] == 0xFB);
        REQUIRE((*build)[4] == 0x00);

        auto state = std::find_if(udpRecords.begin(), udpRecords.end(), [](const TadBytes& one) {
            return one[0] == 0x11;
        });
        REQUIRE(state != udpRecords.end());
        REQUIRE(*state == taBuildUnitStateWord(251, 1));

        auto status = taParsePlayerStatus(udpRecords[3]);
        REQUIRE(status);
        REQUIRE(status->state == 0x32);
        REQUIRE(status->playerId == HostId);

        REQUIRE(sawLaunch);
        REQUIRE(launched.address.playerId == JoinerId);
        REQUIRE(launched.address.tcp.port() == 34701);
        REQUIRE(launched.address.udp.port() == 34751);
        REQUIRE(launched.team == TaNoTeam);
        REQUIRE(launched.name == "rwe");
    }

    TEST_CASE("a second launch is not started", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);
        REQUIRE(h.room->launch(launchParams()));
        REQUIRE_FALSE(h.room->launch(launchParams()));
    }

    TEST_CASE("the keepalive is a 0x07 on its own, then the status records", "[net][ta]")
    {
        Harness h;
        h.room->peerJoined(JoinerId);
        h.sent.clear();

        // How many beats a run sees depends on how long it ran, so the check
        // is that they are all the same group of records.
        h.config.keepaliveInterval = std::chrono::milliseconds(1);
        h.room = std::make_unique<TaBattleroom>(
            h.ioContext,
            h.session,
            h.traffic,
            [&h] { return h.now; },
            h.config);
        h.room->peerJoined(JoinerId);
        h.sent.clear();

        h.ioContext.run_for(std::chrono::milliseconds(20));
        h.ioContext.restart();

        const std::vector<std::uint8_t> beat{0x07, 0x26, 0x06, 0x20, 0x24};
        auto sent = codeList(h.codesTo(JoinerId, TaTransport::Tcp));
        REQUIRE_FALSE(sent.empty());
        for (std::size_t i = 0; i < sent.size(); i += beat.size())
        {
            CAPTURE(i);
            REQUIRE(std::vector<std::uint8_t>(sent.begin() + static_cast<std::ptrdiff_t>(i),
                                              sent.begin() + static_cast<std::ptrdiff_t>(i) + beat.size())
                    == beat);
        }
        REQUIRE(sent.size() % beat.size() == 0);
    }
}
