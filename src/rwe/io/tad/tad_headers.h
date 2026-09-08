#pragma once

// On-disk layout of a Total Annihilation demo file (.tad / .ted), as written by
// TA Demo Recorder and by TA Forever's recorder.
//
// This is NOT a format TotalA.exe writes. A demo is a capture of the DirectPlay
// traffic seen by one peer, framed into length-prefixed records by a
// third-party recorder, so the container below cannot be read out of the
// original binary and was instead ported from
//   ta-forever/gpgnet4ta, libs/tapacket/ (MIT licence)
// which is itself a C++ port of the original Delphi recorder. The subpacket
// payloads it carries *are* TA's own wire format.
//
// See docs/TA-DEMOS.md for what is verified against a real file and what is
// still only read from the reference implementation.

#include <cstdint>

namespace rwe
{
    /** The magic at the start of the header record. Eight bytes, NUL included. */
    static const char TadMagicNumber[8] = {'T', 'A', ' ', 'D', 'e', 'm', 'o', '\0'};

    /** Oldest demo version the reader accepts. */
    static const unsigned int TadMinVersion = 3;

    /** Newest demo version the reader accepts; what a current recorder writes. */
    static const unsigned int TadMaxVersion = 5;

    /**
     * Records are prefixed with a uint16 length that includes the two length
     * bytes themselves, so the payload is length - 2. These are the bounds the
     * reference implementation uses to detect a framing error.
     */
    static const unsigned int TadMinRecordLength = 3;
    static const unsigned int TadMaxRecordLength = 32768;

    /**
     * Version 5 demos carry a block of extra sectors between the header and the
     * player table. The type is a uint32 and the payload starts at offset 4.
     */
    enum class TadExtraSectorType : uint32_t
    {
        Comments = 1,
        Chat = 2,
        RecorderVersion = 3,
        Date = 4,
        RecorderContext = 5,

        /** Obfuscated with XOR 42. */
        PlayerAddr = 6,

        ModId = 7,
    };

    /** Watchers are ordinary entries in the player table, so filter on this. */
    enum class TadSide : int8_t
    {
        Arm = 0,
        Core = 1,
        Watch = 2,
    };

    /**
     * The first byte of a subpacket. Most have no length field: their size comes
     * from a hardcoded table, so misidentifying one desynchronises the rest of
     * the packet. Codes named UnkNN are ones whose size the reference knows but
     * whose meaning it does not.
     */
    enum class TadSubPacketCode : uint8_t
    {
        Zero = 0x00,
        Ping = 0x02,
        Unk03 = 0x03,
        Chat = 0x05,
        PadEncrypt = 0x06,
        Unk07 = 0x07,
        LoadingStarted = 0x08,
        UnitBuildStarted = 0x09,
        Unk0a = 0x0a,
        UnitTakeDamage = 0x0b,
        UnitKilled = 0x0c,
        WeaponFired = 0x0d,
        AreaOfEffect = 0x0e,
        FeatureAction = 0x0f,
        UnitStartScript = 0x10,
        UnitState = 0x11,
        UnitBuildFinished = 0x12,
        PlaySound = 0x13,
        GiveUnit = 0x14,
        Start15 = 0x15,
        ShareResources = 0x16,
        Unk17 = 0x17,
        HostMigration = 0x18,
        Speed = 0x19,
        UnitData = 0x1a,
        Reject = 0x1b,
        Start1e = 0x1e,
        Unk1f = 0x1f,
        PlayerInfo = 0x20,
        Unk21 = 0x21,
        Ident3 = 0x22,
        Ally = 0x23,
        Team = 0x24,
        Ident2 = 0x26,
        PlayerResourceInfo = 0x28,
        Unk29 = 0x29,
        LoadingProgress = 0x2a,

        /** Unit state and movement. The bulk of the stream, and the tick clock. */
        UnitStatAndMove = 0x2c,

        Unk2e = 0x2e,
        ThaldrenExtended = 0x42,
        Unkf6 = 0xf6,
        AllyChat = 0xf9,
        ReplayerServer = 0xfa,
        RecorderDataConnect = 0xfb,
        MapPosition = 0xfc,

        /** SmartPak coalescing; removed by unsmartpak. */
        SmartPakTickOther = 0xfd,
        SmartPakTickStart = 0xfe,
        SmartPakTick = 0xff,
    };

    /** A packet payload is compressed if its first byte is 0x04. */
    static const uint8_t TadPacketUncompressed = 0x03;
    static const uint8_t TadPacketCompressed = 0x04;

#pragma pack(1) // don't pad members

    /**
     * The fixed prefix of the header record. The map name follows at offset 13
     * and runs to the end of the record -- it is delimited by the record length,
     * and is not necessarily NUL-terminated.
     */
    struct TadHeaderPrefix
    {
        char magic[8];
        uint16_t version;
        uint8_t numPlayers;
        uint16_t maxUnits;
    };

    /** The fixed prefix of a player record; the name follows at offset 3. */
    struct TadPlayerPrefix
    {
        uint8_t color;

        /** A TadSide. */
        int8_t side;

        /**
         * Beware: not consistent between different players' recordings of the
         * same game.
         */
        uint8_t number;
    };

    /** The fixed prefix of a packet record; the payload follows at offset 3. */
    struct TadPacketPrefix
    {
        /**
         * Milliseconds since the previous packet, wall clock. Not a game clock:
         * it is contaminated by network jitter, by the 0x19 speed setting and by
         * pauses. The tick clock is the serial carried in 0x2c.
         */
        uint16_t time;

        uint8_t sender;
    };

#pragma pack()
}
