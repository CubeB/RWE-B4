#pragma once

#include <asio.hpp>
#include <chrono>
#include <deque>
#include <future>
#include <network.pb.h>
#include <random>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/rwe_time.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/util/OpaqueId.h>
#include <rwe/util/OpaqueUnit.h>

namespace rwe
{
    struct SequenceNumberTag;
    using SequenceNumber = OpaqueUnit<unsigned int, SequenceNumberTag>;

    class GameNetworkService
    {
    public:
        using CommandSet = std::vector<PlayerCommand>;
        struct EndpointInfo
        {
            PlayerId playerId;
            asio::ip::udp::endpoint endpoint;

            SequenceNumber nextCommandToSend{0};
            SequenceNumber nextCommandToReceive{0};

            GameTime nextHashToSend{0};
            GameTime nextHashToReceive{0};

            /**
             * The time at which the last relevant update packet
             * was received from the remote peer.
             * An update packet is relevant
             * if it contains new commands that we haven't seen before.
             */
            std::optional<Timestamp> lastReceiveTime;

            /**
             * The time anything at all was last heard from this peer, relevant
             * or not, and the only thing that says whether it is still there.
             *
             * Kept apart from lastReceiveTime, which moves only for a packet
             * carrying new commands and so stands still for a peer that is
             * connected and simply has nothing to say -- which is most of a
             * game. A peer sends every 100 ms whether it has anything or not,
             * so silence here means silence.
             *
             * Unset until the first packet, which is why the timeout is
             * measured from when the service started in that case: a peer that
             * never arrives has to be droppable too.
             */
            std::optional<Timestamp> lastPacketTime;

            /**
             * The last reported scene time from this peer,
             * adjusted for RTT.
             */
            std::optional<std::pair<SceneTime, Timestamp>> lastKnownSceneTime;

            std::deque<CommandSet> sendBuffer;

            std::deque<GameHash> hashSendBuffer;

            /**
             * Records the time at which we first sent a packet
             * finishing at the given sequence number.
             * This is used for measuring RTT when we receive acks.
             */
            std::deque<std::pair<SequenceNumber, Timestamp>> sendTimes;

            /**
             * Exponential moving average of round trip time
             * for communication between us and the remote peer.
             */
            float averageRoundTripTime{0};

            EndpointInfo(const PlayerId& playerId, const asio::ip::udp::endpoint& endpoint)
                : playerId(playerId), endpoint(endpoint)
            {
            }
        };

    private:
        std::random_device rd;
        std::default_random_engine gen{rd()};
        std::uniform_int_distribution<int> uniform_dist{};
        PlayerId localPlayerId;
        int port;

        std::thread networkThread;

        asio::io_context ioContext;
        asio::ip::udp::resolver resolver;
        asio::ip::udp::socket socket;
        asio::steady_timer sendTimer;

        std::vector<EndpointInfo> endpoints;

        std::array<char, 1500> sendBuffer;
        std::array<char, 1500> receiveBuffer;
        asio::ip::udp::endpoint currentRemoteEndpoint;

        PlayerCommandService* const playerCommandService;

        SceneTime currentSceneTime{0};

        /**
         * When the network thread started listening, which is where a
         * peer's silence is measured from until it has ever been heard.
         */
        std::optional<Timestamp> startTime;

    public:
        /**
         * What a peer looks like from here, for deciding whether it is still
         * there and, if it is not, from which tick to carry on without it.
         */
        struct PeerStatus
        {
            PlayerId playerId;

            /** How long since anything at all arrived from this peer. */
            std::chrono::milliseconds silence;

            /** The scene time it last reported, adjusted for the round trip. */
            std::optional<SceneTime> lastKnownSceneTime;
        };

        GameNetworkService(PlayerId localPlayerId, int port, const std::vector<EndpointInfo>& endpoints, PlayerCommandService* playerCommandService);

        virtual ~GameNetworkService();

        void start();

        /**
         * Submit new information to be sent on the network.
         * @param currentSceneTime The scene time we are currently simulating.
         *                         This is used to inform peers/synchronise simulation speed.
         * @param commands The latest set of player commands.
         *                 These are not related to the current scene time.
         *                 They will be queued up to be sent over the network
         *                 after all the previously submitted commands.
         */
        void submitCommands(SceneTime currentSceneTime, const CommandSet& commands);

        void submitGameHash(GameHash hash);

        SceneTime estimateAvergeSceneTime(SceneTime localSceneTime);

        float getMaxAverageRttMillis();

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

    private:
        void run();

        void listenForNextMessage();

        void sendLoop();

        void sendToAll();

        void send(EndpointInfo& endpoint);

        void receive(const asio::error_code& error, std::size_t receivedBytes);
    };
}
