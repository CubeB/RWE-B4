#include "LoadingNetworkService.h"
#include <algorithm>
#include <rwe/net/EndpointFamily.h>
#include <rwe/network_util.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <stdexcept>

namespace rwe
{
    LoadingNetworkService::LoadingNetworkService()
        : resolver(ioContext), socket(ioContext), notifyTimer(ioContext)
    {
    }

    LoadingNetworkService::~LoadingNetworkService()
    {
        if (networkThread.joinable())
        {
            ioContext.stop();
            networkThread.join();
        }
    }

    void LoadingNetworkService::addEndpoint(int playerIndex, const std::string& host, const std::string& port)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        // asio guarantees that resolve returns non-empty
        auto results = resolver.resolve(host, port);
        remoteEndpoints.emplace_back(playerIndex, toDualStackEndpoint(results.begin()->endpoint()), Status::Loading);
    }

    void LoadingNetworkService::setLocalSlot(int slot)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        localSlot = slot;
    }

    void LoadingNetworkService::setDoneLoading()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        loadingStatus = Status::Ready;
    }

    bool LoadingNetworkService::areAllClientsReady()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        return std::all_of(remoteEndpoints.begin(), remoteEndpoints.end(), [](const auto& p) { return p.status == Status::Ready; });
    }

    std::vector<std::string> LoadingNetworkService::peersNotReady()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        std::vector<std::string> missing;
        for (const auto& p : remoteEndpoints)
        {
            if (p.status != Status::Ready)
            {
                missing.push_back(
                    "player " + std::to_string(p.playerIndex + 1) + " at "
                    + p.endpoint.address().to_string() + ":" + std::to_string(p.endpoint.port()));
            }
        }
        return missing;
    }

    void LoadingNetworkService::waitForAllToBeReady()
    {
        // Crude, but it works -- and unlike a do/while with no bound it
        // ends. See the header for what an unbounded wait costs.
        auto deadline = getTimestamp() + loadingTimeout;
        while (!areAllClientsReady())
        {
            if (getTimestamp() >= deadline)
            {
                auto missing = peersNotReady();
                std::string message = "Timed out waiting for other players to finish loading (";
                for (std::size_t i = 0; i < missing.size(); ++i)
                {
                    message += (i == 0 ? "" : ", ") + missing[i];
                }
                message += "). They may have lost the connection, or the address may be wrong.";

                LOG_ERROR << message;
                throw std::runtime_error(message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    void LoadingNetworkService::start(const std::string& port)
    {
        networkThread = std::thread(&LoadingNetworkService::run, this, port);
    }

    void LoadingNetworkService::run(const std::string& port)
    {
        try
        {
            LOG_DEBUG << "Opening listen socket on port " << port;
            auto endpoint = asio::ip::udp::endpoint(asio::ip::udp::v6(), std::stoi(port));
            socket.open(endpoint.protocol());
            setDualStack(socket);
            socket.bind(endpoint);

            startListening();

            notifyStatusLoop();

            ioContext.run();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "Network thread died with error: " << e.what();
        }
    }

    void LoadingNetworkService::onReceive(const asio::error_code& error, std::size_t bytesTransferred)
    {
        if (error)
        {
            LOG_ERROR << "Received network error from " << currentRemoteEndpoint.address().to_string() << " port " << currentRemoteEndpoint.port() << ": " << error.message();
            return;
        }

        std::scoped_lock<std::mutex> lock(mutex);
        LOG_DEBUG << "Received network message from " << currentRemoteEndpoint.address().to_string() << " " << currentRemoteEndpoint.port() << ", size " << bytesTransferred;

        if (bytesTransferred < 4)
        {
            LOG_DEBUG << "Received message is too short, ignoring";
            return;
        }

        auto receivedCrc = readInt(&receiveBuffer[bytesTransferred - 4]);
        auto computedCrc = computeCrc(receiveBuffer.data(), bytesTransferred - 4);
        if (receivedCrc != computedCrc)
        {
            LOG_DEBUG << "Message CRC incorrect, ignoring";
            return;
        }

        proto::NetworkMessage message;
        if (!message.ParseFromArray(receiveBuffer.data(), bytesTransferred - 4))
        {
            LOG_DEBUG << "Message did not parse, ignoring";
            return;
        }

        auto it = std::find_if(remoteEndpoints.begin(), remoteEndpoints.end(), [this](const auto& p) { return p.endpoint == currentRemoteEndpoint; });
        if (it == remoteEndpoints.end())
        {
            // Not from where anybody was told to expect a peer. Behind a
            // NAT that is the ordinary case, not an attack: the address the
            // lobby gave was a guess at the port, and the mapping the
            // router made for this machine's socket is wherever it put it.
            // So a whole, well-formed message that names a seat, from the
            // address that seat is already known at, moves that seat's
            // endpoint here. The address has to match and only the port is
            // allowed to differ: this is not authentication -- the CRC is
            // not a MAC -- but spoofing the address was already enough to
            // inject packets, and a different port of the same address buys
            // an attacker nothing it did not have.
            if (!message.has_loading_status() || !message.loading_status().has_sender_slot())
            {
                LOG_DEBUG << "Sender was unknown, aborting";
                return;
            }

            const auto slot = static_cast<int>(message.loading_status().sender_slot());
            it = std::find_if(remoteEndpoints.begin(), remoteEndpoints.end(), [&](const auto& p) {
                return p.playerIndex == slot && p.endpoint.address() == currentRemoteEndpoint.address();
            });
            if (it == remoteEndpoints.end())
            {
                LOG_DEBUG << "Sender was unknown, aborting";
                return;
            }

            LOG_INFO << "Player " << (slot + 1) << " is sending from " << currentRemoteEndpoint.address().to_string() << ":" << currentRemoteEndpoint.port()
                     << ", not " << it->endpoint.address().to_string() << ":" << it->endpoint.port() << "; following it";
            it->endpoint = currentRemoteEndpoint;
        }

        LOG_DEBUG << "Sender was recognised";

        if (!message.has_loading_status())
        {
            LOG_DEBUG << "Sender is already in game!";
            it->status = Status::Ready;
            return;
        }

        switch (message.loading_status().status())
        {
            case proto::LoadingStatusMessage_Status_Loading:
                LOG_DEBUG << "Sender is loading";
                it->status = Status::Loading;
                break;
            case proto::LoadingStatusMessage_Status_Ready:
                LOG_DEBUG << "Sender is ready";
                it->status = Status::Ready;
                break;
            default:
                throw std::logic_error("Unhandled loading status");
        }
    }

    void LoadingNetworkService::notifyStatus()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        LOG_DEBUG << "Notifying peers about loading status";

        proto::NetworkMessage outerMessage;

        {
            auto& innerMessage = *outerMessage.mutable_loading_status();
            if (localSlot)
            {
                innerMessage.set_sender_slot(static_cast<unsigned int>(*localSlot));
            }
            switch (loadingStatus)
            {
                case Status::Loading:
                    LOG_DEBUG << "we are loading";
                    innerMessage.set_status(proto::LoadingStatusMessage_Status_Loading);
                    break;
                case Status::Ready:
                    LOG_DEBUG << "we are ready";
                    innerMessage.set_status(proto::LoadingStatusMessage_Status_Ready);
                    break;
                default:
                    throw std::logic_error("Unhandled loading status");
            }
        }

        auto messageSize = outerMessage.ByteSizeLong();
        if (messageSize > static_cast<unsigned long long>(getSize(sendBuffer) - 4))
        {
            throw std::runtime_error("Message to be sent was bigger than buffer size");
        }
        if (!outerMessage.SerializeToArray(sendBuffer.data(), sendBuffer.size()))
        {
            throw std::runtime_error("Failed to serialize message to buffer");
        }

        // throw in a CRC to verify the message
        writeInt(&sendBuffer[messageSize], computeCrc(sendBuffer.data(), messageSize));

        for (const auto& p : remoteEndpoints)
        {
            LOG_DEBUG << "Sending notification to " << p.endpoint.address().to_string() << " " << p.endpoint.port() << ", size " << outerMessage.ByteSizeLong();
            socket.send_to(asio::buffer(sendBuffer.data(), messageSize + 4), p.endpoint);
        }
    }

    void LoadingNetworkService::notifyStatusLoop()
    {
        notifyStatus();
        notifyTimer.expires_after(std::chrono::milliseconds(100));
        notifyTimer.async_wait([this](const asio::error_code& error) {
            if (error)
            {
                LOG_ERROR << "Received error from network notify timer: " << error.message();
                return;
            }
            notifyStatusLoop();
        });
    }

    void LoadingNetworkService::startListening()
    {
        socket.async_receive_from(
            asio::buffer(receiveBuffer.data(), receiveBuffer.size()),
            currentRemoteEndpoint,
            [this](const auto& error, const auto& bytesTransferred) {
                onReceive(error, bytesTransferred);
                startListening();
            });
    }
    asio::ip::udp::endpoint LoadingNetworkService::getEndpoint(int playerIndex)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        auto it = std::find_if(remoteEndpoints.begin(), remoteEndpoints.end(), [&](const auto& e) { return e.playerIndex == playerIndex; });
        if (it == remoteEndpoints.end())
        {
            throw std::runtime_error("Endpoint not found for index " + std::to_string(playerIndex));
        }
        return it->endpoint;
    }
}
