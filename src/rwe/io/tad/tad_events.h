#pragma once

// Typed decoders for the fixed-length event subpackets of a TA demo stream.
//
// tad_util.cpp knows how long each subpacket is, because a wrong length
// desynchronises the walk. This file knows what is *inside* the handful of them
// that carry the events a conformance corpus is built from: what was built,
// what finished, what was hit, what died, what fired.
//
// Unlike the container in tad_headers.h, none of this comes from the reference
// implementation -- it has no decoder for these payloads either. Every field
// below was read off the corpus and each struct says what the evidence was, so
// that a later reading of TotalA.exe can confirm or overturn it. Fields that
// did not resolve are named `unknown` rather than guessed at.
//
// See docs/TA-DEMOS.md.

#include <cstdint>
#include <optional>
#include <rwe/io/tad/tad_util.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * A position as TA puts it on the wire: three 16.16 fixed-point values in
     * world units, y up.
     *
     * Kept as the raw fixed-point integers because 16.16 has up to 32
     * significant bits and a float has 24, so converting on the way in would
     * quietly lose the low end of a large coordinate. Call tadFixedToDouble
     * when a number to look at is wanted.
     */
    struct TadPosition
    {
        int32_t x;
        int32_t y;
        int32_t z;

        bool operator==(const TadPosition& other) const = default;
    };

    /** Converts one 16.16 fixed-point wire value to world units. */
    double tadFixedToDouble(int32_t value);

    /**
     * An orientation in TA's angle units, where a full turn is 65536 -- the same
     * convention the COB virtual machine uses.
     *
     * Read as (pitch, yaw, roll) from the corpus rather than from the binary: a
     * building on flat ground has x and z of zero and a yaw of 0 or +-32768,
     * while a unit standing on a slope has all three non-zero. That is
     * consistent but not proof of which axis is which.
     */
    struct TadRotation
    {
        int16_t x;
        int16_t y;
        int16_t z;

        bool operator==(const TadRotation& other) const = default;
    };

    /**
     * 0x09, emitted when a nanoframe appears. Every byte is accounted for.
     *
     * NOTE: the second id is the *new unit*, not the builder. That was checked
     * rather than assumed: over demo 14724, 781 of the 790 distinct values of
     * this field reappear as the finished unit of a 0x12, and only 63 of them
     * as a 0x12's builder. The builder is not in this packet at all -- pair it
     * with the 0x12 to learn who built what.
     */
    struct TadBuildStarted
    {
        /** An index into the demo's 0x1a unit table. Not a name; see stage 4. */
        uint16_t typeIndex;

        /** The id of the nanoframe, which becomes the id of the finished unit. */
        uint16_t unitId;

        /** Where the nanoframe was placed. */
        TadPosition position;

        TadRotation rotation;
    };

    /** 0x12, emitted when a nanoframe becomes a unit. */
    struct TadBuildFinished
    {
        uint16_t unitId;
        uint16_t builderId;
    };

    /** 0x0b, emitted when a unit takes damage. */
    struct TadDamage
    {
        uint16_t victimId;

        /** Zero where there is no attacking unit -- terrain, decay, self-damage. */
        uint16_t attackerId;

        /**
         * Constant per attacker over a run of hits, and small (30, 40, 41, 45,
         * 46, 180 in demo 14724), which is what a weapon's damage figure looks
         * like. Not confirmed against a weapon definition, because that needs
         * the type table of stage 4.
         */
        uint16_t damage;

        /**
         * NOT remaining health. Tracked across successive hits on one victim it
         * neither falls monotonically nor falls by `damage`: unit 1523 in demo
         * 14724, hit repeatedly for 30 by unit 8, reads 322, 320, 318, 319,
         * 318, 319. It moves by ones and it goes back up, so whatever it counts
         * is not being spent by the hits. Left unnamed deliberately.
         */
        uint16_t unknown;
    };

    /** 0x0c, emitted when a unit dies. */
    struct TadDeath
    {
        uint16_t unitId;

        /**
         * The DirectPlay id of the killing player, matching the id at offset
         * 0x91 of a player's status message. 0xffffffff where there is none.
         */
        uint32_t killerDplayId;

        uint16_t killerId;

        /**
         * The corpse severity that docs/TOTALA-EXE.md section 22 hands to the
         * unit's COB `Killed`, `clamp(1, 100, (100 * overkill / maxdamage +
         * unit[0xF7]) / 2)`. It never exceeds 100 over 60,075 deaths, and it
         * falls monotonically with how much damage the victim had already
         * absorbed -- a median of 100 for a unit that took no recorded hits at
         * all and 15 for one that took ten or more. That is the overkill term
         * behaving exactly as the disassembly says it should. RWE hard-codes 50.
         *
         * Zero also occurs, which the clamp cannot produce, and every death
         * whose `cause` is 4, 5 or 9 reads zero here -- the causes section 22
         * says skip the script outright.
         *
         * See docs/TOTALA-EXE-WRECKS.md for the corpus evidence.
         */
        uint8_t severity;

        /**
         * The corpse level, in the low nibble, and the damage-type death cause
         * (`unit+0xF5`) in the high one.
         *
         * The level is what the COB `Killed` wrote back into its second
         * parameter and what the spawner at 0x486379 walks the `featuredead`
         * chain with: 0 leaves nothing, 1 the intact wreck, 2 and 3 further down
         * the chain. Over the corpus it tracks the severity at exactly the
         * thresholds the stock scripts use -- 1 or 2 up to severity 25, 2 from
         * 26 to 50, 3 from 51 up.
         *
         * The cause confirms section 22 without a single exception: all 569
         * deaths of cause 4, all 824 of cause 5 and all 949 of cause 9 leave
         * corpse level 0, and all 809 of cause 7 leave level 1, which is that
         * section's "causes 4, 5 and 9 leave nothing and skip the death script
         * outright, cause 7 forces a wreck" read back out of real games.
         */
        uint8_t causeAndLevel;

        /** The corpse level the script wrote back: 0 none, 1 the wreck, 2-3 below it. */
        uint8_t corpseLevel() const { return causeAndLevel & 0x0f; }

        /** The damage-type death cause, `unit+0xF5`. */
        uint8_t cause() const { return causeAndLevel >> 4; }
    };

    /** 0x0d, emitted when a weapon fires. Every byte is accounted for. */
    struct TadShot
    {
        /** Where the shot came from. */
        TadPosition origin;

        /** Where it was aimed. Not a velocity: it is far from the origin. */
        TadPosition target;

        TadRotation rotation;

        /** Zero where the shot was not aimed at a unit. */
        uint16_t targetId;

        uint16_t shooterId;

        /** 0, 1 or 2 over the corpus, dominated by 0. */
        uint8_t unknown;
    };

    /**
     * 0x10, a call into a unit's COB script. The emitter side is decoded in
     * docs/TOTALA-EXE.md (the generic emitter at 0x451DF0), so this is
     * transcription rather than decoding.
     */
    struct TadScriptCall
    {
        uint16_t unitId;

        /** An index into the unit's own COB, not a global one. */
        uint16_t scriptIndex;

        /** How many of the four argument slots are meaningful. */
        uint8_t argCount;

        int32_t args[4];
    };

    /**
     * 0x28, a player's resource state, emitted about every 120 ticks.
     *
     * The first four floats are settled, from watching them move over whole
     * games: stored is bounded above by its capacity, capacity moves in
     * building-sized steps, and metal is the one that sits at zero for long
     * stretches while energy does not.
     *
     * The remaining six are two monotonically increasing triples -- energy
     * first, then metal, which is which way round because over a game's opening
     * the first of each triple grows at 50 a second and 2 a second
     * respectively, the shape of a commander's output rather than the other way
     * about. They are cumulative counters of something; which of the
     * recorder's own `lastshared`/`shared`/`income`/`lasttotal` names they carry
     * is not settled, so they keep positional names.
     *
     * A watcher's record is a useful control: every field zero except
     * energyCounters[2] and metalCounters[2], both exactly 1000.0. That does not
     * fit any reading of those two slots taken from a playing peer, so treat a
     * watcher's record as uninitialised rather than as evidence.
     */
    struct TadResourceStats
    {
        float metalStored;
        float energyStored;
        float metalStorage;
        float energyStorage;
        float energyCounters[3];
        float metalCounters[3];

        /**
         * The 17 bytes between the code byte and the floats, kept whole rather
         * than split on a guess. Mostly zero. Two small values, one around
         * offset 1 and one around offset 5, grow over the course of a game and
         * are shaped like 16.16 fractions; bytes further in are usually but not
         * always zero, so even the field boundaries are not settled.
         */
        uint8_t prefix[17];
    };

    /**
     * One 14-byte record of the 0x1a unit table that a demo carries between its
     * player status records and its packet stream.
     *
     * The table is two blocks: every record of sub 2, then every record of
     * sub 3, each sorted ascending by id. Verified over thirteen games.
     */
    struct TadUnitTableEntry
    {
        uint8_t sub;

        /**
         * A content-derived 32-bit id for a unit type, and the thing a 0x09's
         * type index indexes into. It is NOT a hash of the unit's name: 88
         * hash/form combinations over the real name sets of two mods hit
         * nothing in either table. Naming a type needs TA's own routine read out
         * of TotalA.exe.
         */
        uint32_t id;

        /**
         * In the sub 3 block this takes only 0xffff0201 and 0xffff0101, the
         * latter on at most one entry per game, so it is a class or restriction
         * flag rather than a second checksum. In the sub 2 block it varies.
         */
        uint32_t value;
    };

    /** The 0x1a record, split into its two blocks. */
    struct TadUnitTable
    {
        std::vector<TadUnitTableEntry> listed;

        /**
         * Always a subset of `listed`, and always exactly the mod's unit count:
         * 317 for ProTA 4.8 and 549 in every TA: Escalation 10.2 demo, matching
         * each mod's own published figure. `listed` runs one or two entries
         * longer in four of the thirteen demos, which is not explained.
         */
        std::vector<TadUnitTableEntry> restricted;

        /**
         * An order-independent fingerprint of the restricted block, for telling
         * one data set from another. Not TA's own arithmetic -- just something
         * stable to compare, so that an unrecognised table can be reported as a
         * modded game.
         */
        uint32_t fingerprint() const;

        /** The name of a data set this fingerprint is known to belong to, if any. */
        std::optional<std::string> knownDataSet() const;
    };

    /**
     * Splits the 0x1a unit data record. Returns nothing if its length is not a
     * whole number of records.
     */
    std::optional<TadUnitTable> tadDecodeUnitTable(const TadBytes& record);

    /** 0x19, the game speed setting. Needed as a rejection filter, not as an oracle. */
    struct TadSpeed
    {
        uint16_t value;
    };

    /**
     * Which owner block a unit id falls in.
     *
     * Unit ids partition into contiguous blocks of maxUnits, one per player,
     * numbered from zero and with id zero meaning "no unit". Verified over the
     * corpus: in every demo each sender's units fall in exactly one block and no
     * two senders share one, including the ten-player demo 14727 which uses all
     * ten blocks and the 1500-unit demo 14724, so the arithmetic is not
     * hardcoded to 1000.
     *
     * The block is NOT the player number -- in demo 14733 sender 1 owns block 2
     * and sender 3 owns block 1 -- but it is a stable per-player key, and
     * "is this event about one of the recording peer's own units" is just a
     * comparison of blocks.
     *
     * Returns nothing for id zero or a maxUnits of zero.
     */
    std::optional<unsigned int> tadOwnerBlockOfUnitId(uint16_t unitId, uint16_t maxUnits);

    // Each of these returns nothing if the subpacket is not the expected code or
    // not the expected length, so a caller can hand it anything the walker
    // produced without checking first.

    std::optional<TadBuildStarted> tadDecodeBuildStarted(const TadBytes& subPacket);
    std::optional<TadBuildFinished> tadDecodeBuildFinished(const TadBytes& subPacket);
    std::optional<TadDamage> tadDecodeDamage(const TadBytes& subPacket);
    std::optional<TadDeath> tadDecodeDeath(const TadBytes& subPacket);
    std::optional<TadShot> tadDecodeShot(const TadBytes& subPacket);
    std::optional<TadScriptCall> tadDecodeScriptCall(const TadBytes& subPacket);
    std::optional<TadResourceStats> tadDecodeResourceStats(const TadBytes& subPacket);
    std::optional<TadSpeed> tadDecodeSpeed(const TadBytes& subPacket);
}
