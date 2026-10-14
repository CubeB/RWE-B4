#pragma once

#include <cmath>
#include <cstdint>
#include <rwe/sim/UnitOrder.h>
#include <type_traits>
#include <vector>

namespace rwe
{
    // TOTALA-EXE-INTERFACE S:136: inclusive square tolerance, ignoring height.
    inline bool queuePositionsMatch(const SimVector& a, const SimVector& b)
    {
        return rweAbs(a.x - b.x) <= 16_ss && rweAbs(a.z - b.z) <= 16_ss;
    }

    template <typename T>
    bool queueTargetsMatch(const T& existing, const T& requested)
    {
        return existing == requested;
    }

    template <typename... T>
    bool queueTargetsMatch(const std::variant<T...>& existing, const std::variant<T...>& requested)
    {
        if (existing.index() != requested.index())
        {
            return false;
        }
        return std::visit([&](const auto& value) {
            using V = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<V, SimVector>)
            {
                return queuePositionsMatch(std::get<V>(existing), value);
            }
            else
            {
                return std::get<V>(existing) == value;
            }
        },
            requested);
    }

    inline bool ordersMatchForQueueToggle(const UnitOrder& existing, const UnitOrder& requested)
    {
        if (existing.index() != requested.index())
        {
            return false;
        }
        return std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            const auto& old = std::get<T>(existing);
            if constexpr (requires { value.destination; })
            {
                return queuePositionsMatch(old.destination, value.destination);
            }
            else if constexpr (requires { value.position; })
            {
                // S:136: a build's unit type is deliberately not compared.
                return queuePositionsMatch(old.position, value.position);
            }
            else if constexpr (requires { value.target; })
            {
                return queueTargetsMatch(old.target, value.target);
            }
            else
            {
                return true;
            }
        },
            requested);
    }

    inline SimVector formationDestination(const std::vector<SimVector>& selection, const SimVector& receiver, const SimVector& click)
    {
        if (selection.empty())
        {
            return click;
        }
        int64_t x = 0, z = 0;
        for (const auto& position : selection)
        {
            x += static_cast<int64_t>(std::floor(position.x.value));
            z += static_cast<int64_t>(std::floor(position.z.value));
        }
        auto count = static_cast<int64_t>(selection.size());
        // Integer division truncates the centroid; each receiver keeps its fraction.
        SimVector centre(SimScalar(static_cast<float>(x / count)), click.y, SimScalar(static_cast<float>(z / count)));
        double dx = static_cast<double>(receiver.x.value) - centre.x.value;
        double dz = static_cast<double>(receiver.z.value) - centre.z.value;
        if (std::floor(dx * dx) + std::floor(dz * dz) > 3000.0 * count)
        {
            return click;
        }
        return SimVector(click.x + receiver.x - centre.x, click.y, click.z + receiver.z - centre.z);
    }
}
