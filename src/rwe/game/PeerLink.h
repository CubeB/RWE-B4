#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <network.pb.h>
#include <optional>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/RoundTripWindow.h>
#include <rwe/game/SceneTime.h>
#include <rwe/rwe_time.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/util/OpaqueId.h>
#include <rwe/util/OpaqueUnit.h>
#include <string>
#include <utility>
#include <vector>

namespace rwe
{
    class PlayerCommandService;

    struct SequenceNumberTag;
    using SequenceNumber = OpaqueUnit<unsigned int, SequenceNumberTag>;

    /**
     * This peer's own submitted command and hash streams, and the recent slice
     * of the command stream kept for a returning peer.
     *
     * One of these, not one per remote peer: it is a single stream however
     * many peers it is sent to, and the sequence numbers a rejoining peer is
     * handed are positions in it.
     */
    class LocalStream
    {
    public:
        using CommandSet = std::vector<PlayerCommand>;

        /** How many of this peer's own sets are kept for a returning peer. */
        static constexpr std::size_t RejoinHistoryLength = 3600;

        /**
         * `resumeFromSequence` is where this peer's own streams begin: zero
         * for a game being started, and the rejoin tick minus one for a peer
         * rejoining one in progress.
         */
        explicit LocalStream(SequenceNumber resumeFromSequence = SequenceNumber(0));

        void recordCommandSet(const CommandSet& commands);

        void recordHash();

        SequenceNumber nextSendSequence() const { return nextSendSequence_; }
        SequenceNumber nextHashSequence() const { return nextHashSequence_; }

        /** The oldest sequence number still held, which is where a rejoin may resume. */
        SequenceNumber firstHeldSequence() const;

        /** Whether the held history reaches back to `fromSequence`. */
        bool holdsFrom(SequenceNumber fromSequence) const;

        /** The held sets from `fromSequence` on, in order. */
        std::vector<CommandSet> historyFrom(SequenceNumber fromSequence) const;

    private:
        std::deque<std::pair<SequenceNumber, CommandSet>> sendHistory;
        SequenceNumber nextSendSequence_;
        SequenceNumber nextHashSequence_;
    };

    /**
     * The protocol for talking to one remote peer: sequence numbers, acks and
     * resends for the three streams, the round-trip estimate, the peer's
     * reported scene time, and fitting a packet to a size.
     *
     * It has no socket, no timer and no clock of its own -- the time is passed
     * in -- so two of them can be joined through a fake link and tested
     * in-process. The transport owns it and calls every method from its own
     * thread, except that a link may be built on the game thread before the
     * transport starts.
     */
    class PeerLink
    {
    public:
        using CommandSet = std::vector<PlayerCommand>;
        using PacketIdGenerator = std::function<int()>;

        /** A line of chat that arrived from the remote peer. */
        struct ReceivedChatMessage
        {
            PlayerId sender;
            std::string text;
        };

        /** What a peer looks like from here, for the drop timer and overlay. */
        struct PeerStatus
        {
            PlayerId playerId;

            /** How long since anything at all arrived from this peer. */
            std::chrono::milliseconds silence;

            /** The scene time it last reported, adjusted for the round trip. */
            std::optional<SceneTime> lastKnownSceneTime;

            /**
             * The same, carried forward by the ticks it will have run since
             * that report arrived, for comparing with our own tick now. Kept
             * apart because a drop is cut at what the peer actually said.
             */
            std::optional<float> estimatedSceneTimeNow;

            float averageRoundTripMillis;
            float latestRoundTripMillis;

            /** Over the last few seconds; zero until a sample has been taken. */
            float minRoundTripMillis;
            float maxRoundTripMillis;

            /** Command sets sent and not yet acknowledged. A count that keeps growing is packets being lost. */
            std::size_t unackedCommandSets;

            /**
             * How long the oldest unacknowledged set has been out; zero when
             * nothing is. A round trip is only measured when an ack arrives,
             * so while a peer is not acking this is the only figure that moves.
             */
            std::chrono::milliseconds oldestUnackedAge;
        };

        /** Where the link's streams begin, for an ordinary or a rejoining peer. */
        struct ResumeState
        {
            SequenceNumber nextCommandToSend{0};
            SequenceNumber nextCommandToReceive{0};
            GameTime nextHashToSend{0};
            GameTime nextHashToReceive{0};

            /** The scene time as of the rejoin, so the first packet reports it rather than zero. */
            SceneTime currentSceneTime{0};
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
         * for both. See GameScene, which holds the rest of a larger order for
         * the next tick. Issue #75.
         */
        static constexpr std::size_t MaxCommandSetBytes = 1000;

        /**
         * How often every peer is sent a packet, whether there is anything new
         * or not. It is also the longest a peer holds an ack before sending it.
         */
        static constexpr std::chrono::milliseconds SendInterval{100};

        /**
         * How many commands from the front of `commands` make one set no
         * bigger than MaxCommandSetBytes; at least one while there are any.
         */
        static std::size_t commandsFittingOneSet(const std::vector<PlayerCommand>& commands);

        PeerLink(
            PlayerId localPlayerId,
            PlayerId remotePlayerId,
            PlayerCommandService* playerCommandService,
            PacketIdGenerator packetIdGenerator,
            const ResumeState& resume);

        void submitCommands(SceneTime currentSceneTime, const CommandSet& commands);

        /**
         * Record how this peer is running, so the next packet says it and a
         * remote peer can project this one's scene time at the right rate.
         * Sent every frame rather than only alongside commands, because it
         * changes with the pause key and a stall and has nothing to do with
         * whether a command set was due.
         */
        void submitRunState(unsigned int speedPermille, bool paused, bool stalled);

        void submitGameHash(GameHash hash);

        /** Whether this peer is already owed too much chat to take a new line. */
        bool chatBacklogFull() const { return chatSendBuffer.size() >= MaxPendingChatMessages; }

        void submitChatMessage(const std::string& text);

        /** Puts sets back in the send buffer for a peer that is listened to again. */
        void restoreUnackedCommands(const std::vector<CommandSet>& sets);

        /**
         * The packet to send now: where this peer is in each of the three
         * streams, and everything in them the remote peer has not acked.
         * `sizeLimit` bounds the serialized message; the transport frames it
         * with a CRC afterwards. The rest of an oversized stream goes in a
         * later packet.
         */
        std::vector<char> makePacket(Timestamp now, std::size_t sizeLimit);

        /**
         * Read a packet from the remote peer. Anything well formed counts as a
         * sign of life; new sets, hashes and chat are taken, acknowledged and
         * handed to the command service and the chat inbox.
         */
        void onPacket(const proto::GameUpdateMessage& message, Timestamp now);

        /**
         * Note that a packet arrived, without taking anything from it. What a
         * peer winding itself forward uses: it is heard, so it is not declared
         * lost, but nothing is acked and the sender keeps every set.
         */
        void markHeard(Timestamp now);

        /**
         * Stops handing arriving commands and hashes to the simulation, or
         * starts again. A peer winding itself forward into a game in progress
         * starts with this off: a set arriving mid-catch-up would land at
         * whichever tick the wind had reached rather than at its own.
         */
        void setAcceptingCommands(bool value);

        std::optional<std::pair<SceneTime, Timestamp>> lastKnownSceneTime() const { return lastKnownSceneTime_; }

        /** How the remote peer last said it was running, defaulting to 1x and moving. */
        unsigned int remoteSpeedPermille() const { return remoteSpeedPermille_; }
        bool remotePaused() const { return remotePaused_; }
        bool remoteStalled() const { return remoteStalled_; }

        float averageRoundTripTime() const { return averageRoundTripTime_; }

        PeerStatus status(Timestamp now, std::optional<Timestamp> sinceWhenNeverHeard) const;

        /** Everything that arrived since the last call, in the order it arrived. */
        std::vector<ReceivedChatMessage> takeReceivedChat();

    private:
        /** The id this peer stamps on its own packets. */
        PlayerId localPlayerId;
        PlayerId remotePlayerId;

        /**
         * The position of each of the three streams towards this peer. What
         * this peer has sent and had acked, and what it has taken from the
         * other end. A packet carries all of each.
         */
        SequenceNumber nextCommandToSend;
        SequenceNumber nextCommandToReceive;
        GameTime nextHashToSend;
        GameTime nextHashToReceive;
        SequenceNumber nextChatToSend{0};
        SequenceNumber nextChatToReceive{0};

        std::deque<CommandSet> sendBuffer;
        std::deque<GameHash> hashSendBuffer;
        std::deque<std::string> chatSendBuffer;

        /**
         * The time at which we first sent a packet finishing at the given
         * sequence number, for measuring RTT when acks arrive.
         */
        std::deque<std::pair<SequenceNumber, Timestamp>> sendTimes;

        /**
         * The time at which the last packet carrying new commands arrived.
         * Stood still by a peer that is present and has nothing to say.
         */
        std::optional<Timestamp> lastReceiveTime;

        /**
         * The time anything at all was last heard from this peer, the sign of
         * life the drop timer watches.
         */
        std::optional<Timestamp> lastPacketTime;

        std::optional<std::pair<SceneTime, Timestamp>> lastKnownSceneTime_;

        float averageRoundTripTime_{0};
        RoundTripWindow recentRoundTripTimes;

        SceneTime currentSceneTime{0};

        /** How this peer last said it was running, sent in every packet. */
        unsigned int localSpeedPermille{1000};
        bool localPaused{false};
        bool localStalled{false};

        /** How the remote peer last said it was running, from the last packet. */
        unsigned int remoteSpeedPermille_{1000};
        bool remotePaused_{false};
        bool remoteStalled_{false};

        bool acceptingCommands{true};

        std::vector<ReceivedChatMessage> receivedChat;

        PlayerCommandService* const playerCommandService;
        PacketIdGenerator packetIdGenerator;

        proto::NetworkMessage createMessage(
            int packetId,
            std::chrono::milliseconds ackDelay,
            std::size_t setCount,
            std::size_t hashCount,
            std::size_t chatCount) const;
    };
}
