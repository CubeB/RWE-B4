#pragma once

#include <cmath>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>

namespace rwe
{
    /**
     * Range in the original is flat: horizontal distance only, x and z in
     * whole world units, each square floored on its own and the sum compared
     * with `range * range` (TOTALA-EXE-WEAPONS.md S:127). The weapon
     * eligibility test (0x49ABB0), the fire-time range test (0x49AA80), the
     * acquisition gather (0x40AD80), the chooser's score (0x40B7B0) and the
     * chase distance (0x4034A0) all ask it this way, and none of them reads
     * the y axis. The one place that works in three axes is the ballistic arc
     * solver, which is reached only after this test has passed.
     *
     * The terms are taken in double: the difference of two floats is exact
     * there, so is its square, and `floor` is exact, so the result is the
     * same on every toolchain. The per-term flooring matters because the
     * chooser's draw is a modulo of this sum.
     */
    inline double flatRangeTerm(SimScalar a, SimScalar b)
    {
        auto d = static_cast<double>(simScalarToFloat(a)) - static_cast<double>(simScalarToFloat(b));
        return std::floor(d * d);
    }

    /** floor(dx^2) + floor(dz^2), whole world units squared. */
    inline double flatRangeScore(const SimVector& a, const SimVector& b)
    {
        return flatRangeTerm(a.x, b.x) + flatRangeTerm(a.z, b.z);
    }

    /** `<= range^2` on that score, with the range taken as the whole number of world units it is in the original. */
    inline bool withinFlatRange(const SimVector& a, const SimVector& b, SimScalar range)
    {
        auto r = std::floor(static_cast<double>(simScalarToFloat(range)));
        return flatRangeScore(a, b) <= r * r;
    }
}
