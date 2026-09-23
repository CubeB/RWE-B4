#include "GameNetworkService.h"
#include <rwe/game/chat_util.h>
#include <algorithm>
#include <rwe/network_util.h>
#include <rwe/proto/serialization.h>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/Index.h>
#include <rwe/util/OpaqueId_io.h>
#include <rwe/util/range_util.h>
#include <rwe/util/SimpleLogger.h>
#include <thread>

namespace rwe
{
    GameNetworkService::GameNetworkService(
        PlayerId localPlayerId,
        int port,
        const std::vector<GameNetworkService::EndpointInfo>& endpoints,
        PlayerCommandService* playerCommandService,
        SequenceNumber resumeFromSequence)
        : localPlayerId(localPlayerId),
          port(port),
          resolver(ioContext),
          socket(ioContext),
          sendTimer(ioContext),
          endpoints(endpoints),
          nextSendSequence(resumeFromSequence),
          nextHashSequence(resumeFromSequence),
          playerCommandService(playerCommandService)
    {
    }

    GameNetworkService::~GameNetworkService()
    {
        if (networkThread.joinable())
        {
            ioContext.stop();
            networkThread.join();
        }
    }

    void GameNetworkService::start()
    {
        networkThread = std::thread(&GameNetworkService::run, this);
    }

    void GameNetworkService::submitCommands(SceneTime currentSceneTime, const GameNetworkService::CommandSet& commands)
    {
        asio::post(ioContext,[this, currentSceneTime, commands]() {
            this->currentSceneTime = currentSceneTime;
            for (auto& e : endpoints)
            {
                e.sendBuffer.push_back(commands);
            }

            // Kept whether anyone has been dropped or not, because by the time
            // one has it is too late to start: a returning peer wants the sets
            // from before it went quiet.
            sendHistory.emplace_back(nextSendSequence, commands);
            nextSendSequence = SequenceNumber(nextSendSequence.value + 1);
            while (sendHistory.size() > RejoinHistoryLength)
            {
                sendHistory.pop_front();
            }
        });
    }

    void GameNetworkService::submitGameHash(GameHash hash)
    {
        asio::post(ioContext,[this, hash]() {
            for (auto& e : endpoints)
            {
                e.hashSendBuffer.push_back(hash);
            }

            nextHashSequence = SequenceNumber(nextHashSequence.value + 1);
        });
    }

    bool GameNetworkService::submitChatMessage(const std::string& text)
    {
        // Waits for the answer, where submitCommands does not, because a line
        // can be refused and the player is owed the news that theirs was. It
        // happens once per line typed, not once a tick.
        std::promise<bool> result;
        asio::post(ioContext, [this, text, &result]() {
            for (const auto& e : endpoints)
            {
                if (e.chatSendBuffer.size() >= MaxPendingChatMessages)
                {
                    result.set_value(false);
                    return;
                }
            }

            for (auto& e : endpoints)
            {
                e.chatSendBuffer.push_back(text);
            }

            result.set_value(true);
        });

        return result.get_future().get();
    }

    std::vector<GameNetworkService::ReceivedChatMessage> GameNetworkService::takeChatMessages()
    {
        std::scoped_lock<std::mutex> lock(chatInboxMutex);
        return std::move(chatInbox);
    }

    SceneTime GameNetworkService::estimateAvergeSceneTime(SceneTime localSceneTime)
    {
        std::promise<unsigned int> result;
        asio::post(ioContext,[this, localSceneTime, &result]() {
            auto time = getTimestamp();
            auto otherTimes = choose(endpoints, [](const auto& e) { return e.lastKnownSceneTime; });

            auto finalValue = estimateAverageSceneTimeStatic(localSceneTime, otherTimes, time);
            result.set_value(finalValue);
        });

        return SceneTime(result.get_future().get());
    }

    std::vector<GameNetworkService::PeerStatus> GameNetworkService::getPeerStatuses()
    {
        std::promise<std::vector<PeerStatus>> result;
        asio::post(ioContext, [this, &result]() {
            auto now = getTimestamp();
            std::vector<PeerStatus> statuses;
            for (const auto& e : endpoints)
            {
                // A peer never heard from is measured from when this service
                // started, so that one which never turns up times out like one
                // which turned up and left. Before the thread has started there
                // is no clock to measure from and nobody has had a chance to
                // speak, so the silence is nothing.
                auto since = e.lastPacketTime ? e.lastPacketTime : startTime;
                auto silence = since
                    ? std::chrono::duration_cast<std::chrono::milliseconds>(now - *since)
                    : std::chrono::milliseconds(0);

                statuses.push_back(PeerStatus{
                    e.playerId,
                    silence,
                    e.lastKnownSceneTime ? std::optional<SceneTime>(e.lastKnownSceneTime->first) : std::nullopt});
            }

            result.set_value(std::move(statuses));
        });

        return result.get_future().get();
    }

    void GameNetworkService::forgetPeer(PlayerId playerId)
    {
        asio::post(ioContext, [this, playerId]() {
            for (const auto& e : endpoints)
            {
                if (e.playerId == playerId)
                {
                    // Where it was, so that a peer which comes back to the same
                    // place needs nobody to say where that is.
                    forgottenEndpoints.emplace_back(playerId, e.endpoint);
                }
            }

            endpoints.erase(
                std::remove_if(endpoints.begin(), endpoints.end(), [playerId](const auto& e) { return e.playerId == playerId; }),
                endpoints.end());
        });
    }

    bool GameNetworkService::rememberPeer(PlayerId playerId, SequenceNumber fromSequence, SequenceNumber theirNextSequence)
    {
        std::promise<bool> result;
        asio::post(ioContext, [this, playerId, fromSequence, theirNextSequence, &result]() {
            auto known = std::find_if(forgottenEndpoints.begin(), forgottenEndpoints.end(), [playerId](const auto& p) { return p.first == playerId; });
            if (known == forgottenEndpoints.end())
            {
                LOG_ERROR << "Cannot listen to player " << playerId.value << " again: nothing was ever forgotten about them";
                result.set_value(false);
                return;
            }

            auto haveFrom = sendHistory.empty() ? nextSendSequence : sendHistory.front().first;
            auto haveTo = nextSendSequence;
            if (fromSequence < haveFrom || fromSequence > haveTo)
            {
                LOG_ERROR << "Cannot resume the stream to player " << playerId.value << " at " << fromSequence.value
                          << ": this peer holds " << haveFrom.value << " to " << haveTo.value;
                result.set_value(false);
                return;
            }

            EndpointInfo endpoint(playerId, known->second);
            endpoint.nextCommandToSend = fromSequence;
            endpoint.nextCommandToReceive = theirNextSequence;
            for (const auto& [sequence, commands] : sendHistory)
            {
                if (sequence >= fromSequence)
                {
                    endpoint.sendBuffer.push_back(commands);
                }
            }

            // The hash stream is numbered the same way and needs no history.
            // This peer has not yet run the tick the returning one is waiting
            // for, so there is nothing held back to hand over; saying where
            // this stream has reached is enough, because the receiver skips
            // whatever falls below the position it asked to resume at.
            endpoint.nextHashToSend = GameTime(nextHashSequence.value);
            endpoint.nextHashToReceive = GameTime(theirNextSequence.value);

            forgottenEndpoints.erase(known);
            endpoints.push_back(std::move(endpoint));

            LOG_INFO << "Listening to player " << playerId.value << " again, sending from " << fromSequence.value
                     << " and expecting their set " << theirNextSequence.value;
            result.set_value(true);
        });

        return result.get_future().get();
    }

    void GameNetworkService::setAcceptingCommands(bool value)
    {
        asio::post(ioContext, [this, value]() {
            acceptingCommands = value;
        });
    }

    float GameNetworkService::getMaxAverageRttMillis()
    {
        std::promise<float> result;
        asio::post(ioContext,[this, &result]() {
            auto maxRtt = 0.0f;
            for (const auto& e : endpoints)
            {
                if (e.averageRoundTripTime > maxRtt)
                {
                    maxRtt = e.averageRoundTripTime;
                }
            }

            result.set_value(maxRtt);
        });

        return result.get_future().get();
    }

    void GameNetworkService::run()
    {
        try
        {
            auto endpoint = asio::ip::udp::endpoint(asio::ip::udp::v6(), port);
            socket.open(endpoint.protocol());
            socket.bind(endpoint);

            // Where a peer's silence is measured from until it has been
            // heard once. Set on the network thread, which is the only
            // thread that reads it.
            startTime = getTimestamp();

            listenForNextMessage();

            sendLoop();

            ioContext.run();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Network thread died with error: " << e.what();
        }
    }

    void GameNetworkService::listenForNextMessage()
    {
        socket.async_receive_from(
            asio::buffer(receiveBuffer.data(), receiveBuffer.size()),
            currentRemoteEndpoint,
            [this](const auto& error, const auto& bytesTransferred) {
                receive(error, bytesTransferred);
                listenForNextMessage();
            });
    }

    /**
     * The packet sent to one peer: where we are in each of the three streams
     * that flow to it, and everything in them it has not yet acked.
     *
     * chatCount is how much of the chat buffer to include, because chat is the
     * one part of a packet whose size is not bounded by the tick rate -- see
     * send, which uses it to make an outsized packet fit.
     */
    proto::NetworkMessage createProtoMessage(
        int packetId,
        PlayerId playerId,
        SceneTime currentSceneTime,
        std::chrono::milliseconds ackDelay,
        const GameNetworkService::EndpointInfo& endpoint,
        std::size_t chatCount)
    {
        proto::NetworkMessage outerMessage;
        auto& m = *outerMessage.mutable_game_update();
        m.set_packet_id(packetId);
        m.set_player_id(playerId.value);
        m.set_current_scene_time(currentSceneTime.value);
        m.set_next_command_set_to_send(endpoint.nextCommandToSend.value);
        m.set_next_command_set_to_receive(endpoint.nextCommandToReceive.value);
        m.set_next_game_hash_to_send(endpoint.nextHashToSend.value);
        m.set_next_game_hash_to_receive(endpoint.nextHashToReceive.value);
        m.set_next_chat_to_send(endpoint.nextChatToSend.value);
        m.set_next_chat_to_receive(endpoint.nextChatToReceive.value);
        m.set_ack_delay(ackDelay.count());

        for (const auto& set : endpoint.sendBuffer)
        {
            auto& setMessage = *m.add_command_set();

            for (const auto& cmd : set)
            {
                auto& cmdMessage = *setMessage.add_command();
                serializePlayerCommand(cmd, cmdMessage);
            }
        }

        for (const auto& hash : endpoint.hashSendBuffer)
        {
            m.add_game_hashes(hash.value);
        }

        for (std::size_t i = 0; i < chatCount; ++i)
        {
            m.add_chat()->set_text(endpoint.chatSendBuffer[i]);
        }

        return outerMessage;
    }

    void GameNetworkService::sendLoop()
    {
        sendToAll();
        sendTimer.expires_after(std::chrono::milliseconds(100));
        sendTimer.async_wait([this](const asio::error_code& error) {
            if (error)
            {
                LOG_ERROR << "Error while waiting on timer: " << error.message();
                return;
            }

            sendLoop();
        });
    }

    void GameNetworkService::sendToAll()
    {
        for (auto& e : endpoints)
        {
            send(e);
        }
    }

    void GameNetworkService::send(GameNetworkService::EndpointInfo& endpoint)
    {
        auto packetId = uniform_dist(gen);
        LOG_DEBUG << "Sending packet ID " << packetId << " to endpoint: " << endpoint.endpoint.address().to_string() << ":" << endpoint.endpoint.port();
        std::chrono::milliseconds delay(0);
        auto sendTime = getTimestamp();
        if (endpoint.lastReceiveTime)
        {
            delay = std::chrono::duration_cast<std::chrono::milliseconds>(sendTime - *endpoint.lastReceiveTime);
        }

        auto sizeLimit = static_cast<unsigned long long>(getSize(sendBuffer) - 4);
        auto chatCount = chooseChatCountForPacket(
            endpoint.chatSendBuffer.size(),
            sizeLimit,
            [&](std::size_t count) {
                return createProtoMessage(packetId, localPlayerId, currentSceneTime, delay, endpoint, count).ByteSizeLong();
            });

        auto message = createProtoMessage(packetId, localPlayerId, currentSceneTime, delay, endpoint, chatCount);
        auto messageSize = message.ByteSizeLong();
        if (messageSize > sizeLimit)
        {
            throw std::runtime_error("Message to be sent was bigger than buffer size");
        }
        if (!message.SerializeToArray(sendBuffer.data(), sendBuffer.size()))
        {
            throw std::runtime_error("Failed to serialize message to buffer");
        }

        // throw in a CRC to verify the message
        writeInt(&sendBuffer[messageSize], computeCrc(sendBuffer.data(), messageSize));

        socket.send_to(asio::buffer(sendBuffer.data(), messageSize + 4), endpoint.endpoint);

        auto nextSequenceNumber = SequenceNumber(endpoint.nextCommandToSend.value + (endpoint.sendBuffer.size()));
        if (endpoint.sendTimes.empty() || endpoint.sendTimes.back().first < nextSequenceNumber)
        {
            endpoint.sendTimes.emplace_back(nextSequenceNumber, sendTime);
        }
    }

    void GameNetworkService::receive(const asio::error_code& error, std::size_t receivedBytes)
    {
        if (error)
        {
            LOG_ERROR << "Error on receive: " << error.message();
            return;
        }

        auto receiveTime = getTimestamp();
        LOG_DEBUG << "Received " << receivedBytes << " bytes from endpoint: " << currentRemoteEndpoint.address().to_string() << ":" << currentRemoteEndpoint.port();

        if (receivedBytes == receiveBuffer.size())
        {
            LOG_WARN << "Received " << receivedBytes << " bytes, which filled the entire message buffer!!";
        }

        if (receivedBytes < 4)
        {
            LOG_ERROR << "Received message is too short (" << receivedBytes << " bytes), ignoring";
            return;
        }

        auto endpointIt = std::find_if(endpoints.begin(), endpoints.end(), [this](const auto& e) { return currentRemoteEndpoint == e.endpoint; });
        if (endpointIt == endpoints.end())
        {
            // message was from some unknown address, ignore it
            LOG_DEBUG << "Unknown address, ignoring";
            return;
        }

        auto receivedCrc = readInt(&receiveBuffer[receivedBytes - 4]);
        auto computedCrc = computeCrc(receiveBuffer.data(), receivedBytes - 4);
        if (receivedCrc != computedCrc)
        {
            LOG_ERROR << "Message CRC incorrect, ignoring";
            return;
        }

        proto::NetworkMessage outerMessage;
        outerMessage.ParseFromArray(receiveBuffer.data(), receivedBytes - 4);
        if (!outerMessage.has_game_update())
        {
            // message wasn't a game update, ignore it
            LOG_DEBUG << "Not game update, ignoring";
            return;
        }

        EndpointInfo& endpoint = *endpointIt;

        if (!acceptingCommands)
        {
            // Winding forward into a game in progress. Nothing is taken and so
            // nothing is acked, and the sender keeps every set until it is --
            // but the packet still counts as having been heard, which is what
            // stops this peer being declared lost while it catches up.
            endpoint.lastPacketTime = getTimestamp();
            return;
        }

        const auto& message = outerMessage.game_update();

        LOG_DEBUG << "Packet received with ID " << message.packet_id();
        if (message.player_id() != endpoint.playerId.value)
        {
            LOG_ERROR << "Player " << endpoint.playerId.value << " endpoint sent wrong player ID: " << message.player_id();
            return;
        }

        // Anything well formed from this peer counts as a sign of life,
        // whether or not it carries anything new. lastReceiveTime below
        // moves only for a packet with new commands in it, and so stands
        // still for a peer that is present and has nothing to say.
        endpoint.lastPacketTime = receiveTime;

        LOG_DEBUG << "Received ack to " << message.next_command_set_to_receive() << " and " << message.command_set_size() << " commands starting at " << message.next_command_set_to_send();

        SequenceNumber newNextCommandToSend(message.next_command_set_to_receive());
        if (newNextCommandToSend.value > endpoint.nextCommandToSend.value + endpoint.sendBuffer.size())
        {
            LOG_ERROR << "Remote acked up to " << newNextCommandToSend.value << ", but we are at " << endpoint.nextCommandToSend.value << " and command buffer contains " << endpoint.sendBuffer.size() << " elements";
        }
        while (newNextCommandToSend > endpoint.nextCommandToSend && !endpoint.sendBuffer.empty())
        {
            endpoint.sendBuffer.pop_front();
            endpoint.nextCommandToSend = SequenceNumber(endpoint.nextCommandToSend.value + 1);
        }

        while (!endpoint.sendTimes.empty() && endpoint.nextCommandToSend > endpoint.sendTimes.front().first)
        {
            // skip older send time measurements
            endpoint.sendTimes.pop_front();
        }
        if (!endpoint.sendTimes.empty() && endpoint.nextCommandToSend == endpoint.sendTimes.front().first)
        {
            auto roundTripTime = receiveTime - endpoint.sendTimes.front().second;
            auto ackDelay = std::chrono::milliseconds(message.ack_delay());
            roundTripTime = roundTripTime > ackDelay ? roundTripTime - ackDelay : std::chrono::milliseconds(0);
            auto rttMillis = std::chrono::duration_cast<std::chrono::milliseconds>(roundTripTime).count();
            endpoint.averageRoundTripTime = ema(rttMillis, endpoint.averageRoundTripTime, 0.1f);
            LOG_DEBUG << "Average RTT: " << endpoint.averageRoundTripTime << "ms";
        }

        auto extraFrames = static_cast<unsigned int>((endpoint.averageRoundTripTime / 2.0f) * SimTicksPerSecond / 1000.0f);
        endpoint.lastKnownSceneTime = std::make_pair(SceneTime(message.current_scene_time() + extraFrames), receiveTime);
        LOG_DEBUG << "Estimated peer scene time: " << endpoint.lastKnownSceneTime->first.value;

        SequenceNumber firstCommandNumber(message.next_command_set_to_send());
        if (firstCommandNumber > endpoint.nextCommandToReceive)
        {
            // message starts with commands too far in the future, ignore it.
            // FIXME: this should probably be an error as it shouldn't ever happen
            LOG_ERROR << "First command number in message was too high! Expecting no more than " << endpoint.nextCommandToReceive.value << ", received " << firstCommandNumber.value;
            return;
        }

        auto firstRelevantCommandIndex = (endpoint.nextCommandToReceive - firstCommandNumber).value;

        // if the packet is relevant (contains new information), process it
        if (firstRelevantCommandIndex < static_cast<unsigned int>(message.command_set_size()))
        {
            endpoint.lastReceiveTime = receiveTime;

            for (int i = firstRelevantCommandIndex; i < message.command_set_size(); ++i)
            {
                auto commandSet = deserializeCommandSet(message.command_set(i));
                playerCommandService->pushCommands(endpoint.playerId, commandSet);
                endpoint.nextCommandToReceive = SequenceNumber(endpoint.nextCommandToReceive.value + 1);
            }
        }

        GameTime newNextHashToSend(message.next_game_hash_to_receive());
        if (newNextHashToSend > endpoint.nextHashToSend + GameTime(endpoint.hashSendBuffer.size()))
        {
            LOG_ERROR << "Remote acked up to " << newNextHashToSend.value << ", but we are at " << endpoint.nextHashToSend.value << " and hash buffer contains " << endpoint.hashSendBuffer.size() << " elements";
        }
        while (newNextHashToSend > endpoint.nextHashToSend && !endpoint.hashSendBuffer.empty())
        {
            endpoint.hashSendBuffer.pop_front();
            endpoint.nextHashToSend += GameTime(1);
        }

        GameTime firstGameHashTime(message.next_game_hash_to_send());
        if (firstGameHashTime > endpoint.nextHashToReceive)
        {
            // message starts with hashes too far in the future, ignore it.
            // FIXME: this should probably be an error as it shouldn't ever happen
            LOG_ERROR << "First game hash time in message was too high! Expecting no more than " << endpoint.nextHashToReceive.value << ", received " << firstGameHashTime.value;
            return;
        }

        auto firstRelevantGameHashIndex = (endpoint.nextHashToReceive - firstGameHashTime).value;
        for (int i = firstRelevantGameHashIndex; i < message.game_hashes_size(); ++i)
        {
            playerCommandService->pushHash(endpoint.playerId, GameHash(message.game_hashes(i)));
            endpoint.nextHashToReceive += GameTime(1);
        }

        SequenceNumber newNextChatToSend(message.next_chat_to_receive());
        if (newNextChatToSend > endpoint.nextChatToSend + SequenceNumber(endpoint.chatSendBuffer.size()))
        {
            LOG_ERROR << "Remote acked chat up to " << newNextChatToSend.value << ", but we are at " << endpoint.nextChatToSend.value << " and the chat buffer contains " << endpoint.chatSendBuffer.size() << " elements";
        }
        while (newNextChatToSend > endpoint.nextChatToSend && !endpoint.chatSendBuffer.empty())
        {
            endpoint.chatSendBuffer.pop_front();
            endpoint.nextChatToSend = SequenceNumber(endpoint.nextChatToSend.value + 1);
        }

        SequenceNumber firstChatNumber(message.next_chat_to_send());
        if (firstChatNumber > endpoint.nextChatToReceive)
        {
            LOG_ERROR << "First chat number in message was too high! Expecting no more than " << endpoint.nextChatToReceive.value << ", received " << firstChatNumber.value;
            return;
        }

        auto firstRelevantChatIndex = (endpoint.nextChatToReceive - firstChatNumber).value;
        for (int i = firstRelevantChatIndex; i < message.chat_size(); ++i)
        {
            // Sanitised here rather than at the far end, so that nothing a peer
            // sends reaches the scene, the font or the log unexamined. What
            // survives is printable, one line, and bounded.
            auto text = sanitizeChatText(message.chat(i).text());
            endpoint.nextChatToReceive = SequenceNumber(endpoint.nextChatToReceive.value + 1);

            if (text.empty())
            {
                continue;
            }

            std::scoped_lock<std::mutex> lock(chatInboxMutex);
            chatInbox.push_back(ReceivedChatMessage{endpoint.playerId, text});
        }
    }
}
