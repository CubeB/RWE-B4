#include "FeatureBlast.h"
#include "GameSimulation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rwe
{
    namespace
    {
        constexpr std::size_t FeatureListCapacity = 64;

        bool listedOrRecord(std::vector<FeatureId>& seen, FeatureId id)
        {
            if (std::find(seen.begin(), seen.end(), id) != seen.end())
            {
                return true;
            }
            if (seen.size() < FeatureListCapacity)
            {
                seen.push_back(id);
            }
            return false;
        }
    }

    DiscreteRect featureBlastWindow(const MapTerrain& terrain, const SimVector& position, SimScalar radius, int width, int height)
    {
        const auto origin = terrain.heightmapIndexToWorldCorner(0, 0);
        const auto margin = static_cast<std::int64_t>(std::floor(radius.value)) / 16 + 1;
        const auto x = static_cast<std::int64_t>(std::floor((position.x - origin.x).value)) / 16;
        const auto y = static_cast<std::int64_t>(std::floor((position.z - origin.z).value)) / 16;
        const auto left = static_cast<int>(std::clamp<std::int64_t>(x - margin, 0, width));
        const auto right = static_cast<int>(std::clamp<std::int64_t>(x + margin, left, width));
        const auto top = static_cast<int>(std::clamp<std::int64_t>(y - margin, 0, height));
        const auto bottom = static_cast<int>(std::clamp<std::int64_t>(y + margin, top, height));
        return DiscreteRect(left, top, right - left, bottom - top);
    }

    SimVector featureBlastPoint(const MapTerrain& terrain, Point cell, int width, int height)
    {
        // 64-bit arithmetic also bounds malformed external footprint values.
        const auto x = 16 * static_cast<std::int64_t>(cell.x) + 8 * static_cast<std::int64_t>(width);
        const auto z = 16 * static_cast<std::int64_t>(cell.y) + 8 * static_cast<std::int64_t>(height);
        const auto column = x / 16;
        const auto row = z / 16;
        const auto& heights = terrain.getHeightMap();
        int y = -1;
        if (column >= 0 && row >= 0 && column + 1 < heights.getWidth() && row + 1 < heights.getHeight())
        {
            const auto s = static_cast<int>(column);
            const auto t = static_cast<int>(row);
            const auto u = static_cast<int>(x % 16);
            const auto v = static_cast<int>(z % 16);
            const auto a = static_cast<int>(heights.get(s, t));
            const auto b = static_cast<int>(heights.get(s + 1, t));
            const auto c = static_cast<int>(heights.get(s, t + 1));
            const auto d = static_cast<int>(heights.get(s + 1, t + 1));
            const auto lower = a + (b - a) * u / 16;
            const auto upper = c + (d - c) * u / 16;
            y = lower + (upper - lower) * v / 16;
        }
        const auto origin = terrain.heightmapIndexToWorldCorner(0, 0);
        return SimVector(origin.x + SimScalar(static_cast<float>(x)), SimScalar(y), origin.z + SimScalar(static_cast<float>(z)));
    }

    void applyFeatureBlastCell(GameSimulation& sim, Point cell, const SimVector& position, SimScalar radius,
        const Projectile& projectile, std::vector<FeatureId>& seen)
    {
        const auto id = sim.occupiedGrid.get(cell.x, cell.y).featureId;
        if (!id)
        {
            return;
        }
        auto featureRef = sim.tryGetFeature(*id);
        if (!featureRef)
        {
            return;
        }
        auto& feature = featureRef->get();
        const auto& definition = sim.getFeatureDefinition(feature.featureName);
        // BT-257, owner-approved deterministic policy (2026-10-19): active
        // sprites are immune and consume no address-dependent bookkeeping.
        if (!definition.hasModel && feature.animation != FeatureAnimation::None)
        {
            return;
        }
        auto point = featureBlastPoint(sim.terrain, cell, definition.footprintX, definition.footprintZ);
        if (definition.hasModel)
        {
            const auto footprint = sim.computeFootprintRegion(feature.position, definition.footprintX, definition.footprintZ);
            if (footprint.x == cell.x && footprint.y == cell.y)
            {
                point = feature.position;
            }
        }
        const auto wholeRadius = SimScalar(std::floor(radius.value));
        // floor(distance) < wholeRadius is equivalent to this strict test.
        if (wholeRadius <= 0_ss || (point - position).lengthSquared() >= wholeRadius * wholeRadius)
        {
            return;
        }
        // Failed reach must not consume the identity; qualifying indestructible
        // features do consume it. Once full, the original keeps processing.
        if (listedOrRecord(seen, *id) || definition.indestructible)
        {
            return;
        }
        const auto weapon = sim.weaponDefinitions.find(projectile.weaponType);
        if (weapon != sim.weaponDefinitions.end() && weapon->second.fireStarter != 0 && definition.flamable && !definition.hasModel)
        {
            // BT-259: failed allocation/missing animation still replaces damage.
            sim.igniteFeature(*id);
            return;
        }
        if (auto replacement = sim.damageFeature(*id, projectile.getDamage(std::string())))
        {
            listedOrRecord(seen, *replacement);
        }
    }
}
