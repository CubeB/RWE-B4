#include <catch2/catch_test_macros.hpp>
#include <rwe/net/InviteCode.h>

namespace rwe
{
    namespace
    {
        asio::ip::udp::endpoint ep(const char* ip, unsigned short port)
        {
            return asio::ip::udp::endpoint(asio::ip::make_address_v4(ip), port);
        }
    }

    TEST_CASE("an invite code is an address a person can carry")
    {
        SECTION("an address survives a round trip")
        {
            for (const auto& e : {ep("203.0.113.5", 40001), ep("100.109.106.220", 33780), ep("1.2.3.4", 1), ep("255.255.255.254", 65535)})
            {
                auto code = InviteCode::encode(e);
                REQUIRE(code);
                auto back = InviteCode::decode(*code);
                REQUIRE(back);
                CHECK(*back == e);
            }
        }

        SECTION("it is written in groups, behind RWE, from the alphabet and nothing else")
        {
            auto code = InviteCode::encode(ep("203.0.113.5", 40001));
            REQUIRE(code);
            CHECK(code->size() == 19);
            CHECK(code->substr(0, 4) == "RWE-");
            CHECK((*code)[8] == '-');
            CHECK((*code)[13] == '-');
            for (char c : code->substr(4))
            {
                if (c != '-')
                {
                    CHECK(std::string("0123456789ABCDEFGHJKMNPQRSTVWXYZ").find(c) != std::string::npos);
                }
            }
        }

        SECTION("different addresses are different codes")
        {
            CHECK(*InviteCode::encode(ep("203.0.113.5", 40001)) != *InviteCode::encode(ep("203.0.113.5", 40002)));
            CHECK(*InviteCode::encode(ep("203.0.113.5", 40001)) != *InviteCode::encode(ep("203.0.113.6", 40001)));
        }

        SECTION("a v4-mapped address is the same address")
        {
            auto mapped = asio::ip::udp::endpoint(asio::ip::make_address("::ffff:203.0.113.5"), 40001);
            REQUIRE(InviteCode::encode(mapped));
            CHECK(*InviteCode::encode(mapped) == *InviteCode::encode(ep("203.0.113.5", 40001)));
        }

        SECTION("an address nobody can send to is not given a code")
        {
            CHECK_FALSE(InviteCode::encode(ep("0.0.0.0", 40001)));
            CHECK_FALSE(InviteCode::encode(ep("203.0.113.5", 0)));
            CHECK_FALSE(InviteCode::encode(ep("224.0.0.1", 40001)));
            CHECK_FALSE(InviteCode::encode(asio::ip::udp::endpoint(asio::ip::make_address("2001:db8::1"), 40001)));
        }

        SECTION("reading forgives case, spacing and look-alikes")
        {
            auto code = *InviteCode::encode(ep("203.0.113.5", 40001));

            auto lower = code;
            for (auto& c : lower)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            CHECK(InviteCode::decode(lower) == ep("203.0.113.5", 40001));

            auto spaced = code;
            for (auto& c : spaced)
            {
                if (c == '-')
                {
                    c = ' ';
                }
            }
            CHECK(InviteCode::decode("  " + spaced + "\r\n") == ep("203.0.113.5", 40001));

            // trimmed to the bare characters
            auto bare = code.substr(4);
            CHECK(InviteCode::decode(bare) == ep("203.0.113.5", 40001));

            // the letters that look like digits are read as digits
            std::string lookAlike = code;
            for (auto& c : lookAlike)
            {
                if (c == '0')
                {
                    c = 'O';
                }
                else if (c == '1')
                {
                    c = 'l';
                }
            }
            CHECK(InviteCode::decode(lookAlike) == ep("203.0.113.5", 40001));
        }

        SECTION("a code that has been mistyped does not decode")
        {
            // The check is there to turn a slip into "that is not a code"
            // rather than into a game that times out on the wrong address.
            // Every single-character change of a valid code is refused.
            const auto code = *InviteCode::encode(ep("203.0.113.5", 40001));
            const std::string alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
            int accepted = 0;
            for (std::size_t i = 4; i < code.size(); ++i)
            {
                if (code[i] == '-')
                {
                    continue;
                }
                for (char replacement : alphabet)
                {
                    if (replacement == code[i])
                    {
                        continue;
                    }
                    auto mistyped = code;
                    mistyped[i] = replacement;
                    if (InviteCode::decode(mistyped))
                    {
                        ++accepted;
                    }
                }
            }
            CHECK(accepted == 0);
        }

        SECTION("what is not a code is not read as one")
        {
            CHECK_FALSE(InviteCode::decode(""));
            CHECK_FALSE(InviteCode::decode("192.168.1.1"));
            CHECK_FALSE(InviteCode::decode("192.168.1.1:1338"));
            CHECK_FALSE(InviteCode::decode("my-computer"));
            CHECK_FALSE(InviteCode::decode("RWE-ABCD-EFGH-JKMNP"));
            CHECK_FALSE(InviteCode::decode("RWE-0000-0000-00000"));
            CHECK_FALSE(InviteCode::decode(std::string(500, 'A')));
            // The right length but a character outside the alphabet.
            CHECK_FALSE(InviteCode::decode("RWE-ABCU-EFGH-JKMNP"));
            CHECK_FALSE(InviteCode::isInviteCode("hello"));
            CHECK(InviteCode::isInviteCode(*InviteCode::encode(ep("203.0.113.5", 40001))));
        }

        SECTION("an address is written the way the lobby writes one")
        {
            CHECK(InviteCode::toAddressString(ep("203.0.113.5", 40001)) == "203.0.113.5:40001");
            auto mapped = asio::ip::udp::endpoint(asio::ip::make_address("::ffff:203.0.113.5"), 40001);
            CHECK(InviteCode::toAddressString(mapped) == "203.0.113.5:40001");
        }
    }
}
