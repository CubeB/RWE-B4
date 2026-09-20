#pragma once

#include <cstddef>
#include <cstring>
#include <nlohmann/json.hpp>
#include <optional>
#include <rwe/grid/DiscreteRect.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/ProjectileId.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace rwe
{
    /**
     * The json plumbing every save walk shares.
     *
     * The unit field table needs the same primitives the hand-written walks
     * used, so rather than copying them the primitives moved here and
     * save_util.cpp now includes this file instead of owning its own copies.
     */
    using SaveContextMapping = std::unordered_map<UnitId, uint32_t>;

    struct SaveContext
    {
        std::unordered_map<UnitId, uint32_t> units;
        std::unordered_map<FeatureId, uint32_t> features;
        std::unordered_map<ProjectileId, uint32_t> projectiles;
    };

    struct LoadContext
    {
        std::vector<UnitId> units;
        std::vector<FeatureId> features;
        std::vector<ProjectileId> projectiles;
    };

    inline nlohmann::json saveFloat(float f)
    {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        return bits;
    }

    inline float loadFloat(const nlohmann::json& j)
    {
        auto bits = j.get<uint32_t>();
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }

    inline nlohmann::json saveSimScalar(SimScalar s)
    {
        return saveFloat(s.value);
    }

    inline SimScalar loadSimScalar(const nlohmann::json& j)
    {
        return SimScalar(loadFloat(j));
    }

    inline nlohmann::json saveEnergy(Energy e)
    {
        return saveFloat(e.value);
    }

    inline Energy loadEnergy(const nlohmann::json& j)
    {
        return Energy(loadFloat(j));
    }

    inline nlohmann::json saveMetal(Metal m)
    {
        return saveFloat(m.value);
    }

    inline Metal loadMetal(const nlohmann::json& j)
    {
        return Metal(loadFloat(j));
    }

    inline nlohmann::json saveSimVector(const SimVector& v)
    {
        return nlohmann::json::array({saveFloat(v.x.value), saveFloat(v.y.value), saveFloat(v.z.value)});
    }

    inline SimVector loadSimVector(const nlohmann::json& j)
    {
        return SimVector(SimScalar(loadFloat(j.at(0))), SimScalar(loadFloat(j.at(1))), SimScalar(loadFloat(j.at(2))));
    }

    inline nlohmann::json saveSimAngle(SimAngle a)
    {
        return a.value;
    }

    inline SimAngle loadSimAngle(const nlohmann::json& j)
    {
        return SimAngle(j.get<uint16_t>());
    }

    inline nlohmann::json saveGameTime(GameTime t)
    {
        return t.value;
    }

    inline GameTime loadGameTime(const nlohmann::json& j)
    {
        return GameTime(j.get<unsigned int>());
    }

    template <typename T, typename F>
    nlohmann::json saveOptional(const std::optional<T>& o, F f)
    {
        return o ? nlohmann::json(f(*o)) : nlohmann::json();
    }

    template <typename F>
    auto loadOptional(const nlohmann::json& j, F f) -> std::optional<decltype(f(j))>
    {
        if (j.is_null())
        {
            return std::nullopt;
        }
        return f(j);
    }

    template <typename E>
    nlohmann::json saveEnum(E e)
    {
        return static_cast<std::underlying_type_t<E>>(e);
    }

    template <typename E>
    E loadEnum(const nlohmann::json& j)
    {
        return static_cast<E>(j.get<std::underlying_type_t<E>>());
    }

    inline nlohmann::json saveDiscreteRect(const DiscreteRect& r)
    {
        return nlohmann::json{
            {"x", r.x},
            {"y", r.y},
            {"width", r.width},
            {"height", r.height}};
    }

    inline DiscreteRect loadDiscreteRect(const nlohmann::json& j)
    {
        return DiscreteRect(j.at("x").get<int>(), j.at("y").get<int>(), j.at("width").get<int>(), j.at("height").get<int>());
    }

    namespace detail
    {
        constexpr const char* StaleRef = "stale";
        constexpr unsigned int StaleIdValue = 0xFFu;
    }

    template <typename Id>
    nlohmann::json saveIdRef(Id id, const std::unordered_map<Id, uint32_t>& table)
    {
        auto it = table.find(id);
        if (it == table.end())
        {
            return detail::StaleRef;
        }
        return it->second;
    }

    template <typename Id>
    Id loadIdRef(const nlohmann::json& j, const std::vector<Id>& table)
    {
        if (j.is_string())
        {
            return Id(detail::StaleIdValue);
        }
        return table.at(j.get<uint32_t>());
    }

    inline nlohmann::json saveUnitIdRef(UnitId id, const SaveContext& ctx)
    {
        return saveIdRef(id, ctx.units);
    }

    inline UnitId loadUnitIdRef(const nlohmann::json& j, const LoadContext& ctx)
    {
        return loadIdRef(j, ctx.units);
    }

    inline nlohmann::json saveFeatureIdRef(FeatureId id, const SaveContext& ctx)
    {
        return saveIdRef(id, ctx.features);
    }

    inline FeatureId loadFeatureIdRef(const nlohmann::json& j, const LoadContext& ctx)
    {
        return loadIdRef(j, ctx.features);
    }

    inline nlohmann::json saveProjectileIdRef(ProjectileId id, const SaveContext& ctx)
    {
        return saveIdRef(id, ctx.projectiles);
    }

    inline ProjectileId loadProjectileIdRef(const nlohmann::json& j, const LoadContext& ctx)
    {
        return loadIdRef(j, ctx.projectiles);
    }
}
