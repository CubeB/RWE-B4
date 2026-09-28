#include "TaBattleroom.h"

#include <rwe/net/ta/TaPacket.h>
#include <algorithm>
#include <array>
#include <utility>

namespace rwe
{
    namespace
    {
        /** The sizes the subpacket table in rwe/io/tad gives these codes. */
        constexpr std::size_t TeamSize = 6;
        constexpr std::size_t IdentSize = 41;
        constexpr std::size_t UnitStateWordSize = 4;
        constexpr std::size_t UnitSyncSize = 14;
        constexpr std::size_t ColourRequestSize = 2;
        constexpr std::size_t PlayerNumberSize = 6;

        /** 0x1a sub-types, which are the byte after the code. */
        constexpr std::uint8_t UnitSyncHeaderSubType = 0x00;
        constexpr std::uint8_t UnitSyncIdSubType = 0x02;
        constexpr std::uint8_t UnitSyncEchoSubType = 0x03;

        void writeU16(TadBytes& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void writeU32(TadBytes& out, std::uint32_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
            out.push_back(static_cast<std::uint8_t>(value >> 16));
            out.push_back(static_cast<std::uint8_t>(value >> 24));
        }

        std::uint32_t readU32(const std::uint8_t* p)
        {
            return static_cast<std::uint32_t>(p[0])
                | (static_cast<std::uint32_t>(p[1]) << 8)
                | (static_cast<std::uint32_t>(p[2]) << 16)
                | (static_cast<std::uint32_t>(p[3]) << 24);
        }

        TadBytes code(TadSubPacketCode value)
        {
            return TadBytes{static_cast<std::uint8_t>(value)};
        }

        /** A side byte is 0 or 1; anything else names no side. */
        TadSide sideOf(std::uint8_t value)
        {
            return value <= 1 ? static_cast<TadSide>(value) : TadSide::Watch;
        }

        /**
         * A recorded host's status, from ta-baseline.pcap at 20.610: the first
         * one it sent, before anything in the lobby was changed. taBuildPlayerStatus
         * says which ranges of it are decoded.
         */
        constexpr std::array<std::uint8_t, TaPlayerStatusSize> CapturedHostStatus{
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
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    TadBytes taBuildPlayerStatus(const TaPlayerStatus& status)
    {
        TadBytes out(CapturedHostStatus.begin(), CapturedHostStatus.end());

        std::fill(
            out.begin() + static_cast<std::ptrdiff_t>(TaPlayerStatusMapNameOffset),
            out.begin() + static_cast<std::ptrdiff_t>(TaPlayerStatusMapNameOffset + TaPlayerStatusMapNameLength),
            static_cast<std::uint8_t>(0));

        // A map name longer than the field is truncated rather than allowed to
        // write over the id.
        for (std::size_t i = 0; i < status.mapName.size() && i < TaPlayerStatusMapNameLength; ++i)
        {
            auto character = static_cast<std::uint8_t>(status.mapName[i]);
            out[TaPlayerStatusMapNameOffset + i] = (character == 0) ? 0 : character;
        }

        for (std::size_t i = 0; i < 4; ++i)
        {
            out[TaPlayerStatusPlayerIdOffset + i] =
                static_cast<std::uint8_t>(status.playerId >> (8 * i));
        }
        out[TaPlayerStatusSideOffset] = static_cast<std::uint8_t>(status.side);
        out[TaPlayerStatusColourOffset] = status.colour;
        out[TaPlayerStatusStateOffset] = status.state;
        out[TaPlayerStatusOptionsOffset] = status.options;
        for (std::size_t i = 0; i < 4; ++i)
        {
            out[TaPlayerStatusMapChecksumOffset + i] =
                static_cast<std::uint8_t>(status.mapChecksum >> (8 * i));
        }
        return out;
    }

    std::optional<TaPlayerStatus> taParsePlayerStatus(const TadBytes& subpacket)
    {
        if (subpacket.size() < TaPlayerStatusSize ||
            subpacket[0] != static_cast<std::uint8_t>(TadSubPacketCode::PlayerInfo))
        {
            return std::nullopt;
        }

        TaPlayerStatus status;
        status.playerId = readU32(&subpacket[TaPlayerStatusPlayerIdOffset]);
        status.side = sideOf(subpacket[TaPlayerStatusSideOffset]);
        status.colour = subpacket[TaPlayerStatusColourOffset];
        status.state = subpacket[TaPlayerStatusStateOffset];
        status.options = subpacket[TaPlayerStatusOptionsOffset];
        status.mapChecksum = readU32(&subpacket[TaPlayerStatusMapChecksumOffset]);

        auto begin = subpacket.begin() + static_cast<std::ptrdiff_t>(TaPlayerStatusMapNameOffset);
        auto end = begin + static_cast<std::ptrdiff_t>(TaPlayerStatusMapNameLength);
        auto nul = std::find(begin, end, static_cast<std::uint8_t>(0));

        // NUL-padded printable ASCII and nothing else, so a name written here in
        // UTF-16 is not read as a map and does not refuse a game.
        bool printable = std::all_of(begin, nul, [](std::uint8_t c) { return c >= 0x20 && c < 0x7F; });
        bool padded = std::all_of(nul, end, [](std::uint8_t c) { return c == 0; });
        if (printable && padded)
        {
            status.mapName.assign(begin, nul);
        }
        return status;
    }

    TadBytes taBuildTeam(std::uint32_t playerId, std::uint8_t teamId)
    {
        TadBytes out;
        out.reserve(TeamSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::Team));
        writeU32(out, playerId);
        out.push_back(teamId);
        return out;
    }

    TadBytes taBuildIdent(std::uint32_t playerId, std::uint32_t otherPlayerId)
    {
        TadBytes out;
        out.reserve(IdentSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::Ident2));
        writeU32(out, playerId);
        writeU32(out, otherPlayerId);
        out.resize(IdentSize, 0);
        return out;
    }

    std::uint8_t taGrantColour(std::uint8_t wanted, std::span<const std::uint8_t> taken)
    {
        std::uint8_t start = wanted < TaColourCount ? wanted : 0;
        for (std::uint8_t i = 0; i < TaColourCount; ++i)
        {
            auto colour = static_cast<std::uint8_t>((start + i) % TaColourCount);
            if (std::find(taken.begin(), taken.end(), colour) == taken.end())
            {
                return colour;
            }
        }
        return 0xFF;
    }

    TadBytes taBuildColourGrant(std::uint8_t colour)
    {
        return TadBytes{static_cast<std::uint8_t>(TadSubPacketCode::HostMigration), colour};
    }

    TadBytes taBuildPlayerNumber(std::uint32_t playerId, std::uint8_t number)
    {
        TadBytes out;
        out.reserve(PlayerNumberSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::Ident3));
        writeU32(out, playerId);
        out.push_back(number);
        return out;
    }

    TadBytes taBuildLoadingProgress(std::uint8_t percent)
    {
        return TadBytes{static_cast<std::uint8_t>(TadSubPacketCode::LoadingProgress), percent};
    }

    TadBytes taBuildStart(std::uint8_t value)
    {
        return TadBytes{static_cast<std::uint8_t>(TadSubPacketCode::Start1e), value};
    }

    TadBytes taBuildUnitStateWord(std::uint16_t unitId, std::uint8_t value)
    {
        TadBytes out;
        out.reserve(UnitStateWordSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::UnitState));
        writeU16(out, unitId);
        out.push_back(value);
        return out;
    }

    TadBytes taBuildUnitSyncHeader()
    {
        TadBytes out;
        out.reserve(UnitSyncSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::UnitData));
        out.push_back(UnitSyncHeaderSubType);
        out.resize(UnitSyncSize, 0);
        return out;
    }

    TadBytes taBuildUnitSyncEcho(std::uint32_t unitTypeId, bool inUse)
    {
        TadBytes out;
        out.reserve(UnitSyncSize);
        out.push_back(static_cast<std::uint8_t>(TadSubPacketCode::UnitData));
        out.push_back(UnitSyncEchoSubType);
        out.resize(6, 0);
        writeU32(out, unitTypeId);
        writeU16(out, static_cast<std::uint16_t>(inUse ? 0x0101 : 0x0001));
        writeU16(out, 0xFFFF);
        return out;
    }

    std::optional<std::uint32_t> taParseUnitSyncId(const TadBytes& subpacket)
    {
        if (subpacket.size() < UnitSyncSize ||
            subpacket[0] != static_cast<std::uint8_t>(TadSubPacketCode::UnitData) ||
            subpacket[1] != UnitSyncIdSubType)
        {
            return std::nullopt;
        }
        return readU32(&subpacket[6]);
    }

    bool taEveryPlayerOnOneTeam(std::span<const std::uint8_t> teams)
    {
        std::optional<std::uint8_t> team;
        std::size_t onATeam = 0;
        for (std::uint8_t candidate : teams)
        {
            if (candidate == TaNoTeam)
            {
                continue;
            }
            ++onATeam;
            if (team && *team != candidate)
            {
                return false;
            }
            team = candidate;
        }
        // A player that has chosen no team is not on the one every other player
        // is on, so the rule needs two of them and one team: it is not the
        // one-player game, and not a game half of whose players have not picked.
        return onATeam >= 2;
    }

    TaBattleroom::TaBattleroom(
        asio::io_context& ioContext,
        TaBattleroomHost& host,
        TaOutboundBatcher& traffic,
        TaPinger::TickSource tick,
        TaBattleroomConfig config)
        : session(host),
          traffic(traffic),
          config(std::move(config)),
          pings(
              [this](PeerId peer, std::span<const std::uint8_t> bytes, TaTransport transport) {
                  session.send(peer, bytes, transport);
              },
              TadPacketUncompressed),
          pinger(pings, host.hostPlayerId(), std::move(tick)),
          keepaliveTimer(ioContext),
          pingTimer(ioContext),
          loadingTimer(ioContext)
    {
        lobbyState_ = this->config.lobbyState;
        armKeepalive();
        armPings();
    }

    void TaBattleroom::onReady(std::function<void(const TaBattleroomPeer&)> handler)
    {
        readyHandler = std::move(handler);
    }

    void TaBattleroom::onLaunched(std::function<void(const JoinerInfo&)> handler)
    {
        launchedHandler = std::move(handler);
    }

    void TaBattleroom::onRefused(std::function<void(const std::string&)> handler)
    {
        refusedHandler = std::move(handler);
    }

    std::vector<TaBattleroom::PeerId> TaBattleroom::peerIds() const
    {
        std::vector<PeerId> ids;
        ids.reserve(peers_.size());
        for (const auto& [id, peer] : peers_)
        {
            (void)peer;
            ids.push_back(id);
        }
        return ids;
    }

    std::optional<TaBattleroomPeer> TaBattleroom::peer(PeerId peer) const
    {
        auto it = peers_.find(peer);
        if (it == peers_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<std::uint32_t> TaBattleroom::roundTripTicks(PeerId peer) const
    {
        return pinger.roundTripTicks(peer);
    }

    void TaBattleroom::setMapChecksum(std::uint32_t checksum)
    {
        config.mapChecksum = checksum;
    }

    void TaBattleroom::peerJoined(PeerId peer)
    {
        peers_[peer] = TaBattleroomPeer{peer};
        peers_[peer].name = session.peerName(peer);
        readyAnnounced_[peer] = false;
        if (std::find(joinOrder_.begin(), joinOrder_.end(), peer) == joinOrder_.end())
        {
            joinOrder_.push_back(peer);
        }

        queueStatus(lobbyState_, TaTransport::Tcp);
        traffic.flush();

        // The unit table's header, the first of the joiner's 1 + 2n records. It
        // goes in a message of its own, as it does in the captures.
        traffic.queue(peer, taBuildUnitSyncHeader(), TaTransport::Tcp);
        traffic.flush();
    }

    void TaBattleroom::peerLeft(PeerId peer)
    {
        peers_.erase(peer);
        readyAnnounced_.erase(peer);
        joinOrder_.erase(std::remove(joinOrder_.begin(), joinOrder_.end(), peer), joinOrder_.end());
    }

    void TaBattleroom::handleAppData(PeerId from, std::span<const std::uint8_t> bytes, TaTransport transport)
    {
        auto parsed = taParsePacket(bytes);
        if (!parsed)
        {
            return;
        }

        const TaPacket& packet = parsed->packet;
        bool sawUnitSync = false;
        for (const auto& subpacket : packet.subpackets)
        {
            switch (static_cast<TadSubPacketCode>(subpacket[0]))
            {
                case TadSubPacketCode::Ping:
                    pinger.handle(from, subpacket, transport);
                    break;

                case TadSubPacketCode::PlayerInfo:
                {
                    auto status = taParsePlayerStatus(subpacket);
                    auto it = peers_.find(from);
                    if (!status || it == peers_.end())
                    {
                        break;
                    }
                    if (!status->mapName.empty())
                    {
                        it->second.mapName = status->mapName;
                    }
                    if (status->side != TadSide::Watch)
                    {
                        it->second.side = status->side;
                    }
                    if (status->colour != 0xFF)
                    {
                        it->second.colour = status->colour;
                    }
                    bool ready = (status->state & 0x20) != 0;
                    bool firstTime = ready && !it->second.ready && !readyAnnounced_[from];
                    it->second.ready = ready;
                    if (firstTime && readyHandler)
                    {
                        readyHandler(it->second);
                    }
                    break;
                }

                case TadSubPacketCode::Team:
                {
                    auto it = peers_.find(from);
                    if (it == peers_.end() || subpacket.size() < TeamSize)
                    {
                        break;
                    }
                    it->second.team = subpacket[5];
                    break;
                }

                case TadSubPacketCode::UnitData:
                    sawUnitSync = true;
                    break;

                case TadSubPacketCode::Unk17:
                    // A joiner asks for a colour and waits for the host's 0x18; a
                    // TA that never gets one loads the game as colour 0xff and crashes.
                    if (subpacket.size() >= ColourRequestSize && state_ == TaBattleroomState::Waiting)
                    {
                        grantColour(from, subpacket[1]);
                    }
                    break;

                case TadSubPacketCode::Start15:
                    // A joiner's TCP 0x15 answers our 0x1e: it is through loading.
                    // Only then may in-game traffic reach it.
                    if (transport == TaTransport::Tcp && state_ == TaBattleroomState::Launching)
                    {
                        joinerLoaded_ = true;
                    }
                    break;

                default:
                    break;
            }
        }

        // Once for the packet rather than once per record: a joiner sends its
        // whole table as one message, and answering each record separately would
        // echo the whole table back that many times over.
        if (sawUnitSync && transport == TaTransport::Tcp)
        {
            echoUnitSync(from, packet);
        }

        traffic.flush();
        pings.flush();
    }

    void TaBattleroom::grantColour(PeerId from, std::uint8_t wanted)
    {
        auto it = peers_.find(from);
        if (it == peers_.end())
        {
            return;
        }

        std::vector<std::uint8_t> taken{config.colour};
        for (const auto& [id, other] : peers_)
        {
            if (id != from && other.colour != 0xFF)
            {
                taken.push_back(other.colour);
            }
        }
        auto colour = taGrantColour(wanted, taken);
        if (colour == 0xFF)
        {
            return;
        }
        it->second.colour = colour;
        traffic.queueReply(from, taBuildColourGrant(colour), TaTransport::Tcp);
        traffic.flush();

        // Every capture's host numbers the players once, right after the first
        // grant, joiners first and itself last.
        if (!numbersSent_)
        {
            numbersSent_ = true;
            for (std::size_t i = 0; i < joinOrder_.size(); ++i)
            {
                traffic.queue(from, taBuildPlayerNumber(joinOrder_[i], static_cast<std::uint8_t>(i + 2)), TaTransport::Tcp);
            }
            traffic.queue(from, taBuildPlayerNumber(session.hostPlayerId(), 1), TaTransport::Tcp);
            traffic.flush();

            // RWE's host has no ready button, so it is ready from the moment
            // there is someone to play; a joiner shows a host without bit 0x20
            // as not ready.
            lobbyState_ = config.readyLobbyState;
            queueStatus(lobbyState_, TaTransport::Tcp);
            traffic.flush();
        }
    }

    void TaBattleroom::echoUnitSync(PeerId from, const TaPacket& packet)
    {
        std::vector<std::uint32_t> ids;
        for (const auto& subpacket : packet.subpackets)
        {
            auto id = taParseUnitSyncId(subpacket);
            if (id)
            {
                ids.push_back(*id);
            }
        }
        if (ids.empty())
        {
            return;
        }

        // A real host checks a joiner's checksums against its own; this one
        // echoes the ids back, which is all a joiner counts, so no checksum is
        // ever computed here. Each id is answered twice, in the joiner's order.
        auto answered = std::min(ids.size(), config.maxUnitSyncIdsPerMessage);
        unitSyncRefused_ += static_cast<std::uint32_t>(ids.size() - answered);

        std::size_t batched = 0;
        for (std::size_t i = 0; i < answered; ++i)
        {
            traffic.queueReply(from, taBuildUnitSyncEcho(ids[i], false), TaTransport::Tcp);
            traffic.queueReply(from, taBuildUnitSyncEcho(ids[i], true), TaTransport::Tcp);
            batched += 2;
            if (batched >= config.unitSyncBatchSize)
            {
                traffic.flush();
                batched = 0;
            }
        }
        unitSyncEchoed_ += static_cast<std::uint32_t>(answered);
    }

    void TaBattleroom::queueStatus(std::uint8_t state, TaTransport transport)
    {
        auto ids = peerIds();
        TaPlayerStatus status;
        status.playerId = session.hostPlayerId();
        status.mapName = config.mapName;
        status.side = config.side;
        status.colour = config.colour;
        status.state = state;
        status.options = config.options;
        status.mapChecksum = config.mapChecksum;

        traffic.queueForAll(ids, taBuildPlayerStatus(status), transport);
        traffic.queueForAll(ids, taBuildTeam(session.hostPlayerId(), config.hostTeam), transport);
    }

    void TaBattleroom::armKeepalive()
    {
        keepaliveTimer.expires_after(config.keepaliveInterval);
        keepaliveTimer.async_wait([this](const asio::error_code& error) {
            if (error)
            {
                return;
            }
            // A launch has stopped the battleroom: a peer in the game is kept
            // there by the 0x2c stream and its pings, not by a status beat.
            if (state_ == TaBattleroomState::Waiting)
            {
                sendKeepalive();
            }
            armKeepalive();
        });
    }

    void TaBattleroom::sendKeepalive()
    {
        auto ids = peerIds();
        if (ids.empty())
        {
            return;
        }

        // The 0x07 goes alone: in every capture the keepalive is a 0x07 in a
        // message of its own and then the status records, and a peer reads the
        // pair as the beat whose absence offers it for rejection.
        traffic.queueForAll(ids, code(TadSubPacketCode::Unk07), TaTransport::Tcp);
        traffic.flush();

        for (PeerId id : ids)
        {
            traffic.queue(
                id,
                taBuildIdent(session.hostPlayerId(), id),
                TaTransport::Tcp);
        }
        traffic.queueForAll(ids, code(TadSubPacketCode::PadEncrypt), TaTransport::Tcp);
        queueStatus(lobbyState_, TaTransport::Tcp);
        traffic.flush();
    }

    void TaBattleroom::armPings()
    {
        pingTimer.expires_after(config.pingInterval);
        pingTimer.async_wait([this](const asio::error_code& error) {
            if (error)
            {
                return;
            }
            // A launched game is kept alive by the 0x2c stream alone: every
            // capture's pings stop before the first in-game 0x2c, and a ping
            // received in game makes the peer flush its unit-state queue early.
            if (state_ == TaBattleroomState::Waiting)
            {
                auto ids = peerIds();
                if (!ids.empty())
                {
                    pinger.sendRequests(ids);
                    pings.flush();
                }
            }
            armPings();
        });
    }

    std::string TaBattleroom::launchRefusal() const
    {
        if (peers_.empty())
        {
            return "nobody has joined";
        }

        std::vector<std::uint8_t> teams{config.hostTeam};
        for (const auto& [id, peer] : peers_)
        {
            (void)id;
            teams.push_back(peer.team);
        }

        // No map rule: a joiner's 0x20 keeps naming its own last map whatever
        // the host picked (ta-small.pcap, Great Divide hosted, Canal Crossing
        // reported, launched), so it says nothing about what the joiner loads.
        if (taEveryPlayerOnOneTeam(teams))
        {
            return "every player is on one team";
        }
        return {};
    }

    void TaBattleroom::refuse(const std::string& reason)
    {
        if (refusedHandler)
        {
            refusedHandler(reason);
        }
    }

    bool TaBattleroom::launch(const LaunchParams& params)
    {
        if (state_ != TaBattleroomState::Waiting)
        {
            return false;
        }
        if (auto reason = launchRefusal(); !reason.empty())
        {
            refuse(reason);
            return false;
        }

        launchParams_ = params;
        loadingStep_ = 0;
        joinerLoaded_ = false;
        loadedWaits_ = 0;
        state_ = TaBattleroomState::Launching;

        // The real host's launch, from ta-baseline.pcap and ta-small.pcap:
        // SESSIONDESCCHANGED, then 08 06; a 2a/06 and a 07 every 200 ms, the
        // last 07 with the 1e; then, once the joiner has answered with its own
        // 0x15, the move to UDP and 50 ms later the first in-game bundle. A
        // 0x09 sent with the 1e reached a TA still loading, and it crashed.
        session.sendSessionDescChanged();
        auto ids = peerIds();
        traffic.queueForAll(ids, code(TadSubPacketCode::LoadingStarted), TaTransport::Tcp);
        traffic.queueForAll(ids, code(TadSubPacketCode::PadEncrypt), TaTransport::Tcp);
        traffic.flush();

        if (config.loadingProgress.size() < 2)
        {
            finishLaunch();
            return true;
        }
        armLoadingStep();
        return true;
    }

    void TaBattleroom::armLoadingStep()
    {
        armLaunchTimer(config.loadingStepInterval, [this] { sendLoadingStep(); });
    }

    void TaBattleroom::armLaunchTimer(std::chrono::milliseconds after, std::function<void()> then)
    {
        loadingTimer.expires_after(after);
        loadingTimer.async_wait([then = std::move(then)](const asio::error_code& error) {
            if (!error)
            {
                then();
            }
        });
    }

    void TaBattleroom::sendLoadingStep()
    {
        auto ids = peerIds();
        auto last = config.loadingProgress.size() - 1;
        if (loadingStep_ < last)
        {
            traffic.queueForAll(ids, taBuildLoadingProgress(config.loadingProgress[loadingStep_]), TaTransport::Tcp);
            traffic.queueForAll(ids, code(TadSubPacketCode::PadEncrypt), TaTransport::Tcp);
            traffic.flush();
            traffic.queueForAll(ids, code(TadSubPacketCode::Unk07), TaTransport::Tcp);
            if (loadingStep_ + 1 == last)
            {
                traffic.queueForAll(ids, taBuildStart(1), TaTransport::Tcp);
            }
            traffic.flush();
            ++loadingStep_;
            armLoadingStep();
            return;
        }

        // A real joiner answers the 1e within a step; one that has not after
        // ten is not going to, and the launch goes ahead rather than hang.
        if (!joinerLoaded_ && loadedWaits_ < config.maxLoadedWaits)
        {
            ++loadedWaits_;
            armLoadingStep();
            return;
        }
        finishLaunch();
    }

    void TaBattleroom::finishLaunch()
    {
        moveToUdp();
    }

    void TaBattleroom::moveToUdp()
    {
        auto ids = peerIds();
        traffic.queueForAll(ids, code(TadSubPacketCode::Start15), TaTransport::Udp);
        traffic.flush();
        traffic.queueForAll(ids, code(TadSubPacketCode::Unk07), TaTransport::Udp);
        traffic.flush();
        traffic.queueForAll(ids, taBuildLoadingProgress(TaLoadingComplete), TaTransport::Tcp);
        traffic.queueForAll(ids, code(TadSubPacketCode::PadEncrypt), TaTransport::Tcp);
        traffic.flush();

        armLaunchTimer(config.gameStartDelay, [this] { sendGameStart(); });
    }

    void TaBattleroom::sendGameStart()
    {
        auto ids = peerIds();
        auto builder = tadEncodeBuildStarted(TadBuildStarted{
            launchParams_.commanderTypeIndex,
            launchParams_.commanderUnitId,
            launchParams_.commanderPosition,
            launchParams_.commanderRotation});

        traffic.queueForAll(ids, taBuildLoadingProgress(TaLoadingComplete), TaTransport::Udp);
        traffic.queueForAll(ids, builder, TaTransport::Udp);
        traffic.queueForAll(
            ids,
            taBuildUnitStateWord(launchParams_.commanderUnitId, launchParams_.commanderStateWord),
            TaTransport::Udp);
        queueStatus(config.inGameUdpState, TaTransport::Udp);
        traffic.flush();
        session.sendSessionDescChanged();
        state_ = TaBattleroomState::Launched;

        for (PeerId id : ids)
        {
            if (!launchedHandler)
            {
                continue;
            }
            auto address = session.peerAddress(id);
            if (!address)
            {
                continue;
            }
            JoinerInfo info;
            info.address = *address;
            if (auto joiner = peer(id); joiner)
            {
                info.team = joiner->team;
                info.name = joiner->name;
                info.side = joiner->side;
                info.colour = joiner->colour;
            }
            launchedHandler(info);
        }
    }
}
