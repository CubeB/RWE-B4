#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace rwe
{
    enum class FeatureAnimation : unsigned int
    {
        None,
        Burn,
        Death,
        Reclaim
    };

    /** BT-255/263: play once; an expired shadow supplies no frame. */
    inline std::optional<std::size_t> featureAnimationFrame(const std::vector<unsigned int>& durations, unsigned int elapsed)
    {
        for (std::size_t i = 0; i < durations.size(); ++i)
        {
            if (elapsed < durations[i])
            {
                return i;
            }
            elapsed -= durations[i];
        }
        return std::nullopt;
    }
}
