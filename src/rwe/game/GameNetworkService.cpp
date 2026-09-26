#include "GameNetworkService.h"
#include <algorithm>
#include <rwe/network_util.h>
#include <rwe/proto/serialization.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/range_util.h>

namespace rwe
{
    GameNetworkService::GameNetworkService(
        PlayerId localPlayerId,
        int port,
        const std::vector<GameNetworkService::EndpointInfo>& endpointSeeds,
        PlayerCommandService* playerCommandService,
        SequenceNumber resumeFromSequence)
        : localPlayerId(localPlayerId),
          port(port),
          resolver(ioContext),
          socket(ioContext),
          sendTimer(ioContext),
          flushTimer(ioContext),
          localStream(resumeFromSequence),
          remotePeersPresent(!endpointSeeds.empty()),
          playerCommandService(playerCommandService)
    {
        for (const auto& seed : endpointSeeds)
        {
            PeerLink::ResumeState resume;
            resume.nextCommandToSend = seed.nextCommandToSend;
            resume.nextCommandToReceive = seed.nextCommandToReceive;
            resume.nextHashToSend = seed.nextHashToSend;
            resume.nextHashToReceive = seed.nextHashToReceive;

            endpoints.push_back(PeerEndpoint{
                seed.playerId,
                seed.endpoint,
                std::make_unique<PeerLink>(
                    localPlayerId,
                    seed.playerId,
                    playerCommandService,
                    [this]() { return nextPacketId(); },
                    resume)});
        }
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

    int GameNetworkService::nextPacketId()
    {
        return nextPacketIdCounter++;
    }

    std::size_t GameNetworkService::commandsFittingOneSet(const std::vector<PlayerCommand>& commands)
    {
        return PeerLink::commandsFittingOneSet(commands);
    }

    void GameNetworkService::submitCommands(SceneTime sceneTime, const GameNetworkService::CommandSet& commands)
    {
        asio::post(ioContext, [this, sceneTime, commands]() {
            currentSceneTime = sceneTime;

            // Kept whether anyone has been dropped or not, because by the time
            // one has it is too late to start: a returning peer wants the sets
            // from before it went quiet.
            localStream.recordCommandSet(commands);
            for (auto& e : endpoints)
            {
                e.link->submitCommands(sceneTime, commands);
            }
            sendToAll();
        });
    }

    void GameNetworkService::submitRunState(unsigned int speedPermille, bool paused, bool stalled)
    {
        asio::post(ioContext, [this, speedPermille, paused, stalled]() {
            for (auto& e : endpoints)
            {
                e.link->submitRunState(speedPermille, paused, stalled);
            }
        });
    }

    void GameNetworkService::submitGameHash(GameHash hash)
    {
        asio::post(ioContext, [this, hash]() {
            localStream.recordHash();
            for (auto& e : endpoints)
            {
                e.link->submitGameHash(hash);
            }
            sendToAll();
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
                if (e.link->chatBacklogFull())
                {
                    result.set_value(false);
                    return;
                }
            }

            for (auto& e : endpoints)
            {
                e.link->submitChatMessage(text);
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
        asio::post(ioContext, [this, localSceneTime, &result]() {
            auto time = getTimestamp();
            auto otherTimes = choose(endpoints, [](const auto& e) -> std::optional<PeerSceneTimeReport> {
                auto last = e.link->lastKnownSceneTime();
                if (!last)
                {
                    return std::nullopt;
                }
                return PeerSceneTimeReport{
                    last->first,
                    last->second,
                    PeerRunState{e.link->remoteSpeedPermille(), e.link->remotePaused(), e.link->remoteStalled()}};
            });

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
                statuses.push_back(e.link->status(now, startTime));
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

            if (!localStream.holdsFrom(fromSequence))
            {
                LOG_ERROR << "Cannot resume the stream to player " << playerId.value << " at " << fromSequence.value
                          << ": this peer holds " << localStream.firstHeldSequence().value << " to " << localStream.nextSendSequence().value;
                result.set_value(false);
                return;
            }

            auto endpoint = known->second;

            PeerLink::ResumeState resume;
            resume.nextCommandToSend = fromSequence;
            resume.nextCommandToReceive = theirNextSequence;
            // The hash stream is numbered the same way and needs no history.
            // This peer has not yet run the tick the returning one is waiting
            // for, so there is nothing held back to hand over; saying where
            // this stream has reached is enough, because the receiver skips
            // whatever falls below the position it asked to resume at.
            resume.nextHashToSend = GameTime(localStream.nextHashSequence().value);
            resume.nextHashToReceive = GameTime(theirNextSequence.value);
            resume.currentSceneTime = currentSceneTime;

            auto link = std::make_unique<PeerLink>(
                localPlayerId,
                playerId,
                playerCommandService,
                [this]() { return nextPacketId(); },
                resume);
            link->setAcceptingCommands(acceptingCommands);
            link->restoreUnackedCommands(localStream.historyFrom(fromSequence));

            forgottenEndpoints.erase(known);
            endpoints.push_back(PeerEndpoint{playerId, endpoint, std::move(link)});

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
            for (auto& e : endpoints)
            {
                e.link->setAcceptingCommands(value);
            }
        });
    }

    float GameNetworkService::getMaxAverageRttMillis()
    {
        std::promise<float> result;
        asio::post(ioContext, [this, &result]() {
            auto maxRtt = 0.0f;
            for (const auto& e : endpoints)
            {
                if (e.link->averageRoundTripTime() > maxRtt)
                {
                    maxRtt = e.link->averageRoundTripTime();
                }
            }

            result.set_value(maxRtt);
        });

        return result.get_future().get();
    }

    float GameNetworkService::getMaxRoundTripDeviationMillis()
    {
        std::promise<float> result;
        asio::post(ioContext, [this, &result]() {
            auto maxDeviation = 0.0f;
            for (const auto& e : endpoints)
            {
                if (e.link->roundTripDeviation() > maxDeviation)
                {
                    maxDeviation = e.link->roundTripDeviation();
                }
            }

            result.set_value(maxDeviation);
        });

        return result.get_future().get();
    }

    bool GameNetworkService::hasRemotePeers() const
    {
        return remotePeersPresent;
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
                // A packet that throws costs that packet. Before this, it
                // ended run(), and with nothing left to service the
                // io_context the main thread waited on its next future for
                // ever: one bad datagram froze the game. Issue #75.
                try
                {
                    receive(error, bytesTransferred);
                }
                catch (const std::exception& e)
                {
                    LOG_ERROR << "Ignoring a packet that could not be handled: " << e.what();
                }
                listenForNextMessage();
            });
    }

    void GameNetworkService::sendLoop()
    {
        sendToAll();
        sendTimer.expires_after(SendInterval);
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

    void GameNetworkService::armFlush()
    {
        if (flushArmed)
        {
            return;
        }
        flushArmed = true;
        flushTimer.expires_after(PeerLink::SubmitSendInterval);
        flushTimer.async_wait([this](const asio::error_code& error) {
            flushArmed = false;
            if (error)
            {
                return;
            }
            sendToAll();
        });
    }

    void GameNetworkService::send(GameNetworkService::PeerEndpoint& endpoint)
    {
        auto now = getTimestamp();
        if (!endpoint.link->sendIsDue(now))
        {
            armFlush();
            return;
        }

        auto sizeLimit = sendBuffer.size() - 4;
        auto message = endpoint.link->makePacket(now, sizeLimit);
        if (message.empty())
        {
            return;
        }
        if (message.size() > sizeLimit)
        {
            LOG_ERROR << "A packet of " << message.size() << " bytes is too large to send; skipping it";
            return;
        }

        std::copy(message.begin(), message.end(), sendBuffer.begin());
        writeInt(&sendBuffer[message.size()], computeCrc(sendBuffer.data(), static_cast<unsigned int>(message.size())));

        LOG_DEBUG << "Sending " << message.size() + 4 << " bytes to endpoint: " << endpoint.endpoint.address().to_string() << ":" << endpoint.endpoint.port();
        socket.send_to(asio::buffer(sendBuffer.data(), message.size() + 4), endpoint.endpoint);
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
        auto computedCrc = computeCrc(receiveBuffer.data(), static_cast<unsigned int>(receivedBytes - 4));
        if (receivedCrc != computedCrc)
        {
            LOG_ERROR << "Message CRC incorrect, ignoring";
            return;
        }

        proto::NetworkMessage outerMessage;
        outerMessage.ParseFromArray(receiveBuffer.data(), static_cast<int>(receivedBytes - 4));
        if (!outerMessage.has_game_update())
        {
            // message wasn't a game update, ignore it
            LOG_DEBUG << "Not game update, ignoring";
            return;
        }

        endpointIt->link->onPacket(outerMessage.game_update(), receiveTime);

        auto messages = endpointIt->link->takeReceivedChat();
        if (!messages.empty())
        {
            std::scoped_lock<std::mutex> lock(chatInboxMutex);
            for (auto& message : messages)
            {
                chatInbox.push_back(std::move(message));
            }
        }
    }
}
