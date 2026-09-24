#pragma once

// Writes the record sequence of a .tad / .ted demo file: the inverse of
// TadReader, and the container half of the demo recorder. See
// docs/TA-DEMOS.md, "The container".
//
// Two parts of a real recording cannot be reproduced from RWE state, and are
// written valid but synthetic: both are deliberate divergences from the
// original's bytes, not defects.
//
//  - the 0x1a unit table's content-derived ids. The restricted block carries
//    the count it is given and the fixed pseudo-entry; every other id is a
//    running number. The count comparison and the data-set fingerprint are
//    what the tools use, and neither reads the values.
//  - the player status message's fields other than the DirectPlay id, which is
//    the only offset decoded so far: a 192-byte message with its 0x20 code and
//    the id, zero elsewhere. That is enough for the checksum to verify, which
//    is what tad_probe reports.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <ostream>
#include <rwe/io/tad/TadReader.h>
#include <rwe/io/tad/tad_util.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * Everything a demo carries that is not per-tick. The date and the
     * recorder version are fields rather than clock reads so that a test can
     * write the same bytes twice.
     */
    struct TadWriterSettings
    {
        /** Only 5 is written: the extra sectors need it, and 3 has timestamps. */
        uint16_t version = 5;

        /** Must agree with players.size(); the reader loops this many records. */
        uint8_t numPlayers = 0;

        uint16_t maxUnits = 1000;

        /** Up to 64 bytes; the record length delimits it, so no terminator. */
        std::string mapName;

        std::vector<TadPlayer> players;

        std::string recorderVersion;
        std::string date;

        /**
         * Per player, in player-record order. Empty writes a zero id for every
         * player: the DirectPlay id is the only field of a status message this
         * writes meaningfully, and RWE has no id of its own to give.
         */
        std::vector<uint32_t> dplayIds;

        /** One entry per player; empty writes an address sector for each player anyway. */
        std::vector<std::string> playerAddresses;
    };

    /**
     * Writes the container in the order the reader walks it: header, extra
     * header and sectors, player table, player statuses, the unit table, then
     * a packet per call. The stage is checked, so a caller that gets the order
     * wrong is told rather than handing tad_probe a file that walks out of
     * step.
     */
    class TadWriter
    {
    public:
        TadWriter(std::ostream& stream, TadWriterSettings settings);

        /** Opens the file with truncation; throws if it will not open. */
        TadWriter(const std::filesystem::path& path, TadWriterSettings settings);
        ~TadWriter();

        TadWriter(const TadWriter&) = delete;
        TadWriter& operator=(const TadWriter&) = delete;

        void writeHeader();

        /**
         * The extra header and the sectors it counts: recorder version, date,
         * and one player-address sector per player, obfuscated with XOR 42.
         */
        void writeExtraSectors();

        void writePlayers();

        /**
         * A 192-byte 0x20 message per player, built here and encrypted and
         * compressed the way the reader undoes it.
         */
        void writePlayerStatuses();

        /**
         * Builds the 0x1a record from the data set's unit count: that many
         * restricted entries including the fixed pseudo-entry, with
         * deterministic synthetic ids, and the same count listed.
         */
        void writeUnitTable(std::size_t unitTypeCount);

        /**
         * A packet record: the time, the sender and one plain payload holding
         * the subpackets back to back. Uncompressed: the compressor the status
         * message uses assumes a three-byte header and a packet record's is
         * one, so reusing it would shift every back-reference.
         */
        void writePacket(uint16_t time, uint8_t sender, const std::vector<TadBytes>& subPackets);

        /** Flushes, and closes the file if this owns one. Idempotent. */
        void close();

    private:
        enum class Stage
        {
            Start,
            Header,
            Sectors,
            Players,
            Statuses,
            UnitTable,
            Packets,
            Closed,
        };

        void expectStage(Stage expected, const char* what);
        void writeRecord(const TadBytes& payload);
        void writeSector(uint32_t type, const TadBytes& data);

        /** Declaration order matters: stream points into file when this owns one. */
        std::unique_ptr<std::ofstream> file;
        std::ostream* stream;
        TadWriterSettings settings;
        Stage stage{Stage::Start};
    };
}
