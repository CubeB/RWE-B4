#include "float_math.h"

#include <cmath>
#include <rwe/math/det_math.h>

namespace rwe
{
    float rweAbs(float v)
    {
        return std::abs(v);
    }

    float rweSqrt(float f)
    {
        return std::sqrt(f);
    }

    float rweCos(float v)
    {
        return detCos(v);
    }

    float rweSin(float v)
    {
        return detSin(v);
    }

    float rweAcos(float v)
    {
        return detAcos(v);
    }

    float rweAtan2(float a, float b)
    {
        return detAtan2(a, b);
    }
}
