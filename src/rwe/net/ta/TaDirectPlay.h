#pragma once

// DirectPlay 4's session layer, as Total Annihilation uses it.
//
// This is the C++ port of the DirectPlay half of tools/ta-net/tanet.py and the
// message shapes tools/ta-net/fakehost.py answers. docs/TA-NETWORK.md is the
// reference for what each message carries and when it is sent; the byte layouts
// come from [MC-DPL4CS] (DirectPlay 4 Protocol: Core and Service Providers).
//
// The codec owns nothing but bytes: it does not know about the game, the
// battleroom or TA packets. Every length, count and offset read from the wire is
// bounded before use, so a truncated or hostile message is refused rather than
// walked off the end.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace rwe
{
    class TaDirectPlayException : public std::runtime_error
    {
    public:
        explicit TaDirectPlayException(const std::string& message);
    };

    /** The 20-bit size / 12-bit token word that opens every DirectPlay TCP message. */
    inline constexpr std::uint32_t TaDirectPlayToken = 0xFAB;

    /** The dialect Total Annihilation speaks. */
    inline constexpr std::uint16_t TaDirectPlayDialect = 0x000E;

    /** Total Annihilation's application GUID, {99797420-F5F5-11CF-9827-00A0241496C8}. */
    inline constexpr std::array<std::uint8_t, 16> TaApplicationGuid{
        0x20, 0x74, 0x79, 0x99, 0xF5, 0xF5, 0xCF, 0x11,
        0x98, 0x27, 0x00, 0xA0, 0x24, 0x14, 0x96, 0xC8};

    enum class TaDirectPlayCommand : std::uint16_t
    {
        EnumSessionsReply = 0x0001,
        EnumSessions = 0x0002,
        RequestPlayerId = 0x0005,
        RequestPlayerReply = 0x0007,
        CreatePlayer = 0x0008,
        DeletePlayer = 0x000B,
        AddForwardRequest = 0x0013,
        Ping = 0x0016,
        PingReply = 0x0017,
        SessionDescChanged = 0x001A,
        SuperEnumPlayersReply = 0x0029,
        AddForward = 0x002E,
        AddForwardAck = 0x002F,
    };

    /** A sockaddr_in: family little-endian, port big-endian, address, padding. */
    struct TaDpAddress
    {
        std::uint16_t family{2};
        std::uint16_t port{0};
        std::array<std::uint8_t, 4> ip{0, 0, 0, 0};
        std::array<std::uint8_t, 8> padding{};
    };

    /** The 20-byte header over TCP: size and token, then the sender's reply address. */
    struct TaDpHeader
    {
        std::uint32_t size{0};
        TaDpAddress reply;
    };

    /**
     * A DirectPlay system message: the header, the command and the bytes after
     * the `"play"`, command and dialect. The payload is kept verbatim so a
     * message this codec does not model still round-trips.
     */
    struct TaDpMessage
    {
        TaDpHeader header;
        TaDirectPlayCommand command;
        std::vector<std::uint8_t> payload;
    };

    /** The DPSESSIONDESC2 structure, 80 bytes on the wire. */
    struct TaSessionDescription
    {
        std::uint32_t size{80};
        std::uint32_t flags{0};
        std::array<std::uint8_t, 16> instanceGuid{};
        std::array<std::uint8_t, 16> applicationGuid = TaApplicationGuid;
        std::uint32_t maxPlayers{10};
        std::uint32_t currentPlayers{1};
        std::uint32_t reserved1{0};
        std::uint32_t reserved2{0};
        std::uint32_t hostPlayerId{0};
        std::uint32_t user2{0};

        /** The options byte in the top byte, the current-player count in the bottom. */
        std::uint32_t user3{0};
        std::uint32_t user4{4};
        std::uint32_t reserved3{0x000A000A};
        std::uint32_t reserved4{0x010300FA};

        std::uint8_t options() const { return static_cast<std::uint8_t>(user3 >> 24); }
        void setOptions(std::uint8_t value) { user3 = (user3 & 0x00FFFFFFu) | (static_cast<std::uint32_t>(value) << 24); }
    };

    /** A DPLAYI_SUPERPACKEDPLAYER plus its optional fields and service-provider addresses. */
    struct TaSuperPackedPlayer
    {
        std::uint32_t size{16};
        std::uint32_t flags{0};

        /** DPPLAYERTYPE_SYSPLAYER (1) for a system player, otherwise a named one. */
        std::uint32_t id{0};
        std::uint32_t infoMask{0};

        /** The dialect version for a system player, else its system player's id. */
        std::uint32_t versionOrSystemPlayerId{TaDirectPlayDialect};

        /** Present when infoMask says so; empty otherwise. */
        std::string shortName;
        std::string longName;
        std::vector<std::uint8_t> playerData;
        bool hasPlayerData{false};

        TaDpAddress tcpAddress;
        TaDpAddress udpAddress;
    };

    struct TaEnumSessionsReply
    {
        TaSessionDescription session;
        std::string name;
    };

    struct TaSuperEnumPlayersReply
    {
        TaSessionDescription session;
        std::string name;
        std::vector<TaSuperPackedPlayer> players;
    };

    struct TaSessionDescChanged
    {
        TaSessionDescription session;
        std::string name;

        /** Zero padding a recorded host carries after the name; preserved verbatim. */
        std::vector<std::uint8_t> tail;
    };

    struct TaAddForwardRequest
    {
        std::uint32_t systemPlayerId{0};
        TaDpAddress tcpAddress;
        TaDpAddress udpAddress;
        std::uint32_t targetId{0};
    };

    struct TaCreatePlayer
    {
        std::uint32_t playerId{0};
        std::string name;
        TaDpAddress tcpAddress;
        TaDpAddress udpAddress;
    };

    struct TaAppData
    {
        std::uint32_t from{0};
        std::uint32_t to{0};
        std::vector<std::uint8_t> payload;
    };

    /** Whether a TCP message carries the DirectPlay system signature rather than application data. */
    bool taIsDirectPlayMessage(const std::uint8_t* data, std::size_t len);

    /** The size field of a TCP message, or 0 if too short or the token is wrong. */
    std::uint32_t taPeekDirectPlayMessageSize(const std::uint8_t* data, std::size_t len);

    /** An address with a zero IP, so the receiver reads the packet's source address. */
    TaDpAddress taMakeReplyAddress(std::uint16_t port, std::array<std::uint8_t, 4> ip = {0, 0, 0, 0});

    /** The 20-byte header and the framed message. Throws if either is malformed. */
    TaDpMessage taDecodeDirectPlayMessage(const std::uint8_t* data, std::size_t len);

    std::vector<std::uint8_t> taEncodeDirectPlayMessage(const TaDpMessage& message);

    /** A system message: the 28-byte header (20 + `"play"` + command + dialect), then the payload. */
    std::vector<std::uint8_t> taEncodeSystemMessage(
        TaDirectPlayCommand command,
        const TaDpAddress& reply,
        std::span<const std::uint8_t> payload);

    /** Application data over TCP: the 20-byte header, then the player ids and the payload. */
    std::vector<std::uint8_t> taEncodeAppDataTcp(
        const TaDpAddress& reply,
        std::uint32_t from,
        std::uint32_t to,
        std::span<const std::uint8_t> payload);

    /** Application data over UDP has no header at all: the player ids then the payload. */
    std::vector<std::uint8_t> taEncodeAppDataUdp(
        std::uint32_t from,
        std::uint32_t to,
        std::span<const std::uint8_t> payload);

    TaAppData taDecodeAppDataTcp(const std::uint8_t* data, std::size_t len);

    TaAppData taDecodeAppDataUdp(const std::uint8_t* data, std::size_t len);

    /** The session description at the front of a payload, exactly 80 bytes consumed. */
    TaSessionDescription taDecodeSessionDescription(std::span<const std::uint8_t> payload);

    void taWriteSessionDescription(std::vector<std::uint8_t>& out, const TaSessionDescription& session);

    TaEnumSessionsReply taDecodeEnumSessionsReply(std::span<const std::uint8_t> payload);

    std::vector<std::uint8_t> taEncodeEnumSessionsReply(const TaSessionDescription& session, const std::string& name);

    TaSuperEnumPlayersReply taDecodeSuperEnumPlayersReply(std::span<const std::uint8_t> payload);

    std::vector<std::uint8_t> taEncodeSuperEnumPlayersReply(const TaSuperEnumPlayersReply& reply);

    TaSessionDescChanged taDecodeSessionDescChanged(std::span<const std::uint8_t> payload);

    std::vector<std::uint8_t> taEncodeSessionDescChanged(const TaSessionDescChanged& message);

    TaAddForwardRequest taDecodeAddForwardRequest(std::span<const std::uint8_t> payload);

    TaCreatePlayer taDecodeCreatePlayer(std::span<const std::uint8_t> payload);

    std::uint32_t taDecodeRequestPlayerId(std::span<const std::uint8_t> payload);

    std::uint32_t taDecodeRequestPlayerReply(std::span<const std::uint8_t> payload);

    std::uint32_t taDecodeDeletePlayer(std::span<const std::uint8_t> payload);

    std::vector<std::uint8_t> taEncodeRequestPlayerId(std::uint32_t flags);

    std::vector<std::uint8_t> taEncodeRequestPlayerReply(std::uint32_t playerId);

    std::vector<std::uint8_t> taEncodeDeletePlayer(std::uint32_t playerId);

    /**
     * A session name: the game name padded to 16 characters, then the map name,
     * as Total Annihilation builds it (docs/TA-NETWORK.md, "DirectPlay").
     */
    std::string taSessionName(const std::string& gameName, const std::string& mapName);

    std::string taUtf16LeToUtf8(const std::uint8_t* data, std::size_t len);

    std::vector<std::uint8_t> taUtf8ToUtf16Le(const std::string& text);
}
