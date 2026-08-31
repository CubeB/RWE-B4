#pragma once

#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/sim/LosTables.h>

namespace rwe
{
    /**
     * Reads Total Annihilation's gamedata/los.tdf, whose shape is
     *
     *     [TABLEINFO] { numtables=9; }
     *     [TABLE5] { numlines=8; line1=5, 0,1, 0,2, 0,3, 0,4, 0,5; ... }
     *
     * Each lineN is a step count followed by that many dx,dy pairs: the cell
     * offsets of one ray, given for a single quadrant. TABLE_r holds the fan
     * used for a sight radius of r cells, and numtables bounds the radius, so
     * the returned LosTables has entries for radii 0..numtables-1 (entry 0
     * being empty). Tables present in the file beyond numtables-1 are ignored,
     * exactly as the original does.
     *
     * Throws TdfValueException if numtables or a table is missing or malformed.
     */
    LosTables parseLosTdf(const TdfBlock& root);
}
