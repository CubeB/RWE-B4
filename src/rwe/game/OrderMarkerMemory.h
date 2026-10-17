#pragma once

#include <memory>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    /**
     * Where an order's marker, and the trail to it, are drawn for a target
     * unit the player may not be able to see (BT-184).
     *
     * The original freezes the marker's horizontal position at the last place
     * the target was seen and keeps following its height; it does not follow a
     * target through fog or under a cloak. A marker drawn at the true position
     * would show the player where an unseen enemy went.
     *
     * Presentation only: nothing here is hashed, saved or read by the
     * simulation. The memory is per target, refreshed every time the target is
     * resolved while visible; the first time a target is resolved while hidden
     * and nothing is remembered, the cache is filled from its true position
     * and then frozen, as the original does.
     *
     * The container is kept out of the header so that including this in
     * GameScene.h costs the translation units that include it nothing.
     */
    class OrderMarkerMemory
    {
    public:
        OrderMarkerMemory();
        ~OrderMarkerMemory();
        OrderMarkerMemory(const OrderMarkerMemory&) = delete;
        OrderMarkerMemory& operator=(const OrderMarkerMemory&) = delete;

        /**
         * The position to draw for `target`, whose true position is `live`.
         * `visible` is whether the local player can see the target now.
         */
        SimVector resolve(UnitId target, const SimVector& live, bool visible);

        /** Forgets everything remembered. */
        void clear();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
