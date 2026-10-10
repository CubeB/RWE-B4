#pragma once

#include <array>
#include <atomic>
#include <asio.hpp>
#include <chrono>
#include <functional>
#include <mutex>
#include <network.pb.h>
#include <string>
#include <thread>
#include <vector>

namespace rwe
{
    class LoadingNetworkService
    {
    public:
        enum class Status
        {
            Loading,
            Ready
        };

        struct PlayerInfo
        {
            int playerIndex;
            asio::ip::udp::endpoint endpoint;
            Status status;
            PlayerInfo(int playerIndex, const asio::ip::udp::endpoint& endpoint, Status status) : playerIndex(playerIndex), endpoint(endpoint), status(status) {}
        };

    private:
        std::thread networkThread;

        // state shared between threads
        std::mutex mutex;
        Status loadingStatus{Status::Loading};
        std::vector<PlayerInfo> remoteEndpoints;

        // state owned by the worker thread
        asio::io_context ioContext;
        asio::ip::udp::resolver resolver;
        asio::ip::udp::socket socket;
        std::array<char, 1500> sendBuffer;
        std::array<char, 1500> receiveBuffer;
        asio::ip::udp::endpoint currentRemoteEndpoint;
        asio::steady_timer notifyTimer;

    public:
        LoadingNetworkService();

        virtual ~LoadingNetworkService();

        void addEndpoint(int playerIndex, const std::string& host, const std::string& port);

        asio::ip::udp::endpoint getEndpoint(int playerIndex);

        void setDoneLoading();

        bool areAllClientsReady();

        /**
         * How long waitForAllToBeReady keeps waiting before it gives up.
         *
         * Generous, because the wait is not for the network: it is for the
         * other machine to finish loading the map, which on a slow disk with
         * a large data set is the whole cost of starting a game and nothing
         * to do with how well the link is doing.
         */
        static constexpr std::chrono::milliseconds defaultLoadingTimeout{120000};

        /**
         * How long waitForAllToBeReady keeps waiting before it gives up.
         *
         * Generous, because the wait is not for the network: it is for the
         * other machine to finish loading the map, which on a slow disk with
         * a large data set is the whole cost of starting a game and nothing
         * to do with how well the link is doing.
         *
         * Settable so a test can make it expire without waiting two minutes
         * for it.
         */
        std::chrono::milliseconds loadingTimeout{defaultLoadingTimeout};

        /**
         * Blocks until every peer has said it has finished loading, or
         * throws naming the ones that never did.
         *
         * A peer that has gone away -- crashed, lost the network, closed
         * the window -- never says anything at all, so an unbounded wait
         * here does not wait: it hangs. The loading screen is black, the log
         * shows a healthy ten-a-second heartbeat of "we are ready" being
         * sent into nothing, and nothing on the machine says why. That is a
         * much worse failure than being told which peer is missing.
         */
        void waitForAllToBeReady();

        /** The peers still loading, as "player n at host:port". */
        std::vector<std::string> peersNotReady();

        void start(const std::string& port);

    private:
        void run(const std::string& port);

        void onReceive(const asio::error_code& error, std::size_t bytesTransferred);

        void notifyStatus();

        void notifyStatusLoop();

        void startListening();
    };
}
