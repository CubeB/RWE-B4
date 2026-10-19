#include "GameSimulation.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <rwe/sim/SimRandom.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    void GameSimulation::igniteFeature(FeatureId id)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return;
        }
        auto& feature = featureRef->get();
        const auto& definition = getFeatureDefinition(feature.featureName);
        if (!definition.flamable || definition.hasModel || feature.burningUntil || definition.burnFrameTicks.empty())
        {
            return;
        }

        // BT-44/48/255: refusal precedes the timer draw. Model placement and
        // the other temporary sprite states are the remaining #683 work.
        std::vector<FeatureId> active;
        unsigned int stateful = 0;
        unsigned int newest = 0;
        for (const auto& [otherId, other] : features)
        {
            if (getFeatureDefinition(other.featureName).hasModel || other.burningUntil)
            {
                ++stateful;
            }
            if (other.burningUntil)
            {
                active.push_back(otherId);
                newest = std::max(newest, other.burnOrder);
            }
        }
        if (stateful >= 2048)
        {
            return;
        }
        if (newest == std::numeric_limits<unsigned int>::max())
        {
            // Only relative active order matters; preserve it on wrap.
            std::sort(active.begin(), active.end(), [&](FeatureId a, FeatureId b) {
                const auto orderA = getFeature(a).burnOrder;
                const auto orderB = getFeature(b).burnOrder;
                return orderA != orderB ? orderA < orderB : a.value < b.value;
            });
            newest = 0;
            for (auto otherId : active)
            {
                getFeature(otherId).burnOrder = ++newest;
            }
        }
        unsigned int duration = 0;
        for (auto ticks : definition.burnFrameTicks)
        {
            if (ticks > std::numeric_limits<unsigned int>::max() - duration)
            {
                return;
            }
            duration += ticks;
        }
        if (duration == 0)
        {
            return;
        }
        const auto sparkTicks = static_cast<std::uint16_t>(static_cast<std::uint64_t>(definition.sparkTime) * 30u);
        const auto half = static_cast<unsigned int>(sparkTicks / 2u);
        const auto timer = static_cast<std::uint8_t>(half + (half < 2 ? 0u : randomBelow(rng, half)));
        feature.burnTicksRemaining = duration;
        feature.burnSparkTicksRemaining = timer;
        feature.burnOrder = newest + 1;
        feature.burningUntil = gameTime + GameTime(duration);
        feature.nextSpark = timer == 0 ? GameTime(0) : gameTime + GameTime(timer);
    }

    void GameSimulation::spreadFeatureFire(FeatureId id, const SimVector& wind)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return;
        }
        const auto position = featureRef->get().position;
        const auto& definition = getFeatureDefinition(featureRef->get().featureName);
        const auto origin = computeFootprintRegion(position, definition.footprintX, definition.footprintZ);
        const auto weaponName = toUpper(definition.burnWeapon);
        auto tryCell = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= static_cast<int>(occupiedGrid.getWidth()) || y >= static_cast<int>(occupiedGrid.getHeight()))
            {
                return;
            }
            auto candidate = occupiedGrid.get(x, y).featureId;
            if (!candidate || *candidate == id)
            {
                return;
            }
            const auto& other = getFeature(*candidate);
            const auto& otherDefinition = getFeatureDefinition(other.featureName);
            if (!otherDefinition.flamable || otherDefinition.hasModel || other.burningUntil)
            {
                return;
            }
            const auto footprint = computeFootprintRegion(other.position, otherDefinition.footprintX, otherDefinition.footprintZ);
            if (footprint.x != x || footprint.y != y)
            {
                return;
            }
            if (randomBelow(rng, 100) < otherDefinition.spreadChance)
            {
                igniteFeature(*candidate);
            }
        };
        // BT-44/255: square first, then five fixed-point wind samples. Newly
        // ignited cells immediately fail eligibility, but wait for next pass.
        for (int y = origin.y - 3; y <= origin.y + 3; ++y)
        {
            for (int x = origin.x - 3; x <= origin.x + 3; ++x)
            {
                if (x != origin.x || y != origin.y)
                {
                    tryCell(x, y);
                }
            }
        }
        std::int64_t x = static_cast<std::int64_t>(origin.x) * 65536;
        std::int64_t y = static_cast<std::int64_t>(origin.y) * 65536;
        const auto stepX = 2 * static_cast<std::int64_t>(wind.x.value * 65536.0f);
        const auto stepY = 2 * static_cast<std::int64_t>(wind.z.value * 65536.0f);
        auto wholeCell = [](std::int64_t value) {
            return (value >= 0 ? value : value - 65535) / 65536;
        };
        auto previousX = wholeCell(x);
        auto previousY = wholeCell(y);
        for (unsigned int sample = 0; sample < 5; ++sample)
        {
            x += stepX;
            y += stepY;
            const auto cellX = wholeCell(x);
            const auto cellY = wholeCell(y);
            if (cellX != previousX || cellY != previousY)
            {
                if (cellX >= 0 && cellY >= 0 && cellX < occupiedGrid.getWidth() && cellY < occupiedGrid.getHeight())
                {
                    tryCell(static_cast<int>(cellX), static_cast<int>(cellY));
                }
                previousX = cellX;
                previousY = cellY;
            }
        }
        if (auto weapon = weaponDefinitions.find(weaponName); weapon != weaponDefinitions.end())
        {
            // BT-49: a damage record, not a spawned projectile (and no launch RNG).
            Projectile blast{};
            blast.weaponType = weaponName;
            blast.owner = PlayerId(10);
            blast.position = position;
            blast.previousPosition = position;
            blast.origin = position;
            blast.damage = weapon->second.damage;
            blast.damageRadius = weapon->second.damageRadius;
            blast.edgeEffectiveness = weapon->second.edgeEffectiveness;
            applyDamageInRadius(position, blast.damageRadius, blast, "feature_burn");
        }
    }

    void GameSimulation::updateBurningFeatures(const SimVector& wind)
    {
        std::vector<FeatureId> burning;
        for (const auto& [id, feature] : features)
        {
            if (feature.burningUntil)
            {
                burning.push_back(id);
            }
        }
        std::sort(burning.begin(), burning.end(), [&](FeatureId a, FeatureId b) {
            const auto orderA = getFeature(a).burnOrder;
            const auto orderB = getFeature(b).burnOrder;
            return orderA != orderB ? orderA > orderB : a.value > b.value;
        });
        for (auto id : burning)
        {
            auto featureRef = tryGetFeature(id);
            if (!featureRef)
            {
                continue;
            }
            auto& feature = featureRef->get();
            if (!feature.burningUntil)
            {
                continue;
            }
            if (feature.burnTicksRemaining == 0 || --feature.burnTicksRemaining == 0)
            {
                replaceFeature(id, getFeatureDefinition(feature.featureName).featureBurnt);
                continue;
            }
            feature.burningUntil = gameTime + GameTime(feature.burnTicksRemaining);
            if (feature.burnSparkTicksRemaining != 0)
            {
                --feature.burnSparkTicksRemaining;
                feature.nextSpark = feature.burnSparkTicksRemaining == 0 ? GameTime(0) : gameTime + GameTime(feature.burnSparkTicksRemaining);
                if (feature.burnSparkTicksRemaining == 0)
                {
                    spreadFeatureFire(id, wind);
                }
            }
        }
    }

}
