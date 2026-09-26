#include "PeerLink.h"
#include <algorithm>
#include <rwe/game/PlayerCommandService.h>
#include <rwe/game/chat_util.h>
#include <rwe/network_util.h>
#include <rwe/proto/serialization.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/SimpleLogger.h>
#include <stdexcept>

namespace rwe
{
    LocalStream::LocalStream(SequenceNumber resumeFromSequence)
        : nextSendSequence_(resumeFromSequence), nextHashSequence_(resumeFromSequence)
    {
    }

    void LocalStream::recordCommandSet(const CommandSet& commands)
    {
        sendHistory.emplace_back(nextSendSequence_, commands);
        nextSendSequence_ = SequenceNumber(nextSendSequence_.value + 1);
        while (sendHistory.size() > RejoinHistoryLength)
        {
            sendHistory.pop_front();
        }
    }

    void LocalStream::recordHash()
    {
        nextHashSequence_ = SequenceNumber(nextHashSequence_.value + 1);
    }

    SequenceNumber LocalStream::firstHeldSequence() const
    {
        return sendHistory.empty() ? nextSendSequence_ : sendHistory.front().first;
    }

    bool LocalStream::holdsFrom(SequenceNumber fromSequence) const
    {
        return fromSequence >= firstHeldSequence() && fromSequence <= nextSendSequence_;
    }

    std::vector<LocalStream::CommandSet> LocalStream::historyFrom(SequenceNumber fromSequence) const
    {
        std::vector<CommandSet> out;
        for (const auto& [sequence, commands] : sendHistory)
        {
            if (sequence >= fromSequence)
            {
                out.push_back(commands);
            }
        }
        return out;
    }

    PeerLink::PeerLink(
        PlayerId localPlayerId,
        PlayerId remotePlayerId,
        PlayerCommandService* playerCommandService,
        PacketIdGenerator packetIdGenerator,
        const ResumeState& resume)
        : localPlayerId(localPlayerId),
          remotePlayerId(remotePlayerId),
          nextCommandToSend(resume.nextCommandToSend),
          nextCommandToReceive(resume.nextCommandToReceive),
          nextHashToSend(resume.nextHashToSend),
          nextHashToReceive(resume.nextHashToReceive),
          currentSceneTime(resume.currentSceneTime),
          playerCommandService(playerCommandService),
          packetIdGenerator(std::move(packetIdGenerator))
    {
    }

    std::size_t PeerLink::commandsFittingOneSet(const std::vector<PlayerCommand>& commands)
    {
        return rwe::commandsFittingOneSet(commands, MaxCommandSetBytes);
    }

    void PeerLink::submitCommands(SceneTime sceneTime, const CommandSet& commands)
    {
        currentSceneTime = sceneTime;
        sendBuffer.push_back(commands);
    }

    void PeerLink::submitGameHash(GameHash hash)
    {
        hashSendBuffer.push_back(hash);
    }

    void PeerLink::submitChatMessage(const std::string& text)
    {
        chatSendBuffer.push_back(text);
    }

    void PeerLink::restoreUnackedCommands(const std::vector<CommandSet>& sets)
    {
        for (const auto& set : sets)
        {
            sendBuffer.push_back(set);
        }
    }

    proto::NetworkMessage PeerLink::createMessage(
        int packetId,
        std::chrono::milliseconds ackDelay,
        std::size_t setCount,
        std::size_t hashCount,
        std::size_t chatCount) const
    {
        proto::NetworkMessage outerMessage;
        auto& m = *outerMessage.mutable_game_update();
        m.set_packet_id(packetId);
        m.set_player_id(localPlayerId.value);
        m.set_current_scene_time(currentSceneTime.value);
        m.set_next_command_set_to_send(nextCommandToSend.value);
        m.set_next_command_set_to_receive(nextCommandToReceive.value);
        m.set_next_game_hash_to_send(nextHashToSend.value);
        m.set_next_game_hash_to_receive(nextHashToReceive.value);
        m.set_next_chat_to_send(nextChatToSend.value);
        m.set_next_chat_to_receive(nextChatToReceive.value);
        m.set_ack_delay(ackDelay.count());

        for (std::size_t i = 0; i < setCount; ++i)
        {
            auto& setMessage = *m.add_command_set();

            for (const auto& cmd : sendBuffer[i])
            {
                auto& cmdMessage = *setMessage.add_command();
                serializePlayerCommand(cmd, cmdMessage);
            }
        }

        for (std::size_t i = 0; i < hashCount; ++i)
        {
            m.add_game_hashes(hashSendBuffer[i].value);
        }

        for (std::size_t i = 0; i < chatCount; ++i)
        {
            m.add_chat()->set_text(chatSendBuffer[i]);
        }

        return outerMessage;
    }

    std::vector<char> PeerLink::makePacket(Timestamp now, std::size_t sizeLimit)
    {
        auto packetId = packetIdGenerator();
        LOG_DEBUG << "Sending packet ID " << packetId << " to player " << remotePlayerId.value;

        std::chrono::milliseconds delay(0);
        if (lastReceiveTime)
        {
            delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - *lastReceiveTime);
        }

        // As much of each stream as fits, commands first because the game
        // cannot move without them, then hashes, then chat. The rest is
        // unacked and goes next time. Issue #75.
        auto sizeOf = [&](std::size_t sets, std::size_t hashes, std::size_t chats) {
            return createMessage(packetId, delay, sets, hashes, chats).ByteSizeLong();
        };
        auto setCount = longestPrefixThatFits(sendBuffer.size(), sizeLimit, [&](std::size_t n) { return sizeOf(n, 0, 0); });
        if (setCount == 0 && !sendBuffer.empty())
        {
            // A single set bigger than a datagram. GameScene splits a tick's
            // commands so that this cannot happen; if it does, the set can
            // never be sent and this peer will be dropped for silence, which
            // is the right outcome and better than a hung game.
            LOG_ERROR << "A command set is too large for a packet and cannot be sent";
        }
        auto hashCount = longestPrefixThatFits(hashSendBuffer.size(), sizeLimit, [&](std::size_t n) { return sizeOf(setCount, n, 0); });
        auto chatCount = chooseChatCountForPacket(chatSendBuffer.size(), sizeLimit, [&](std::size_t n) { return sizeOf(setCount, hashCount, n); });

        auto message = createMessage(packetId, delay, setCount, hashCount, chatCount);
        auto messageSize = message.ByteSizeLong();
        if (messageSize > sizeLimit)
        {
            LOG_ERROR << "A packet of " << messageSize << " bytes is too large to send; skipping it";
            return {};
        }

        std::vector<char> bytes(static_cast<std::size_t>(messageSize));
        if (!message.SerializeToArray(bytes.data(), static_cast<int>(bytes.size())))
        {
            throw std::runtime_error("Failed to serialize message to buffer");
        }

        // Keyed by the sequence number just past the last set this packet
        // carried, so the ack that reaches it is the one that completes this
        // packet's round trip.
        auto nextSequenceNumber = SequenceNumber(nextCommandToSend.value + setCount);
        if (sendTimes.empty() || sendTimes.back().first < nextSequenceNumber)
        {
            sendTimes.emplace_back(nextSequenceNumber, now);
        }

        lastSendTime = now;

        return bytes;
    }

    bool PeerLink::sendIsDue(Timestamp now) const
    {
        return !lastSendTime || now - *lastSendTime >= SubmitSendInterval;
    }

    void PeerLink::onPacket(const proto::GameUpdateMessage& message, Timestamp now)
    {
        if (!acceptingCommands)
        {
            // Winding forward into a game in progress. Nothing is taken and so
            // nothing is acked, and the sender keeps every set until it is --
            // but the packet still counts as having been heard, which is what
            // stops this peer being declared lost while it catches up.
            markHeard(now);
            return;
        }

        LOG_DEBUG << "Packet received with ID " << message.packet_id();
        if (message.player_id() != remotePlayerId.value)
        {
            LOG_ERROR << "Player " << remotePlayerId.value << " endpoint sent wrong player ID: " << message.player_id();
            return;
        }

        // Anything well formed from this peer counts as a sign of life,
        // whether or not it carries anything new. lastReceiveTime below
        // moves only for a packet with new commands in it, and so stands
        // still for a peer that is present and has nothing to say.
        lastPacketTime = now;

        LOG_DEBUG << "Received ack to " << message.next_command_set_to_receive() << " and " << message.command_set_size() << " commands starting at " << message.next_command_set_to_send();

        SequenceNumber newNextCommandToSend(message.next_command_set_to_receive());
        if (newNextCommandToSend.value > nextCommandToSend.value + sendBuffer.size())
        {
            LOG_ERROR << "Remote acked up to " << newNextCommandToSend.value << ", but we are at " << nextCommandToSend.value << " and command buffer contains " << sendBuffer.size() << " elements";
        }
        while (newNextCommandToSend > nextCommandToSend && !sendBuffer.empty())
        {
            sendBuffer.pop_front();
            nextCommandToSend = SequenceNumber(nextCommandToSend.value + 1);
        }

        while (!sendTimes.empty() && nextCommandToSend > sendTimes.front().first)
        {
            // skip older send time measurements
            sendTimes.pop_front();
        }
        if (!sendTimes.empty() && nextCommandToSend == sendTimes.front().first)
        {
            auto roundTripTime = now - sendTimes.front().second;
            auto ackDelay = std::chrono::milliseconds(message.ack_delay());
            roundTripTime = roundTripTime > ackDelay ? roundTripTime - ackDelay : std::chrono::milliseconds(0);
            auto rttMillis = std::chrono::duration_cast<std::chrono::milliseconds>(roundTripTime).count();
            averageRoundTripTime_ = ema(rttMillis, averageRoundTripTime_, 0.1f);
            recentRoundTripTimes.add(static_cast<float>(rttMillis));
            LOG_DEBUG << "Average RTT: " << averageRoundTripTime_ << "ms";
        }

        auto extraFrames = static_cast<unsigned int>((averageRoundTripTime_ / 2.0f) * SimTicksPerSecond / 1000.0f);
        lastKnownSceneTime_ = std::make_pair(SceneTime(message.current_scene_time() + extraFrames), now);
        LOG_DEBUG << "Estimated peer scene time: " << lastKnownSceneTime_->first.value;

        SequenceNumber firstCommandNumber(message.next_command_set_to_send());
        if (firstCommandNumber > nextCommandToReceive)
        {
            // message starts with commands too far in the future, ignore it.
            LOG_ERROR << "First command number in message was too high! Expecting no more than " << nextCommandToReceive.value << ", received " << firstCommandNumber.value;
            return;
        }

        auto firstRelevantCommandIndex = (nextCommandToReceive - firstCommandNumber).value;

        // if the packet is relevant (contains new information), process it
        if (firstRelevantCommandIndex < static_cast<unsigned int>(message.command_set_size()))
        {
            // Read every new set before taking any of them, so that a packet
            // with one set that cannot be read is refused whole. Nothing of it
            // is acked, and a peer that keeps sending such a thing falls
            // silent as far as the game is concerned and is dropped.
            std::vector<CommandSet> commandSets;
            try
            {
                for (auto i = static_cast<int>(firstRelevantCommandIndex); i < message.command_set_size(); ++i)
                {
                    commandSets.push_back(deserializeCommandSet(message.command_set(i)));
                }
            }
            catch (const std::exception& e)
            {
                LOG_ERROR << "Player " << remotePlayerId.value << " sent a command set that could not be read (" << e.what() << "); ignoring the packet";
                return;
            }

            lastReceiveTime = now;

            for (const auto& commandSet : commandSets)
            {
                // No further ahead than a peer could honestly be. A set costs
                // memory until its tick runs, and a peer that sends a stream
                // far into the future would otherwise have that memory for
                // the asking. What is not taken is not acked, and is sent
                // again once the game has caught up.
                if (playerCommandService->bufferedCommandCount(remotePlayerId) >= MaxSetsAheadOfTheGame)
                {
                    break;
                }
                playerCommandService->pushCommands(remotePlayerId, commandSet);
                nextCommandToReceive = SequenceNumber(nextCommandToReceive.value + 1);
            }
        }

        GameTime newNextHashToSend(message.next_game_hash_to_receive());
        if (newNextHashToSend > nextHashToSend + GameTime(hashSendBuffer.size()))
        {
            LOG_ERROR << "Remote acked up to " << newNextHashToSend.value << ", but we are at " << nextHashToSend.value << " and hash buffer contains " << hashSendBuffer.size() << " elements";
        }
        while (newNextHashToSend > nextHashToSend && !hashSendBuffer.empty())
        {
            hashSendBuffer.pop_front();
            nextHashToSend += GameTime(1);
        }

        GameTime firstGameHashTime(message.next_game_hash_to_send());
        if (firstGameHashTime > nextHashToReceive)
        {
            // message starts with hashes too far in the future, ignore it.
            LOG_ERROR << "First game hash time in message was too high! Expecting no more than " << nextHashToReceive.value << ", received " << firstGameHashTime.value;
            return;
        }

        // Compared unsigned against the count before either becomes an int
        // index: the difference can be anything a peer's numbering makes it,
        // and past INT_MAX a signed index would be negative.
        auto firstRelevantGameHashIndex = (nextHashToReceive - firstGameHashTime).value;
        if (firstRelevantGameHashIndex < static_cast<unsigned int>(message.game_hashes_size()))
        {
            for (auto i = static_cast<int>(firstRelevantGameHashIndex); i < message.game_hashes_size(); ++i)
            {
                if (playerCommandService->bufferedHashCount(remotePlayerId) >= MaxSetsAheadOfTheGame)
                {
                    break;
                }
                playerCommandService->pushHash(remotePlayerId, GameHash(message.game_hashes(i)));
                nextHashToReceive += GameTime(1);
            }
        }

        SequenceNumber newNextChatToSend(message.next_chat_to_receive());
        if (newNextChatToSend > nextChatToSend + SequenceNumber(chatSendBuffer.size()))
        {
            LOG_ERROR << "Remote acked chat up to " << newNextChatToSend.value << ", but we are at " << nextChatToSend.value << " and the chat buffer contains " << chatSendBuffer.size() << " elements";
        }
        while (newNextChatToSend > nextChatToSend && !chatSendBuffer.empty())
        {
            chatSendBuffer.pop_front();
            nextChatToSend = SequenceNumber(nextChatToSend.value + 1);
        }

        SequenceNumber firstChatNumber(message.next_chat_to_send());
        if (firstChatNumber > nextChatToReceive)
        {
            LOG_ERROR << "First chat number in message was too high! Expecting no more than " << nextChatToReceive.value << ", received " << firstChatNumber.value;
            return;
        }

        auto firstRelevantChatIndex = (nextChatToReceive - firstChatNumber).value;
        if (firstRelevantChatIndex >= static_cast<unsigned int>(message.chat_size()))
        {
            return;
        }
        for (auto i = static_cast<int>(firstRelevantChatIndex); i < message.chat_size(); ++i)
        {
            // Sanitised here rather than at the far end, so that nothing a peer
            // sends reaches the scene, the font or the log unexamined. What
            // survives is printable, one line, and bounded.
            auto text = sanitizeChatText(message.chat(i).text());
            nextChatToReceive = SequenceNumber(nextChatToReceive.value + 1);

            if (text.empty())
            {
                continue;
            }

            receivedChat.push_back(ReceivedChatMessage{remotePlayerId, text});
        }
    }

    void PeerLink::markHeard(Timestamp now)
    {
        lastPacketTime = now;
    }

    void PeerLink::setAcceptingCommands(bool value)
    {
        acceptingCommands = value;
    }

    PeerLink::PeerStatus PeerLink::status(Timestamp now, std::optional<Timestamp> sinceWhenNeverHeard) const
    {
        // A peer never heard from is measured from when the transport started,
        // so that one which never turns up times out like one which turned up
        // and left. Before the transport has started there is no clock to
        // measure from and nobody has had a chance to speak, so it is nothing.
        auto since = lastPacketTime ? lastPacketTime : sinceWhenNeverHeard;
        auto silence = since
            ? std::chrono::duration_cast<std::chrono::milliseconds>(now - *since)
            : std::chrono::milliseconds(0);

        // Each send time is keyed by the sequence number just past the last
        // set that packet carried, so the first packet to carry the oldest
        // unacked set is the first key beyond it.
        auto oldestUnackedAge = std::chrono::milliseconds(0);
        auto firstSend = std::find_if(sendTimes.begin(), sendTimes.end(), [&](const auto& t) { return t.first > nextCommandToSend; });
        if (firstSend != sendTimes.end())
        {
            oldestUnackedAge = std::chrono::duration_cast<std::chrono::milliseconds>(now - firstSend->second);
        }

        // Carried forward across the ordinary gap between packets and no
        // further: a peer that has gone quiet may have stopped, and assuming
        // it kept running would hide exactly that. At 1x speed only; a packet
        // does not say its sender's speed yet (#354).
        std::optional<float> estimatedNow;
        if (lastKnownSceneTime_)
        {
            auto sinceReport = std::min(now - lastKnownSceneTime_->second, std::chrono::duration_cast<Timestamp::duration>(2 * SendInterval));
            auto ticks = std::chrono::duration<float, std::milli>(sinceReport).count() / static_cast<float>(SimMillisecondsPerTick);
            estimatedNow = static_cast<float>(lastKnownSceneTime_->first.value) + ticks;
        }

        return PeerStatus{
            remotePlayerId,
            silence,
            lastKnownSceneTime_ ? std::optional<SceneTime>(lastKnownSceneTime_->first) : std::nullopt,
            estimatedNow,
            averageRoundTripTime_,
            recentRoundTripTimes.latest(),
            recentRoundTripTimes.min(),
            recentRoundTripTimes.max(),
            sendBuffer.size(),
            oldestUnackedAge};
    }

    std::vector<PeerLink::ReceivedChatMessage> PeerLink::takeReceivedChat()
    {
        std::vector<ReceivedChatMessage> out;
        out.swap(receivedChat);
        return out;
    }
}
