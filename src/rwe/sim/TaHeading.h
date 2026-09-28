#pragma once

#include <cstdint>
#include <rwe/sim/SimAngle.h>

namespace rwe
{
    // TA's heading faces (-sin h, -cos h) (TOTALA-EXE-MISSIONS.md, `Dir`) and
    // RWE's faces (sin h, cos h), so the two name one facing half a turn apart.

    inline SimAngle simAngleFromTaYaw(int16_t yaw)
    {
        return SimAngle(static_cast<uint16_t>(static_cast<uint16_t>(yaw) + 0x8000u));
    }

    inline int16_t taYawFromSimAngle(SimAngle angle)
    {
        return static_cast<int16_t>(static_cast<uint16_t>(angle.value + 0x8000u));
    }
}
