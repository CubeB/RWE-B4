#pragma once

// Encoders for the event subpackets tad_events.h decodes: each is that
// decoder's inverse, and each writes the length tadExpectedSubPacketSize
// expects for its code.
//
// They exist for the recorder (M3 of the demo-output plan): a demo RWE writes
// has to be readable by the tools that read TA's, so the wire format here is
// the decoders' layout read backwards and the tests hold it there. Decoding a
// real subpacket from the corpus and encoding it again gives the bytes back,
// and any subpacket of the right code and fixed length round-trips.
//
// 0x2c is the one that is not a fixed layout: it is a bit stream whose byte
// length is patched in after the body. See tadEncodeUnitState.
//
// See docs/TA-DEMOS.md.

#include <cstdint>
#include <rwe/io/tad/tad_events.h>
#include <rwe/io/tad/tad_util.h>

namespace rwe
{
    /**
     * 0x09, 23 bytes. The type index is the 1-based load-order index, so the
     * caller needs the data set's tadUnitLoadOrder; the id is the nanoframe's,
     * not its builder's.
     */
    TadBytes tadEncodeBuildStarted(const TadBuildStarted& buildStarted);

    /**
     * 0x0b, 9 bytes. The trailing field goes out as given: what it counts is
     * not settled (TadDamage::unknown), so there is nothing to recompute it
     * from.
     */
    TadBytes tadEncodeDamage(const TadDamage& damage);

    /**
     * 0x0c, 11 bytes. `causeAndLevel` goes out whole, so the cause and the
     * corpse level are packed by the caller exactly as TadDeath unpacks them.
     */
    TadBytes tadEncodeDeath(const TadDeath& death);

    /**
     * 0x0d, 36 bytes. `weaponSlot` is the 0-based index into the shooter's FBI
     * weapons, and `targetId` is zero where the shot was not aimed at a unit.
     */
    TadBytes tadEncodeShot(const TadShot& shot);

    /**
     * 0x10, 22 bytes. The script index is an index into the unit's own COB,
     * the way the emitting side at 0x451DF0 writes it.
     */
    TadBytes tadEncodeScriptCall(const TadScriptCall& scriptCall);

    /** 0x12, 5 bytes. */
    TadBytes tadEncodeBuildFinished(const TadBuildFinished& buildFinished);

    /** 0x19, 3 bytes. */
    TadBytes tadEncodeSpeed(const TadSpeed& speed);

    /**
     * 0x28, 58 bytes. All ten floats are written; the 17-byte prefix is opaque
     * and goes out as given.
     */
    TadBytes tadEncodeResourceStats(const TadResourceStats& stats);

    /**
     * 0x2c, variable length: the inverse of tadDecodeUnitState.
     *
     * The body is a bit stream -- least significant bit first into
     * little-endian 32-bit words -- and its byte count is patched into bytes
     * 1-2 after the body is written, which is the length
     * tadExpectedSubPacketSize reads. The three header bytes are outside the
     * bits.
     *
     * What the wire does not carry, the layout supplies: the width of a type
     * index and whether each type flies, which decides whose mover serialiser
     * an update gets -- the receiver picks by type too (0x43DC00), not from
     * anything in the entry. A state the layout cannot describe -- no room for
     * a type index, a type index of zero or out of range, a mover variant the
     * type does not use -- yields an empty result rather than a stream no
     * reader could walk.
     *
     * `sync->index` is not transmitted: it is `tick % maxUnits`, which the
     * caller sets for clarity. A sync written without a `speed` is told from
     * one with it the way TA's receiver tells them apart, from what is left of
     * the subpacket.
     */
    TadBytes tadEncodeUnitState(const TadUnitState& state, const TadUnitStateLayout& layout);
}
