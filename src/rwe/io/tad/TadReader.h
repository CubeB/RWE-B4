#pragma once

// Reads the record sequence of a .tad / .ted demo file.
//
// Ported from ta-forever/gpgnet4ta, libs/tapacket/TADemoParser.cpp (MIT
// licence), minus its resumable state machine: that exists so the recorder can
// tail a game in progress, and offline analysis has no use for it.
//
// See docs/TA-DEMOS.md.

#include <cstdint>
#include <istream>
#include <optional>
#include <rwe/io/tad/tad_headers.h>
#include <rwe/io/tad/tad_util.h>
#include <string>
#include <vector>

namespace rwe
{
    struct TadHeader
    {
        uint16_t version;
        uint8_t numPlayers;
        uint16_t maxUnits;

        /** Delimited by the record length, so it may not be NUL-terminated. */
        std::string mapName;
    };

    struct TadExtraSector
    {
        uint32_t type;

        /** Already de-obfuscated if the type calls for it. */
        TadBytes data;
    };

    struct TadPlayer
    {
        uint8_t color;
        int8_t side;
        uint8_t number;
        std::string name;

        bool isWatcher() const { return static_cast<TadSide>(side) == TadSide::Watch; }
    };

    struct TadPlayerStatus
    {
        uint8_t number;

        /** Decrypted, decompressed, and with the leading seven bytes dropped. */
        TadBytes statusMessage;

        /**
         * The DirectPlay id at offset 0x91 of the status message, which is what
         * a 0x1b reject refers to. Absent if the message was too short.
         */
        std::optional<uint32_t> dplayId;

        /** Whether the status message's own checksum came out right. */
        TadChecksum checksum;
    };

    struct TadPacket
    {
        uint16_t time;
        uint8_t sender;
    };

    /**
     * Receives the records of a demo as they are read. Every method has a
     * do-nothing default, so a caller overrides only what it wants.
     */
    struct TadHandler
    {
        virtual ~TadHandler() = default;

        virtual void onHeader(const TadHeader&) {}
        virtual void onExtraSector(const TadExtraSector&, unsigned int, unsigned int) {}
        virtual void onPlayer(const TadPlayer&, unsigned int, unsigned int) {}
        virtual void onPlayerStatus(const TadPlayerStatus&, unsigned int, unsigned int) {}
        virtual void onUnitData(const TadBytes&) {}

        /**
         * @param subPackets the packet's payload after decompression and
         *        unsmartpak.
         * @param stats what went wrong walking this packet, if anything.
         */
        virtual void onPacket(const TadPacket&, const std::vector<TadBytes>&, const TadWalkStats&) {}
    };

    /**
     * Reads a demo from beginning to end, calling the handler as it goes.
     * Throws TadException on a framing or validation error.
     */
    void readTad(std::istream& stream, TadHandler& handler);
}
