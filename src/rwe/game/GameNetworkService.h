#pragma once

#include <asio.hpp>
#include <chrono>
#include <deque>
#include <future>
#include <mutex>
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
#include <string>

namespace rwe
{
    struct SequenceNumberTag;
    using SequenceNumber = OpaqueUnit<unsigned int, SequenceNumberTag>;

    class GameNetworkService
    {
    public:
        using CommandSet = std::vector<PlayerCommand>;
        /** A line of chat that arrived from a peer. */
        struct ReceivedChatMessage
        {
            PlayerId sender;
            std::string text;
        };

        /**
         * How many unacked lines a peer may be owed before further ones are
         * refused. A line is resent until it is acked, so without a ceiling a
         * peer that has stopped answering is a growing packet.
         */
        static constexpr std::size_t MaxPendingChatMessages = 32;

        /**
         * How many command sets, and how many hashes, a peer may have waiting
         * in this peer's buffers before more are refused: thirty seconds'
         * worth. An honest peer is a command buffer's depth ahead, a few
         * ticks; without a ceiling a peer could send a stream far into the
         * future and have this machine hold all of it. Issue #75.
         */
        static constexpr unsigned int MaxSetsAheadOfTheGame = 900;

        /**
         * The most one command set may come to on the wire. A set is a tick's
         * commands and cannot be split across packets, and a packet is 1500
         * bytes with a header and every unacked set in it; this leaves room
         * for both. A move order costs about 30 bytes a unit, so this is some
         * thirty units a tick. See GameScene, which holds the rest of a
         * larger order for the next tick. Issue #75.
         */
        static constexpr std::size_t MaxCommandSetBytes = 1000;

        /**
         * How many commands from the front of `commands` make one set no
         * bigger than MaxCommandSetBytes; at least one while there are any.
         * Here rather than in the scene so that the scene does not have to
         * include the protobuf headers to ask.
         */
        static std::size_t commandsFittingOneSet(const std::vector<PlayerCommand>& commands);

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
             * Chat waiting to go to this peer, and where in that stream we
             * are. The same scheme as the commands and the hashes: the
             * buffer holds everything not yet acked, every packet carries
             * all of it, and an ack pops the front.
             */
            SequenceNumber nextChatToSend{0};
            SequenceNumber nextChatToReceive{0};
            std::deque<std::string> chatSendBuffer;

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

        /**
         * The address of every peer that has been forgotten, so that one which
         * comes back can be listened to again without being told where it is.
         *
         * A returning peer reaches the game through the lobby, which knows
         * where it is now and could say; keeping the old address means the
         * ordinary case -- the same machine, the same port, a process that was
         * restarted -- needs nobody to say anything.
         */
        std::vector<std::pair<PlayerId, asio::ip::udp::endpoint>> forgottenEndpoints;

        /**
         * This peer's own submitted command sets, by sequence number, for as
         * far back as RejoinHistoryLength.
         *
         * A sequence number is an absolute position in a peer's stream -- set N
         * is the one the simulation runs on tick N+1 -- because submitCommands
         * and PlayerCommandService::pushCommands are called together, once per
         * tick, and neither ever skips. That is what lets a returning peer be
         * handed the middle of a stream rather than the start of one.
         *
         * A per-endpoint send buffer cannot serve: it holds what that peer has
         * not acked, and a peer that was dropped has no buffer at all. This is
         * the other half of what the issue calls the command log since the
         * save, and the only half that has to live in the engine.
         */
        std::deque<std::pair<SequenceNumber, CommandSet>> sendHistory;

        /** How many of this peer's own sets are kept for a returning peer. */
        static constexpr std::size_t RejoinHistoryLength = 3600;

        /**
         * How far along this peer's own two streams are: the next sequence
         * number a submitted set or hash will carry.
         *
         * Both are ordinary indices -- set N runs on tick N+1, hash N is the
         * state at the end of tick N+1 -- and a returning peer is given a
         * position in each rather than the whole of either.
         */
        SequenceNumber nextSendSequence{0};
        SequenceNumber nextHashSequence{0};

        /**
         * Whether what arrives is handed to the simulation yet.
         *
         * False on a peer that is winding itself forward into a game in
         * progress. Its command buffers are being filled from the recording,
         * and a set arriving live would be appended to those same buffers
         * mid-wind -- landing at whichever tick the catch-up had reached
         * rather than at the one it belongs to.
         *
         * Not answered rather than not listened to: the service still runs,
         * because the scene asks it for the round trip time every frame and
         * waits for the answer. A set that is not taken is simply not acked,
         * and the peer that sent it keeps sending it until it is.
         */
        bool acceptingCommands{true};

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

        /**
         * `resumeFromSequence` is where this peer's own streams begin, which is
         * zero for a game being started and the rejoin tick minus one for a
         * peer rejoining one in progress: it has no sets or hashes of its own
         * for the ticks it missed, and the peers that stayed are expecting its
         * stream to pick up exactly there.
         */
        GameNetworkService(PlayerId localPlayerId, int port, const std::vector<EndpointInfo>& endpoints, PlayerCommandService* playerCommandService, SequenceNumber resumeFromSequence = SequenceNumber(0));

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
         * `fromSequence`. The bundle is then older than RejoinHistoryLength and
         * the gap cannot be closed from here; whoever asked has to build a
         * newer one.
         */
        bool rememberPeer(PlayerId playerId, SequenceNumber fromSequence, SequenceNumber theirNextSequence);

        /**
         * Stops handing arriving commands and hashes to the simulation, or
         * starts again. A peer winding itself forward into a game in progress
         * starts with this off; see acceptingCommands.
         */
        void setAcceptingCommands(bool value);

    private:
        void run();

        void listenForNextMessage();

        void sendLoop();

        void sendToAll();

        void send(EndpointInfo& endpoint);

        void receive(const asio::error_code& error, std::size_t receivedBytes);
    };
}
