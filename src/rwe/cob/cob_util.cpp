#include "cob_util.h"
#include <cmath>
#include <rwe/cob/CobPosition.h>
#include <rwe/fixed_point.h>
#include <rwe/math/det_math.h>
#include <rwe/math/rwe_math.h>

namespace rwe
{
    uint32_t cobPackCoords(CobPosition x, CobPosition z)
    {
        // The original adds: the high word of the 16.16 X is left in the top
        // half and the arithmetic shift of Z by 16 is added to it (0x480811,
        // 0x4808C2). A negative Z is all ones in the top half, so it borrows
        // one from X; the decoder gives it back. X times 65536 plus Z is
        // exact for any signed X and Z within sixteen bits, where an OR is
        // right only for a non-negative Z (BT-76).
        auto intX = static_cast<int16_t>(x.toInt());
        auto intZ = static_cast<int16_t>(z.toInt());
        return (static_cast<uint32_t>(static_cast<int32_t>(intX)) << 16) + static_cast<uint32_t>(static_cast<int32_t>(intZ));
    }

    std::pair<CobPosition, CobPosition> cobUnpackCoords(uint32_t xz)
    {
        // The top half and the signed low half; when the low half is negative
        // 0x10000 is added to the top half before use, which cancels the
        // borrow the encoder made (0x480965, 0x480994, 0x480A0D).
        auto low = static_cast<int16_t>(static_cast<uint16_t>(xz & 0xffff));
        uint32_t top = xz;
        if (low < 0)
        {
            top += 0x10000u;
        }
        auto x = CobPosition::fromInt(static_cast<int16_t>(static_cast<uint16_t>(top >> 16)));
        auto z = CobPosition::fromInt(low);
        return std::make_pair(x, z);
    }

    int cobAtan(int a, int b)
    {
        auto result = RadiansAngle::fromUnwrappedAngle(detAtan2(fromFixedPoint(a), fromFixedPoint(b)));
        return static_cast<int>(toCobAngle(result).value);
    }

    CobPosition cobHypot(CobPosition a, CobPosition b)
    {
        return CobPosition::fromFloat(detHypot(a.toFloat(), b.toFloat()));
    }

    RadiansAngle toRadians(CobAngle angle)
    {
        return RadiansAngle::fromUnwrappedAngle(static_cast<float>(angle.value) * (Pif / 32768.0f));
    }

    CobAngle toCobAngle(RadiansAngle angle)
    {
        return CobAngle(static_cast<uint16_t>(std::round(angle.value * (32768.0f / Pif))));
    }
}
