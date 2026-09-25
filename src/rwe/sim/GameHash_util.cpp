#include "GameHash_util.h"
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/MissionScripts.h>

#include <rwe/game/UnitStateFieldTable.h>

namespace rwe
{
    GameHash computeHashOf(GameHash hash)
    {
        return hash;
    }

    GameHash computeHashOf(float f)
    {
        return GameHash(static_cast<uint32_t>(f * (1u << 16u)));
    }

    GameHash computeHashOf(bool b)
    {
        return b ? GameHash(1) : GameHash(0);
    }

    GameHash computeHashOf(uint32_t i)
    {
        return GameHash(i);
    }

    GameHash computeHashOf(int32_t i)
    {
        return GameHash(static_cast<uint32_t>(i));
    }

    GameHash computeHashOf(const std::string& s)
    {
        uint32_t sum = 0;
        for (const char& c : s)
        {
            sum += c;
        }
        return GameHash(sum);
    }

    GameHash computeHashOf(const char* s)
    {
        uint32_t sum = 0;
        for (; *s != '\0'; ++s)
        {
            sum += *s;
        }
        return GameHash(sum);
    }

    GameHash computeHashOf(const GamePlayerInfo& p)
    {
        return combineHashes(
            p.type,
            p.color,
            p.status,
            p.side,
            p.metal,
            p.maxMetal,
            p.energy,
            p.maxEnergy,
            p.hasBaseStorage,
            p.metalStalled,
            p.energyStalled,
            p.unitsKilled,
            p.unitsLost,
            p.metalProduced,
            p.energyProduced,
            p.metalExcess,
            p.energyExcess,
            p.desiredMetalConsumptionBuffer,
            p.desiredEnergyConsumptionBuffer,
            p.previousDesiredMetalConsumptionBuffer,
            p.previousDesiredEnergyConsumptionBuffer,
            p.metalRequestBuffer,
            p.energyRequestBuffer,
            p.metalDebt,
            p.energyDebt,
            p.metalProductionBuffer,
            p.energyProductionBuffer,
            p.previousMetalProductionBuffer,
            p.previousEnergyProductionBuffer);
    }

    /**
     * The unit's page of the sync hash, walked from the field table in
     * UnitStateFieldTable rather than from a hand-written list.
     *
     * Every member of the table's hash part has to be initialised by the
     * time a unit exists, and UnitState's constructor names only three of
     * its own -- the rest depend on having a default member initialiser. One
     * without one holds whatever was in the memory the unit was built in,
     * which is zero while the heap is young and rubbish once it is not, and
     * that rubbish goes straight into the sync hash. `nanoPoint` was missing
     * its initialiser and desynced a replay keyframe about one run in ten.
     * "a new unit hashes the same wherever in memory it was built", in this
     * file's test, is what stops the next one.
     *
     * The table's hash step is `computeHashOf` over the member, the same
     * call the combineHashes list used to make; the combination itself is a
     * plain addition, folded over the rows.
     */
    GameHash computeHashOf(const UnitState& u)
    {
        GameHash hash(0);
        for (const auto& field : unitStateFieldTable())
        {
            if (const auto* h = std::get_if<UnitStateFieldHashed>(&field.walks))
            {
                hash += h->hash(u);
            }
        }
        return hash;
    }

    GameHash computeHashOf(const AttackLeash& l)
    {
        return combineHashes(l.anchor, l.distance);
    }

    GameHash computeHashOf(const MoveOrder& o) { return computeHashOf(o.destination); }
    GameHash computeHashOf(const AttackOrder& o) { return combineHashes(o.target, o.leash, o.lastSeenPosition); }
    GameHash computeHashOf(const BuildOrder& o) { return combineHashes(o.unitType, o.position); }
    GameHash computeHashOf(const BuggerOffOrder& o) { return computeHashOf(o.rect); }
    GameHash computeHashOf(const CompleteBuildOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const GuardOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const ReclaimOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const RepairOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const PatrolOrder& o) { return computeHashOf(o.destination); }

    GameHash computeHashOf(const CaptureOrder& o)
    {
        // progress and totalWork are the reason this whole family exists --
        // see section 96 and the note in the header.
        return combineHashes(o.target, o.progress, o.totalWork);
    }

    GameHash computeHashOf(const LoadOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const UnloadOrder& o) { return combineHashes(o.destination, o.parkedUntil); }
    GameHash computeHashOf(const DgunOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const LandOnAirBaseOrder& o) { return computeHashOf(o.target); }
    GameHash computeHashOf(const ResurrectOrder& o) { return computeHashOf(o.target); }

    GameHash computeHashOf(const UnitWeapon& w)
    {
        return combineHashes(
            w.stockedRounds,
            w.queuedRounds,
            w.stockpileProgress,
            w.stockpileStepDelay);
    }

    GameHash computeHashOf(const UnitPhysicsInfoGround& p)
    {
        return combineHashes(p.steeringInfo, p.currentSpeed, p.pitch, p.roll);
    }

    GameHash computeHashOf(const UnitPhysicsInfoAir& p)
    {
        return combineHashes(p.movementState, p.roll, p.bankAccum, p.pitch);
    }

    GameHash computeHashOf(const AirMovementStateTakingOff& p)
    {
        return combineHashes(p.currentVelocity, p.targetPosition.value_or(SimVector(0_ss, 0_ss, 0_ss)));
    }

    GameHash computeHashOf(const AirMovementStateLanding& /*p*/)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const AirMovementStateFlying& /*p*/)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const AirMovementStateAttackRun& p)
    {
        return combineHashes(
            p.lastKnownTargetPos,
            p.runOutDirection,
            p.runOutDistance,
            static_cast<uint32_t>(p.phase),
            p.bombsDroppedThisPass,
            p.releasePoint,
            p.strafingPass,
            p.breakWaypoint);
    }

    GameHash computeHashOf(const AirMovementStateHoverAttack& p)
    {
        return combineHashes(
            p.station,
            p.targetPosition,
            static_cast<uint32_t>(p.swingPositive),
            p.outOfRangeArrivals,
            static_cast<uint32_t>(p.phase));
    }

    GameHash computeHashOf(const AirMovementStateDogfight& p)
    {
        return combineHashes(
            p.goalPosition,
            p.goalVelocity,
            p.nextDecision.value,
            p.offNoseCounter,
            static_cast<uint32_t>(p.breakLeft),
            p.breakWaypoint,
            static_cast<uint32_t>(p.phase));
    }

    GameHash computeHashOf(const SteeringInfo& s)
    {
        return combineHashes(s.targetAngle, s.targetSpeed);
    }

    GameHash computeHashOf(const Vector3f& v)
    {
        return combineHashes(v.x, v.y, v.z);
    }

    GameHash computeHashOf(const Projectile& projectile)
    {
        GameHash h = combineHashes(
            projectile.owner,
            projectile.attacker,
            projectile.position,
            projectile.origin,
            projectile.velocity,
            projectile.damageRadius,
            projectile.edgeEffectiveness,
            projectile.heading,
            projectile.pitch,
            projectile.speed,
            projectile.secondPhase,
            projectile.targetProjectile,
            projectile.motorOut);

        for (const auto& [_, damage] : projectile.damage)
        {
            h += computeHashOf(damage);
        }
        return h;
    }

    GameHash computeHashOf(const UnitBehaviorStateIdle&)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const UnitBehaviorStateBuilding& s)
    {
        return combineHashes(s.targetUnit, s.nanoParticleOrigin);
    }

    GameHash computeHashOf(const UnitBehaviorStateResurrecting& s)
    {
        return combineHashes(s.target, s.nanoParticleOrigin);
    }

    GameHash computeHashOf(const UnitBehaviorStateReclaiming& s)
    {
        return combineHashes(s.target, s.nanoParticleOrigin, s.stepCounter);
    }

    GameHash computeHashOf(const UnitBehaviorStateCreatingUnit& s)
    {
        return combineHashes(
            s.position,
            s.owner,
            s.unitType,
            s.status);
    }

    GameHash computeHashOf(const UnitCreationStatusPending& s)
    {
        return combineHashes(s.attempts, s.nextAttempt);
    }
    GameHash computeHashOf(const UnitCreationStatusDone& s)
    {
        return combineHashes(s.unitId);
    }

    GameHash computeHashOf(const UnitCreationStatusFailed&)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const UnitState::LifeStateAlive&)
    {
        return GameHash(0);
    }
    GameHash computeHashOf(const UnitState::LifeStateDead&)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const NavigationGoalLandingLocation&)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const NavigationStateIdle&)
    {
        return GameHash(0);
    }

    GameHash computeHashOf(const NavigationStateMoving& m)
    {
        return combineHashes(
            m.pathDestination,
            m.pathRequested,
            m.reachableDestination);
    }

    GameHash computeHashOf(const NavigationStateMovingToLandingSpot& m)
    {
        return combineHashes(m.landingLocation);
    }

    GameHash computeHashOf(const NavigationStateInfo& i)
    {
        return combineHashes(i.desiredDestination, i.state);
    }

    GameHash computeHashOf(const DiscreteRect& r)
    {
        return combineHashes(r.x, r.y, r.width, r.height);
    }

    GameHash computeHashOf(const UnitState::AirWorkOrbitState& s)
    {
        return combineHashes(
            s.workPosition,
            s.bearing,
            s.started);
    }

    GameHash computeHashOf(const UnitState::AirLoiterState& s)
    {
        return combineHashes(
            static_cast<uint32_t>(s.reason),
            s.anchor,
            s.bearing);
    }

    GameHash computeHashOf(const MapFeature& f)
    {
        return combineHashes(
            f.featureName,
            f.position,
            f.rotation,
            f.velocity,
            f.reclaimProgress,
            f.hitPoints,
            f.burningUntil,
            f.nextSpark);
    }

    GameHash computeHashOf(const MissionRules& m)
    {
        // The rules' own state only. The parameters are the mission's, the
        // same on every peer from the start; what the rules have seen is what
        // two peers can disagree about. Every field is folded in by position
        // rather than summed: a count going from 1 to 0 as its rule latches
        // would otherwise leave the sum exactly where it was, and that is the
        // commonest thing a defeat rule does.
        uint32_t accumulator = 0;
        auto fold = [&](GameHash h) { accumulator = (accumulator * 31u) + h.value; };
        for (const auto* rules : {&m.victory, &m.defeat})
        {
            for (const auto& r : *rules)
            {
                fold(computeHashOf(r.kind));
                fold(computeHashOf(r.number));
                fold(computeHashOf(r.satisfied));
                fold(computeHashOf(r.celebrated));
            }
        }
        fold(computeHashOf(m.enabled));
        fold(computeHashOf(m.countdown));
        // Victory is the enum's zero, so an outcome is one more than it.
        fold(GameHash(m.outcome ? 1u + static_cast<uint32_t>(*m.outcome) : 0u));
        return GameHash(accumulator);
    }

    GameHash computeHashOf(const MissionScripts& m)
    {
        // Folded by position, for the reason the rules are: the state is
        // mostly small counts and flags that a sum would cancel.
        uint32_t accumulator = 0;
        auto fold = [&](GameHash h) { accumulator = (accumulator * 31u) + h.value; };
        for (const auto& [id, script] : m.scripts)
        {
            fold(computeHashOf(id));
            fold(computeHashOf(script.started));
            fold(computeHashOf(script.wakeAt));
            fold(computeHashOf(static_cast<uint32_t>(script.steps.size())));
            for (const auto& step : script.steps)
            {
                fold(computeHashOf(step.kind));
                fold(computeHashOf(step.ticks));
                fold(computeHashOf(step.count));
                fold(computeHashOf(step.hit));
            }
        }
        return GameHash(accumulator);
    }

    GameHash computeHashOf(const Grid<ExploredMask>& grid)
    {
        std::uint32_t accumulator = 0;
        for (auto cell : grid.getVector())
        {
            accumulator = (accumulator * 31u) + static_cast<std::uint32_t>(cell);
        }
        return GameHash(accumulator);
    }

    GameHash computeHashOf(const GameSimulation& simulation)
    {
        return combineHashes(
            simulation.gameTime,
            simulation.players,
            simulation.units,
            simulation.projectiles,
            simulation.features,
            simulation.currentWindVector,
            simulation.featureRegrowthCursor,
            // The explored grid is per line-of-sight group and feeds canSeeUnit
            // in Permanent mode, where it decides targets. A peer that disagrees
            // about what it has seen is a desync to catch, not player-facing
            // state to leave out.
            simulation.explored,
            // Absent in a skirmish, and then it adds nothing, so a skirmish's
            // hashes are what they were.
            simulation.missionRules ? computeHashOf(*simulation.missionRules) : GameHash(0),
            simulation.missionScripts ? computeHashOf(*simulation.missionScripts) : GameHash(0));
    }
}
