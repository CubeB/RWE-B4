#include "NatTraversal.h"
#include <algorithm>
#include <array>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        constexpr std::size_t maxHeardPeers = 16;
        constexpr std::size_t maxPunchTargets = 16;

        std::string describe(const asio::ip::udp::endpoint& e)
        {
            return e.address().to_string() + ":" + std::to_string(e.port());
        }

        asio::ip::udp::endpoint plainEndpoint(const asio::ip::udp::endpoint& e)
        {
            auto address = e.address();
            if (address.is_v6() && address.to_v6().is_v4_mapped())
            {
                address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
            }
            return asio::ip::udp::endpoint(address, e.port());
        }
    }

    const std::string& NatTraversal::punchMessage()
    {
        static const std::string message = "RWE-PUNCH/1\n";
        return message;
    }

    NatTraversal::NatTraversal(LanDiscovery& discovery) : discovery(discovery)
    {
    }

    NatTraversal::~NatTraversal()
    {
        stop();
    }

    void NatTraversal::start(unsigned short gamePort, const std::vector<stun::Server>& servers)
    {
        stop();
        stopRequested = false;
        {
            std::scoped_lock<std::mutex> lock(mutex);
            current = Status{};
            current.phase = Phase::Working;
            heard.clear();
            bindProblem.clear();
        }
        worker = std::thread(&NatTraversal::run, this, gamePort, servers, std::vector<asio::ip::udp::endpoint>{});
    }

    void NatTraversal::start(unsigned short gamePort, std::vector<asio::ip::udp::endpoint> servers)
    {
        stop();
        stopRequested = false;
        {
            std::scoped_lock<std::mutex> lock(mutex);
            current = Status{};
            current.phase = Phase::Working;
            heard.clear();
            bindProblem.clear();
        }
        worker = std::thread(&NatTraversal::run, this, gamePort, std::vector<stun::Server>{}, std::move(servers));
    }

    void NatTraversal::stop()
    {
        stopRequested = true;
        if (worker.joinable())
        {
            worker.join();
        }

        discovery.setForeignDatagramHandler(nullptr);
        std::scoped_lock<std::mutex> lock(mutex);
        discoveryProbe.reset();
        gameProbe.reset();
        if (current.phase == Phase::Working)
        {
            current.phase = Phase::Idle;
        }
    }

    NatTraversal::Status NatTraversal::status() const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        return current;
    }

    void NatTraversal::setPunchTargets(std::vector<asio::ip::udp::endpoint> targets)
    {
        for (auto& t : targets)
        {
            t = plainEndpoint(t);
        }
        if (targets.size() > maxPunchTargets)
        {
            targets.resize(maxPunchTargets);
        }
        std::scoped_lock<std::mutex> lock(mutex);
        punchTargets = std::move(targets);
    }

    std::vector<asio::ip::udp::endpoint> NatTraversal::heardFrom() const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        return heard;
    }

    bool NatTraversal::onDiscoveryDatagram(const char* data, std::size_t size, const asio::ip::udp::endpoint& from)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        if (!discoveryProbe)
        {
            return false;
        }
        return discoveryProbe->onDatagram(data, size, from, getTimestamp());
    }

    void NatTraversal::refreshStatus()
    {
        // The mutex is held.
        if (!discoveryProbe || !gameProbe)
        {
            return;
        }

        const auto discoveryState = discoveryProbe->state();
        const auto gameState = gameProbe->state();

        current.discoveryAddress = discoveryProbe->mapped();
        current.gameAddress = gameProbe->mapped();

        if (discoveryState == stun::ReflexiveProbe::State::Pending || gameState == stun::ReflexiveProbe::State::Pending)
        {
            current.phase = Phase::Working;
            return;
        }

        if (!bindProblem.empty())
        {
            current.phase = Phase::Failed;
            current.problem = bindProblem;
            return;
        }

        if (discoveryState == stun::ReflexiveProbe::State::Failed || gameState == stun::ReflexiveProbe::State::Failed)
        {
            current.phase = Phase::Failed;
            current.problem = "No reply from the internet; UDP may be blocked on this connection.";
            return;
        }

        if (discoveryProbe->mapping() == stun::ReflexiveProbe::Mapping::EndpointDependent
            || gameProbe->mapping() == stun::ReflexiveProbe::Mapping::EndpointDependent)
        {
            current.phase = Phase::Failed;
            current.problem = "This router gives each destination its own port, so direct play cannot work.";
            return;
        }

        current.phase = Phase::Ready;
        current.problem.clear();
    }

    void NatTraversal::run(unsigned short gamePort, std::vector<stun::Server> names, std::vector<asio::ip::udp::endpoint> servers)
    {
        try
        {
            if (!names.empty())
            {
                // A name lookup cannot be interrupted, and a lobby that is
                // being closed -- or a game that has been started -- must not
                // wait on a DNS server that is slow to say no. So it is run
                // on a thread of its own, which holds its own copies and is
                // left to finish if nobody is waiting any more.
                struct Lookup
                {
                    std::mutex mutex;
                    bool done{false};
                    std::vector<asio::ip::udp::endpoint> found;
                };
                auto lookup = std::make_shared<Lookup>();
                std::thread([lookup, names]() {
                    auto found = stun::resolveServers(names);
                    std::scoped_lock<std::mutex> lock(lookup->mutex);
                    lookup->found = std::move(found);
                    lookup->done = true;
                }).detach();

                while (!stopRequested)
                {
                    {
                        std::scoped_lock<std::mutex> lock(lookup->mutex);
                        if (lookup->done)
                        {
                            servers = lookup->found;
                            break;
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            if (stopRequested)
            {
                return;
            }

            // The game port first, so that its failure to bind is known
            // before anything is said about the rest.
            asio::io_context context;
            asio::ip::udp::socket socket(context);
            std::string problem;
            {
                asio::error_code ec;
                socket.open(asio::ip::udp::v4(), ec);
                if (!ec)
                {
                    // Deliberately not reuse_address: on Windows that lets a
                    // second socket take the port while this one holds it, and
                    // the game must be told it is in use, not share it.
                    socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), gamePort), ec);
                }
                if (!ec)
                {
                    socket.non_blocking(true, ec);
                }
                if (ec)
                {
                    problem = "UDP port " + std::to_string(gamePort) + " is in use; is another game running?";
                    LOG_WARN << "NAT traversal: UDP port " << gamePort << " could not be opened: " << ec.message();
                }
            }

            const auto sendFromGameSocket = [&socket](const asio::ip::udp::endpoint& to, const std::string& data) {
                if (!socket.is_open())
                {
                    return false;
                }
                asio::error_code ec;
                socket.send_to(asio::buffer(data), to, 0, ec);
                return !ec;
            };

            {
                std::scoped_lock<std::mutex> lock(mutex);
                discoveryProbe = std::make_unique<stun::ReflexiveProbe>(
                    [this](const asio::ip::udp::endpoint& to, const std::string& data) { return discovery.sendRaw(to, data); });
                gameProbe = std::make_unique<stun::ReflexiveProbe>(sendFromGameSocket);
                bindProblem = problem;
            }

            discovery.setForeignDatagramHandler(
                [this](const char* data, std::size_t size, const asio::ip::udp::endpoint& from) {
                    return onDiscoveryDatagram(data, size, from);
                });

            {
                std::scoped_lock<std::mutex> lock(mutex);
                const auto now = getTimestamp();
                if (servers.empty())
                {
                    bindProblem = bindProblem.empty() ? "Cannot look up the address servers; is there an internet connection?" : bindProblem;
                }
                discoveryProbe->begin(servers, now);
                if (problem.empty())
                {
                    gameProbe->begin(servers, now);
                }
                else
                {
                    gameProbe->begin({}, now);
                }
                refreshStatus();
            }

            std::array<char, 2048> buffer{};
            auto lastPunch = getTimestamp() - punchInterval;

            while (!stopRequested)
            {
                // Everything waiting on the game port.
                for (;;)
                {
                    asio::ip::udp::endpoint from;
                    asio::error_code ec;
                    const auto received = socket.is_open() ? socket.receive_from(asio::buffer(buffer), from, 0, ec) : 0;
                    if (ec == asio::error::connection_reset)
                    {
                        // Windows reports an ICMP port-unreachable from an
                        // earlier send as an error on the next receive. It is
                        // about a datagram already sent, not about this one.
                        continue;
                    }
                    if (ec || received == 0)
                    {
                        break;
                    }

                    const auto now = getTimestamp();
                    std::scoped_lock<std::mutex> lock(mutex);
                    if (gameProbe && gameProbe->onDatagram(buffer.data(), received, from, now))
                    {
                        continue;
                    }

                    const auto& magic = punchMessage();
                    if (received == magic.size() && std::equal(magic.begin(), magic.end(), buffer.begin()))
                    {
                        const auto source = plainEndpoint(from);
                        if (std::find(heard.begin(), heard.end(), source) == heard.end() && heard.size() < maxHeardPeers)
                        {
                            heard.push_back(source);
                            LOG_INFO << "NAT traversal: a datagram reached this machine's game port from " << describe(source);
                        }
                    }
                }

                const auto now = getTimestamp();
                std::vector<asio::ip::udp::endpoint> targets;
                {
                    std::scoped_lock<std::mutex> lock(mutex);
                    if (discoveryProbe)
                    {
                        discoveryProbe->tick(now);
                    }
                    if (gameProbe)
                    {
                        gameProbe->tick(now);
                    }
                    refreshStatus();

                    if (now - lastPunch >= punchInterval)
                    {
                        targets = punchTargets;
                        lastPunch = now;
                    }
                }

                for (const auto& target : targets)
                {
                    sendFromGameSocket(target, punchMessage());
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // Close it here, on the thread that owned it, before stop() returns:
            // the game binds this port next.
            asio::error_code ignored;
            socket.close(ignored);
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "NAT traversal thread died: " << e.what();
            std::scoped_lock<std::mutex> lock(mutex);
            current.phase = Phase::Failed;
            current.problem = std::string("Internet connection setup failed: ") + e.what();
        }
    }
}
