#pragma once

// The game a joining Total Annihilation is put into, and the DirectPlay host
// in front of it.
//
// The host session and the battleroom are asio objects and the game is not:
// `TaHostGame` owns an io_context on a thread of its own and is the only thing
// that touches them, and it moves everything across in one mutex-guarded queue
// in each direction. Nothing the network thread produces reaches the scene or
// the simulation except by being taken from `takeInbound` on the game's own
// thread, and nothing the game produces is sent except by being put through
// `queueOutbound`.
//
// It is in three phases, which is also its lifetime:
//
// - a waiting panel reads `lobbyState()` and asks for a launch;
// - `launch` runs the battleroom's launch, and the joiner is in game;
// - `leave` sends the quit sequence and stops the thread.
//
// docs/TA-NETWORK.md, "Playtesting against a real TA" and "DirectPlay".

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/io/tad/tad_util.h>
#include <rwe/net/ta/TaBattleroom.h>
#include <rwe/net/ta/TaHostSession.h>
#include <rwe/net/ta/TaPacket.h>
#include <string>
#include <vector>

namespace rwe
{
    /** One thing the network thread has for the game thread. */
    struct TaHostInbound
    {
        enum class Kind
        {
            /** A peer's decoded TA packet, at most one per message. */
            Packet,

            /** A peer has gone: DELETEPLAYER, or a connection that timed out. */
            PeerLeft,
        };

        Kind kind{Kind::Packet};

        /** TaHostSession::PeerId, which is what a send is addressed by. */
        std::uint32_t peer{0};

        /**
         * The packet's sender, as the driver is keyed on it: the joiner is the
         * second player of the session, which is the block its 0x2c describes
         * and the sender byte the driver wants.
         */
        std::uint8_t sender{1};

        /**
         * The packet's marker, the per-sender count that falls by one per
         * packet. Nothing means a reply, which counts for nothing in its
         * sender's order; TaLiveReceiver takes that as the sequence.
         */
        std::optional<std::uint32_t> sequence;

        TaPacket packet;
        std::vector<TadBytes> subPackets;
    };

    /** What the waiting panel shows about one player in the battleroom. */
    struct TaHostLobbyPeer
    {
        std::string name;
        TadSide side{TadSide::Watch};
        std::uint8_t colour{0xFF};
        std::uint8_t team{TaNoTeam};

        /** Bit 0x20 of the player's own state byte. */
        bool ready{false};
    };

    struct TaHostLobbyState
    {
        std::vector<TaHostLobbyPeer> peers;
        TaBattleroomState phase{TaBattleroomState::Waiting};

        /** Why the last launch was refused, or empty. */
        std::string refusal;

        /** Set once the joiner is in game, with its side, colour, ids and ports. */
        std::optional<TaBattleroom::JoinerInfo> launched;

        /** The ports bound, which is what a real TA has to be able to find. */
        std::uint16_t enumPort{0};
        std::uint16_t gameTcpPort{0};
        std::uint16_t gameUdpPort{0};
    };

    struct TaHostGameConfig
    {
        std::string mapName;

        /** The session's name is this padded to 16, then the map's. */
        std::string gameName{"rwe"};

        /** Bytes 150 and 151 of the host's own status, and RWE's own side. */
        TadSide side{TadSide::Arm};
        std::uint8_t colour{0};

        std::uint8_t team{TaNoTeam};

        /** Byte 157; the bits are in docs/TA-NETWORK.md, "The battleroom". */
        std::uint8_t options{0x4F};

        /** The id block size both sides agree on, which sets the 0x2c cycle. */
        std::uint16_t maxUnits{250};

        bool autoLaunch{false};

        /**
         * The three listening ports, which are the real ones unless a caller
         * shifts them: `TaHostPorts::atBase` is what --port-base is, and zero
         * in any of them is the OS choosing one, which is what a test wants.
         */
        TaHostPorts ports;
    };

    /** What a run of the host did, for the log. */
    struct TaHostGameStats
    {
        /** Messages the game thread queued and the network thread could not send. */
        std::uint64_t outboundDropped{0};

        /** Packets the game thread had no room for, which a stalled frame causes. */
        std::uint64_t inboundDropped{0};

        /** Messages that were not a TA packet, and cost themselves. */
        std::uint64_t packetsNotTa{0};
    };

    /**
     * Hosts a game for a joining TA and carries it once launched.
     *
     * The constructor binds the ports, so a host that cannot have them says so
     * there rather than in the middle of a game's first frame. The thread is
     * running by the time it returns.
     */
    class TaHostGame
    {
    public:
        explicit TaHostGame(TaHostGameConfig config);

        ~TaHostGame();

        TaHostGame(const TaHostGame&) = delete;
        TaHostGame& operator=(const TaHostGame&) = delete;

        const TaHostGameConfig& config() const;

        /** The battleroom as the panel shows it, taken under the lock. */
        TaHostLobbyState lobbyState() const;

        /**
         * Starts the launch, with the host's real commander. The joiner is in
         * game a second or two later, once the loading ladder has walked;
         * `lobbyState().launched` says so.
         */
        void launch(TaBattleroom::LaunchParams params);

        /** The launch, if the joiner is ready and it is wanted at once. */
        void autoLaunchIfReady(TaBattleroom::LaunchParams params);

        /** The player slot the joiner takes in RWE's game; the first is ours. */
        static constexpr unsigned int remotePlayerSlot = 1;

        /**
         * The id block this host's own units sit in. A joining TA takes block
         * 0 for itself, so the host's is 1: ta-baseline.pcap has the recorded
         * host's commander at 251 and the joiner's at 1, with maxUnits 250.
         * Two owners in one block is every unit of one of them deleted on the
         * other machine, because an empty full-state record is a deletion.
         */
        static constexpr unsigned int hostUnitBlock = 1;

        /** The wire id of the host's commander, which is block 1's slot 0. */
        std::uint16_t commanderUnitId() const;

        /** Takes everything the network thread has queued, and clears it. */
        std::vector<TaHostInbound> takeInbound();

        /**
         * The hosted map's checksum, for bytes 170-173 of the host's status.
         * The panel computes it from the map data it has and leaves it here;
         * the battleroom puts it in every status from then on.
         */
        void setMapChecksum(std::uint32_t checksum);

        /**
         * Sends one message, from the game thread. Returns it to the outbound
         * queue, which the network thread empties; nothing here touches a
         * socket.
         */
        void queueOutbound(std::uint32_t peer, std::span<const std::uint8_t> bytes, TaTransport transport);

        /**
         * The game is over: the host commander's death with TA's cause-8 quit
         * death, then DELETEPLAYER for both of the host's own ids, then the
         * thread stops. Safe to call twice and before a launch.
         */
        void leave();

        TaHostGameStats stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
