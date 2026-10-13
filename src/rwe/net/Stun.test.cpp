#include <catch2/catch_test_macros.hpp>
#include <rwe/net/Stun.h>

namespace rwe
{
    namespace
    {
        using asio::ip::udp;

        void putU16(std::string& s, unsigned v)
        {
            s.push_back(static_cast<char>((v >> 8) & 0xFF));
            s.push_back(static_cast<char>(v & 0xFF));
        }

        std::string attribute(unsigned type, const std::string& value)
        {
            std::string a;
            putU16(a, type);
            putU16(a, static_cast<unsigned>(value.size()));
            a += value;
            while (a.size() % 4 != 0)
            {
                a.push_back('\0');
            }
            return a;
        }

        /** An address attribute value: reserved, family, port, address -- XOR'd or not. */
        std::string addressValue(const udp::endpoint& e, bool xorWithCookie, unsigned char family = 1)
        {
            std::string v;
            v.push_back('\0');
            v.push_back(static_cast<char>(family));
            unsigned port = e.port();
            auto bytes = e.address().to_v4().to_bytes();
            std::uint32_t addr = (static_cast<std::uint32_t>(bytes[0]) << 24) | (static_cast<std::uint32_t>(bytes[1]) << 16)
                | (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
            if (xorWithCookie)
            {
                port ^= 0x2112;
                addr ^= 0x2112A442;
            }
            putU16(v, port);
            v.push_back(static_cast<char>(addr >> 24));
            v.push_back(static_cast<char>((addr >> 16) & 0xFF));
            v.push_back(static_cast<char>((addr >> 8) & 0xFF));
            v.push_back(static_cast<char>(addr & 0xFF));
            return v;
        }

        std::string message(unsigned type, const stun::TransactionId& id, const std::string& attributes)
        {
            std::string m;
            putU16(m, type);
            putU16(m, static_cast<unsigned>(attributes.size()));
            m += std::string("\x21\x12\xA4\x42", 4);
            m.append(reinterpret_cast<const char*>(id.data()), id.size());
            m += attributes;
            return m;
        }

        stun::TransactionId someId()
        {
            return stun::TransactionId{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        }

        udp::endpoint ep(const char* ip, unsigned short port)
        {
            return udp::endpoint(asio::ip::make_address_v4(ip), port);
        }

        std::string response(const stun::TransactionId& id, const udp::endpoint& mapped)
        {
            return message(0x0101, id, attribute(0x0020, addressValue(mapped, true)));
        }

        const Timestamp t0 = Timestamp{} + std::chrono::seconds(1000);
    }

    TEST_CASE("STUN requests and responses")
    {
        SECTION("a binding request is the 20 byte header and nothing else")
        {
            auto request = stun::makeBindingRequest(someId());
            REQUIRE(request.size() == 20);
            CHECK(static_cast<unsigned char>(request[0]) == 0x00);
            CHECK(static_cast<unsigned char>(request[1]) == 0x01);
            CHECK(static_cast<unsigned char>(request[2]) == 0x00);
            CHECK(static_cast<unsigned char>(request[3]) == 0x00);
            CHECK(request.substr(4, 4) == std::string("\x21\x12\xA4\x42", 4));
            CHECK(request.substr(8, 12) == std::string("\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C", 12));
            CHECK(stun::looksLikeStun(request.data(), request.size()));
        }

        SECTION("transaction ids are not repeated")
        {
            CHECK(stun::randomTransactionId() != stun::randomTransactionId());
        }

        SECTION("an XOR-MAPPED-ADDRESS is undone")
        {
            // RFC 5769 section 2.2's example address, 192.0.2.1:32853, in
            // the bytes a server writes for it.
            auto bytes = message(0x0101, someId(), attribute(0x0020, std::string("\x00\x01\xA1\x47\xE1\x12\xA6\x43", 8)));
            auto parsed = stun::parseBindingResponse(bytes.data(), bytes.size());
            REQUIRE(parsed);
            CHECK(parsed->mapped == ep("192.0.2.1", 32853));
            CHECK(parsed->id == someId());
        }

        SECTION("an old server's plain MAPPED-ADDRESS is read when it is all there is")
        {
            auto bytes = message(0x0101, someId(), attribute(0x0001, addressValue(ep("203.0.113.9", 4000), false)));
            auto parsed = stun::parseBindingResponse(bytes.data(), bytes.size());
            REQUIRE(parsed);
            CHECK(parsed->mapped == ep("203.0.113.9", 4000));
        }

        SECTION("the XOR form wins when both are there")
        {
            // A router that rewrites addresses it finds in the payload
            // damages the plain one; that is what the XOR form is for.
            auto bytes = message(0x0101, someId(),
                attribute(0x0001, addressValue(ep("10.0.0.1", 1111), false))
                    + attribute(0x0020, addressValue(ep("203.0.113.9", 4000), true)));
            auto parsed = stun::parseBindingResponse(bytes.data(), bytes.size());
            REQUIRE(parsed);
            CHECK(parsed->mapped == ep("203.0.113.9", 4000));
        }

        SECTION("unknown attributes are stepped over, padding and all")
        {
            auto bytes = message(0x0101, someId(),
                attribute(0x8022, "RWE") // SOFTWARE, three bytes, one of padding
                    + attribute(0x0020, addressValue(ep("198.51.100.7", 12345), true)));
            auto parsed = stun::parseBindingResponse(bytes.data(), bytes.size());
            REQUIRE(parsed);
            CHECK(parsed->mapped == ep("198.51.100.7", 12345));
        }

        SECTION("what is not a success response with an address is refused")
        {
            auto good = response(someId(), ep("203.0.113.9", 4000));
            REQUIRE(stun::parseBindingResponse(good.data(), good.size()));

            // too short, and an empty one
            CHECK_FALSE(stun::parseBindingResponse(good.data(), 19));
            CHECK_FALSE(stun::parseBindingResponse(good.data(), 0));

            // the cookie is wrong
            auto badCookie = good;
            badCookie[4] = '\x00';
            CHECK_FALSE(stun::parseBindingResponse(badCookie.data(), badCookie.size()));

            // the length field says more than there is, and less
            auto tooLong = good;
            tooLong[3] = static_cast<char>(tooLong[3] + 4);
            CHECK_FALSE(stun::parseBindingResponse(tooLong.data(), tooLong.size()));
            auto tooShort = good;
            tooShort[3] = static_cast<char>(tooShort[3] - 4);
            CHECK_FALSE(stun::parseBindingResponse(tooShort.data(), tooShort.size()));

            // a request, and an error response
            auto request = message(0x0001, someId(), attribute(0x0020, addressValue(ep("203.0.113.9", 4000), true)));
            CHECK_FALSE(stun::parseBindingResponse(request.data(), request.size()));
            auto error = message(0x0111, someId(), attribute(0x0020, addressValue(ep("203.0.113.9", 4000), true)));
            CHECK_FALSE(stun::parseBindingResponse(error.data(), error.size()));

            // a success with no address in it
            auto nothing = message(0x0101, someId(), attribute(0x8022, "RWE!"));
            CHECK_FALSE(stun::parseBindingResponse(nothing.data(), nothing.size()));

            // an IPv6 family, which this does not use
            auto v6 = message(0x0101, someId(), attribute(0x0020, addressValue(ep("203.0.113.9", 4000), true, 2)));
            CHECK_FALSE(stun::parseBindingResponse(v6.data(), v6.size()));

            // an address that cannot be sent to
            auto port0 = message(0x0101, someId(), attribute(0x0020, addressValue(ep("203.0.113.9", 0), true)));
            CHECK_FALSE(stun::parseBindingResponse(port0.data(), port0.size()));
            auto zero = message(0x0101, someId(), attribute(0x0020, addressValue(ep("0.0.0.0", 4000), true)));
            CHECK_FALSE(stun::parseBindingResponse(zero.data(), zero.size()));
        }

        SECTION("an attribute that runs off the end is refused rather than read")
        {
            std::string a;
            putU16(a, 0x0020);
            putU16(a, 200); // claims 200 bytes
            a += std::string(8, '\0');
            auto bytes = message(0x0101, someId(), a);
            CHECK_FALSE(stun::parseBindingResponse(bytes.data(), bytes.size()));
        }

        SECTION("no prefix of a response, and no byte flipped in one, is more than a refusal")
        {
            // The parser reads what a stranger sent. It may accept or refuse
            // any of these; it must not read outside them. Run under a
            // sanitizer this is the check; here it is that nothing throws.
            auto good = response(someId(), ep("203.0.113.9", 4000));
            for (std::size_t n = 0; n <= good.size(); ++n)
            {
                (void)stun::parseBindingResponse(good.data(), n);
            }
            for (std::size_t i = 0; i < good.size(); ++i)
            {
                for (int bit = 0; bit < 8; ++bit)
                {
                    auto copy = good;
                    copy[i] = static_cast<char>(copy[i] ^ (1 << bit));
                    (void)stun::parseBindingResponse(copy.data(), copy.size());
                }
            }
            SUCCEED();
        }
    }

    TEST_CASE("a reflexive probe finds an address and says how much to trust it")
    {
        std::vector<std::pair<udp::endpoint, std::string>> sent;
        stun::ReflexiveProbe probe([&](const udp::endpoint& to, const std::string& data) {
            sent.emplace_back(to, data);
            return true;
        });

        const auto serverA = ep("198.51.100.1", 3478);
        const auto serverB = ep("198.51.100.2", 3478);

        const auto idOf = [&](std::size_t i) {
            stun::TransactionId id{};
            std::copy(sent[i].second.begin() + 8, sent[i].second.begin() + 20, id.begin());
            return id;
        };

        SECTION("every server is asked at once, from the beginning")
        {
            probe.begin({serverA, serverB}, t0);
            REQUIRE(sent.size() == 2);
            CHECK(sent[0].first == serverA);
            CHECK(sent[1].first == serverB);
            CHECK(idOf(0) != idOf(1));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Pending);
        }

        SECTION("one answer is an address, not yet a verdict on the router")
        {
            probe.begin({serverA, serverB}, t0);
            REQUIRE(probe.onDatagram(response(idOf(0), ep("203.0.113.5", 40001)).data(), 32, serverA, t0));
            // Settles after a moment, or when everybody has answered.
            CHECK(probe.state() == stun::ReflexiveProbe::State::Pending);
            probe.tick(t0 + std::chrono::milliseconds(100));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Pending);
            probe.tick(t0 + stun::ReflexiveProbe::settleTime + std::chrono::milliseconds(1));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Done);
            REQUIRE(probe.mapped());
            CHECK(*probe.mapped() == ep("203.0.113.5", 40001));
            CHECK(probe.mapping() == stun::ReflexiveProbe::Mapping::Unknown);
        }

        SECTION("two servers that saw the same port: the router can be punched")
        {
            probe.begin({serverA, serverB}, t0);
            REQUIRE(probe.onDatagram(response(idOf(0), ep("203.0.113.5", 40001)).data(), 32, serverA, t0));
            REQUIRE(probe.onDatagram(response(idOf(1), ep("203.0.113.5", 40001)).data(), 32, serverB, t0));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Done);
            CHECK(probe.mapping() == stun::ReflexiveProbe::Mapping::EndpointIndependent);
        }

        SECTION("two servers that saw different ports: the router cannot")
        {
            probe.begin({serverA, serverB}, t0);
            REQUIRE(probe.onDatagram(response(idOf(0), ep("203.0.113.5", 40001)).data(), 32, serverA, t0));
            REQUIRE(probe.onDatagram(response(idOf(1), ep("203.0.113.5", 40002)).data(), 32, serverB, t0));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Done);
            CHECK(probe.mapping() == stun::ReflexiveProbe::Mapping::EndpointDependent);
        }

        SECTION("an answer is taken only from the server asked, with the id it was sent")
        {
            probe.begin({serverA}, t0);
            const auto good = response(idOf(0), ep("203.0.113.5", 40001));

            // right id, wrong sender
            CHECK_FALSE(probe.onDatagram(good.data(), good.size(), ep("198.51.100.99", 3478), t0));
            // right sender, wrong id
            const auto other = response(someId(), ep("203.0.113.5", 40001));
            CHECK_FALSE(probe.onDatagram(other.data(), other.size(), serverA, t0));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Pending);
            CHECK_FALSE(probe.mapped());

            CHECK(probe.onDatagram(good.data(), good.size(), serverA, t0));
        }

        SECTION("an unanswered request is repeated, and given up on")
        {
            probe.begin({serverA}, t0);
            REQUIRE(sent.size() == 1);

            probe.tick(t0 + std::chrono::milliseconds(100));
            CHECK(sent.size() == 1);
            probe.tick(t0 + stun::ReflexiveProbe::retryInterval + std::chrono::milliseconds(1));
            CHECK(sent.size() == 2);
            // The same id, so that a late first answer still counts.
            CHECK(idOf(0) == idOf(1));

            probe.tick(t0 + stun::ReflexiveProbe::timeout + std::chrono::milliseconds(1));
            CHECK(probe.state() == stun::ReflexiveProbe::State::Failed);
        }

        SECTION("a server that has answered is not asked again")
        {
            probe.begin({serverA, serverB}, t0);
            REQUIRE(probe.onDatagram(response(idOf(0), ep("203.0.113.5", 40001)).data(), 32, serverA, t0));
            sent.clear();
            probe.tick(t0 + stun::ReflexiveProbe::retryInterval + std::chrono::milliseconds(1));
            REQUIRE(sent.size() == 1);
            CHECK(sent[0].first == serverB);
        }

        SECTION("no servers is a failure at once")
        {
            probe.begin({}, t0);
            CHECK(probe.state() == stun::ReflexiveProbe::State::Failed);
        }
    }
}
