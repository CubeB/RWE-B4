#pragma once

// What a live sender is launched with, and what one tick of it yields.
//
// Deliberately not in TaLiveSender.h. The simulation calls a sender's hooks
// from a header nearly everything includes, and the containers a header carries
// are paid for by most of the tree.

#include <cstdint>
#include <string>
#include <vector>
#include <rwe/sim/PlayerId.h>

namespace rwe
{
    /** What a live sender is launched with: the player whose units this machine owns on the wire, and what to encode them against. */
    struct TaLiveSenderSettings
    {
        /** The Local player. Its units take the host's owner block, and nothing else is described. */
        PlayerId sender{0};

        /** The id block size, agreed with the peer at launch: a slot is `tick % maxUnits`. */
        uint16_t maxUnits = 1000;

        /**
         * The first id block to allocate from. One for a host: a joining TA
         * takes block 0, so a host that also sat in it would have its units
         * erased on the peer and the peer's on itself. See
         * TaWireTapeSettings::firstBlock.
         */
        unsigned int firstBlock = 0;

        /** The data set's unit types in TA's load order, as `tadUnitLoadOrder` gives them. */
        std::vector<std::string> unitLoadOrder;
    };

    /**
     * One tick's subpackets, already in the order they go on the wire: the
     * unit pass (0x09, 0x12, 0x0d), the 0x2c, the projectile pass (0x0b, 0x0c),
     * then the settle's 0x28.
     *
     * One tick is a batch rather than a message because that is where the split
     * falls: the network layer puts six of them in a UDP message, thirty ticks
     * a second, which is what TA itself does.
     */
    struct TaLiveBatch
    {
        /** The simulation tick these records describe, which is the 0x2c's own serial. */
        uint32_t tick{0};

        std::vector<std::vector<uint8_t>> subPackets;
    };
}
