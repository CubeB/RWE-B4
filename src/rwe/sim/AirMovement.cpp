#include "AirMovement.h"
#include <rwe/math/rwe_math.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/util/match.h>

namespace rwe
{
    SimVector AirMovement::airVelocity(const AirMovementState& state)
    {
        return match(
            state,
            [](const AirMovementStateFlying& m) { return m.currentVelocity; },
            [](const AirMovementStateTakingOff& m) { return m.currentVelocity; },
            [](const AirMovementStateAttackRun& m) { return m.currentVelocity; },
            [](const AirMovementStateHoverAttack& m) { return m.currentVelocity; },
            [](const AirMovementStateDogfight& m) { return m.currentVelocity; },
            [](const AirMovementStateLanding&) { return SimVector(0_ss, 0_ss, 0_ss); });
    }

    SimScalar AirMovement::bankAngle(const UnitState& unit, const UnitDefinition& unitDefinition, UnitPhysicsInfoAir& physics, const SimVector& deltaVelocity)
    {
        // 62259/65536 in the original's fixed point.
        const SimScalar lag(0.9499817f);
        physics.bankAccum = (physics.bankAccum * lag) + deltaVelocity;

        // Sideways is the component along the aircraft's right hand.
        auto heading = unit.rotation;
        auto lateral = (physics.bankAccum.x * cos(heading)) - (physics.bankAccum.z * sin(heading));

        // Gravity is 112 world units per second squared on nearly every map the
        // game ships, which is 112/900 per tick squared; dividing by (1 - lag)
        // undoes the lag's gain.
        const SimScalar gravityOverLagGain((112.0f / 900.0f) / (1.0f - 0.9499817f));

        // Accelerating to the right drops the right wing, so the model rolls
        // the other way about its nose.
        auto angle = atan2(-unitDefinition.bankScale * lateral, gravityOverLagGain);
        auto radians = toRadians(angle).value;
        if (radians > Pif)
        {
            radians -= 2.0f * Pif;
        }
        return SimScalar(radians);
    }

    AirMovementState AirMovement::step(const AirMovementState& state, const AirFrameCommand& command, const UnitState& unit, const UnitDefinition& unitDefinition)
    {
        return match(
            state,
            [&](const AirMovementStateFlying& m) -> AirMovementState {
                AirMovementStateFlying next = m;
                if (command.targetPosition)
                {
                    next.targetPosition = command.targetPosition;
                }
                next.currentVelocity = computeNewAirUnitVelocity(unit, unitDefinition, next);
                return next;
            },
            [&](const AirMovementStateTakingOff& m) -> AirMovementState {
                // Gather speed towards the destination on the way up, at most
                // half pace until it reaches cruise height.
                AirMovementStateFlying asFlying;
                asFlying.targetPosition = m.targetPosition;
                asFlying.currentVelocity = m.currentVelocity;
                auto velocity = computeNewAirUnitVelocity(unit, unitDefinition, asFlying);
                velocity.y = 0_ss;
                auto limit = unitDefinition.maxVelocity / 2_ss;
                if (velocity.lengthSquared() > limit * limit)
                {
                    velocity = velocity.normalized() * limit;
                }
                AirMovementStateTakingOff next = m;
                next.currentVelocity = velocity;
                return next;
            },
            [&](const AirMovementStateLanding& m) -> AirMovementState {
                // do nothing
                return m;
            },
            [&](const AirMovementStateAttackRun& m) -> AirMovementState {
                AirMovementStateAttackRun next = m;
                next.currentVelocity = computeNewAttackRunVelocity(unit, unitDefinition, next);
                return next;
            },
            [&](const AirMovementStateHoverAttack& m) -> AirMovementState {
                AirMovementStateHoverAttack next = m;
                next.currentVelocity = computeNewHoverAttackVelocity(unit, unitDefinition, next);
                return next;
            },
            [&](const AirMovementStateDogfight& m) -> AirMovementState {
                AirMovementStateDogfight next = m;
                // The goal runs away from the fighter by its own velocity every
                // tick, in x and z only -- the original's goal object
                // integrates itself when it is resolved (0x44EA60) and never
                // touches the height.
                next.goalPosition = SimVector(
                    next.goalPosition.x + next.goalVelocity.x,
                    next.goalPosition.y,
                    next.goalPosition.z + next.goalVelocity.z);
                next.currentVelocity = computeNewDogfightVelocity(unit, unitDefinition, next);
                return next;
            });
    }

    void AirMovement::updateVelocity(UnitPhysicsInfoAir& physics, const UnitState& unit, const UnitDefinition& unitDefinition)
    {
        physics.previousRoll = physics.roll;
        auto velocityBefore = airVelocity(physics.movementState);

        physics.movementState = step(physics.movementState, AirFrameCommand{}, unit, unitDefinition);

        physics.roll = bankAngle(unit, unitDefinition, physics, airVelocity(physics.movementState) - velocityBefore);
    }

    bool AirMovement::takeoffReachedCruise(const AirMovementStateTakingOff& /*state*/, SimScalar unitY, SimScalar targetHeight)
    {
        return unitY == targetHeight;
    }

    AirMovementStateFlying AirMovement::finishTakeoff(const AirMovementStateTakingOff& state)
    {
        // Keep the heading and speed built up during the climb.
        AirMovementStateFlying flying;
        flying.targetPosition = state.targetPosition;
        flying.currentVelocity = state.currentVelocity;
        return flying;
    }

    AirMovementStateAttackRun AirMovement::beginAttackRun(
        const AttackTarget& target,
        const SimVector& targetPositionAtAltitude,
        SimAngle rotation,
        const SimVector& currentVelocity,
        bool strafingPass,
        SimScalar runOutDistance)
    {
        AirMovementStateAttackRun runState(target);
        // Cache target position at cruise altitude so steering doesn't dive.
        runState.lastKnownTargetPos = targetPositionAtAltitude;
        runState.runOutDirection = UnitState::toDirection(rotation);
        runState.strafingPass = strafingPass;
        runState.runOutDistance = runOutDistance;
        runState.phase = AirMovementStateAttackRun::Phase::Approaching;
        // Carry the speed it already had: an aircraft that turns to attack does
        // not come to a halt first.
        runState.currentVelocity = currentVelocity;
        return runState;
    }

    AirMovement::AttackRunStep AirMovement::stepAttackRun(
        AirMovementStateAttackRun& run,
        const UnitState& unit,
        const UnitDefinition& unitDefinition,
        const MapTerrain& terrain,
        const SimVector& targetPositionAtAltitude,
        const SimVector& targetPosition,
        SimScalar weaponMaxRange)
    {
        AttackRunStep result;

        // Refresh cached target position (target may move).
        run.lastKnownTargetPos = targetPositionAtAltitude;

        result.previousPhase = run.phase;

        auto geometry = computeAttackRunGeometry(unitDefinition, weaponMaxRange);
        SimVector heading = run.currentVelocity;
        heading.y = 0_ss;
        if (heading.lengthSquared() == 0_ss)
        {
            heading = UnitState::toDirection(unit.rotation);
        }
        result.weaponsHot = stepAttackRunPhase(unit.position, heading, targetPosition, geometry, run);

        // Never run out past the edge of the map: turn back early instead.
        if (run.phase == AirMovementStateAttackRun::Phase::Departing)
        {
            const auto& heights = terrain.getHeightMap();
            auto corner = terrain.heightmapIndexToWorldCorner(0, 0);
            auto margin = 64_ss;
            auto minX = corner.x + margin;
            auto minZ = corner.z + margin;
            auto maxX = corner.x + (SimScalar(static_cast<float>(heights.getWidth())) * MapTerrain::HeightTileWidthInWorldUnits) - margin;
            auto maxZ = corner.z + (SimScalar(static_cast<float>(heights.getHeight())) * MapTerrain::HeightTileHeightInWorldUnits) - margin;
            const auto& p = unit.position;
            if (p.x < minX || p.x > maxX || p.z < minZ || p.z > maxZ)
            {
                run.phase = AirMovementStateAttackRun::Phase::Approaching;
            }
        }

        return result;
    }

    SimVector AirMovement::strafeBreakWaypoint(
        const SimVector& unitPosition,
        const SimVector& velocity,
        SimAngle rotation,
        SimScalar weaponMaxRange,
        bool breakLeft)
    {
        SimVector breakHeading = velocity;
        breakHeading.y = 0_ss;
        if (breakHeading.lengthSquared() == 0_ss)
        {
            breakHeading = UnitState::toDirection(rotation);
        }
        auto quarterTurn = SimAngle(16384);
        auto heading = UnitState::toRotation(breakHeading);
        auto breakDirection = UnitState::toDirection(breakLeft ? heading + quarterTurn : heading - quarterTurn);
        auto reach = rweMax(weaponMaxRange, 1_ss);
        return SimVector(
            unitPosition.x + (breakDirection.x * reach),
            unitPosition.y,
            unitPosition.z + (breakDirection.z * reach));
    }

    void AirMovement::captureRunOutDirection(
        AirMovementStateAttackRun& run,
        AirMovementStateAttackRun::Phase previousPhase,
        const SimVector& unitPosition,
        const SimVector& targetPosition,
        SimAngle rotation)
    {
        if (previousPhase != AirMovementStateAttackRun::Phase::Approaching
            || run.phase != AirMovementStateAttackRun::Phase::Engaging)
        {
            return;
        }

        // Capture the engagement heading: prefer the direction we're already
        // moving in so the line-up stays smooth. If we're not moving yet, fall
        // back to the line through the target.
        SimVector heading = run.currentVelocity;
        heading.y = 0_ss;
        if (heading.lengthSquared() == 0_ss)
        {
            heading = SimVector(targetPosition.x - unitPosition.x, 0_ss, targetPosition.z - unitPosition.z);
        }
        run.runOutDirection = heading.normalizedOr(UnitState::toDirection(rotation));
    }

    SimScalar AirMovement::loiterRadius(bool hasPrimaryWeapon, SimScalar primaryWeaponMaxRange)
    {
        if (!hasPrimaryWeapon)
        {
            return LoiterUnarmedRadius;
        }
        return primaryWeaponMaxRange + LoiterStandoff;
    }

    SimAngle AirMovement::loiterEntryBearing(unsigned int draw)
    {
        return SimAngle(draw % 0x10000u);
    }

    SimVector AirMovement::loiterStation(const SimVector& anchor, SimAngle bearing, SimScalar radius)
    {
        return anchor + (UnitState::toDirection(bearing) * radius);
    }

    bool AirMovement::loiterArrived(const SimVector& unitPosition, const SimVector& station)
    {
        SimVector toStation(station.x - unitPosition.x, 0_ss, station.z - unitPosition.z);
        return toStation.lengthSquared() <= LoiterArrivalTolerance * LoiterArrivalTolerance;
    }

    SimAngle AirMovement::nextLoiterBearing(SimAngle bearing, SimAngle stepBase, unsigned int draw)
    {
        return bearing - stepBase - SimAngle(draw % (LoiterStepJitter.value + 1u));
    }

    bool AirMovement::canDivertForRepair(bool canFly, bool isAirborne, bool inDogfight, bool frontOrderBreaksOff, bool hurtEnough)
    {
        return canFly && isAirborne && !inDogfight && frontOrderBreaksOff && hurtEnough;
    }
}
