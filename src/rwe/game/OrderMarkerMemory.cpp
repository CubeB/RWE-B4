#include "OrderMarkerMemory.h"

#include <unordered_map>

namespace rwe
{
    struct OrderMarkerMemory::Impl
    {
        struct Seen
        {
            SimScalar x;
            SimScalar z;
        };

        std::unordered_map<UnitId, Seen> lastSeen;
    };

    OrderMarkerMemory::OrderMarkerMemory() : impl(std::make_unique<Impl>()) {}

    OrderMarkerMemory::~OrderMarkerMemory() = default;

    SimVector OrderMarkerMemory::resolve(UnitId target, const SimVector& live, bool visible)
    {
        auto it = impl->lastSeen.find(target);

        if (!visible && it != impl->lastSeen.end())
        {
            // Frozen horizontally; the height still follows the target, which
            // is what the original does.
            return SimVector(it->second.x, live.y, it->second.z);
        }

        impl->lastSeen[target] = Impl::Seen{live.x, live.z};
        return live;
    }

    void OrderMarkerMemory::clear()
    {
        impl->lastSeen.clear();
    }
}
