#pragma once

#include <array>
#include <asio.hpp>
#include <atomic>
#include <functional>
#include <rwe/net/Stun.h>
#include <string>
#include <thread>

namespace rwe
{
    /**
     * A STUN server on loopback, for a test or a probe that has no business
     * asking somebody else's. It answers each Binding request with the
     * address the request came from, passed through `disguise` -- which is
     * how a test stands in for a router that has rewritten the source.
     *
     * Header only, and not part of the game: it is here so that the unit
     * tests and `ui_probe --multiplayer-flow` share one copy.
     */
    class FakeStunServer
    {
    public:
        using Disguise = std::function<asio::ip::udp::endpoint(const asio::ip::udp::endpoint&)>;

        explicit FakeStunServer(Disguise disguise = nullptr)
            : disguise(std::move(disguise)),
              socket(context, asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), 0))
        {
            socket.non_blocking(true);
            thread = std::thread([this]() { serve(); });
        }

        ~FakeStunServer()
        {
            stopping = true;
            thread.join();
        }

        FakeStunServer(const FakeStunServer&) = delete;
        FakeStunServer& operator=(const FakeStunServer&) = delete;

        asio::ip::udp::endpoint endpoint() const { return socket.local_endpoint(); }

        /** `127.0.0.1:port`, as a probe's --stun list or the menu's seam takes it. */
        std::string address() const { return "127.0.0.1:" + std::to_string(socket.local_endpoint().port()); }

        int requests() const { return requestCount; }

    private:
        void serve()
        {
            std::array<char, 1500> buffer{};
            while (!stopping)
            {
                asio::ip::udp::endpoint from;
                asio::error_code ec;
                const auto n = socket.receive_from(asio::buffer(buffer), from, 0, ec);
                if (ec || n < 20)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    continue;
                }
                if (!stun::looksLikeStun(buffer.data(), n))
                {
                    continue;
                }
                ++requestCount;

                auto seen = disguise ? disguise(from) : from;
                auto bytes = seen.address().to_v4().to_bytes();
                std::uint32_t addr = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
                    | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
                addr ^= 0x2112A442;
                const unsigned port = seen.port() ^ 0x2112;

                std::string out;
                out += std::string("\x01\x01\x00\x0C\x21\x12\xA4\x42", 8);
                out.append(buffer.data() + 8, 12);
                out += std::string("\x00\x20\x00\x08\x00\x01", 6);
                out.push_back(static_cast<char>(port >> 8));
                out.push_back(static_cast<char>(port & 0xFF));
                out.push_back(static_cast<char>(addr >> 24));
                out.push_back(static_cast<char>((addr >> 16) & 0xFF));
                out.push_back(static_cast<char>((addr >> 8) & 0xFF));
                out.push_back(static_cast<char>(addr & 0xFF));
                socket.send_to(asio::buffer(out), from, 0, ec);
            }
        }

        Disguise disguise;
        asio::io_context context;
        asio::ip::udp::socket socket;
        std::atomic<bool> stopping{false};
        std::atomic<int> requestCount{0};
        std::thread thread;
    };
}
