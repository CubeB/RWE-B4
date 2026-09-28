#pragma once

// What a peer has told this machine about what that peer simulates, so that a
// record naming one of its units or players can name it.
//
// TA is owner-authoritative, so a unit id is a position in its owner's block
// and only the owner's allocation of that block says which slot. A machine
// sending to a peer cannot invent the peer's ids: it has to be given them, and
// these are the two things it needs. See docs/TA-NETWORK.md, "In game".

#include <cstdint>
#include <functional>
#include <optional>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct TaPeerIds
    {
        /** The peer's global wire id for a unit it simulates, or nothing where the peer has not named it yet. */
        std::function<std::optional<uint16_t>(UnitId)> unitId;

        /** The peer's DirectPlay id for one of its players, which a 0x0c's killer is named by. */
        std::function<std::optional<uint32_t>(PlayerId)> dplayId;
    };
}
