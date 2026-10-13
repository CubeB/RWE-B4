#include "SimScalar.h"

#include <algorithm>
#include <cmath>
#include <rwe/math/det_math.h>

namespace rwe
{
    SimScalar rweAbs(SimScalar s)
    {
        return s > 0_ss ? s : -s;
    }

    SimScalar rweSqrt(SimScalar s)
    {
        return SimScalar(std::sqrt(s.value));
    }

    SimScalar rweMax(SimScalar a, SimScalar b)
    {
        return SimScalar(std::max(a.value, b.value));
    }

    SimScalar rweMin(SimScalar a, SimScalar b)
    {
        return SimScalar(std::min(a.value, b.value));
    }

    SimScalar rweAtan2(SimScalar a, SimScalar b)
    {
        return SimScalar(detAtan2(a.value, b.value));
    }

    SimScalar rweCos(SimScalar v)
    {
        return SimScalar(detCos(v.value));
    }

    SimScalar rweSin(SimScalar v)
    {
        return SimScalar(detSin(v.value));
    }

    SimScalar rweAcos(SimScalar v)
    {
        return SimScalar(detAcos(v.value));
    }
}
