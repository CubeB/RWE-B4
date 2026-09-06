#pragma once

#include <cstdint>
#include <rwe/sim/GameHash.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/util/match.h>

namespace rwe
{
    GameHash computeHashOf(GameHash hash);

    GameHash computeHashOf(float f);

    GameHash computeHashOf(bool b);

    GameHash computeHashOf(uint32_t i);

    GameHash computeHashOf(int32_t i);

    GameHash computeHashOf(const std::string& s);

    GameHash computeHashOf(const char* s);

    GameHash computeHashOf(const GamePlayerInfo& p);

    GameHash computeHashOf(const UnitState& u);

    /**
     * Only the stockpile counters. The rest of a weapon's state is not in the
     * hash yet, and cannot be until the aiming state stops carrying a raw
     * CobThread pointer, which differs between machines.
     */
    GameHash computeHashOf(const UnitWeapon& w);


    /**
     * The orders a unit is carrying.
     *
     * Worth hashing for its own sake -- a peer that disagrees about what a
     * unit is *doing* is desynced whether or not its position has drifted yet
     * -- and necessary since section 96, which moved capture progress onto
     * CaptureOrder to match the original and would otherwise have taken it
     * out of the hash's sight.
     */
    GameHash computeHashOf(const AttackLeash& l);
    GameHash computeHashOf(const MoveOrder& o);
    GameHash computeHashOf(const AttackOrder& o);
    GameHash computeHashOf(const BuildOrder& o);
    GameHash computeHashOf(const BuggerOffOrder& o);
    GameHash computeHashOf(const CompleteBuildOrder& o);
    GameHash computeHashOf(const GuardOrder& o);
    GameHash computeHashOf(const ReclaimOrder& o);
    GameHash computeHashOf(const RepairOrder& o);
    GameHash computeHashOf(const PatrolOrder& o);
    GameHash computeHashOf(const CaptureOrder& o);
    GameHash computeHashOf(const LoadOrder& o);
    GameHash computeHashOf(const UnloadOrder& o);
    GameHash computeHashOf(const DgunOrder& o);
    GameHash computeHashOf(const LandOnAirBaseOrder& o);
    GameHash computeHashOf(const ResurrectOrder& o);

    GameHash computeHashOf(const UnitPhysicsInfoGround& p);
    GameHash computeHashOf(const UnitPhysicsInfoAir& p);
    GameHash computeHashOf(const AirMovementStateTakingOff& p);
    GameHash computeHashOf(const AirMovementStateLanding& p);
    GameHash computeHashOf(const AirMovementStateFlying& p);
    GameHash computeHashOf(const AirMovementStateAttackRun& p);
    GameHash computeHashOf(const AirMovementStateHoverAttack& p);

    GameHash computeHashOf(const AirMovementStateDogfight& p);

    GameHash computeHashOf(const SteeringInfo& s);

    GameHash computeHashOf(const Projectile& projectile);

    GameHash computeHashOf(const UnitBehaviorStateIdle&);

    GameHash computeHashOf(const UnitBehaviorStateBuilding& s);
    GameHash computeHashOf(const UnitBehaviorStateReclaiming& s);
    GameHash computeHashOf(const UnitBehaviorStateResurrecting& s);

    GameHash computeHashOf(const UnitBehaviorStateCreatingUnit&);


    GameHash computeHashOf(const UnitCreationStatusPending&);
    GameHash computeHashOf(const UnitCreationStatusDone&);
    GameHash computeHashOf(const UnitCreationStatusFailed&);

    GameHash computeHashOf(const UnitState::LifeStateAlive&);
    GameHash computeHashOf(const UnitState::LifeStateDead&);

    GameHash computeHashOf(const NavigationGoalLandingLocation&);

    GameHash computeHashOf(const NavigationStateIdle&);
    GameHash computeHashOf(const NavigationStateMoving& m);
    GameHash computeHashOf(const NavigationStateMovingToLandingSpot&);
    GameHash computeHashOf(const NavigationStateInfo& i);

    GameHash computeHashOf(const DiscreteRect& r);

    GameHash computeHashOf(const MapFeature& f);
    GameHash computeHashOf(const UnitState::AirWorkOrbitState& s);
    GameHash computeHashOf(const UnitState::AirLoiterState& s);

    GameHash computeHashOf(const GameSimulation& simulation);

    template <typename... Ts>
    GameHash combineHashes(const Ts&... items);

    template <typename Val>
    GameHash computeHashOf(const Vector3x<Val>& v)
    {
        return combineHashes(v.x, v.y, v.z);
    }

    template <typename T, typename Tag>
    GameHash computeHashOf(const OpaqueId<T, Tag>& id)
    {
        return computeHashOf(id.value);
    }

    template <typename T>
    GameHash computeHashOf(const std::optional<T>& o)
    {
        return o ? computeHashOf(*o) : GameHash(0);
    }

    /**
     * A queue, hashed by position as well as by content.
     *
     * The other container helpers fold with `sum +=`, which is right for a
     * set and wrong for a queue: it cannot tell [move, attack] from
     * [attack, move], and for an order queue that ordering is the whole
     * meaning. The index is mixed in so a reordering changes the hash.
     */
    template <typename T>
    GameHash computeHashOf(const std::deque<T>& d)
    {
        // Folded rather than summed. Mixing the position in additively does
        // not work -- the sum of the indices is the same whatever order the
        // items come in, so it cancels exactly -- which the test for this
        // caught. Multiplying the accumulator each step is what makes the
        // sequence matter.
        uint32_t accumulator = 0;
        for (const auto& x : d)
        {
            accumulator = (accumulator * 31u) + computeHashOf(x).value;
        }
        return GameHash(accumulator);
    }

    template <typename T>
    GameHash computeHashOf(const std::vector<T>& v)
    {
        GameHash sum(0);
        for (const auto& x : v)
        {
            sum += computeHashOf(x);
        }
        return sum;
    }

    template <typename T, std::size_t N>
    GameHash computeHashOf(const std::array<T, N>& a)
    {
        GameHash sum(0);
        for (const auto& x : a)
        {
            sum += computeHashOf(x);
        }
        return sum;
    }

    template <typename T, typename Tag>
    GameHash computeHashOf(const VectorMap<T, Tag>& v)
    {
        GameHash sum(0);
        for (const auto& x : v)
        {
            sum += computeHashOf(x);
        }
        return sum;
    }

    template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
    GameHash computeHashOf(const T& en)
    {
        return GameHash(static_cast<std::underlying_type_t<T>>(en));
    }

    template <typename... Ts>
    GameHash computeHashOf(const std::variant<Ts...>& v)
    {
        return GameHash(v.index())
            + match(v, [](const auto& x) { return computeHashOf(x); });
    }

    template <typename A, typename B>
    GameHash computeHashOf(const std::pair<A, B>& p)
    {
        return computeHashOf(p.first) + computeHashOf(p.second);
    }

    template <typename... Ts>
    GameHash combineHashes(const Ts&... items)
    {
        uint32_t sum = 0;
        ((sum = sum + computeHashOf(items).value), ...);
        return GameHash(sum);
    }
}
