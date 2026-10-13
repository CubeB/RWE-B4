#include "InviteCode.h"
#include <array>
#include <cstdint>

namespace rwe
{
    namespace
    {
        constexpr char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        constexpr std::uint8_t codeVersion = 1;
        constexpr std::size_t payloadBytes = 8;
        constexpr std::size_t codeCharacters = 13;

        /** CRC-8, polynomial 0x07: catches every one- and two-character slip. */
        std::uint8_t crc8(const std::uint8_t* data, std::size_t size)
        {
            std::uint8_t crc = 0;
            for (std::size_t i = 0; i < size; ++i)
            {
                crc ^= data[i];
                for (int bit = 0; bit < 8; ++bit)
                {
                    crc = static_cast<std::uint8_t>((crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1));
                }
            }
            return crc;
        }

        bool isSendable(const asio::ip::address_v4& address, unsigned short port)
        {
            return port != 0 && !address.is_unspecified() && !address.is_multicast();
        }

        /** The value of a character of the alphabet, or -1. Reading is forgiving of case and look-alikes. */
        int valueOf(char c)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - 'a' + 'A');
            }
            switch (c)
            {
                case 'O':
                    c = '0';
                    break;
                case 'I':
                case 'L':
                    c = '1';
                    break;
                default:
                    break;
            }

            for (int i = 0; i < 32; ++i)
            {
                if (alphabet[i] == c)
                {
                    return i;
                }
            }
            return -1;
        }

        bool isSeparator(char c)
        {
            return c == '-' || c == ' ' || c == '_' || c == '.' || c == '\t' || c == '\r' || c == '\n';
        }
    }

    std::optional<std::string> InviteCode::encode(const asio::ip::udp::endpoint& endpoint)
    {
        auto address = endpoint.address();
        if (address.is_v6() && address.to_v6().is_v4_mapped())
        {
            address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
        }
        if (!address.is_v4() || !isSendable(address.to_v4(), endpoint.port()))
        {
            return std::nullopt;
        }

        std::array<std::uint8_t, payloadBytes> bytes{};
        bytes[0] = static_cast<std::uint8_t>(codeVersion << 4);
        const auto octets = address.to_v4().to_bytes();
        for (std::size_t i = 0; i < 4; ++i)
        {
            bytes[1 + i] = octets[i];
        }
        bytes[5] = static_cast<std::uint8_t>(endpoint.port() >> 8);
        bytes[6] = static_cast<std::uint8_t>(endpoint.port() & 0xFF);
        bytes[7] = crc8(bytes.data(), 7);

        // 64 bits, five at a time from the top; the last character carries
        // four bits and a zero.
        std::string characters;
        std::uint32_t buffer = 0;
        int bits = 0;
        for (auto byte : bytes)
        {
            buffer = (buffer << 8) | byte;
            bits += 8;
            while (bits >= 5)
            {
                characters.push_back(alphabet[(buffer >> (bits - 5)) & 0x1F]);
                bits -= 5;
            }
        }
        if (bits > 0)
        {
            characters.push_back(alphabet[(buffer << (5 - bits)) & 0x1F]);
        }

        return "RWE-" + characters.substr(0, 4) + "-" + characters.substr(4, 4) + "-" + characters.substr(8);
    }

    std::optional<asio::ip::udp::endpoint> InviteCode::decode(const std::string& text)
    {
        if (text.size() > 64)
        {
            return std::nullopt;
        }

        std::string cleaned;
        for (char c : text)
        {
            if (isSeparator(c))
            {
                continue;
            }
            cleaned.push_back(c);
        }

        // The "RWE" that leads a written code. A bare thirteen characters is
        // accepted too, for a code somebody has trimmed.
        if (cleaned.size() == codeCharacters + 3)
        {
            const auto upper = [](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; };
            if (upper(cleaned[0]) != 'R' || upper(cleaned[1]) != 'W' || upper(cleaned[2]) != 'E')
            {
                return std::nullopt;
            }
            cleaned.erase(0, 3);
        }
        if (cleaned.size() != codeCharacters)
        {
            return std::nullopt;
        }

        std::array<std::uint8_t, payloadBytes> bytes{};
        std::uint32_t buffer = 0;
        int bits = 0;
        std::size_t out = 0;
        for (std::size_t i = 0; i < cleaned.size(); ++i)
        {
            const auto value = valueOf(cleaned[i]);
            if (value < 0)
            {
                return std::nullopt;
            }

            buffer = (buffer << 5) | static_cast<std::uint32_t>(value);
            bits += 5;
            if (bits >= 8 && out < payloadBytes)
            {
                bytes[out++] = static_cast<std::uint8_t>((buffer >> (bits - 8)) & 0xFF);
                bits -= 8;
            }
        }

        // 13 * 5 = 65 bits: eight bytes and one that is padding, and it is
        // zero. A one there is a character that was changed, or not a code.
        if (out != payloadBytes || bits != 1 || (buffer & 1) != 0)
        {
            return std::nullopt;
        }

        if ((bytes[0] >> 4) != codeVersion || crc8(bytes.data(), 7) != bytes[7])
        {
            return std::nullopt;
        }

        asio::ip::address_v4::bytes_type octets{bytes[1], bytes[2], bytes[3], bytes[4]};
        const asio::ip::address_v4 address(octets);
        const auto port = static_cast<unsigned short>((bytes[5] << 8) | bytes[6]);
        if (!isSendable(address, port))
        {
            return std::nullopt;
        }
        return asio::ip::udp::endpoint(address, port);
    }

    bool InviteCode::isInviteCode(const std::string& text)
    {
        return decode(text).has_value();
    }

    std::string InviteCode::toAddressString(const asio::ip::udp::endpoint& endpoint)
    {
        auto address = endpoint.address();
        if (address.is_v6() && address.to_v6().is_v4_mapped())
        {
            address = asio::ip::make_address_v4(asio::ip::v4_mapped, address.to_v6());
        }
        if (address.is_v6())
        {
            return "[" + address.to_string() + "]:" + std::to_string(endpoint.port());
        }
        return address.to_string() + ":" + std::to_string(endpoint.port());
    }
}
