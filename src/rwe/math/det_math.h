#pragma once

namespace rwe
{
    /**
     * sin, cos, atan, atan2, acos and hypot that give the same bits on every
     * compiler, C runtime and CPU.
     *
     * The C library's versions do not. Measured 2026-10-13 over 200000 inputs,
     * MinGW's float sin/cos disagree with glibc's on about 1.4% of them, atan
     * on 5%, acos on 8.5% and atan2 on 16%, and an MSVC build disagrees with
     * both in its own places. A simulation is lockstep: one ulp in one unit's
     * position is a desync a few hundred ticks later, and two peers on
     * different builds of the same commit reached tick 6140 before it showed.
     *
     * These use only +, -, *, / and sqrt, which IEEE 754 requires to be
     * correctly rounded, in double, and round to float once at the end. They
     * are as accurate as the C library's (within one float ulp, usually
     * correctly rounded), which is all the simulation ever needed; what they
     * add is that the answer is a property of the input, not of the machine.
     *
     * Everything the simulation computes with a transcendental function goes
     * through here: SimScalar's and SimAngle's, the COB runtime's, and the
     * matrix rotations that pieces are posed with. CMakeLists.txt turns off
     * floating-point contraction for the same reason -- a fused multiply-add is
     * a different rounding.
     */
    float detSin(float x);
    float detCos(float x);
    float detAtan(float x);
    /** The angle of the point (x, y), in (-pi, pi], with C's conventions for zeros, infinities and NaN. */
    float detAtan2(float y, float x);
    float detAcos(float x);
    float detHypot(float a, float b);
}
