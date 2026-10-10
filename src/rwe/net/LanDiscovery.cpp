#include "LanDiscovery.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        constexpr const char* magic = "RWE-DISCOVERY/1";

        /** The setup screen offers ten seats, so ten is all a lobby can have. */
        constexpr std::size_t maxSlots = 10;
        constexpr std::size_t maxFieldLength = 128;

        bool isHex(char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        int hexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        }

        bool isUnreserved(char c)
        {
            auto u = static_cast<unsigned char>(c);
            if (u <= 0x20 || u >= 0x7f) return false;
            switch (c)
            {
                case '%':
                case ',':
                case ';':
                case '=':
                case '\n':
                case '\r':
                    return false;
                default:
                    return true;
            }
        }

        std::string percentEncode(const std::string& s)
        {
            std::string out;
            out.reserve(s.size());
            for (char c : s)
            {
                if (isUnreserved(c))
                {
                    out.push_back(c);
                }
                else
                {
                    std::array<char, 4> buf{};
                    std::snprintf(buf.data(), buf.size(), "%%%02X", static_cast<unsigned char>(c));
                    out.append(buf.data());
                }
            }
            return out;
        }

        /**
         * Strict on purpose: this is a field somebody else wrote. A bad
         * escape, a NUL, or a truncated pair ends the parse rather than
         * producing a lobby with a hole in its name, because a half-decoded
         * map name would be silently wrong and the game would start on a map
         * nobody chose.
         */
        bool percentDecode(const std::string& s, std::string& out)
        {
            out.clear();
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                char c = s[i];
                if (c != '%')
                {
                    if (!isUnreserved(c)) return false;
                    out.push_back(c);
                    continue;
                }
                if (i + 2 >= s.size()) return false;
                if (!isHex(s[i + 1]) || !isHex(s[i + 2])) return false;
                auto value = static_cast<char>((hexValue(s[i + 1]) << 4) | hexValue(s[i + 2]));
                if (value == '\0') return false;
                out.push_back(value);
                i += 2;
            }
            return true;
        }

        std::vector<std::string> split(const std::string& s, char sep)
        {
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (true)
            {
                auto pos = s.find(sep, start);
                if (pos == std::string::npos)
                {
                    parts.push_back(s.substr(start));
                    return parts;
                }
                parts.push_back(s.substr(start, pos - start));
                start = pos + 1;
            }
        }

        bool parseUnsigned(const std::string& s, unsigned long& out)
        {
            if (s.empty() || s.size() > 10) return false;
            unsigned long value = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9') return false;
                value = value * 10 + static_cast<unsigned long>(c - '0');
                if (value > 0xfffffffful) return false;
            }
            out = value;
            return true;
        }

        bool parseInt(const std::string& s, int& out)
        {
            if (s.empty() || s.size() > 11) return false;
            bool negative = false;
            std::size_t i = 0;
            if (s[0] == '-')
            {
                negative = true;
                i = 1;
            }
            if (i >= s.size()) return false;
            long value = 0;
            for (; i < s.size(); ++i)
            {
                if (s[i] < '0' || s[i] > '9') return false;
                value = value * 10 + (s[i] - '0');
                if (value > 2147483647L) return false;
            }
            out = static_cast<int>(negative ? -value : value);
            return true;
        }

        bool knownController(const std::string& s)
        {
            return s == "open" || s == "human" || s == "computer" || s == "network";
        }

        bool knownSide(const std::string& s)
        {
            return s == "ARM" || s == "CORE";
        }

        std::optional<DiscoveredSlot> decodeSlot(const std::string& line)
        {
            auto f = split(line, ',');
            if (f.size() != 7) return std::nullopt;

            DiscoveredSlot slot;
            if (!parseInt(f[0], slot.index)) return std::nullopt;
            if (slot.index < 0 || slot.index >= static_cast<int>(maxSlots)) return std::nullopt;

            slot.controller = f[1];
            if (!knownController(slot.controller)) return std::nullopt;

            slot.side = f[2];
            if (!knownSide(slot.side)) return std::nullopt;

            unsigned long colour = 0;
            if (!parseUnsigned(f[3], colour) || colour > 255) return std::nullopt;
            slot.colour = static_cast<unsigned int>(colour);

            if (!parseInt(f[4], slot.team) || slot.team < -1 || slot.team > 9) return std::nullopt;

            if (!percentDecode(f[5], slot.name)) return std::nullopt;
            if (slot.name.size() > maxFieldLength) return std::nullopt;

            if (!percentDecode(f[6], slot.address)) return std::nullopt;
            if (slot.address.size() > maxFieldLength) return std::nullopt;

            return slot;
        }
    }

    std::optional<std::string> LanDiscovery::encode(const DiscoveredGame& game)
    {
        if (game.session.empty() || game.session.size() > maxFieldLength) return std::nullopt;
        if (game.map.empty() || game.map.size() > maxFieldLength) return std::nullopt;
        if (game.port == 0 || game.port > 65535) return std::nullopt;
        if (game.slots.size() > maxSlots) return std::nullopt;

        std::string out{magic};
        out += "\nsession=" + percentEncode(game.session);
        out += "\nmap=" + percentEncode(game.map);
        out += "\nport=" + std::to_string(game.port);
        if (!game.label.empty())
        {
            if (game.label.size() > maxFieldLength) return std::nullopt;
            out += "\nlabel=" + percentEncode(game.label);
        }

        for (const auto& slot : game.slots)
        {
            if (slot.index < 0 || slot.index >= static_cast<int>(maxSlots)) return std::nullopt;
            if (!knownController(slot.controller)) return std::nullopt;
            if (!knownSide(slot.side)) return std::nullopt;
            if (slot.colour > 255) return std::nullopt;
            if (slot.team < -1 || slot.team > 9) return std::nullopt;
            if (slot.name.size() > maxFieldLength || slot.address.size() > maxFieldLength) return std::nullopt;

            out += "\nslot=" + std::to_string(slot.index);
            out += "," + slot.controller;
            out += "," + slot.side;
            out += "," + std::to_string(slot.colour);
            out += "," + std::to_string(slot.team);
            out += "," + percentEncode(slot.name);
            out += "," + percentEncode(slot.address);
        }

        if (out.size() > maxMessageSize) return std::nullopt;
        return out;
    }

    std::optional<DiscoveredGame> LanDiscovery::decode(const std::string& text)
    {
        if (text.empty() || text.size() > maxMessageSize) return std::nullopt;

        auto lines = split(text, '\n');
        if (lines.empty() || lines[0] != magic) return std::nullopt;

        DiscoveredGame game;
        bool havePort = false;
        std::vector<int> seenSlots;

        for (std::size_t i = 1; i < lines.size(); ++i)
        {
            const auto& line = lines[i];
            if (line.empty()) continue;

            auto eq = line.find('=');
            if (eq == std::string::npos || eq == 0) return std::nullopt;
            auto key = line.substr(0, eq);
            auto value = line.substr(eq + 1);

            if (key == "session")
            {
                if (!percentDecode(value, game.session)) return std::nullopt;
                if (game.session.empty() || game.session.size() > maxFieldLength) return std::nullopt;
            }
            else if (key == "map")
            {
                if (!percentDecode(value, game.map)) return std::nullopt;
                if (game.map.empty() || game.map.size() > maxFieldLength) return std::nullopt;
            }
            else if (key == "label")
            {
                if (!percentDecode(value, game.label)) return std::nullopt;
                if (game.label.size() > maxFieldLength) return std::nullopt;
            }
            else if (key == "port")
            {
                unsigned long port = 0;
                if (!parseUnsigned(value, port) || port == 0 || port > 65535) return std::nullopt;
                game.port = static_cast<unsigned int>(port);
                havePort = true;
            }
            else if (key == "slot")
            {
                if (game.slots.size() >= maxSlots) return std::nullopt;
                auto slot = decodeSlot(value);
                if (!slot) return std::nullopt;
                // A seat listed twice is ambiguous about which wins, and a
                // lobby is small enough that it is always a mistake.
                if (std::find(seenSlots.begin(), seenSlots.end(), slot->index) != seenSlots.end())
                {
                    return std::nullopt;
                }
                seenSlots.push_back(slot->index);
                game.slots.push_back(*slot);
            }
            else
            {
                // A field from a later version of this format. Ignoring it
                // is what lets an older build hear a newer announcement and
                // still make sense of it.
            }
        }

        if (game.session.empty() || game.map.empty() || !havePort) return std::nullopt;
        return game;
    }

    LanDiscovery::LanDiscovery() = default;

    LanDiscovery::~LanDiscovery()
    {
        stop();
    }

    void LanDiscovery::start(unsigned short port)
    {
        stop();

        bound = false;
        // start() does not return until the socket is bound or has failed to
        // bind, so isRunning() is a real answer rather than a guess. See the
        // header for what a flag set on the network thread would cost.
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        networkThread = std::thread(
            [this, port, promise] {
                bindAndServe(port, promise);
            });

        future.wait();
    }

    void LanDiscovery::stop()
    {
        {
            std::scoped_lock<std::mutex> lock(mutex);
            advertised.reset();
        }
        ioContext.stop();
        if (networkThread.joinable())
        {
            networkThread.join();
        }
        ioContext.restart();
        std::scoped_lock<std::mutex> lock(mutex);
        socket.reset();
        heard.clear();
    }

    bool LanDiscovery::isRunning() const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        return socket.has_value();
    }

    void LanDiscovery::bindAndServe(unsigned short port, std::shared_ptr<std::promise<bool>> bound)
    {
        try
        {
            auto endpoint = asio::ip::udp::endpoint(asio::ip::udp::v4(), port);
            asio::ip::udp::socket s(ioContext);
            s.open(endpoint.protocol());
            s.set_option(asio::socket_base::reuse_address(true));
            s.bind(endpoint);

            {
                std::scoped_lock<std::mutex> lock(mutex);
                socket = std::move(s);
                this->bound = true;
            }

            // Said while there is still no serving to do, and before any of
            // it: the caller is waiting on this and the serving below does
            // not end until stop() is called.
            bound->set_value(true);

            listen();

            announceLoop();

            ioContext.run();
            return;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR << "LAN discovery thread died with error: " << e.what();
            std::scoped_lock<std::mutex> lock(mutex);
            socket.reset();
            this->bound = false;
            bound->set_value(false);
            return;
        }
    }

    void LanDiscovery::listen()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        if (!socket) return;

        socket->async_receive_from(
            asio::buffer(receiveBuffer.data(), receiveBuffer.size()),
            currentSender,
            [this](const asio::error_code& error, std::size_t bytes) {
                if (error)
                {
                    if (error != asio::error::operation_aborted)
                    {
                        LOG_DEBUG << "LAN discovery receive error: " << error.message();
                    }
                    return;
                }

                if (bytes > 0 && bytes <= maxMessageSize)
                {
                    auto text = std::string(receiveBuffer.data(), bytes);
                    if (auto game = decode(text))
                    {
                        std::scoped_lock<std::mutex> inner(mutex);
                        game->sourceAddress = currentSender.address().to_string() + ":" + std::to_string(currentSender.port());
                        game->lastSeen = getTimestamp();

                        auto it = std::find_if(heard.begin(), heard.end(), [&](const auto& h) {
                            return h.session == game->session;
                        });
                        if (it != heard.end())
                        {
                            *it = *game;
                        }
                        else
                        {
                            heard.push_back(*game);
                            LOG_DEBUG << "LAN discovery heard a lobby: " << game->map
                                      << " from " << game->sourceAddress;
                        }
                    }
                }

                listen();
            });
    }

    void LanDiscovery::announceLoop()
    {
        prune();

        std::optional<DiscoveredGame> toAnnounce;
        {
            std::scoped_lock<std::mutex> lock(mutex);
            toAnnounce = advertised;
        }

        if (toAnnounce)
        {
            if (auto text = encode(*toAnnounce))
            {
                auto addresses = broadcastAddresses();
                std::scoped_lock<std::mutex> lock(mutex);
                if (socket)
                {
                    for (const auto& a : addresses)
                    {
                        asio::error_code ec;
                        socket->send_to(
                            asio::buffer(*text),
                            asio::ip::udp::endpoint(asio::ip::make_address(a), LanDiscovery::defaultPort),
                            0,
                            ec);
                        if (ec)
                        {
                            LOG_DEBUG << "LAN discovery could not broadcast to " << a << ": " << ec.message();
                        }
                    }
                }
            }
            else
            {
                LOG_DEBUG << "LAN discovery could not encode its lobby; not advertising";
            }
        }

        auto timer = std::make_shared<asio::steady_timer>(ioContext);
        timer->expires_after(interval);
        timer->async_wait([this, timer](const asio::error_code& error) {
            if (error) return;
            announceLoop();
        });
    }

    void LanDiscovery::prune()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        auto cutoff = getTimestamp() - expiry;
        heard.erase(
            std::remove_if(heard.begin(), heard.end(), [&](const auto& h) {
                return h.lastSeen < cutoff;
            }),
            heard.end());
    }

    std::vector<std::string> LanDiscovery::broadcastAddresses() const
    {
        // The limited broadcast reaches every subnet this machine is on,
        // which is the whole of "the local network" for a game that needs
        // one. Loopback is here so that two instances on one machine can see
        // each other: that is how this is tested, and it is also what makes
        // the feature checkable by anyone without a second PC.
        return {"255.255.255.255", "127.0.0.1"};
    }

    void LanDiscovery::advertise(const DiscoveredGame& game)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        advertised = game;
    }

    void LanDiscovery::stopAdvertising()
    {
        std::scoped_lock<std::mutex> lock(mutex);
        advertised.reset();
    }

    std::vector<DiscoveredGame> LanDiscovery::games() const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        auto cutoff = getTimestamp() - expiry;
        std::vector<DiscoveredGame> live;
        for (const auto& h : heard)
        {
            if (h.lastSeen >= cutoff)
            {
                live.push_back(h);
            }
        }
        return live;
    }
}