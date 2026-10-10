#include "LanDiscovery.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <future>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    namespace
    {
        constexpr const char* magic = "RWE-DISCOVERY/1";
        constexpr const char* probeMagic = "RWE-DISCOVERY-PROBE/1\n";
        constexpr const char* optionKeyPrefix = "opt.";
        constexpr std::size_t optionKeyPrefixLength = 4;
        /** More than a LAN of people typing addresses has any use for. */
        constexpr std::size_t maxTrackedProbers = 64;

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

        /**
         * A whole number that may be out of range, as a value that is. A
         * resource amount is a quantity somebody else chose: -5 and
         * 99999999999 are both well formed and both mean "as little" and "as
         * much as allowed", so they are clamped by the caller rather than
         * making the whole announcement unreadable. What is not a number at
         * all is still refused.
         */
        bool parseSaturatingInt(const std::string& s, long long& out)
        {
            if (s.empty() || s.size() > 20) return false;
            bool negative = false;
            std::size_t i = 0;
            if (s[0] == '-')
            {
                negative = true;
                i = 1;
            }
            if (i >= s.size()) return false;
            long long value = 0;
            for (; i < s.size(); ++i)
            {
                if (s[i] < '0' || s[i] > '9') return false;
                if (value < 1000000000LL)
                {
                    value = value * 10 + (s[i] - '0');
                }
            }
            out = negative ? -value : value;
            return true;
        }

        int clampResource(long long value)
        {
            if (value < 0) return 0;
            if (value > LanDiscovery::maxStartingResource) return LanDiscovery::maxStartingResource;
            return static_cast<int>(value);
        }

        bool isOptionNameChar(char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        }

        bool isValidOptionName(const std::string& name)
        {
            if (name.empty() || name.size() > LanDiscovery::maxOptionNameLength) return false;
            return std::all_of(name.begin(), name.end(), isOptionNameChar);
        }

        bool isValidPasswordHash(const std::string& hash)
        {
            if (hash.empty()) return true;
            if (hash.size() != 16) return false;
            return std::all_of(hash.begin(), hash.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        }

        bool knownController(const std::string& s)
        {
            return s == "open" || s == "human" || s == "computer" || s == "network";
        }

        bool knownSide(const std::string& s)
        {
            return s == "ARM" || s == "CORE";
        }

        /** What a `slotx=` line says about one seat, until its seat is known. */
        struct SlotExtras
        {
            int index{-1};
            int metal{1000};
            int energy{1000};
            std::string personality;
        };

        /**
         * `index,metal,energy,personality`. Carried apart from `slot=` so an
         * older decoder, which wants exactly seven fields there, drops
         * nothing it used to read.
         */
        std::optional<SlotExtras> decodeSlotExtras(const std::string& line)
        {
            auto f = split(line, ',');
            if (f.size() != 4) return std::nullopt;

            SlotExtras extras;
            if (!parseInt(f[0], extras.index)) return std::nullopt;
            if (extras.index < 0 || extras.index >= static_cast<int>(maxSlots)) return std::nullopt;

            long long metal = 0;
            long long energy = 0;
            if (!parseSaturatingInt(f[1], metal)) return std::nullopt;
            if (!parseSaturatingInt(f[2], energy)) return std::nullopt;
            extras.metal = clampResource(metal);
            extras.energy = clampResource(energy);

            if (!percentDecode(f[3], extras.personality)) return std::nullopt;
            if (extras.personality.size() > LanDiscovery::maxPersonalityLength) return std::nullopt;

            return extras;
        }

        std::string describeEndpoint(const asio::ip::udp::endpoint& endpoint)
        {
            auto address = endpoint.address().to_string();
            if (endpoint.address().is_v6())
            {
                address = "[" + address + "]";
            }
            return address + ":" + std::to_string(endpoint.port());
        }

        /**
         * An address written out, and nothing else. asio's parser on Windows
         * is WSAStringToAddress, which accepts a trailing `:port` and
         * `[v6]:port` and throws the port away, so "192.168.2.120:1350"
         * parses as an address with no complaint and then goes to the wrong
         * port. The character set is checked here first, and the caller has
         * already taken any port off, so what reaches asio is only an address.
         */
        std::optional<asio::ip::address> parseBareAddress(const std::string& s)
        {
            if (s.empty() || s.size() > 45) return std::nullopt;
            bool hasColon = s.find(':') != std::string::npos;
            for (char c : s)
            {
                bool ok = (c >= '0' && c <= '9') || c == '.' || c == ':'
                    || (hasColon && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
                if (!ok) return std::nullopt;
            }

            asio::error_code ec;
            auto address = asio::ip::make_address(s, ec);
            if (ec) return std::nullopt;
            return address;
        }

        bool parsePort(const std::string& s, unsigned short& out)
        {
            unsigned long value = 0;
            if (!parseUnsigned(s, value) || value == 0 || value > 65535) return false;
            out = static_cast<unsigned short>(value);
            return true;
        }

        /**
         * Who a probe is to go to: `a.b.c.d`, `a.b.c.d:port`, `v6`,
         * `[v6]`, `[v6]:port` or a v4-mapped `::ffff:a.b.c.d[:port]`. Addresses
         * only: a name would have to be resolved, and resolving blocks on
         * the network for as long as a DNS server cares to take.
         *
         * This is not getHostAndPort, which splits at the last colon and so
         * reads the last group of an unbracketed IPv6 address as a port.
         */
        std::optional<asio::ip::udp::endpoint> parseProbeTarget(const std::string& input)
        {
            auto first = input.find_first_not_of(" \t");
            if (first == std::string::npos) return std::nullopt;
            auto last = input.find_last_not_of(" \t");
            auto text = input.substr(first, last - first + 1);
            if (text.size() > 64) return std::nullopt;

            unsigned short port = LanDiscovery::defaultPort;
            std::string host = text;

            if (text.front() == '[')
            {
                auto close = text.find(']');
                if (close == std::string::npos) return std::nullopt;
                host = text.substr(1, close - 1);
                auto rest = text.substr(close + 1);
                if (!rest.empty())
                {
                    if (rest[0] != ':' || !parsePort(rest.substr(1), port)) return std::nullopt;
                }
            }
            else
            {
                auto colons = std::count(text.begin(), text.end(), ':');
                if (colons == 1)
                {
                    auto colon = text.find(':');
                    host = text.substr(0, colon);
                    if (!parsePort(text.substr(colon + 1), port)) return std::nullopt;
                }
                else if (colons >= 2)
                {
                    // A v6 address, which is all colons, or a v4-mapped one
                    // with a port after it: "::ffff:1.2.3.4:80". The second
                    // is the one with a dot before the last colon and none
                    // after it; "::ffff:1.2.3.4" has its dot last.
                    auto lastColon = text.rfind(':');
                    auto tail = text.substr(lastColon + 1);
                    if (text.find('.') != std::string::npos && tail.find('.') == std::string::npos)
                    {
                        host = text.substr(0, lastColon);
                        if (!parsePort(tail, port)) return std::nullopt;
                    }
                }
            }

            auto address = parseBareAddress(host);
            if (!address) return std::nullopt;
            return asio::ip::udp::endpoint(*address, port);
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

    std::string LanDiscovery::hashPassword(const std::string& session, const std::string& password)
    {
        // FNV-1a, 64 bit. Written out rather than borrowed so that the value
        // a peer computes does not depend on the standard library it was
        // built with: std::hash is allowed to differ between them.
        constexpr unsigned long long offsetBasis = 0xcbf29ce484222325ULL;
        constexpr unsigned long long prime = 0x100000001b3ULL;

        unsigned long long hash = offsetBasis;
        auto mix = [&](const std::string& s) {
            for (char c : s)
            {
                hash ^= static_cast<unsigned char>(c);
                hash *= prime;
            }
        };
        mix(session);
        mix("\n");
        mix(password);

        static constexpr char digits[] = "0123456789abcdef";
        std::string out(16, '0');
        for (int i = 15; i >= 0; --i)
        {
            out[static_cast<std::size_t>(i)] = digits[hash & 0xf];
            hash >>= 4;
        }
        return out;
    }

    const std::string& LanDiscovery::probeMessage()
    {
        static const std::string message{probeMagic};
        return message;
    }

    std::optional<std::string> LanDiscovery::encode(const DiscoveredGame& game)
    {
        if (game.session.empty() || game.session.size() > maxFieldLength) return std::nullopt;
        if (game.map.empty() || game.map.size() > maxFieldLength) return std::nullopt;
        if (game.port == 0 || game.port > 65535) return std::nullopt;
        if (game.slots.size() > maxSlots) return std::nullopt;
        if (game.hostName.size() > maxHostNameLength) return std::nullopt;
        if (!isValidPasswordHash(game.passwordHash)) return std::nullopt;
        if (game.options.size() > maxOptions) return std::nullopt;

        std::string out{magic};
        out += "\nsession=" + percentEncode(game.session);
        out += "\nmap=" + percentEncode(game.map);
        out += "\nport=" + std::to_string(game.port);
        if (!game.label.empty())
        {
            if (game.label.size() > maxFieldLength) return std::nullopt;
            out += "\nlabel=" + percentEncode(game.label);
        }
        if (!game.hostName.empty())
        {
            out += "\nhost=" + percentEncode(game.hostName);
        }
        if (!game.passwordHash.empty())
        {
            out += "\npw=" + game.passwordHash;
        }
        if (game.launching)
        {
            out += "\nlaunch=1";
        }

        for (const auto& slot : game.slots)
        {
            if (slot.index < 0 || slot.index >= static_cast<int>(maxSlots)) return std::nullopt;
            if (!knownController(slot.controller)) return std::nullopt;
            if (!knownSide(slot.side)) return std::nullopt;
            if (slot.colour > 255) return std::nullopt;
            if (slot.team < -1 || slot.team > 9) return std::nullopt;
            if (slot.name.size() > maxFieldLength || slot.address.size() > maxFieldLength) return std::nullopt;
            if (slot.personality.size() > maxPersonalityLength) return std::nullopt;

            out += "\nslot=" + std::to_string(slot.index);
            out += "," + slot.controller;
            out += "," + slot.side;
            out += "," + std::to_string(slot.colour);
            out += "," + std::to_string(slot.team);
            out += "," + percentEncode(slot.name);
            out += "," + percentEncode(slot.address);
        }

        // A seat's extras, only where they differ from what a decoder
        // assumes for a seat that says nothing: most seats are open or
        // plain, and a datagram is a small budget.
        for (const auto& slot : game.slots)
        {
            auto metal = clampResource(slot.metal);
            auto energy = clampResource(slot.energy);
            if (metal == 1000 && energy == 1000 && slot.personality.empty()) continue;

            out += "\nslotx=" + std::to_string(slot.index);
            out += "," + std::to_string(metal);
            out += "," + std::to_string(energy);
            out += "," + percentEncode(slot.personality);
        }

        // Options come last, because they are what gives way. Everything
        // above is needed to join the lobby at all; an option is something
        // a guest is told in addition. If the datagram would be too big the
        // trailing options are left off, one at a time, and only a lobby
        // that does not fit even with none is refused. Which options made it
        // is therefore the host's list from the front.
        if (out.size() > maxMessageSize) return std::nullopt;

        std::vector<std::string> seenNames;
        for (const auto& [name, value] : game.options)
        {
            if (!isValidOptionName(name)) return std::nullopt;
            if (value.size() > maxOptionValueLength) return std::nullopt;
            // The first of a repeated name is the one that counts, and is
            // the only one worth the bytes.
            if (std::find(seenNames.begin(), seenNames.end(), name) != seenNames.end()) continue;
            seenNames.push_back(name);

            auto line = "\n" + std::string(optionKeyPrefix) + name + "=" + percentEncode(value);
            if (out.size() + line.size() > maxMessageSize) break;
            out += line;
        }

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
        std::vector<SlotExtras> extras;

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
            else if (key == "host")
            {
                if (!percentDecode(value, game.hostName)) return std::nullopt;
                if (game.hostName.size() > maxHostNameLength) return std::nullopt;
            }
            else if (key == "pw")
            {
                // Not percent-decoded: it is hex or it is nothing, and a
                // hash that needs an escape is not one.
                if (!isValidPasswordHash(value)) return std::nullopt;
                game.passwordHash = value;
            }
            else if (key == "launch")
            {
                if (value == "1")
                {
                    game.launching = true;
                }
                else if (value == "0")
                {
                    game.launching = false;
                }
                else
                {
                    return std::nullopt;
                }
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
            else if (key == "slotx")
            {
                auto x = decodeSlotExtras(value);
                if (!x) return std::nullopt;
                if (std::any_of(extras.begin(), extras.end(), [&](const auto& e) { return e.index == x->index; }))
                {
                    return std::nullopt;
                }
                extras.push_back(*x);
            }
            else if (key.compare(0, optionKeyPrefixLength, optionKeyPrefix) == 0)
            {
                auto name = key.substr(optionKeyPrefixLength);
                if (!isValidOptionName(name)) return std::nullopt;

                std::string optionValue;
                if (!percentDecode(value, optionValue)) return std::nullopt;
                if (optionValue.size() > maxOptionValueLength) return std::nullopt;

                // A name given twice keeps the first, as the format says;
                // the repeat is neither an error nor a count against the
                // limit, since it adds nothing.
                auto existing = std::find_if(game.options.begin(), game.options.end(), [&](const auto& o) {
                    return o.first == name;
                });
                if (existing != game.options.end()) continue;

                if (game.options.size() >= maxOptions) return std::nullopt;
                game.options.emplace_back(std::move(name), std::move(optionValue));
            }
            else
            {
                // A field from a later version of this format. Ignoring it
                // is what lets an older build hear a newer announcement and
                // still make sense of it.
            }
        }

        // Extras for a seat the lobby does not list describe nothing, and
        // are ignored; they are not worth refusing the lobby over.
        for (const auto& x : extras)
        {
            auto it = std::find_if(game.slots.begin(), game.slots.end(), [&](const auto& s) {
                return s.index == x.index;
            });
            if (it == game.slots.end()) continue;
            it->metal = x.metal;
            it->energy = x.energy;
            it->personality = x.personality;
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
        socket6.reset();
        heard.clear();
        recentProbeReplies.clear();
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
            // Sending to 255.255.255.255 is refused without this, on
            // Windows and on Linux alike. Only loopback worked before it.
            s.set_option(asio::socket_base::broadcast(true));
            s.bind(endpoint);

            // The IPv6 side is for probes to and from an IPv6 address, and
            // nothing else needs it. A machine without IPv6, or one that
            // will not bind it, is not a machine that cannot discover, so
            // a failure here is noted and the IPv4 socket carries on.
            std::optional<asio::ip::udp::socket> s6;
            try
            {
                asio::ip::udp::socket t(ioContext);
                t.open(asio::ip::udp::v6());
                t.set_option(asio::ip::v6_only(true));
                t.set_option(asio::socket_base::reuse_address(true));
                t.bind(asio::ip::udp::endpoint(asio::ip::udp::v6(), port));
                s6 = std::move(t);
            }
            catch (const std::exception& e)
            {
                LOG_DEBUG << "LAN discovery has no IPv6 socket: " << e.what();
            }

            {
                std::scoped_lock<std::mutex> lock(mutex);
                socket = std::move(s);
                socket6 = std::move(s6);
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
            socket6.reset();
            this->bound = false;
            bound->set_value(false);
            return;
        }
    }

    void LanDiscovery::listen()
    {
        listenOn(false);
        listenOn(true);
    }

    void LanDiscovery::listenOn(bool v6)
    {
        std::scoped_lock<std::mutex> lock(mutex);
        auto& s = v6 ? socket6 : socket;
        if (!s) return;

        auto& buffer = v6 ? receiveBuffer6 : receiveBuffer;
        auto& sender = v6 ? currentSender6 : currentSender;

        s->async_receive_from(
            asio::buffer(buffer.data(), buffer.size()),
            sender,
            [this, v6](const asio::error_code& error, std::size_t bytes) {
                if (error)
                {
                    if (error == asio::error::operation_aborted)
                    {
                        return;
                    }

                    // These say something about one datagram, or about
                    // somebody else, and not about the socket. On Windows a
                    // datagram sent to a port nobody is listening on comes
                    // back as an ICMP error that is reported on the *next
                    // receive* as a connection reset, and a datagram bigger
                    // than the buffer as a message size error. Giving up on
                    // either would leave this deaf from then on, to every
                    // lobby, for the price of one hostile or unlucky packet
                    // -- which a probe to an address that is not running
                    // the game causes in the ordinary course of things.
                    if (error == asio::error::connection_reset
                        || error == asio::error::connection_refused
                        || error == asio::error::message_size
                        || error == asio::error::host_unreachable
                        || error == asio::error::network_unreachable)
                    {
                        listenOn(v6);
                        return;
                    }

                    LOG_DEBUG << "LAN discovery receive error: " << error.message();
                    return;
                }

                handleDatagram(v6, bytes);
                listenOn(v6);
            });
    }

    void LanDiscovery::handleDatagram(bool v6, std::size_t bytes)
    {
        if (bytes == 0 || bytes > maxMessageSize) return;

        const auto& buffer = v6 ? receiveBuffer6 : receiveBuffer;
        const auto& sender = v6 ? currentSender6 : currentSender;
        auto text = std::string(buffer.data(), bytes);

        // A probe is exactly its fixed text, with or without the newline.
        // Anything else is not one, and falls through to being read as an
        // announcement, which it will not be.
        if (text == probeMagic || text == std::string(probeMagic, std::strlen(probeMagic) - 1))
        {
            answerProbe(v6, sender);
            return;
        }

        auto game = decode(text);
        if (!game) return;

        std::scoped_lock<std::mutex> inner(mutex);
        game->sourceAddress = describeEndpoint(sender);
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

    void LanDiscovery::answerProbe(bool v6, const asio::ip::udp::endpoint& sender)
    {
        std::scoped_lock<std::mutex> lock(mutex);

        // A machine that is not advertising has nothing to say, and says it.
        if (!advertised) return;

        auto& s = v6 ? socket6 : socket;
        if (!s) return;

        // One reply per sender per probeReplyInterval. The reply is a
        // hundred times the size of the probe, and a probe's source address
        // is whatever the sender wrote there, so an unlimited answer is
        // somebody's amplifier. The key is the address alone, not the port,
        // so changing ports is no way round it, and the table is bounded: a
        // flood of different addresses is answered until it is full and then
        // not at all, which costs the flood's victims nothing real.
        auto now = getTimestamp();
        recentProbeReplies.erase(
            std::remove_if(recentProbeReplies.begin(), recentProbeReplies.end(), [&](const auto& r) {
                return now - r.second >= probeReplyInterval;
            }),
            recentProbeReplies.end());

        auto who = sender.address().to_string();
        if (std::any_of(recentProbeReplies.begin(), recentProbeReplies.end(), [&](const auto& r) {
                return r.first == who;
            }))
        {
            return;
        }
        if (recentProbeReplies.size() >= maxTrackedProbers) return;

        auto text = encode(*advertised);
        if (!text) return;

        recentProbeReplies.emplace_back(std::move(who), now);

        asio::error_code ec;
        s->send_to(asio::buffer(*text), sender, 0, ec);
        if (ec)
        {
            LOG_DEBUG << "LAN discovery could not answer a probe from " << describeEndpoint(sender)
                      << ": " << ec.message();
        }
    }

    bool LanDiscovery::probe(const std::string& hostAndPort)
    {
        auto target = parseProbeTarget(hostAndPort);
        if (!target) return false;

        auto address = target->address();
        auto port = target->port();

        // A v4-mapped address names an IPv4 machine, and the v6-only socket
        // cannot reach one. Hand it to the IPv4 socket as the plain address.
        if (address.is_v6() && address.to_v6().is_v4_mapped())
        {
            address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
        }
        asio::ip::udp::endpoint destination(address, port);

        std::scoped_lock<std::mutex> lock(mutex);
        auto& s = destination.address().is_v6() ? socket6 : socket;
        if (!s) return false;

        // Sent from the same socket that listens, so that the reply lands
        // on the one the receive loop is already serving. The socket and
        // the destination are the same family, which is what keeps this
        // clear of the Winsock fault EndpointFamily.h describes.
        asio::error_code ec;
        s->send_to(asio::buffer(probeMessage()), destination, 0, ec);
        if (ec)
        {
            LOG_DEBUG << "LAN discovery could not probe " << describeEndpoint(destination) << ": " << ec.message();
            return false;
        }
        return true;
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
        // No session is empty on the wire, so this excludes nothing.
        return games(std::string());
    }

    std::vector<DiscoveredGame> LanDiscovery::games(const std::string& excludeSession) const
    {
        std::scoped_lock<std::mutex> lock(mutex);
        auto cutoff = getTimestamp() - expiry;
        std::vector<DiscoveredGame> live;
        for (const auto& h : heard)
        {
            if (h.lastSeen >= cutoff && h.session != excludeSession)
            {
                live.push_back(h);
            }
        }
        return live;
    }
}
