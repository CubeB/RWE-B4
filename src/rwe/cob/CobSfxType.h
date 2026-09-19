
#pragma once

namespace rwe
{
    enum class CobSfxType
    {
        Vtol = 0,
        Thrust,
        Wake1,
        Wake2,
        ReverseWake1,
        ReverseWake2,

        WhiteSmoke = 257,
        BlackSmoke,
        /** 259. Bubbles rising from something under water; see TOTALA-EXE.md S:4. */
        SubBubbles,
    };
}
