#pragma once

// What a joining Total Annihilation does between arriving and playing, from
// the host's side: the status and team records the battleroom is made of, the
// keepalive that keeps a joiner from offering us for rejection, unit sync by
// echoing the joiner's own unit ids, and the launch.
// docs/TA-NETWORK.md, "The battleroom", "Unit sync" and "Launch".
//
// TaHostSession owns the sockets and the handshake and reports a peer joining,
// application data arriving and a peer leaving. This owns what the bytes inside
// that data mean, and the timers a host needs to keep them coming. It knows
// nothing about a game: a launch says what a game needs to start and then gets
// out of the way.

#include <asio.hpp>
#include <rwe/io/tad/tad_encoders.h>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/io/tad/tad_util.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/net/ta/TaOutboundBatcher.h>
#include <rwe/net/ta/TaPinger.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rwe
{
    /**
     * The team an untouched battleroom reports, and what a player that has
     * chosen none says. It is a team in the wire's sense only in that it is a
     * byte: a player on it has not joined one, which is what taEveryPlayerOnOneTeam
     * turns on.
     */
    inline constexpr std::uint8_t TaNoTeam = 5;

    /** 0x20, the size of a player's status record on the wire. */
    inline constexpr std::size_t TaPlayerStatusSize = 186;

    /**
     * The 32-byte field at offset 1: the only text a 0x20 carries. The captures
     * put the map there in ASCII, NUL-padded.
     */
    inline constexpr std::size_t TaPlayerStatusMapNameOffset = 1;
    inline constexpr std::size_t TaPlayerStatusMapNameLength = 32;

    /** The DirectPlay id, at 0x91: a REJECT record names a player by it. */
    inline constexpr std::size_t TaPlayerStatusPlayerIdOffset = 145;

    /** The state byte; gpgnet4ta reads bit 0x20 of it as ready. */
    inline constexpr std::size_t TaPlayerStatusStateOffset = 156;

    /** The host's options byte. docs/TA-NETWORK.md, "The battleroom", for the bits. */
    inline constexpr std::size_t TaPlayerStatusOptionsOffset = 157;

    /** 0x02a, the progress the recorded host reached on its last step. */
    inline constexpr std::uint8_t TaLoadingComplete = 0x64;

    /**
     * What the host knows about one player in the battleroom.
     */
    struct TaPlayerStatus
    {
        /** Bytes 145-148. */
        std::uint32_t playerId{0};

        /** Bytes 1-32. Longer than the field is truncated, not a longer record. */
        std::string mapName;

        /** Byte 156. */
        std::uint8_t state{0};

        /** Byte 157, which only a host sends: a joiner leaves it zero. */
        std::uint8_t options{0};
    };

    /**
     * 0x20, built from fields over a template taken from a recorded host's
     * status. The record's body is undecoded past the four fields below
     * (docs/TA-DEMOS.md, D8), so the bytes that are not written are the
     * recorded host's, and the build says which:
     *
     * - 0: the code.
     * - 1-32: the map name, ASCII and NUL-padded. The only text a 0x20 carries.
     * - 33-140: zeros in every record of every capture.
     * - 141-144: `05 c0 03 00`, constant across the captures.
     * - 145-148: the DirectPlay id.
     * - 149-155: `01 00`, a byte, a byte, `00`, `01 00`. Bytes 151 and 152 are
     *   the only ones that differ between two peers, and both captures with a
     *   0x09 in them put the two peers on the same side, so nothing here can be
     *   told apart from a colour or a lobby slot.
     * - 156: the state byte.
     * - 157: the options byte.
     * - 158-169: the game settings, the same words the session description
     *   carries in its reserved fields: 4, 10, 10, 250 and 0x0103.
     * - 170-185: sixteen bytes, `29 05 be 6e` then zeros in the recorded host's
     *   and whatever a peer sends elsewhere; unidentified.
     */
    TadBytes taBuildPlayerStatus(const TaPlayerStatus& status);

    /**
     * The 0x20 a peer sent, or nothing for a record too short to hold the
     * fields.
     *
     * The map name is bytes 1-32 read as NUL-padded printable ASCII, which is
     * the shape the captures have, and an empty string for any other shape.
     * Bytes 1-32 are the only text a 0x20 carries; the scripted joiner of #430
     * puts its own name in the same bytes in UTF-16, and a host that read a map
     * out of that would refuse a game over a field it had not understood.
     */
    std::optional<TaPlayerStatus> taParsePlayerStatus(const TadBytes& subpacket);

    /** 0x24: whose team, and which. */
    TadBytes taBuildTeam(std::uint32_t playerId, std::uint8_t teamId);

    /**
     * 0x26, the 41-byte ident record the keepalive carries: the two player ids
     * and 32 bytes that every capture leaves empty.
     */
    TadBytes taBuildIdent(std::uint32_t playerId, std::uint32_t otherPlayerId);

    /** 0x2a: loading progress, a percentage. */
    TadBytes taBuildLoadingProgress(std::uint8_t percent);

    /** 0x1e: the game starts. Every capture says 1. */
    TadBytes taBuildStart(std::uint8_t value);

    /** 0x11: a unit's state word, a u16 unit id and a byte whose meaning is not decoded. */
    TadBytes taBuildUnitStateWord(std::uint16_t unitId, std::uint8_t value);

    /** 0x1a sub-type 0: the unit table's header, fourteen bytes of it. */
    TadBytes taBuildUnitSyncHeader();

    /**
     * 0x1a sub-type 3: one unit type id, its status and a limit of 0xffff.
     * `inUse` is what distinguishes 0x0001 from 0x0101, and a host answers
     * every id both ways.
     */
    TadBytes taBuildUnitSyncEcho(std::uint32_t unitTypeId, bool inUse);

    /** The unit type id in a 0x1a sub-type 2 record, or nothing for any other record. */
    std::optional<std::uint32_t> taParseUnitSyncId(const TadBytes& subpacket);

    /**
     * True when two or more of `teams` are the same real team and none is
     * another, which is the game TA's host refuses to start. TaNoTeam counts
     * for nothing: a player that has not chosen a team is not on one, and the
     * one-player game the host would otherwise refuse is a legal game.
     */
    bool taEveryPlayerOnOneTeam(std::span<const std::uint8_t> teams);

    /**
     * True when one of `theirs` names a map and it is not `ourMap`. A player
     * that has named none has not disagreed about anything.
     */
    bool taMapNamesDiffer(std::string_view ourMap, std::span<const std::string> theirs);

    /** What the host is doing, for a front end showing how far a game has got. */
    enum class TaBattleroomState
    {
        /** Nobody has joined, or the joiners are still syncing units. */
        Waiting,

        /** 0x08 has gone out and the loading ladder is walking. */
        Launching,

        /** The move to UDP has happened. */
        Launched,
    };

    /** What the host knows about one player in the battleroom. */
    struct TaBattleroomPeer
    {
        /** The DirectPlay id, which is the key everywhere else. */
        std::uint32_t playerId{0};

        /** What CREATEPLAYER carried, four characters wide in practice. */
        std::string name;

        /** TaNoTeam until the player's own 0x24 says otherwise. */
        std::uint8_t team{TaNoTeam};

        /** Bit 0x20 of the player's state byte. */
        bool ready{false};

        /** What the player's 0x20 named in bytes 1-32, empty until it does. */
        std::string mapName;

        /** Whether the sub-type 0 record has gone out, which the exchange needs. */
        bool unitSyncStarted{false};
    };

    struct TaBattleroomConfig
    {
        /** The map, which the status record names and a launch compares against. */
        std::string mapName;

        /** Byte 157. The bits are in docs/TA-NETWORK.md, "The battleroom". */
        std::uint8_t options{0x4F};

        /** The host's own team in the battleroom. */
        std::uint8_t hostTeam{TaNoTeam};

        /**
         * Byte 156 in each state. Every capture's host walked 0x01 in the
         * battleroom, 0x02 once loading, 0x22 in game over TCP and 0x32 over
         * UDP, and only a joiner ever set bit 0x20.
         */
        std::uint8_t lobbyState{0x01};
        std::uint8_t loadingState{0x02};
        std::uint8_t inGameState{0x22};
        std::uint8_t inGameUdpState{0x32};

        /** How often the battleroom keepalive goes out; a host that stops is offered for rejection. */
        std::chrono::milliseconds keepaliveInterval{2000};

        /** How often a ping request goes out, and how often one is expected back. */
        std::chrono::milliseconds pingInterval{2000};

        /** The gap between loading progress steps. */
        std::chrono::milliseconds loadingStepInterval{200};

        /** The ladder the progress walks, as the recorded host walked it. */
        std::vector<std::uint8_t> loadingProgress{0x00, 0x26, 0x36, TaLoadingComplete};

        /** Echo records to a message, which keeps an uncompressed one near the size TA sends. */
        std::size_t unitSyncBatchSize{35};

        /**
         * Ids answered out of one message of sub-type 2 records. A data set has
         * fewer unit types than this and no peer has more, so a peer cannot
         * make the queue grow without bound by sending a long message.
         */
        std::size_t maxUnitSyncIdsPerMessage{512};
    };

    /**
     * The host's half of the battleroom, unit sync and launch.
     *
     * Every record it sends goes out whole: the traffic batcher is flushed
     * before each of its own calls returns, and the one the pinger is on is
     * flushed with it, because a peer reading the traffic needs the messages
     * to be whole and a 0x07 kept apart from the records that travel with it.
     */
    class TaBattleroom
    {
    public:
        using PeerId = TaHostSession::PeerId;

        /** Where the host's commander goes, which the game decides. */
        struct LaunchParams
        {
            /** 0x09's type: a 1-based load-order index, so the data set decides it. */
            std::uint16_t commanderTypeIndex{0};

            /** 0x09's and 0x11's unit id: the commander's own block, slot zero. */
            std::uint16_t commanderUnitId{0};

            TadPosition commanderPosition;
            TadRotation commanderRotation;

            /** 0x11's byte, whose meaning is not decoded; every capture says 1. */
            std::uint8_t commanderStateWord{1};
        };

        /** What a game needs to know about the peer it is about to play. */
        struct JoinerInfo
        {
            TaPeerAddress address;

            /** What the joiner's own 0x24 said, or TaNoTeam. */
            std::uint8_t team{TaNoTeam};

            std::string name;

            /**
             * The corpus does not decode a side. Both peers in every capture
             * that reached a launch sat on the same one, so the only bytes that
             * differ between two players cannot be told apart from a colour or a
             * lobby slot, and this stays Watch -- neither -- until a game fills
             * it in from its own player table.
             */
            TadSide side{TadSide::Watch};
        };

        /**
         * @param host the session, for the host's own id, its sends and the
         *        addresses a launch reports.
         * @param traffic where the compressed traffic goes; a batcher of the
         *        caller's, because the pinger below needs one of its own and
         *        there is no reason for three of them.
         * @param tick the clock a 0x02 carries: milliseconds, near GetTickCount.
         */
        TaBattleroom(
            asio::io_context& ioContext,
            TaBattleroomHost& host,
            TaOutboundBatcher& traffic,
            TaPinger::TickSource tick,
            TaBattleroomConfig config);

        /** A peer finished the handshake: it gets our status, our team and the unit table's header. */
        void peerJoined(PeerId peer);

        /** A peer is gone. The host offers the session again. */
        void peerLeft(PeerId peer);

        /**
         * One application message. Unwraps it, answers every ping, echoes the
         * unit ids it carries and reads the sender's status and team, then
         * flushes. Bytes that are not a TA packet cost that message.
         */
        void handleAppData(PeerId from, std::span<const std::uint8_t> bytes, TaTransport transport);

        /**
         * Starts the launch: 0x08, the loading ladder to TaLoadingComplete,
         * 0x1e, SESSIONDESCCHANGED twice, and then the move to UDP.
         *
         * False when a host rule refuses it -- every player on one team, or a
         * joiner on another map -- which onRefused is told about too. A launch
         * already under way is not started again.
         */
        bool launch(const LaunchParams& params);

        /** A joiner's state byte has said ready for the first time. */
        void onReady(std::function<void(const TaBattleroomPeer&)> handler);

        /** The move to UDP has happened, once per joiner. */
        void onLaunched(std::function<void(const JoinerInfo&)> handler);

        /** A host rule has refused a launch, with the reason. */
        void onRefused(std::function<void(const std::string&)> handler);

        /** Everyone who has finished the handshake. */
        std::vector<PeerId> peerIds() const;

        std::optional<TaBattleroomPeer> peer(PeerId peer) const;

        /** Latency to a peer in ticks, once it has answered a request. */
        std::optional<std::uint32_t> roundTripTicks(PeerId peer) const;

        /** Ids answered and refused, for a caller logging the exchange. */
        std::uint32_t unitSyncEchoed() const { return unitSyncEchoed_; }
        std::uint32_t unitSyncRefused() const { return unitSyncRefused_; }

        TaBattleroomState state() const { return state_; }

    private:
        TaBattleroomHost& session;
        TaOutboundBatcher& traffic;
        TaBattleroomConfig config;

        TaOutboundBatcher pings;
        TaPinger pinger;

        std::map<PeerId, TaBattleroomPeer> peers_;
        std::map<PeerId, bool> readyAnnounced_;

        asio::steady_timer keepaliveTimer;
        asio::steady_timer pingTimer;
        asio::steady_timer loadingTimer;

        TaBattleroomState state_{TaBattleroomState::Waiting};
        LaunchParams launchParams_;
        std::size_t loadingStep_{0};
        std::uint32_t unitSyncEchoed_{0};
        std::uint32_t unitSyncRefused_{0};

        std::function<void(const TaBattleroomPeer&)> readyHandler;
        std::function<void(const JoinerInfo&)> launchedHandler;
        std::function<void(const std::string&)> refusedHandler;

        void armKeepalive();
        void armPings();
        void armLoadingStep();

        void sendKeepalive();
        void sendLoadingStep();
        void finishLaunch();
        void moveToUdp();

        /** Our status and team, as one message. */
        void queueStatus(std::uint8_t state, TaTransport transport);

        void echoUnitSync(PeerId from, const TaPacket& packet);

        /** The reason a launch may not go ahead, or an empty string. */
        std::string launchRefusal() const;

        void refuse(const std::string& reason);
    };
}
