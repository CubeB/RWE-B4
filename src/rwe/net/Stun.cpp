#include "Stun.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <random>
#include <rwe/util/SimpleLogger.h>

namespace rwe::stun
{
    namespace
    {
        constexpr std::uint32_t magicCookie = 0x2112A442;
        constexpr std::uint16_t bindingRequest = 0x0001;
        constexpr std::uint16_t bindingSuccess = 0x0101;
        constexpr std::uint16_t attrMappedAddress = 0x0001;
        constexpr std::uint16_t attrXorMappedAddress = 0x0020;
        constexpr std::size_t headerSize = 20;

        std::uint16_t readU16(const unsigned char* p)
        {
            return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
        }

        std::uint32_t readU32(const unsigned char* p)
        {
            return (static_cast<std::uint32_t>(p[0]) << 24)
                | (static_cast<std::uint32_t>(p[1]) << 16)
                | (static_cast<std::uint32_t>(p[2]) << 8)
                | static_cast<std::uint32_t>(p[3]);
        }

        void putU16(std::string& out, std::uint16_t v)
        {
            out.push_back(static_cast<char>(v >> 8));
            out.push_back(static_cast<char>(v & 0xFF));
        }

        void putU32(std::string& out, std::uint32_t v)
        {
            out.push_back(static_cast<char>(v >> 24));
            out.push_back(static_cast<char>((v >> 16) & 0xFF));
            out.push_back(static_cast<char>((v >> 8) & 0xFF));
            out.push_back(static_cast<char>(v & 0xFF));
        }

        /** An address that something outside can be sent to. */
        bool isSendable(const asio::ip::address_v4& address, unsigned short port)
        {
            return port != 0 && !address.is_unspecified() && !address.is_multicast();
        }

        /**
         * The IPv4 address in a MAPPED-ADDRESS or XOR-MAPPED-ADDRESS value,
         * or nothing for a family this does not use or a value that is not
         * the size of one. `xorWithCookie` undoes the XOR of RFC 5389 §15.2.
         */
        std::optional<asio::ip::udp::endpoint> readAddress(const unsigned char* value, std::size_t length, bool xorWithCookie)
        {
            // reserved(1) family(1) port(2) address(4)
            if (length != 8 || value[1] != 0x01)
            {
                return std::nullopt;
            }

            auto port = readU16(value + 2);
            auto raw = readU32(value + 4);
            if (xorWithCookie)
            {
                port ^= static_cast<std::uint16_t>(magicCookie >> 16);
                raw ^= magicCookie;
            }

            asio::ip::address_v4 address(raw);
            if (!isSendable(address, port))
            {
                return std::nullopt;
            }
            return asio::ip::udp::endpoint(address, port);
        }

        /** The same machine, however the endpoint was spelled. */
        bool sameEndpoint(const asio::ip::udp::endpoint& a, const asio::ip::udp::endpoint& b)
        {
            const auto plain = [](const asio::ip::udp::endpoint& e) {
                auto address = e.address();
                if (address.is_v6() && address.to_v6().is_v4_mapped())
                {
                    address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
                }
                return asio::ip::udp::endpoint(address, e.port());
            };
            return plain(a) == plain(b);
        }
    }

    const std::vector<Server>& defaultServers()
    {
        // Operated by Google, Cloudflare and sipgate. Two of the first are
        // here so that one provider being unreachable from a network still
        // leaves a second answer to compare the first with.
        static const std::vector<Server> servers{
            {"stun.l.google.com", 19302},
            {"stun1.l.google.com", 19302},
            {"stun.cloudflare.com", 3478},
            {"stun.sipgate.net", 3478},
        };
        return servers;
    }

    TransactionId randomTransactionId()
    {
        // Nothing here is secret -- the id is how a response is matched to
        // its request and keeps an off-path datagram from being taken for
        // one -- so a seeded generator is enough. One per thread, so that
        // two threads never share state.
        thread_local std::mt19937_64 generator{std::random_device{}()};

        TransactionId id{};
        for (std::size_t i = 0; i < id.size(); i += 4)
        {
            auto word = static_cast<std::uint32_t>(generator());
            for (std::size_t b = 0; b < 4; ++b)
            {
                id[i + b] = static_cast<std::uint8_t>((word >> (8 * b)) & 0xFF);
            }
        }
        return id;
    }

    std::string makeBindingRequest(const TransactionId& id)
    {
        std::string out;
        out.reserve(headerSize);
        putU16(out, bindingRequest);
        putU16(out, 0);
        putU32(out, magicCookie);
        out.append(reinterpret_cast<const char*>(id.data()), id.size());
        return out;
    }

    bool looksLikeStun(const char* data, std::size_t size)
    {
        if (size < headerSize || (size % 4) != 0)
        {
            return false;
        }
        const auto* p = reinterpret_cast<const unsigned char*>(data);
        if ((p[0] & 0xC0) != 0)
        {
            return false;
        }
        if (readU32(p + 4) != magicCookie)
        {
            return false;
        }
        return static_cast<std::size_t>(readU16(p + 2)) == size - headerSize;
    }

    std::optional<BindingResponse> parseBindingResponse(const char* data, std::size_t size)
    {
        if (!looksLikeStun(data, size))
        {
            return std::nullopt;
        }

        const auto* p = reinterpret_cast<const unsigned char*>(data);
        if (readU16(p) != bindingSuccess)
        {
            return std::nullopt;
        }

        BindingResponse response;
        std::copy(p + 8, p + 20, response.id.begin());

        std::optional<asio::ip::udp::endpoint> plain;
        std::optional<asio::ip::udp::endpoint> xored;

        std::size_t pos = headerSize;
        while (pos + 4 <= size)
        {
            const auto type = readU16(p + pos);
            const std::size_t length = readU16(p + pos + 2);
            pos += 4;
            if (length > size - pos)
            {
                // An attribute that runs off the end of the message.
                return std::nullopt;
            }

            if (type == attrXorMappedAddress && !xored)
            {
                xored = readAddress(p + pos, length, true);
            }
            else if (type == attrMappedAddress && !plain)
            {
                plain = readAddress(p + pos, length, false);
            }

            // Attributes are padded to a multiple of four.
            const std::size_t padded = (length + 3) & ~static_cast<std::size_t>(3);
            if (padded > size - pos)
            {
                break;
            }
            pos += padded;
        }

        if (xored)
        {
            response.mapped = *xored;
        }
        else if (plain)
        {
            response.mapped = *plain;
        }
        else
        {
            return std::nullopt;
        }
        return response;
    }

    std::vector<asio::ip::udp::endpoint> resolveServers(const std::vector<Server>& servers)
    {
        std::vector<asio::ip::udp::endpoint> found;
        asio::io_context context;
        asio::ip::udp::resolver resolver(context);

        for (const auto& server : servers)
        {
            asio::error_code ec;
            auto results = resolver.resolve(asio::ip::udp::v4(), server.host, std::to_string(server.port), ec);
            if (ec || results.empty())
            {
                LOG_DEBUG << "STUN server " << server.host << " did not resolve: " << ec.message();
                continue;
            }

            const auto endpoint = results.begin()->endpoint();
            if (std::find(found.begin(), found.end(), endpoint) == found.end())
            {
                found.push_back(endpoint);
            }
        }
        return found;
    }

    ReflexiveProbe::ReflexiveProbe(Send send) : send(std::move(send))
    {
    }

    void ReflexiveProbe::begin(std::vector<asio::ip::udp::endpoint> servers, Timestamp now)
    {
        queries.clear();
        for (const auto& server : servers)
        {
            Query q;
            q.server = server;
            q.id = randomTransactionId();
            queries.push_back(std::move(q));
        }

        startedAt = now;
        lastSentAt = now;
        firstAnswerAt = {};
        currentState = queries.empty() ? State::Failed : State::Pending;
        sendUnanswered();
    }

    void ReflexiveProbe::sendUnanswered()
    {
        if (!send)
        {
            return;
        }
        for (const auto& q : queries)
        {
            if (!q.answer)
            {
                send(q.server, makeBindingRequest(q.id));
            }
        }
    }

    bool ReflexiveProbe::onDatagram(const char* data, std::size_t size, const asio::ip::udp::endpoint& from, Timestamp now)
    {
        if (currentState != State::Pending && currentState != State::Done)
        {
            return false;
        }

        auto response = parseBindingResponse(data, size);
        if (!response)
        {
            return false;
        }

        // Matched on the transaction id and on where it came from: an id is
        // sixty-odd bits nobody off the path has seen, and the address check
        // is what stops one server's answer being counted for another.
        auto query = std::find_if(queries.begin(), queries.end(), [&](const Query& q) {
            return q.id == response->id && sameEndpoint(q.server, from);
        });
        if (query == queries.end())
        {
            return false;
        }

        if (!query->answer)
        {
            query->answer = response->mapped;
            if (answerCount() == 1)
            {
                firstAnswerAt = now;
            }
        }
        settleIfDue(now);
        return true;
    }

    void ReflexiveProbe::tick(Timestamp now)
    {
        if (currentState != State::Pending)
        {
            return;
        }

        settleIfDue(now);
        if (currentState != State::Pending)
        {
            return;
        }

        if (now - startedAt >= timeout)
        {
            currentState = answerCount() == 0 ? State::Failed : State::Done;
            return;
        }

        if (now - lastSentAt >= retryInterval)
        {
            lastSentAt = now;
            sendUnanswered();
        }
    }

    void ReflexiveProbe::settleIfDue(Timestamp now)
    {
        if (currentState != State::Pending)
        {
            return;
        }

        const auto answers = answerCount();
        if (answers == 0)
        {
            return;
        }
        if (answers == queries.size() || now - firstAnswerAt >= settleTime)
        {
            currentState = State::Done;
        }
    }

    std::size_t ReflexiveProbe::answerCount() const
    {
        return static_cast<std::size_t>(std::count_if(queries.begin(), queries.end(), [](const Query& q) { return q.answer.has_value(); }));
    }

    std::optional<asio::ip::udp::endpoint> ReflexiveProbe::mapped() const
    {
        // The address the most servers agreed on; ties go to the one heard
        // first in the list, which is the one asked first.
        std::map<asio::ip::udp::endpoint, int> votes;
        for (const auto& q : queries)
        {
            if (q.answer)
            {
                ++votes[*q.answer];
            }
        }
        if (votes.empty())
        {
            return std::nullopt;
        }

        std::optional<asio::ip::udp::endpoint> best;
        int bestVotes = 0;
        for (const auto& q : queries)
        {
            if (q.answer && votes[*q.answer] > bestVotes)
            {
                best = q.answer;
                bestVotes = votes[*q.answer];
            }
        }
        return best;
    }

    ReflexiveProbe::Mapping ReflexiveProbe::mapping() const
    {
        std::optional<asio::ip::udp::endpoint> first;
        std::size_t answers = 0;
        for (const auto& q : queries)
        {
            if (!q.answer)
            {
                continue;
            }
            ++answers;
            if (!first)
            {
                first = q.answer;
            }
            else if (*first != *q.answer)
            {
                return Mapping::EndpointDependent;
            }
        }
        return answers >= 2 ? Mapping::EndpointIndependent : Mapping::Unknown;
    }
}
