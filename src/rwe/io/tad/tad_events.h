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
#include <variant>
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
        /**
         * Which unit type, as a 1-based index into the unit load order -- NOT
         * an index into the demo's 0x1a table, which is the reading this
         * started from and which the corpus refutes.
         *
         * The 0x1a table is sorted by a content-derived id, so its order is
         * effectively random with respect to which side a unit belongs to. The
         * indices in this field are not: in demo 14724 the ARM player's indices
         * all fall in 4..152 and the CORE player's in 159..310, in 14733 the
         * split is 7..258 against 268..525, and in the ten-player 14727 the
         * blocks fall into a low group and a high group the same way. A random
         * permutation gives that for one demo with probability about 4e-13.
         *
         * It is TA's load-order index, which the FBI loader assigns to each unit
         * type and stores at record+0x21e (docs/TOTALA-EXE.md section 106). The
         * order is the one tadUnitLoadOrder produces: every units\*.FBI name in
         * the merged VFS, sorted, numbered from one. See that function for the
         * evidence, and docs/TA-DEMOS.md for the whole argument.
         */
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

        /**
         * Which of the shooter's weapons fired, as a 0-based index into its
         * FBI's Weapon1/Weapon2/Weapon3 -- so `WeaponN` with N = weaponSlot + 1.
         *
         * Over the thirteen-demo corpus, with shooters named by the unit id's
         * most recent build rather than its first, every slot a type was seen
         * firing on lands on a WeaponN that type's FBI actually fills, in 644 of
         * 658 (demo, type) observations.
         *
         * The near-misses are the confirmation rather than the exception.
         * Fifteen types fail a naive `slot < number of weapons` test by emitting
         * slots 0 and 2 and never 1 -- ARMSAM, ARMJETH, CORMIST, ARMYORK and
         * friends -- and every one of them defines Weapon1 and Weapon3 and
         * leaves Weapon2 empty, which is the standard TA convention of putting
         * the anti-air weapon in the third slot. Test slot OCCUPANCY, not weapon
         * count.
         *
         * The residual fourteen are NOT explained, and the obvious explanation
         * has been measured and does not cover them. They are types the data set
         * gives no weapon at all, so a leftover naming error is the natural
         * reading -- but they account for 7,180 shots, 1.1% of the named total
         * rather than a handful, and their median staleness against the build
         * that named them is 10,008 ticks against a conforming 7,083, nowhere
         * near the separation a recycled id gives. The competing story, that
         * 0x0d covers something besides weapons, is damaged too: ARMULAB and
         * ARMFAHP carry LAB_DIR in slot 3 and the shots attributed to them read
         * slot 0, so a record reporting LAB_DIR would have to say 2. None of it
         * touches the 112-to-14 gap the reading rests on.
         *
         * `tad_episodes --weapon-slots` prints the table this came from. See
         * docs/TA-DEMOS.md, the 0x0d section.
         */
        uint8_t weaponSlot;
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
     * 0x28, a player's resource state, emitted every 120 ticks.
     *
     * WHOSE state: the sender's own. The record carries no player id, and a
     * sender emits exactly `numPlayers - 1` of them on one tick, which looks
     * like a table of every other player and is not one -- every copy in a
     * burst is identical, over all 60,445 multi-copy bursts of the thirteen-demo
     * corpus without exception. It is one unicast per peer, which the recorder
     * sees all of. Attribute a burst to its sender and drop the copies.
     *
     * Confirmed independently of that argument: a sender's `metalStorage` and
     * `energyStorage` step exactly when the sender's OWN owner block finishes a
     * building that grants storage. Of 5,987 storage steps in the corpus, 4,416
     * are explained by one owner block and it is the sender's; 53 by some other
     * block, which is what window-edge cases and builds begun before the
     * recording started look like.
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
         *
         * Byte 0 is the one thing here that is legible: it reads 1 instead of 0
         * only in a game's closing seconds, and those records arrive every five
         * ticks or so rather than every 120. It is the sole field that ever
         * varies within a burst, and only there, which is two successive samples
         * landing on one tick rather than a per-recipient field.
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
         * A content-derived 32-bit id for a unit type. It is NOT a hash of the
         * unit's name: 88 hash/form combinations over the real name sets of two
         * mods hit nothing in either table, and reproducing it needs TA's own
         * routine read out of TotalA.exe.
         *
         * A 0x09's type index does NOT index into this table -- that reading is
         * refuted on TadBuildStarted::typeIndex, and naming a type does not need
         * this id. Its remaining use is identifying the data set, which
         * TadUnitTable::knownDataSet does by fingerprint.
         */
        uint32_t id;

        /**
         * Three fields, not a number. Decoded from the packet builder at
         * 0x46d630 in TotalA.exe: the low byte, the second byte and the top word
         * are read from three separate places in the source record, so the
         * dword only looks like one value because of how it is packed.
         *
         * In the sub 3 block the low byte is always 1, the second byte is 1, and
         * the top word is the 0xffff sentinel -- 0xffff0101, on every entry but
         * one. The exception is the same in every demo of both data sets and is
         * described on TadUnitTable::pseudoEntryId. In the sub 2 block all three
         * vary.
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
         * The one id that is not a unit type.
         *
         * It appears in both blocks of every demo in the corpus, in *both* data
         * sets -- it is the single id ProTA's 317 and Escalation's 549 have in
         * common -- and it is the only entry whose `value` is not 0xffff0101.
         * Being shared between two unrelated data sets, it cannot be derived
         * from either's unit files, so it is a fixed pseudo-entry rather than a
         * unit. In demo 14735 its top word reads 1000 instead of the 0xffff
         * sentinel, which is what a limit rather than a flag looks like.
         */
        static const uint32_t pseudoEntryId = 2455016279u;

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

    // ---------------------------------------------------------------------
    // 0x2c, unit state. Unlike everything above, this one was read out of
    // TotalA.exe rather than off the corpus, and then checked against the
    // corpus -- docs/TA-DEMOS.md, "0x2c, unit state".
    //
    // It is a BIT stream, not a byte layout: the builder at 0x48B710 writes
    // through the bit writer at 0x415C10, which packs each field least
    // significant bit first into little-endian 32-bit words. Reading the
    // subpacket's bytes as one little-endian integer and taking fields from the
    // bottom is the same thing.
    //
    // The builder, the per-unit serialisers and the receiver are all unpatched
    // in TA: Escalation's TotalA.exe (tools/exe/patchdiff.py --range), so the
    // GOG v3.1 reading applies to the whole corpus.
    // ---------------------------------------------------------------------

    /** A navigator waypoint: whole world units, x and z. */
    struct TadWaypoint
    {
        int16_t x;
        int16_t z;

        bool operator==(const TadWaypoint& other) const = default;
    };

    /**
     * A ground unit's path, from its navigator's serialiser at 0x44F4A0.
     *
     * Sent only when the navigator's "path changed" bit is set or the blocked
     * bit has changed (0x44F480), so it is a delta: a unit that is not in a
     * packet has the path it last had.
     */
    struct TadGroundPath
    {
        /** mover+0x2E bit 2, the mover's blocked flag. */
        bool blocked;

        /**
         * The next zero to three waypoints of the path, from navigator+0x0C.
         * None means the unit has no path -- it has stopped. The navigator
         * holds up to twenty, and only the first three are sent; a remote
         * machine steers along those and waits for the next update.
         *
         * Corpus: the first waypoint is within 32 world units of where a 0x0d
         * places the unit within ten ticks in 72% of 210,770 cases and within
         * 128 in 97%, against 3% for the same comparison made with another
         * unit's waypoint.
         */
        std::vector<TadWaypoint> waypoints;
    };

    /**
     * An aircraft's move goal, the 0x36-byte object docs/TOTALA-EXE-MISSIONS.md
     * calls a goal (vtable 0x4FD3B8), from its serialiser at 0x44DDC0. Each
     * optional is present exactly when its flag bit is set.
     */
    struct TadMoveGoal
    {
        /** The goal's flag word, low byte only -- the wire carries eight bits. */
        uint8_t flags;

        /** Flag 0x01: goal+0x10, not identified. */
        std::optional<int16_t> unknown10;

        /** Flag 0x01: the attached unit's global id, zero for none. */
        std::optional<uint16_t> attachedUnitId;

        /** Flag 0x10: arrival tolerance, whole world units. */
        std::optional<int16_t> tolerance;

        /** Flag 0x08: cruise altitude above ground. */
        std::optional<int16_t> altitude;

        /** Flag 0x40: heading offset, used by the relative-goal resolver. */
        std::optional<int16_t> headingOffset;

        /**
         * Flag 0x20: the goal position, 16.16. y is the absolute target height,
         * clamped to 511 by 0x44E6C0, and never exceeds it in the corpus.
         */
        std::optional<TadPosition> position;
    };

    /**
     * An aircraft's moving goal (vtable 0x4FD3F8), from its serialiser at
     * 0x44E930: a point that advances by a fixed velocity every tick (the
     * resolver at 0x44EA60 adds one to the other). 463 of them in the corpus.
     */
    struct TadMovingGoal
    {
        TadPosition position;

        /** Added to position every tick; 16.16 world units a tick. */
        TadPosition velocity;

        /** A heading, present when the goal's flag bit 0 is set. */
        std::optional<uint16_t> heading;
    };

    /** An aircraft's mover state, from 0x4908C0. */
    struct TadAirMover
    {
        /** No goal, a move goal, or a moving goal: the wire's two-bit kind 0, 1, 2. */
        std::variant<std::monostate, TadMoveGoal, TadMovingGoal> goal;

        /** mover+0x2E bits 0-1, the movement mode: 1 landed, 2 flying. */
        uint8_t movementMode;
    };

    /** One unit's entry in the per-tick part of a 0x2c. */
    struct TadUnitUpdate
    {
        /**
         * The unit's index within its owner's block, NOT its global id. The
         * global id is `block * maxUnits + index + 1`; see tadUnitIdOfIndex.
         */
        uint16_t index;

        /** The same 1-based load-order index a 0x09 carries. */
        uint16_t typeIndex;

        /**
         * Which serialiser wrote the rest is decided by the unit's type, not by
         * anything on the wire: canfly picks the aircraft mover at 0x43DC5F, and
         * everything else gets a navigator. That is why decoding needs the data
         * set.
         */
        std::variant<TadGroundPath, TadAirMover> mover;
    };

    /** The unit a carried unit is attached to, in place of a position. */
    struct TadCarried
    {
        /** The carrier's global id, fifteen bits wide on the wire. */
        uint16_t carrierId;

        /** unit+0xF9, the carrier piece the unit hangs from. */
        int8_t piece;
    };

    /**
     * The full-state record every 0x2c ends with, from 0x48B200: one unit of
     * the sender's per tick, round robin, the one whose index is
     * `tick % maxUnits`. So each unit's position is on the wire once every
     * maxUnits ticks (33 seconds at 1000) and at no other time.
     */
    struct TadUnitSync
    {
        /** The unit's index within its owner's block: `tick % maxUnits`. */
        uint16_t index;

        /** Zero for an empty slot, in which case nothing below is meaningful. */
        uint16_t typeIndex;

        /** unit+0x108, current health. */
        uint16_t health;

        /**
         * 0 once the unit is complete; otherwise `1 + trunc(254 * remaining)`,
         * where remaining is the float at unit+0x104 that counts down from 1.0.
         */
        uint8_t buildProgress;

        /** unit+0x10E: bit 1 armoured, bit 4 paralysed, and more. */
        uint8_t flags10E;

        /** unit+0x110 bits 0-1; 2 is airborne. */
        uint8_t motionState;

        /** Present for a unit riding a transport or pad, instead of a position. */
        std::optional<TadCarried> carried;

        TadPosition position;

        /**
         * Sent as y, z, x (unit+0x66, +0x68, +0x64) and stored here the usual
         * way round, so it compares equal to the rotation of the unit's 0x09.
         */
        TadRotation rotation;

        /**
         * mover+0x20, the current speed as 16.16 world units a tick. Present
         * exactly when the unit has a mover; buildings do not.
         */
        std::optional<int32_t> speed;
    };

    /** 0x2c, a sender's unit state for one tick. */
    struct TadUnitState
    {
        /** The sender's tick, the serial docs/TA-DEMOS.md uses as the clock. */
        uint32_t tick;

        /**
         * Units whose movers had something new (their serialiser's vtable
         * +0x1C said so), in unit order, until the packet reaches 512 bytes.
         */
        std::vector<TadUnitUpdate> updates;

        /** Absent only if the sender wrote no sync bit, which the corpus never does. */
        std::optional<TadUnitSync> sync;
    };

    /** What decoding a 0x2c needs to know about the data set. */
    struct TadUnitStateLayout
    {
        /**
         * The width of a type index on the wire: the bit length of the unit
         * type count (0x42D65B), so 10 for Escalation's 549 and 9 for ProTA's
         * 317.
         */
        unsigned int typeIndexBits;

        /** Whether each type index flies, indexed by the 1-based type index. */
        std::vector<bool> canFly;

        /** The header's maxUnits, which says whose turn the full-state record is. */
        uint16_t maxUnits;
    };

    /**
     * Builds the layout from each type's canfly in load order -- element 0 is
     * type index 1, the order tadUnitLoadOrder gives -- and the demo header's
     * maxUnits.
     */
    TadUnitStateLayout tadUnitStateLayout(const std::vector<bool>& canFlyInLoadOrder, uint16_t maxUnits);

    /**
     * Decodes a 0x2c. Returns nothing unless every bit is accounted for: the
     * declared length must match the subpacket, every type index must be in
     * range, and the fields must end in the subpacket's last byte. Over the
     * thirteen-demo corpus all 7,422,196 decode.
     */
    std::optional<TadUnitState> tadDecodeUnitState(const TadBytes& subPacket, const TadUnitStateLayout& layout);

    /** A block-relative unit index as a global id, the inverse of tadOwnerBlockOfUnitId. */
    uint16_t tadUnitIdOfIndex(unsigned int block, uint16_t index, uint16_t maxUnits);

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

    /**
     * The order TA's FBI loader assigns unit types, given every unit file name
     * in the data set.
     *
     * TA enumerates units\*.FBI through the VFS, which presents every archive
     * as one merged directory, and hands each type the next index. The order is
     * the sorted file name and the numbering starts at one, so this returns the
     * names sorted and a caller indexes with tadUnitNameForTypeIndex.
     *
     * Names are the file stem, upper-cased and compared as bytes. Case folding
     * is defensive rather than tested: every stem in both data sets is already
     * upper case, and only the extension varies. Byte order is load-bearing --
     * Escalation ships ALL_L2.FBI, and '_' sorts after 'Z'.
     *
     * The evidence, over the thirteen-demo corpus (docs/TA-DEMOS.md):
     *
     *  - Scoring candidate orderings by how constant buildDuration/BuildTime is
     *    for one builder, this one gives a mean coefficient of variation of
     *    0.171 in ProTA 4.8 against 0.494 for the same sort numbered from zero,
     *    and every other offset in -8..+8 scores worse than 0.5.
     *  - It replicates in a second data set with a different unit count and a
     *    different archive layout: TA: Escalation 10.2's 549 types, spread over
     *    seven archives, score 0.359 here against 0.836 or worse at every other
     *    offset. Concatenating the archives instead of merging them scores no
     *    better than 0.773 in any of the 5040 orders.
     *  - Index 0 never appears anywhere in the corpus, which is what a 1-based
     *    index looks like.
     *  - Checked against fields the ordering was not fitted to: every one of
     *    demo 14724's type indices lands on the side its owner's header entry
     *    declares (30 of 30), and the builder's own WorkerTime predicts the
     *    duration it builds at -- 63 of 439 builder-type/product-type pairs in
     *    Escalation match ceil(BuildTime / (WorkerTime/30)) to within two ticks,
     *    against 0 of 344 for the zero-based sort, with the rest shortened by
     *    assists. Those pairs also read correctly: aircraft plants build
     *    aircraft and kbot labs build kbots.
     */
    std::vector<std::string> tadUnitLoadOrder(std::vector<std::string> unitFileStems);

    /**
     * Names the type a 0x09 refers to, or nothing if the index is out of range.
     *
     * loadOrder must have come from tadUnitLoadOrder over the same data set the
     * demo was recorded on -- a demo's 0x1a table carries the type count, so a
     * caller can check it has the right one before trusting a name.
     */
    std::optional<std::string> tadUnitNameForTypeIndex(
        const std::vector<std::string>& loadOrder,
        uint16_t typeIndex);
}
