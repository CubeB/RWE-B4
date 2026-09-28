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
    /** The team a battleroom reports for a player that has chosen none. */
    inline constexpr std::uint8_t TaNoTeam = 5;

    /** 0x20, the size of a player's status record on the wire. */
    inline constexpr std::size_t TaPlayerStatusSize = 186;

    /** Bytes 1-32: the map name, the only text a 0x20 carries. */
    inline constexpr std::size_t TaPlayerStatusMapNameOffset = 1;
    inline constexpr std::size_t TaPlayerStatusMapNameLength = 32;

    /** The DirectPlay id, at 0x91: a REJECT record names a player by it. */
    inline constexpr std::size_t TaPlayerStatusPlayerIdOffset = 145;

    /** 0 is ARM and 1 is CORE, as TadSide numbers them. */
    inline constexpr std::size_t TaPlayerStatusSideOffset = 150;

    /**
     * The player's colour, which arrives as 0xff and is given a value when the
     * player joins: 0xff is "not yet assigned" and not a colour.
     */
    inline constexpr std::size_t TaPlayerStatusColourOffset = 151;

    /** The state byte; gpgnet4ta reads bit 0x20 of it as ready. */
    inline constexpr std::size_t TaPlayerStatusStateOffset = 156;

    /** The options byte. docs/TA-NETWORK.md, "The battleroom", for the bits. */
    inline constexpr std::size_t TaPlayerStatusOptionsOffset = 157;

    /** 0x2a, the progress the recorded host reached on its last step. */
    inline constexpr std::uint8_t TaLoadingComplete = 0x64;

    /** The six fields a 0x20 places, at the offsets named above. */
    struct TaPlayerStatus
    {
        std::uint32_t playerId{0};

        /** Longer than the field is truncated, not a longer record. */
        std::string mapName;

        /** TadSide::Watch for a byte that is neither 0 nor 1. */
        TadSide side{TadSide::Arm};

        std::uint8_t colour{0};

        std::uint8_t state{0};

        /** Which only a host sends: a joiner leaves it zero. */
        std::uint8_t options{0};
    };

    /**
     * 0x20, built from fields over a template taken from a recorded host's
     * status. Six fields are placed (TA-DEMOS D8); the rest is the recorded
     * host's, and what each range is:
     *
     * - 0: the code. 33-140: zeros in every capture. 141-144: `05 c0 03 00`.
     * - 149: `01`. 153-155: `00 01 00`. 152: 1 in a host's record and 0 in a
     *   joiner's, in all four captures, which is what bit 0 of the state byte
     *   also does; it is unidentified, and the template happens to be a host's.
     * - 158-169: the game settings, the words the session description keeps in
     *   its reserved fields: 4, 10, 10, 250 and 0x0103.
     * - 170-185: `29 05 be 6e` then zeros. Unidentified.
     *
     * ta-sides.pcap holds 65 status records, 57 of them after two players have
     * exchanged their settings, and across those 57 the only bytes that ever
     * change are 150, 151 and 156 -- the side, the colour and the state.
     */
    TadBytes taBuildPlayerStatus(const TaPlayerStatus& status);

    /**
     * The 0x20 a peer sent, or nothing for a record too short to hold the
     * fields. The map name is bytes 1-32 read as NUL-padded printable ASCII --
     * the shape the captures have -- and empty for any other shape, so a name
     * written there in UTF-16 is not read as a map and does not refuse a game.
     */
    std::optional<TaPlayerStatus> taParsePlayerStatus(const TadBytes& subpacket);

    /** 0x24: whose team, and which. */
    TadBytes taBuildTeam(std::uint32_t playerId, std::uint8_t teamId);

    /** 0x26: the two player ids, and 32 bytes every capture leaves empty. */
    TadBytes taBuildIdent(std::uint32_t playerId, std::uint32_t otherPlayerId);

    /** How many colours a battleroom offers. */
    inline constexpr std::uint8_t TaColourCount = 10;

    /**
     * The colour a host grants a joiner's 0x17: the one asked for if nobody
     * has it, otherwise the next free one after it. 0xff when all are taken.
     */
    std::uint8_t taGrantColour(std::uint8_t wanted, std::span<const std::uint8_t> taken);

    /** 0x18: the host's answer to a 0x17, the colour the joiner may have. */
    TadBytes taBuildColourGrant(std::uint8_t colour);

    /** 0x22: a player's id and its 1-based number in the game, the host being 1. */
    TadBytes taBuildPlayerNumber(std::uint32_t playerId, std::uint8_t number);

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
     * another, which is the game TA's host refuses to start. TaNoTeam counts for
     * nothing, so neither does a one-player game or a game half of whose players
     * have not picked.
     */
    bool taEveryPlayerOnOneTeam(std::span<const std::uint8_t> teams);

    /** True when one of `theirs` names a map and it is not `ourMap`. */
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

        /** TadSide::Watch until the player's 0x20 says which one it is on. */
        TadSide side{TadSide::Watch};

        /** 0xff until the player's 0x20 gives it a colour. */
        std::uint8_t colour{0xFF};

        /** Bit 0x20 of the player's state byte. */
        bool ready{false};

        /** What the player's 0x20 named in bytes 1-32, empty until it does. */
        std::string mapName;
    };

    struct TaBattleroomConfig
    {
        /** The map, which the status record names and a launch compares against. */
        std::string mapName;

        /** Byte 157. The bits are in docs/TA-NETWORK.md, "The battleroom". */
        std::uint8_t options{0x4F};

        std::uint8_t hostTeam{TaNoTeam};

        /** Bytes 150 and 151 of the host's own status: its side and its colour. */
        TadSide side{TadSide::Arm};
        std::uint8_t colour{0};

        /**
         * Byte 156 in each state. Every capture's host walked 0x01 in the
         * battleroom, 0x02 once loading and 0x32 in game over UDP, and only a
         * joiner ever set bit 0x20.
         */
        std::uint8_t lobbyState{0x01};
        std::uint8_t loadingState{0x02};
        std::uint8_t inGameUdpState{0x32};

        /** A host that stops beating is offered for rejection. */
        std::chrono::milliseconds keepaliveInterval{2000};

        std::chrono::milliseconds pingInterval{2000};

        std::chrono::milliseconds loadingStepInterval{200};

        /** From the move to UDP to the first in-game bundle; a real host waits about 50 ms. */
        std::chrono::milliseconds gameStartDelay{50};

        /** Loading steps to wait for a joiner's 0x15 before launching anyway. */
        unsigned int maxLoadedWaits{10};

        /** The ladder the progress walks, as the recorded host walked it. */
        std::vector<std::uint8_t> loadingProgress{0x00, 0x26, 0x36, TaLoadingComplete};

        /** Echo records to a message, which keeps an uncompressed one near the size TA sends. */
        std::size_t unitSyncBatchSize{35};

        /**
         * Ids answered out of one message of sub-type 2 records. No data set has
         * more unit types than this, so a long message cannot make the queue grow
         * without bound.
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

            /** What the joiner's 0x20 said at bytes 150 and 151. */
            TadSide side{TadSide::Watch};
            std::uint8_t colour{0xFF};
        };

        /**
         * @param traffic the caller's batcher, framing compressed; the pinger
         *        below needs one of its own because a 0x02 does not compress.
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
         * One application message: answers every ping, echoes the unit ids it
         * carries, and reads the sender's status and team. Bytes that are not a
         * TA packet cost that message.
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

        /** Joiners in the order they joined; the host is number 1 and they follow. */
        std::vector<PeerId> joinOrder_;
        bool numbersSent_{false};

        asio::steady_timer keepaliveTimer;
        asio::steady_timer pingTimer;
        asio::steady_timer loadingTimer;

        TaBattleroomState state_{TaBattleroomState::Waiting};
        LaunchParams launchParams_;
        std::size_t loadingStep_{0};
        bool joinerLoaded_{false};
        unsigned int loadedWaits_{0};
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
        void sendGameStart();
        void armLaunchTimer(std::chrono::milliseconds after, std::function<void()> then);

        /** Our status and team, as one message. */
        void queueStatus(std::uint8_t state, TaTransport transport);

        void echoUnitSync(PeerId from, const TaPacket& packet);

        void grantColour(PeerId from, std::uint8_t wanted);

        /** The reason a launch may not go ahead, or an empty string. */
        std::string launchRefusal() const;

        void refuse(const std::string& reason);
    };
}
