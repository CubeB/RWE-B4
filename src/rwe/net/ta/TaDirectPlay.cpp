#include "TaDirectPlay.h"
#include <algorithm>
#include <cstring>

namespace rwe
{
    namespace
    {
        std::uint16_t readU16Le(const std::uint8_t* data, std::size_t off)
        {
            return static_cast<std::uint16_t>(data[off] | (data[off + 1] << 8));
        }

        std::uint16_t readU16Be(const std::uint8_t* data, std::size_t off)
        {
            return static_cast<std::uint16_t>((data[off] << 8) | data[off + 1]);
        }

        std::uint32_t readU32Le(const std::uint8_t* data, std::size_t off)
        {
            return static_cast<std::uint32_t>(data[off])
                | (static_cast<std::uint32_t>(data[off + 1]) << 8)
                | (static_cast<std::uint32_t>(data[off + 2]) << 16)
                | (static_cast<std::uint32_t>(data[off + 3]) << 24);
        }

        void requireWithin(std::size_t len, std::size_t off, std::size_t size, const char* what)
        {
            if (off > len || size > len - off)
            {
                throw TaDirectPlayException(std::string("DirectPlay message is too short for ") + what);
            }
        }

        void writeU16Le(std::vector<std::uint8_t>& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value & 0xFF));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        void writeU16Be(std::vector<std::uint8_t>& out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value >> 8));
            out.push_back(static_cast<std::uint8_t>(value & 0xFF));
        }

        void writeU32Le(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value & 0xFF));
            out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
            out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
            out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
        }

        void writeAddress(std::vector<std::uint8_t>& out, const TaDpAddress& address)
        {
            writeU16Le(out, address.family);
            writeU16Be(out, address.port);
            out.insert(out.end(), address.ip.begin(), address.ip.end());
            out.insert(out.end(), address.padding.begin(), address.padding.end());
        }

        TaDpAddress readAddress(const std::uint8_t* data, std::size_t len, std::size_t off, const char* what)
        {
            requireWithin(len, off, 16, what);
            TaDpAddress address;
            address.family = readU16Le(data, off);
            address.port = readU16Be(data, off + 2);
            std::copy(data + off + 4, data + off + 8, address.ip.begin());
            std::copy(data + off + 8, data + off + 16, address.padding.begin());
            return address;
        }

        /**
         * The null-terminated UTF-16LE string beginning at `off`, and the offset
         * just past its terminator. The terminator must be there; without it the
         * caller cannot know where the next field begins.
         */
        std::pair<std::string, std::size_t> readUtf16Z(const std::uint8_t* data, std::size_t len, std::size_t off, const char* what)
        {
            std::size_t end = off;
            while (end + 1 < len && !(data[end] == 0 && data[end + 1] == 0))
            {
                end += 2;
            }

            if (end + 1 >= len)
            {
                throw TaDirectPlayException(std::string("DirectPlay message has an unterminated ") + what);
            }

            return {taUtf16LeToUtf8(data + off, end - off), end + 2};
        }

        std::vector<std::uint8_t> utf16Z(const std::string& text)
        {
            auto bytes = taUtf8ToUtf16Le(text);
            bytes.push_back(0);
            bytes.push_back(0);
            return bytes;
        }

        void append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes)
        {
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
    }

    TaDirectPlayException::TaDirectPlayException(const std::string& message)
        : std::runtime_error(message)
    {
    }

    bool taIsDirectPlayMessage(const std::uint8_t* data, std::size_t len)
    {
        return len >= 24 && data[20] == 'p' && data[21] == 'l' && data[22] == 'a' && data[23] == 'y';
    }

    std::uint32_t taPeekDirectPlayMessageSize(const std::uint8_t* data, std::size_t len)
    {
        if (len < 4)
        {
            return 0;
        }

        auto word = readU32Le(data, 0);
        if ((word >> 20) != TaDirectPlayToken)
        {
            return 0;
        }

        return word & 0x000FFFFFu;
    }

    TaDpAddress taMakeReplyAddress(std::uint16_t port, std::array<std::uint8_t, 4> ip)
    {
        TaDpAddress address;
        address.port = port;
        address.ip = ip;
        return address;
    }

    TaDpMessage taDecodeDirectPlayMessage(const std::uint8_t* data, std::size_t len)
    {
        if (len < 28)
        {
            throw TaDirectPlayException("DirectPlay message is shorter than its header");
        }

        auto word = readU32Le(data, 0);
        if ((word >> 20) != TaDirectPlayToken)
        {
            throw TaDirectPlayException("DirectPlay message has the wrong token");
        }

        auto size = word & 0x000FFFFFu;
        if (size != len)
        {
            throw TaDirectPlayException("DirectPlay message size does not match the bytes given");
        }

        if (!taIsDirectPlayMessage(data, len))
        {
            throw TaDirectPlayException("DirectPlay message is missing its signature");
        }

        TaDpMessage message;
        message.header.size = size;
        message.header.reply = readAddress(data, len, 4, "the reply address");
        message.command = static_cast<TaDirectPlayCommand>(readU16Le(data, 24));
        message.payload.assign(data + 28, data + len);
        return message;
    }

    std::vector<std::uint8_t> taEncodeDirectPlayMessage(const TaDpMessage& message)
    {
        std::vector<std::uint8_t> out;
        out.reserve(28 + message.payload.size());
        writeU32Le(out, (TaDirectPlayToken << 20) | (static_cast<std::uint32_t>(28 + message.payload.size()) & 0x000FFFFFu));
        writeAddress(out, message.header.reply);
        out.push_back('p');
        out.push_back('l');
        out.push_back('a');
        out.push_back('y');
        writeU16Le(out, static_cast<std::uint16_t>(message.command));
        writeU16Le(out, TaDirectPlayDialect);
        append(out, message.payload);
        return out;
    }

    std::vector<std::uint8_t> taEncodeSystemMessage(
        TaDirectPlayCommand command,
        const TaDpAddress& reply,
        std::span<const std::uint8_t> payload)
    {
        TaDpMessage message;
        message.header.reply = reply;
        message.command = command;
        message.payload.assign(payload.begin(), payload.end());
        return taEncodeDirectPlayMessage(message);
    }

    std::vector<std::uint8_t> taEncodeAppDataTcp(
        const TaDpAddress& reply,
        std::uint32_t from,
        std::uint32_t to,
        std::span<const std::uint8_t> payload)
    {
        std::vector<std::uint8_t> out;
        auto size = 20 + 8 + payload.size();
        out.reserve(size);
        writeU32Le(out, (TaDirectPlayToken << 20) | (static_cast<std::uint32_t>(size) & 0x000FFFFFu));
        writeAddress(out, reply);
        writeU32Le(out, from);
        writeU32Le(out, to);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    std::vector<std::uint8_t> taEncodeAppDataUdp(
        std::uint32_t from,
        std::uint32_t to,
        std::span<const std::uint8_t> payload)
    {
        std::vector<std::uint8_t> out;
        out.reserve(8 + payload.size());
        writeU32Le(out, from);
        writeU32Le(out, to);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    TaAppData taDecodeAppDataTcp(const std::uint8_t* data, std::size_t len)
    {
        if (taIsDirectPlayMessage(data, len))
        {
            throw TaDirectPlayException("expected application data but found a DirectPlay system message");
        }
        requireWithin(len, 20, 8, "the application data player ids");
        TaAppData app;
        app.from = readU32Le(data, 20);
        app.to = readU32Le(data, 24);
        app.payload.assign(data + 28, data + len);
        return app;
    }

    TaAppData taDecodeAppDataUdp(const std::uint8_t* data, std::size_t len)
    {
        requireWithin(len, 0, 8, "the application data player ids");
        TaAppData app;
        app.from = readU32Le(data, 0);
        app.to = readU32Le(data, 4);
        app.payload.assign(data + 8, data + len);
        return app;
    }

    TaSessionDescription taDecodeSessionDescription(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 80, "the session description");
        const auto* data = payload.data();
        TaSessionDescription session;
        session.size = readU32Le(data, 0);
        session.flags = readU32Le(data, 4);
        std::copy(data + 8, data + 24, session.instanceGuid.begin());
        std::copy(data + 24, data + 40, session.applicationGuid.begin());
        session.maxPlayers = readU32Le(data, 40);
        session.currentPlayers = readU32Le(data, 44);
        session.reserved1 = readU32Le(data, 48);
        session.reserved2 = readU32Le(data, 52);
        session.hostPlayerId = readU32Le(data, 56);
        session.user2 = readU32Le(data, 60);
        session.user3 = readU32Le(data, 64);
        session.user4 = readU32Le(data, 68);
        session.reserved3 = readU32Le(data, 72);
        session.reserved4 = readU32Le(data, 76);
        return session;
    }

    void taWriteSessionDescription(std::vector<std::uint8_t>& out, const TaSessionDescription& session)
    {
        writeU32Le(out, session.size);
        writeU32Le(out, session.flags);
        out.insert(out.end(), session.instanceGuid.begin(), session.instanceGuid.end());
        out.insert(out.end(), session.applicationGuid.begin(), session.applicationGuid.end());
        writeU32Le(out, session.maxPlayers);
        writeU32Le(out, session.currentPlayers);
        writeU32Le(out, session.reserved1);
        writeU32Le(out, session.reserved2);
        writeU32Le(out, session.hostPlayerId);
        writeU32Le(out, session.user2);
        writeU32Le(out, session.user3);
        writeU32Le(out, session.user4);
        writeU32Le(out, session.reserved3);
        writeU32Le(out, session.reserved4);
    }

    TaEnumSessionsReply taDecodeEnumSessionsReply(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 84, "the session enumeration reply");
        TaEnumSessionsReply reply;
        reply.session = taDecodeSessionDescription(payload);
        auto offset = readU32Le(payload.data(), 80);
        if (offset != 92)
        {
            throw TaDirectPlayException("the session enumeration reply names its name at an unexpected offset");
        }
        auto [name, _] = readUtf16Z(payload.data(), payload.size(), 84, "session name");
        reply.name = std::move(name);
        return reply;
    }

    std::vector<std::uint8_t> taEncodeEnumSessionsReply(const TaSessionDescription& session, const std::string& name)
    {
        std::vector<std::uint8_t> out;
        taWriteSessionDescription(out, session);
        writeU32Le(out, 8 + 80 + 4);
        append(out, utf16Z(name));
        return out;
    }

    TaSuperPackedPlayer taDecodeSuperPackedPlayer(const std::uint8_t* data, std::size_t len, std::size_t& offset, const char* what)
    {
        requireWithin(len, offset, 20, what);
        TaSuperPackedPlayer player;
        player.size = readU32Le(data, offset);
        player.flags = readU32Le(data, offset + 4);
        player.id = readU32Le(data, offset + 8);
        player.infoMask = readU32Le(data, offset + 12);
        player.versionOrSystemPlayerId = readU32Le(data, offset + 16);
        offset += 20;

        if (player.infoMask & 0x1)
        {
            auto [shortName, next] = readUtf16Z(data, len, offset, "a player's short name");
            player.shortName = std::move(shortName);
            offset = next;
        }
        if (player.infoMask & 0x2)
        {
            auto [longName, next] = readUtf16Z(data, len, offset, "a player's long name");
            player.longName = std::move(longName);
            offset = next;
        }

        auto playerDataLengthSize = (player.infoMask >> 4) & 0x3;
        if (playerDataLengthSize != 0)
        {
            requireWithin(len, offset, playerDataLengthSize, "a player's data length");
            std::uint32_t length = 0;
            for (std::size_t i = 0; i < playerDataLengthSize; ++i)
            {
                length |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
            }
            offset += playerDataLengthSize;
            requireWithin(len, offset, length, "a player's data");
            player.playerData.assign(data + offset, data + offset + length);
            player.hasPlayerData = true;
            offset += length;
        }

        auto spDataLengthSize = (player.infoMask >> 2) & 0x3;
        if (spDataLengthSize != 0)
        {
            requireWithin(len, offset, spDataLengthSize, "a player's service provider data length");
            std::uint32_t length = 0;
            for (std::size_t i = 0; i < spDataLengthSize; ++i)
            {
                length |= static_cast<std::uint32_t>(data[offset + i]) << (8 * i);
            }
            offset += spDataLengthSize;
            requireWithin(len, offset, length, "a player's service provider data");
            if (length >= 32)
            {
                player.tcpAddress = readAddress(data, len, offset, "a player's TCP address");
                player.udpAddress = readAddress(data, len, offset + 16, "a player's UDP address");
            }
            offset += length;
        }

        return player;
    }

    void taWriteSuperPackedPlayer(std::vector<std::uint8_t>& out, const TaSuperPackedPlayer& player)
    {
        writeU32Le(out, player.size);
        writeU32Le(out, player.flags);
        writeU32Le(out, player.id);
        writeU32Le(out, player.infoMask);
        writeU32Le(out, player.versionOrSystemPlayerId);

        if (player.infoMask & 0x1)
        {
            append(out, utf16Z(player.shortName));
        }
        if (player.infoMask & 0x2)
        {
            append(out, utf16Z(player.longName));
        }

        auto playerDataLengthSize = (player.infoMask >> 4) & 0x3;
        if (playerDataLengthSize != 0)
        {
            auto length = static_cast<std::uint32_t>(player.playerData.size());
            for (std::size_t i = 0; i < playerDataLengthSize; ++i)
            {
                out.push_back(static_cast<std::uint8_t>((length >> (8 * i)) & 0xFF));
            }
            append(out, player.playerData);
        }

        auto spDataLengthSize = (player.infoMask >> 2) & 0x3;
        if (spDataLengthSize != 0)
        {
            std::vector<std::uint8_t> spData;
            writeAddress(spData, player.tcpAddress);
            writeAddress(spData, player.udpAddress);
            auto length = static_cast<std::uint32_t>(spData.size());
            for (std::size_t i = 0; i < spDataLengthSize; ++i)
            {
                out.push_back(static_cast<std::uint8_t>((length >> (8 * i)) & 0xFF));
            }
            append(out, spData);
        }
    }

    TaSuperEnumPlayersReply taDecodeSuperEnumPlayersReply(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 28 + 80, "the super enum players reply");
        const auto* data = payload.data();

        auto playerCount = readU32Le(data, 0);
        auto groupCount = readU32Le(data, 4);
        auto packedOffset = readU32Le(data, 8);
        auto shortcutCount = readU32Le(data, 12);
        auto descriptionOffset = readU32Le(data, 16);
        auto nameOffset = readU32Le(data, 20);
        // passwordOffset is read for completeness; nothing follows it in the messages TA sends.

        // The offsets are from the start of the DPSP envelope, which is 8 bytes
        // before the payload this function is handed.
        auto toPayloadOffset = [](std::uint32_t offset) -> std::size_t {
            if (offset < 8)
            {
                throw TaDirectPlayException("a super enum players reply offset points into its header");
            }
            return offset - 8;
        };

        TaSuperEnumPlayersReply reply;
        reply.session = taDecodeSessionDescription(payload.subspan(toPayloadOffset(descriptionOffset)));
        auto [name, nameEnd] = readUtf16Z(payload.data(), payload.size(), toPayloadOffset(nameOffset), "session name");
        reply.name = std::move(name);
        (void)nameEnd;

        auto packed = toPayloadOffset(packedOffset);
        requireWithin(payload.size(), packed, 0, "the packed players");
        auto total = playerCount + groupCount + shortcutCount;
        // A message cannot hold more players than there are bytes for, and each
        // entry takes at least 20; cap the loop so a huge count is refused.
        if (total > (payload.size() - packed) / 20)
        {
            throw TaDirectPlayException("the super enum players reply claims more players than it can hold");
        }

        auto offset = packed;
        for (std::uint32_t i = 0; i < total; ++i)
        {
            // Groups and shortcuts are skipped by kind below; players are kept.
            auto player = taDecodeSuperPackedPlayer(data, payload.size(), offset, "a packed player");
            if (i < playerCount)
            {
                reply.players.push_back(std::move(player));
            }
        }

        return reply;
    }

    std::vector<std::uint8_t> taEncodeSuperEnumPlayersReply(const TaSuperEnumPlayersReply& reply)
    {
        std::vector<std::uint8_t> players;
        for (const auto& player : reply.players)
        {
            taWriteSuperPackedPlayer(players, player);
        }

        auto nameBytes = utf16Z(reply.name);
        constexpr std::uint32_t descriptionOffset = 8 + 28;
        constexpr std::uint32_t nameOffset = descriptionOffset + 80;
        auto packedOffset = static_cast<std::uint32_t>(nameOffset + nameBytes.size());

        std::vector<std::uint8_t> out;
        out.reserve(28 + 28 + 80 + nameBytes.size() + players.size());
        writeU32Le(out, static_cast<std::uint32_t>(reply.players.size()));
        writeU32Le(out, 0);
        writeU32Le(out, packedOffset);
        writeU32Le(out, 0);
        writeU32Le(out, descriptionOffset);
        writeU32Le(out, nameOffset);
        writeU32Le(out, 0);
        taWriteSessionDescription(out, reply.session);
        append(out, nameBytes);
        append(out, players);
        return out;
    }

    TaSessionDescChanged taDecodeSessionDescChanged(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 92, "the session description changed message");
        const auto* data = payload.data();

        auto nameOffset = readU32Le(data, 4);
        if (nameOffset != 100)
        {
            throw TaDirectPlayException("the session description changed message names its name at an unexpected offset");
        }

        TaSessionDescChanged message;
        message.session = taDecodeSessionDescription(payload.subspan(12));
        auto [name, nameEnd] = readUtf16Z(data, payload.size(), 92, "session name");
        message.name = std::move(name);
        message.tail.assign(payload.begin() + static_cast<std::ptrdiff_t>(nameEnd), payload.end());
        return message;
    }

    std::vector<std::uint8_t> taEncodeSessionDescChanged(const TaSessionDescChanged& message)
    {
        auto nameBytes = utf16Z(message.name);
        constexpr std::uint32_t nameOffset = 8 + 12 + 80;

        std::vector<std::uint8_t> out;
        out.reserve(12 + 80 + nameBytes.size() + message.tail.size());
        writeU32Le(out, 0);
        writeU32Le(out, nameOffset);
        writeU32Le(out, static_cast<std::uint32_t>(nameOffset + nameBytes.size()));
        taWriteSessionDescription(out, message.session);
        append(out, nameBytes);
        append(out, message.tail);
        return out;
    }

    TaAddForwardRequest taDecodeAddForwardRequest(std::span<const std::uint8_t> payload)
    {
        // The joining player's system id and its two addresses sit at fixed
        // offsets in the request TA sends; the fake host reads the same window.
        requireWithin(payload.size(), 0, 106, "the add forward request");
        const auto* data = payload.data();

        TaAddForwardRequest request;
        request.systemPlayerId = readU32Le(data, 4);
        request.tcpAddress = readAddress(data, payload.size(), 68, "the add forward request's TCP address");
        request.udpAddress = readAddress(data, payload.size(), 84, "the add forward request's UDP address");
        request.targetId = readU32Le(data, 102);
        return request;
    }

    TaCreatePlayer taDecodeCreatePlayer(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 120, "the create player message");
        const auto* data = payload.data();

        TaCreatePlayer player;
        player.playerId = readU32Le(data, 28);
        auto [name, _] = readUtf16Z(data, payload.size(), 68, "the created player's name");
        player.name = std::move(name);
        player.tcpAddress = readAddress(data, payload.size(), 88, "the created player's TCP address");
        player.udpAddress = readAddress(data, payload.size(), 104, "the created player's UDP address");
        return player;
    }

    std::uint32_t taDecodeRequestPlayerId(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 4, "the request player id flags");
        return readU32Le(payload.data(), 0);
    }

    std::uint32_t taDecodeRequestPlayerReply(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 4, "the request player reply id");
        return readU32Le(payload.data(), 0);
    }

    std::uint32_t taDecodeDeletePlayer(std::span<const std::uint8_t> payload)
    {
        requireWithin(payload.size(), 0, 8, "the delete player id");
        return readU32Le(payload.data(), 4);
    }

    std::vector<std::uint8_t> taEncodeRequestPlayerId(std::uint32_t flags)
    {
        std::vector<std::uint8_t> out;
        writeU32Le(out, flags);
        return out;
    }

    std::vector<std::uint8_t> taEncodeRequestPlayerReply(std::uint32_t playerId)
    {
        std::vector<std::uint8_t> out(40, 0);
        std::uint32_t value = playerId;
        out[0] = static_cast<std::uint8_t>(value & 0xFF);
        out[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        out[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        out[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
        return out;
    }

    std::vector<std::uint8_t> taEncodeDeletePlayer(std::uint32_t playerId)
    {
        std::vector<std::uint8_t> out(20, 0);
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        out[4] = static_cast<std::uint8_t>(playerId & 0xFF);
        out[5] = static_cast<std::uint8_t>((playerId >> 8) & 0xFF);
        out[6] = static_cast<std::uint8_t>((playerId >> 16) & 0xFF);
        out[7] = static_cast<std::uint8_t>((playerId >> 24) & 0xFF);
        return out;
    }

    std::string taSessionName(const std::string& gameName, const std::string& mapName)
    {
        std::string name = gameName;
        if (name.size() < 16)
        {
            name.append(16 - name.size(), ' ');
        }
        name += mapName;
        return name;
    }

    std::string taUtf16LeToUtf8(const std::uint8_t* data, std::size_t len)
    {
        std::string out;
        for (std::size_t i = 0; i + 1 < len; i += 2)
        {
            auto unit = readU16Le(data, i);
            std::uint32_t codePoint = unit;
            if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < len)
            {
                auto low = readU16Le(data, i + 2);
                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    codePoint = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }

            if (codePoint < 0x80)
            {
                out.push_back(static_cast<char>(codePoint));
            }
            else if (codePoint < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else if (codePoint < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
            }
        }
        return out;
    }

    std::vector<std::uint8_t> taUtf8ToUtf16Le(const std::string& text)
    {
        std::vector<std::uint8_t> out;
        std::size_t i = 0;
        while (i < text.size())
        {
            auto byte = static_cast<std::uint8_t>(text[i]);
            std::uint32_t codePoint = 0;
            std::size_t extra = 0;
            if (byte < 0x80)
            {
                codePoint = byte;
            }
            else if ((byte & 0xE0) == 0xC0)
            {
                codePoint = byte & 0x1F;
                extra = 1;
            }
            else if ((byte & 0xF0) == 0xE0)
            {
                codePoint = byte & 0x0F;
                extra = 2;
            }
            else if ((byte & 0xF8) == 0xF0)
            {
                codePoint = byte & 0x07;
                extra = 3;
            }
            else
            {
                // Not valid UTF-8: emit the replacement character.
                codePoint = 0xFFFD;
            }

            ++i;
            for (std::size_t j = 0; j < extra && i < text.size(); ++j, ++i)
            {
                codePoint = (codePoint << 6) | (static_cast<std::uint8_t>(text[i]) & 0x3F);
            }

            if (codePoint <= 0xFFFF)
            {
                writeU16Le(out, static_cast<std::uint16_t>(codePoint));
            }
            else
            {
                codePoint -= 0x10000;
                writeU16Le(out, static_cast<std::uint16_t>(0xD800 + (codePoint >> 10)));
                writeU16Le(out, static_cast<std::uint16_t>(0xDC00 + (codePoint & 0x3FF)));
            }
        }
        return out;
    }
}
