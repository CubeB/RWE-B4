#include "GameSimulation.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <rwe/sim/SimRandom.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    unsigned int GameSimulation::countStatefulFeatures() const
    {
        unsigned int count = 0;
        for (const auto& [id, feature] : features)
        {
            if (getFeatureDefinition(feature.featureName).hasModel || feature.animation != FeatureAnimation::None)
            {
                ++count;
            }
        }
        return count;
    }

    bool GameSimulation::startFeatureAnimation(FeatureId id, FeatureAnimation animation)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef || animation == FeatureAnimation::None)
        {
            return false;
        }
        auto& feature = featureRef->get();
        const auto& definition = getFeatureDefinition(feature.featureName);
        if (definition.hasModel || feature.animation != FeatureAnimation::None || countStatefulFeatures() >= 2048)
        {
            return false;
        }
        unsigned int duration = 0;
        for (auto ticks : definition.animationFrames(animation))
        {
            if (ticks > std::numeric_limits<unsigned int>::max() - duration)
            {
                return false;
            }
            duration += ticks;
        }
        if (duration == 0)
        {
            return false;
        }
        std::vector<FeatureId> active;
        unsigned int newest = 0;
        for (const auto& [otherId, other] : features)
        {
            if (other.animation != FeatureAnimation::None)
            {
                active.push_back(otherId);
                newest = std::max(newest, other.animationOrder);
            }
        }
        if (newest == std::numeric_limits<unsigned int>::max())
        {
            std::sort(active.begin(), active.end(), [&](FeatureId a, FeatureId b) {
                const auto orderA = getFeature(a).animationOrder;
                const auto orderB = getFeature(b).animationOrder;
                return orderA != orderB ? orderA < orderB : a.value < b.value;
            });
            newest = 0;
            for (auto otherId : active)
            {
                getFeature(otherId).animationOrder = ++newest;
            }
        }
        // BT-261/263: animation starts without changing footprint or blocking.
        feature.animation = animation;
        feature.animationTicksRemaining = duration;
        feature.animationOrder = newest + 1;
        return true;
    }

    void GameSimulation::igniteFeature(FeatureId id)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return;
        }
        auto& feature = featureRef->get();
        const auto& definition = getFeatureDefinition(feature.featureName);
        if (!definition.flamable || !startFeatureAnimation(id, FeatureAnimation::Burn))
        {
            return;
        }
        // BT-255/259: refusal precedes the timer draw.
        const auto sparkTicks = static_cast<std::uint16_t>(static_cast<std::uint64_t>(definition.sparkTime) * 30u);
        const auto half = static_cast<unsigned int>(sparkTicks / 2u);
        const auto timer = static_cast<std::uint8_t>(half + (half < 2 ? 0u : randomBelow(rng, half)));
        feature.burnSparkTicksRemaining = timer;
        feature.burningUntil = gameTime + GameTime(feature.animationTicksRemaining);
        feature.nextSpark = timer == 0 ? GameTime(0) : gameTime + GameTime(timer);
        const auto origin = computeFootprintRegion(feature.position, definition.footprintX, definition.footprintZ);
        // BT-262: known X/Z is the origin corner. Existing audio limitations
        // remain #583; no original uninitialized-Y audibility is claimed.
        events.push_back(FeatureIgnitedEvent{terrain.heightmapIndexToWorldCorner(origin.x, origin.y)});
    }

    std::optional<FeatureId> GameSimulation::damageFeature(FeatureId id, unsigned int damage)
    {
        auto featureRef = tryGetFeature(id);
        if (!featureRef)
        {
            return std::nullopt;
        }
        auto& feature = featureRef->get();
        const auto& definition = getFeatureDefinition(feature.featureName);
        if (definition.indestructible || (!definition.hasModel && feature.animation != FeatureAnimation::None))
        {
            return std::nullopt;
        }
        if (damage < feature.hitPoints)
        {
            feature.hitPoints -= damage;
            return std::nullopt;
        }
        if (!definition.hasModel && !definition.deathFrameTicks.empty())
        {
            // Failed allocation keeps the pre-hit health (BT-261).
            startFeatureAnimation(id, FeatureAnimation::Death);
        }
        else
        {
            return replaceFeature(id, definition.featureDead);
        }
        return std::nullopt;
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
            if (!otherDefinition.flamable || otherDefinition.hasModel || other.animation != FeatureAnimation::None)
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

    void GameSimulation::updateFeatureAnimations(const SimVector& wind)
    {
        std::vector<FeatureId> burning;
        for (const auto& [id, feature] : features)
        {
            if (feature.animation != FeatureAnimation::None)
            {
                burning.push_back(id);
            }
        }
        std::sort(burning.begin(), burning.end(), [&](FeatureId a, FeatureId b) {
            const auto orderA = getFeature(a).animationOrder;
            const auto orderB = getFeature(b).animationOrder;
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
            if (feature.animation == FeatureAnimation::None)
            {
                continue;
            }
            if (feature.animationTicksRemaining == 0 || --feature.animationTicksRemaining == 0)
            {
                const auto& definition = getFeatureDefinition(feature.featureName);
                const auto successor = feature.animation == FeatureAnimation::Burn ? definition.featureBurnt
                    : feature.animation == FeatureAnimation::Reclaim ? definition.featureReclamate : definition.featureDead;
                replaceFeature(id, successor);
                continue;
            }
            if (feature.animation != FeatureAnimation::Burn)
            {
                continue;
            }
            feature.burningUntil = gameTime + GameTime(feature.animationTicksRemaining);
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
