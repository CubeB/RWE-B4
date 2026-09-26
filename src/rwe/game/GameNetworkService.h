#pragma once

#include <array>
#include <asio.hpp>
#include <chrono>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <network.pb.h>
#include <rwe/game/PeerLink.h>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/network_util.h>
#include <rwe/rwe_time.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <thread>
#include <vector>

namespace rwe
{
    /**
     * The transport half of the lockstep: a UDP socket, a send timer and the
     * thread they run on. It moves bytes and nothing else -- what a packet
     * means and what goes in one lives in PeerLink, one per remote peer. This
     * is also the lock discipline: the game thread posts work to the io
     * context and, where it needs an answer, waits for it, so no PeerLink is
     * reached from two threads.
     */
    class GameNetworkService
    {
    public:
        using CommandSet = PeerLink::CommandSet;
        using ReceivedChatMessage = PeerLink::ReceivedChatMessage;
        using PeerStatus = PeerLink::PeerStatus;

        static constexpr std::size_t MaxPendingChatMessages = PeerLink::MaxPendingChatMessages;
        static constexpr unsigned int MaxSetsAheadOfTheGame = PeerLink::MaxSetsAheadOfTheGame;
        static constexpr std::size_t MaxCommandSetBytes = PeerLink::MaxCommandSetBytes;
        static constexpr std::chrono::milliseconds SendInterval = PeerLink::SendInterval;

        static std::size_t commandsFittingOneSet(const std::vector<PlayerCommand>& commands);

        /**
         * A remote peer's address, plus where its streams begin. The seed a
         * game is built from; the protocol state that grows from it is a
         * PeerLink, held by the transport.
         */
        struct EndpointInfo
        {
            PlayerId playerId;
            asio::ip::udp::endpoint endpoint;

            SequenceNumber nextCommandToSend{0};
            SequenceNumber nextCommandToReceive{0};

            GameTime nextHashToSend{0};
            GameTime nextHashToReceive{0};

            EndpointInfo(const PlayerId& playerId, const asio::ip::udp::endpoint& endpoint)
                : playerId(playerId), endpoint(endpoint)
            {
            }
        };

        /**
         * `resumeFromSequence` is where this peer's own streams begin, which is
         * zero for a game being started and the rejoin tick minus one for a
         * peer rejoining one in progress: it has no sets or hashes of its own
         * for the ticks it missed, and the peers that stayed are expecting its
         * stream to pick up exactly there.
         */
        GameNetworkService(PlayerId localPlayerId, int port, const std::vector<EndpointInfo>& endpointSeeds, PlayerCommandService* playerCommandService, SequenceNumber resumeFromSequence = SequenceNumber(0));

        virtual ~GameNetworkService();

        void start();

        void submitCommands(SceneTime currentSceneTime, const CommandSet& commands);

        /**
         * Tell every peer how this one is running: its speed, whether it is
         * paused, whether it is stalled waiting on commands, and the speed its
         * machine can sustain. Carried in every packet, because a remote peer
         * projects this one's scene time by it and caps the game speed to it.
         */
        void submitRunState(unsigned int speedPermille, bool paused, bool stalled, unsigned int sustainableSpeedPermille);

        void submitGameHash(GameHash hash);

        /**
         * Queue a line of chat for every peer.
         *
         * Returns false if it was refused, which is what a peer already owed
         * MaxPendingChatMessages does: it has stopped acking, and the line
         * would only make the packets to it bigger.
         */
        bool submitChatMessage(const std::string& text);

        /** Everything that has arrived since the last call, in the order it arrived. */
        std::vector<ReceivedChatMessage> takeChatMessages();

        SceneTime estimateAvergeSceneTime(SceneTime localSceneTime);

        /**
         * Every remote peer's id and the speed its machine reported it can
         * sustain, for the speed governor. One call, off the network thread,
         * as estimateAvergeSceneTime is.
         */
        std::vector<PeerCapacity> peerSustainableSpeeds();

        float getMaxAverageRttMillis();

        /**
         * Whether there is a peer to wait for at all.
         *
         * A game with nobody else in it has no lockstep round trip to cover,
         * so the local player's orders do not need the buffer that pays for
         * one. See GameScene::localHumanCommandsAreFedPerTick.
         */
        bool hasRemotePeers() const;

        /**
         * Every peer, how long it has been quiet, and how far along it said it
         * was. One call rather than two because both come off the network
         * thread and both are wanted at the same moment: when a tick will not
         * go ahead and somebody has to decide whether a peer has gone.
         */
        std::vector<PeerStatus> getPeerStatuses();

        /**
         * Stop listening to a peer that has been dropped.
         *
         * Its stream is closed and the game has carried on past the tick it was
         * cut at, so anything further from it -- a packet still in flight, or a
         * peer that has come back to life -- would be commands after the cut,
         * which is a desync rather than a recovery. Coming back is issue #44's
         * third part and wants the save, not this.
         */
        void forgetPeer(PlayerId playerId);

        /**
         * Listen to a dropped peer again, and start sending to it from
         * `fromSequence` -- the position in this peer's own stream that the
         * returning one already has, which is the tick its catch-up bundle
         * ended at.
         *
         * `theirNextSequence` is where its stream resumes, so that a packet
         * from it carrying sets below that -- one still in flight from before
         * it went quiet, or a resend it has not learned is unwanted -- is
         * discarded rather than pushed after the ticks it missed.
         *
         * Returns false when this peer no longer holds its own stream back to
         * `fromSequence`. The bundle is then older than the rejoin history and
         * the gap cannot be closed from here; whoever asked has to build a
         * newer one.
         */
        bool rememberPeer(PlayerId playerId, SequenceNumber fromSequence, SequenceNumber theirNextSequence);

        /**
         * Stops handing arriving commands and hashes to the simulation, or
         * starts again. A peer winding itself forward into a game in progress
         * starts with this off.
         */
        void setAcceptingCommands(bool value);

    private:
        /**
         * A remote peer's address and its protocol state. Built on the game
         * thread before start(); owned and used by the network thread after.
         */
        struct PeerEndpoint
        {
            PlayerId playerId;
            asio::ip::udp::endpoint endpoint;
            std::unique_ptr<PeerLink> link;
        };

        /**
         * The id stamped on the next packet to any peer. Only ever logged, so
         * a counter will do and the wire stays free of a hidden entropy source.
         */
        int nextPacketIdCounter{0};

        PlayerId localPlayerId;
        int port;

        std::thread networkThread;

        asio::io_context ioContext;
        asio::ip::udp::resolver resolver;
        asio::ip::udp::socket socket;
        asio::steady_timer sendTimer;

        std::vector<PeerEndpoint> endpoints;

        /** This peer's own command and hash streams, shared by every peer. */
        LocalStream localStream;

        /**
         * Whether anybody is on the other end of this game. Fixed when the
         * service is built: a game that began with nobody else never gains a
         * peer, and one that began with peers keeps the lockstep buffer even
         * if they are later dropped, because the local player's orders still
         * have to be held for the tick they were agreed at.
         */
        const bool remotePeersPresent;

        /**
         * The address of every peer that has been forgotten, so that one which
         * comes back can be listened to again without being told where it is.
         */
        std::vector<std::pair<PlayerId, asio::ip::udp::endpoint>> forgottenEndpoints;

        /**
         * Whether what arrives is handed to the simulation yet, mirrored to
         * every PeerLink, which is where the refusal happens. Kept here as well
         * so that a peer remembered into a catch-up is born with the right
         * setting; see setAcceptingCommands.
         */
        bool acceptingCommands{true};

        /**
         * The scene time last submitted, so that a peer remembered into a game
         * in progress reports where the game is rather than zero. Mirrored into
         * each PeerLink by submitCommands.
         */
        SceneTime currentSceneTime{0};

        std::array<char, 1500> sendBuffer;
        std::array<char, 1500> receiveBuffer;
        asio::ip::udp::endpoint currentRemoteEndpoint;

        PlayerCommandService* const playerCommandService;

        /**
         * Chat that has arrived and not yet been collected by the scene.
         *
         * The one piece of state here touched by both threads without going
         * through the io context, because it travels the other way: every
         * other call posts work to the network thread and waits, and a
         * message arriving has nobody to wait for it.
         */
        std::mutex chatInboxMutex;
        std::vector<ReceivedChatMessage> chatInbox;

        /**
         * When the network thread started listening, which is where a
         * peer's silence is measured from until it has ever been heard.
         */
        std::optional<Timestamp> startTime;

        int nextPacketId();

        void run();

        void listenForNextMessage();

        void sendLoop();

        void sendToAll();

        void send(PeerEndpoint& endpoint);

        void receive(const asio::error_code& error, std::size_t receivedBytes);
    };
}
