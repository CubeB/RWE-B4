#pragma once

#include <cstdint>
#include <deque>
#include <nlohmann/json.hpp>
#include <rwe/collections/VectorMap.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GamePlayerInfo.h>
#include <rwe/sim/MapFeature.h>
#include <rwe/sim/PlayerVisibility.h>
#include <rwe/sim/Projectile.h>
#include <rwe/sim/UnitState.h>
#include <rwe/util/OpaqueId.h>
#include <rwe/util/match.h>
#include <string>
#include <utility>

namespace rwe
{
    struct GameSimulation;

    nlohmann::json dumpJson(float f);

    nlohmann::json dumpJson(bool b);

    nlohmann::json dumpJson(uint32_t i);

    nlohmann::json dumpJson(int32_t i);

    nlohmann::json dumpJson(const std::string& s);

    nlohmann::json dumpJson(const char* s);

    nlohmann::json dumpJson(const GamePlayerInfo& p);

    struct MissionRules;
    nlohmann::json dumpJson(const MissionRules& m);

    struct MissionScripts;
    nlohmann::json dumpJson(const MissionScripts& m);

    nlohmann::json dumpJson(const UnitState& u);

    /** Only the stockpile counters, to match what the hash covers. */
    nlohmann::json dumpJson(const UnitWeapon& w);

    nlohmann::json dumpJson(const UnitPhysicsInfoGround& p);
    nlohmann::json dumpJson(const UnitPhysicsInfoAir& p);
    nlohmann::json dumpJson(const AirMovementStateTakingOff& p);
    nlohmann::json dumpJson(const AirMovementStateLanding& p);
    nlohmann::json dumpJson(const AirMovementStateFlying& p);
    nlohmann::json dumpJson(const AirMovementStateAttackRun& p);
    nlohmann::json dumpJson(const AirMovementStateHoverAttack& p);

    nlohmann::json dumpJson(const AirMovementStateDogfight& p);

    nlohmann::json dumpJson(const UnitState::AirWorkOrbitState& s);

    /**
     * The order queue, and the orders in it.
     *
     * Each of these covers exactly what this order's `computeHashOf` covers
     * and nothing else. That is the dump's standing rule -- a field the hash
     * reads and the dump does not is a desync you cannot bisect -- and the
     * order queue is where it matters most, because capture progress lives on
     * a CaptureOrder rather than on the unit (section 96).
     */
    nlohmann::json dumpJson(const AttackLeash& l);
    nlohmann::json dumpJson(const MoveOrder& o);
    nlohmann::json dumpJson(const AttackOrder& o);
    nlohmann::json dumpJson(const BuildOrder& o);
    nlohmann::json dumpJson(const BuggerOffOrder& o);
    nlohmann::json dumpJson(const CompleteBuildOrder& o);
    nlohmann::json dumpJson(const GuardOrder& o);
    nlohmann::json dumpJson(const ReclaimOrder& o);
    nlohmann::json dumpJson(const RepairOrder& o);
    nlohmann::json dumpJson(const PatrolOrder& o);
    nlohmann::json dumpJson(const CaptureOrder& o);
    nlohmann::json dumpJson(const LoadOrder& o);
    nlohmann::json dumpJson(const UnloadOrder& o);
    nlohmann::json dumpJson(const DgunOrder& o);
    nlohmann::json dumpJson(const LandOnAirBaseOrder& o);
    nlohmann::json dumpJson(const ResurrectOrder& o);

    nlohmann::json dumpJson(const UnitState::AirLoiterState& s);

    nlohmann::json dumpJson(const SteeringInfo& s);

    nlohmann::json dumpJson(const Vector3f& v);

    template <typename Val>
    nlohmann::json dumpJson(const Vector3x<Val>& v)
    {
        return nlohmann::json{
            {"x", dumpJson(v.x)},
            {"y", dumpJson(v.y)},
            {"z", dumpJson(v.z)},
        };
    }

    /**
     * Every field computeHashOf(const MapFeature&) reads, and in its order.
     *
     * The simulation's hash walks the feature list, so a burning tree whose
     * clock has drifted, or a wreck one peer has reclaimed further than
     * another, desyncs the game. Until this existed the dump had nothing to
     * say about any of it: it wrote players, units and projectiles, and the
     * features it was hashing were simply absent, so that whole class of
     * mismatch came with an empty diff.
     */
    nlohmann::json dumpJson(const MapFeature& f);

    nlohmann::json dumpJson(const Projectile& projectile);

    nlohmann::json dumpJson(const UnitBehaviorStateIdle&);

    nlohmann::json dumpJson(const UnitBehaviorStateBuilding&);

    nlohmann::json dumpJson(const UnitBehaviorStateReclaiming&);
    nlohmann::json dumpJson(const UnitBehaviorStateResurrecting&);

    nlohmann::json dumpJson(const UnitBehaviorStateCreatingUnit&);

    nlohmann::json dumpJson(const UnitCreationStatusPending&);
    nlohmann::json dumpJson(const UnitCreationStatusDone&);
    nlohmann::json dumpJson(const UnitCreationStatusFailed&);

    nlohmann::json dumpJson(const UnitState::LifeStateAlive&);
    nlohmann::json dumpJson(const UnitState::LifeStateDead&);

    nlohmann::json dumpJson(const NavigationGoalLandingLocation& m);

    nlohmann::json dumpJson(const NavigationStateIdle& m);
    nlohmann::json dumpJson(const NavigationStateMoving& m);
    nlohmann::json dumpJson(const NavigationStateMovingToLandingSpot& m);
    nlohmann::json dumpJson(const NavigationStateInfo& m);

    nlohmann::json dumpJson(const DiscreteRect& r);

    /** The explored grid as a flat array of its per-cell group masks. */
    nlohmann::json dumpJson(const Grid<ExploredMask>& grid);

    nlohmann::json dumpJson(const GameSimulation& simulation);

    template <typename T, typename Tag>
    nlohmann::json dumpJson(const OpaqueId<T, Tag>& id)
    {
        return dumpJson(id.value);
    }

    template <typename T>
    nlohmann::json dumpJson(const std::optional<T>& o)
    {
        return o ? dumpJson(*o) : nlohmann::json();
    }

    template <typename T>
    nlohmann::json dumpJson(const std::vector<T>& v)
    {
        nlohmann::json j;
        for (const auto& e : v)
        {
            j.push_back(dumpJson(e));
        }
        return j;
    }

    template <typename T>
    nlohmann::json dumpJson(const std::deque<T>& v)
    {
        nlohmann::json j;
        for (const auto& e : v)
        {
            j.push_back(dumpJson(e));
        }
        return j;
    }

    template <typename T, std::size_t N>
    nlohmann::json dumpJson(const std::array<T, N>& a)
    {
        nlohmann::json j;
        for (const auto& e : a)
        {
            j.push_back(dumpJson(e));
        }
        return j;
    }

    template <typename A, typename B>
    nlohmann::json dumpJson(const std::pair<A, B>& p)
    {
        return nlohmann::json{
            {"first", dumpJson(p.first)},
            {"second", dumpJson(p.second)}};
    }

    template <typename T, typename Tag>
    nlohmann::json dumpJson(const VectorMap<T, Tag>& v)
    {
        nlohmann::json j;
        for (const auto& e : v)
        {
            j.push_back(dumpJson(e));
        }
        return j;
    }

    template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
    nlohmann::json dumpJson(const T& en)
    {
        return nlohmann::json(static_cast<std::underlying_type_t<T>>(en));
    }

    template <typename... Ts>
    nlohmann::json dumpJson(const std::variant<Ts...>& v)
    {
        nlohmann::json j;
        j["variant"] = v.index();
        j["data"] = match(v, [](const auto& x) { return dumpJson(x); });
        return j;
    }
}
