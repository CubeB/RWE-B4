#include "UnitBehaviorService_util.h"
#include <algorithm>
#include <rwe/sim/SimTicksPerSecond.h>

#include <stdexcept>

namespace rwe
{
    SimAngle angleTo(const Vector2x<SimScalar>& lhs, const Vector2x<SimScalar>& rhs)
    {
        return atan2(lhs.det(rhs), lhs.dot(rhs));
    }

    SimVector toDirection(SimAngle heading, SimAngle pitch)
    {
        return Matrix4x<SimScalar>::rotationY(sin(heading), cos(heading))
            * Matrix4x<SimScalar>::rotationX(sin(pitch), cos(pitch))
            * SimVector(0_ss, 0_ss, 1_ss);
    }

    SimVector rotateDirectionXZ(const SimVector& direction, SimAngle angle)
    {
        return Matrix4x<SimScalar>::rotationY(sin(angle), cos(angle)) * direction;
    }

    SimScalar getTurnRadius(SimScalar speed, SimScalar turnRate)
    {
        return speed / angularToRadians(turnRate);
    }

    std::optional<SimVector> findLandingLocation(const GameSimulation& sim, ConstUnitInfo unitInfo)
    {
        // Aircraft set down on dry land only: nothing in TA lands on the sea,
        // and an aircraft that touched down over water used to sink into it.
        // Look at the spot underneath first, then work outwards a ring of
        // heightmap tiles at a time until there is somewhere clear to put it.
        auto canLandAt = [&](const SimVector& candidate) {
            auto ground = sim.terrain.getHeightAt(candidate.x, candidate.z);
            if (ground < sim.terrain.getSeaLevel())
            {
                return false;
            }
            // isCollisionAt also rejects a footprint that runs off the map.
            auto footprintRect = sim.computeFootprintRegion(candidate, unitInfo.definition->movementCollisionInfo);
            return !sim.isCollisionAt(footprintRect, unitInfo.id);
        };

        const auto& position = unitInfo.state->position;
        if (canLandAt(position))
        {
            return position;
        }

        // Nothing underneath, so a search is needed. A successful one is
        // remembered by the caller, but a failure would be repeated every
        // tick for as long as the aircraft loiters over water, so only look
        // once a second. Sim time drives the throttle, so every peer agrees.
        if (sim.gameTime.value % static_cast<unsigned int>(SimTicksPerSecond) != 0)
        {
            return std::nullopt;
        }

        auto origin = sim.terrain.worldToHeightmapCoordinate(position);
        const int maxRings = 48;
        for (int ring = 1; ring <= maxRings; ++ring)
        {
            for (int dz = -ring; dz <= ring; ++dz)
            {
                for (int dx = -ring; dx <= ring; ++dx)
                {
                    if (std::max(std::abs(dx), std::abs(dz)) != ring)
                    {
                        continue;
                    }
                    auto candidate = sim.terrain.heightmapIndexToWorldCenter(origin.x + dx, origin.y + dz);
                    candidate.y = sim.terrain.getHeightAt(candidate.x, candidate.z);
                    if (canLandAt(candidate))
                    {
                        return candidate;
                    }
                }
            }
        }

        // Nowhere within reach: stay airborne rather than ditching.
        return std::nullopt;
    }

    std::pair<SimAngle, SimAngle> computeHeadingAndPitch(SimAngle rotation, const SimVector& from, const SimVector& to, SimScalar speed, SimScalar gravity, SimScalar zOffset, ProjectilePhysicsType projectileType)
    {
        return match(
            projectileType,
            [&](const ProjectilePhysicsTypeLineOfSight&) {
                return computeLineOfSightHeadingAndPitch(rotation, from, to);
            },
            [&](const ProjectilePhysicsTypeTracking&) {
                return computeLineOfSightHeadingAndPitch(rotation, from, to);
            },
            [&](const ProjectilePhysicsTypeBallistic&) {
                return computeBallisticHeadingAndPitch(rotation, from, to, speed, gravity, zOffset);
            },
            [&](const ProjectilePhysicsTypeBomb&) {
                // Bombs don't actually use this aim — their release is decided
                // by the bombsight predicate in tryFireWeapon. We return a
                // line-of-sight aim so the COB AimWeapon dance doesn't choke.
                return computeLineOfSightHeadingAndPitch(rotation, from, to);
            });
    }

    std::pair<SimAngle, SimAngle> computeLineOfSightHeadingAndPitch(SimAngle rotation, const SimVector& from, const SimVector& to)
    {
        auto aimVector = to - from;
        if (aimVector.lengthSquared() == 0_ss)
        {
            aimVector = UnitState::toDirection(rotation);
        }

        SimVector aimVectorXZ(aimVector.x, 0_ss, aimVector.z);

        auto heading = UnitState::toRotation(aimVectorXZ);
        heading = heading - rotation;

        auto pitch = atan2(aimVector.y, aimVectorXZ.length());

        return {heading, pitch};
    }

    std::pair<SimAngle, SimAngle> computeBallisticHeadingAndPitch(SimAngle rotation, const SimVector& from, const SimVector& to, SimScalar speed, SimScalar gravity, SimScalar zOffset)
    {
        auto aimVector = to - from;
        if (aimVector.lengthSquared() == 0_ss)
        {
            aimVector = UnitState::toDirection(rotation);
        }

        SimVector aimVectorXZ(aimVector.x, 0_ss, aimVector.z);

        auto heading = UnitState::toRotation(aimVectorXZ);
        heading = heading - rotation;

        auto pitches = computeFiringAngles(speed, gravity, aimVectorXZ.length() - zOffset, aimVector.y);
        if (!pitches)
        {
            return {heading, EighthTurn};
        }

        return {heading, pitches->second};
    }


    std::optional<std::pair<SimAngle, SimAngle>> computeFiringAngles(SimScalar speed, SimScalar gravity, SimScalar targetX, SimScalar targetY)
    {
        auto inner = (gravity * targetX * targetX) + (2_ss * speed * speed * targetY);
        auto beforeSquareRoot = (speed * speed * speed * speed) - (gravity * inner);
        if (beforeSquareRoot < 0_ss)
        {
            return std::nullopt;
        }
        auto plusMinus = rweSqrt(beforeSquareRoot);

        auto result1 = atan(((speed * speed) + plusMinus) / (gravity * targetX));
        auto result2 = atan(((speed * speed) - plusMinus) / (gravity * targetX));

        return std::make_pair(result1, result2);
    }

    SteeringInfo seek(const UnitState& unit, const UnitDefinition& unitDefinition, const SimVector& destination)
    {
        SimVector xzPosition(unit.position.x, 0_ss, unit.position.z);
        SimVector xzDestination(destination.x, 0_ss, destination.z);
        auto xzDirection = xzDestination - xzPosition;

        // Already at destination horizontally — hold heading and stop.
        // Reaches here when an aircraft is ordered to attack the ground
        // directly below it, or when a unit is exactly on its target.
        if (xzDirection.lengthSquared() == 0_ss)
        {
            return SteeringInfo{unit.rotation, 0_ss};
        }

        // scale desired speed proportionally to how aligned we are
        // with the target direction
        auto normalizedUnitDirection = UnitState::toDirection(unit.rotation);
        auto normalizedXzDirection = xzDirection.normalized();
        auto speedFactor = rweMax(0_ss, normalizedUnitDirection.dot(normalizedXzDirection));

        // Bias the speed factor towards zero if we are within our turn radius of the goal.
        // This is to try and discourage units from orbiting their destination.
        auto turnRadius = getTurnRadius(unitDefinition.maxVelocity, unitDefinition.turnRate);
        if (xzDirection.lengthSquared() <= turnRadius * turnRadius)
        {
            speedFactor = speedFactor * speedFactor;
        }

        return SteeringInfo{
            UnitState::toRotation(xzDirection),
            unitDefinition.maxVelocity * speedFactor,
        };
    }

    SteeringInfo arrive(const UnitState& unit, const UnitDefinition& unitDefinition, const UnitPhysicsInfoGround& physics, const SimVector& destination)
    {
        SimVector xzPosition(unit.position.x, 0_ss, unit.position.z);
        SimVector xzDestination(destination.x, 0_ss, destination.z);
        auto distanceSquared = xzPosition.distanceSquared(xzDestination);
        auto brakingDistance = (physics.currentSpeed * physics.currentSpeed) / (2_ss * unitDefinition.brakeRate);

        if (distanceSquared > (brakingDistance * brakingDistance))
        {
            return seek(unit, unitDefinition, destination);
        }

        // slow down when approaching the destination
        auto xzDirection = xzDestination - xzPosition;
        return SteeringInfo{
            UnitState::toRotation(xzDirection),
            0_ss,
        };
    }

    SimScalar computeSlopeSpeedFactor(const MapTerrain& terrain, const UnitState& unit, unsigned int maxSlope)
    {
        // Units that can climb anything are not slowed by anything.
        if (maxSlope == 0 || maxSlope >= 255)
        {
            return 1_ss;
        }

        // Rise over one heightmap tile in the direction we are facing, in the
        // same units the movement class limit uses (height per tile).
        auto forward = UnitState::toDirection(unit.rotation);
        auto ahead = unit.position + (forward * MapTerrain::HeightTileWidthInWorldUnits);
        auto extent = terrain.worldToHeightmapCoordinate(ahead);
        const auto& heights = terrain.getHeightMap();
        if (extent.x < 0 || extent.y < 0 || extent.x >= heights.getWidth() || extent.y >= heights.getHeight())
        {
            return 1_ss;
        }

        auto rise = terrain.getHeightAt(ahead.x, ahead.z) - terrain.getHeightAt(unit.position.x, unit.position.z);
        if (rise <= 0_ss)
        {
            // Downhill and flat are full speed.
            return 1_ss;
        }

        // At the slope limit the unit crawls at half speed; never below a quarter.
        auto ratio = rise / SimScalar(static_cast<float>(maxSlope));
        auto factor = 1_ss - (ratio / 2_ss);
        return rweMax(factor, 1_ss / 4_ss);
    }

    SimScalar computeNewGroundUnitSpeed(const MapTerrain& terrain, const UnitState& unit, const UnitDefinition& unitDefinition, const UnitPhysicsInfoGround& physics, unsigned int maxSlope)
    {
        SimScalar newSpeed;
        if (physics.steeringInfo.targetSpeed > physics.currentSpeed)
        {
            // accelerate to target speed
            if (physics.steeringInfo.targetSpeed - physics.currentSpeed <= unitDefinition.acceleration)
            {
                newSpeed = physics.steeringInfo.targetSpeed;
            }
            else
            {
                newSpeed = physics.currentSpeed + unitDefinition.acceleration;
            }
        }
        else
        {
            // brake to target speed
            if (physics.currentSpeed - physics.steeringInfo.targetSpeed <= unitDefinition.brakeRate)
            {
                newSpeed = physics.steeringInfo.targetSpeed;
            }
            else
            {
                newSpeed = physics.currentSpeed - unitDefinition.brakeRate;
            }
        }

        auto effectiveMaxSpeed = unitDefinition.maxVelocity;
        if (unit.position.y < terrain.getSeaLevel())
        {
            effectiveMaxSpeed /= 2_ss;
        }
        effectiveMaxSpeed = effectiveMaxSpeed * computeSlopeSpeedFactor(terrain, unit, maxSlope);
        newSpeed = std::clamp(newSpeed, 0_ss, effectiveMaxSpeed);

        return newSpeed;
    }

    SimVector decelerate(SimVector currentVelocity, SimScalar deceleration)
    {
        // Slower than one tick's braking: stop dead. Subtracting the full
        // step would overshoot past zero and flip the velocity, and because
        // the flipped magnitudes differ the "stationary" aircraft would
        // drift steadily off its spot, a fraction of a unit every tick.
        if (currentVelocity.lengthSquared() <= deceleration * deceleration)
        {
            return SimVector(0_ss, 0_ss, 0_ss);
        }
        auto currentDirection = currentVelocity.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
        return currentVelocity - (currentDirection * deceleration);
    }

    /**
     * Range at which an aircraft's arrival profile stops steepening, in world
     * units. Eight in the original.
     */
    static constexpr SimScalar AirArrivalTaperDistance = 8_ss;

    SimVector computeNewAirUnitVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateFlying& physics)
    {
        if (!physics.targetPosition)
        {
            return decelerate(physics.currentVelocity, unitDefinition.acceleration);
        }

        // Drag, applied before steering exactly as the original does. This is
        // the only thing holding an aircraft to MaxVelocity: it settles where
        // the acceleration it adds each tick balances the speed drag takes
        // away, which is MaxVelocity exactly, so nothing below needs a clamp.
        auto drag = unitDefinition.maxVelocity > 0_ss
            ? 1_ss - (unitDefinition.acceleration / unitDefinition.maxVelocity)
            : 1_ss;
        auto currentVelocity = physics.currentVelocity * drag;

        // The original's arrival profile: steer for sqrt(2 * Acceleration *
        // distance) towards the target, so the aircraft is always travelling
        // exactly as fast as it can still shed before it gets there. Holding
        // the range at a floor of AirArrivalTaperDistance turns the last few
        // units into a straight run-down to a stop, instead of a curve that
        // would demand ever harder braking the closer it came.
        auto toTarget = *physics.targetPosition - unit.position;
        auto profileRange = rweMax(toTarget.length(), AirArrivalTaperDistance);
        auto targetVelocity = toTarget * rweSqrt((2_ss * unitDefinition.acceleration) / profileRange);

        // Move at most one tick's acceleration towards the profile. Once the
        // aircraft is on the profile this step goes slack of its own accord,
        // which is what stops it rocking. The version this replaces was
        // bang-bang: it spent every tick of every trip at either full throttle
        // or full braking, and chattered between the two whenever it sat near
        // its destination. An aircraft's bank is computed from its change in
        // velocity, so that chatter showed up as a visible rock -- worst on a
        // construction aircraft, which holds station seven times an orbit.
        auto velocityDelta = targetVelocity - currentVelocity;
        if (velocityDelta.lengthSquared() > (unitDefinition.acceleration * unitDefinition.acceleration))
        {
            velocityDelta = velocityDelta.normalized() * unitDefinition.acceleration;
        }

        return currentVelocity + velocityDelta;
    }

    SimScalar attackRunTurnRadius(const UnitDefinition& unitDefinition)
    {
        // The circle an aircraft flies at full speed: speed over turn rate.
        if (unitDefinition.turnRate <= 0_ss)
        {
            return 0_ss;
        }
        return getTurnRadius(unitDefinition.maxVelocity, unitDefinition.turnRate);
    }

    AttackRunGeometry computeAttackRunGeometry(const UnitDefinition& unitDefinition, SimScalar weaponMaxRange)
    {
        AttackRunGeometry geometry;
        geometry.turnRadius = attackRunTurnRadius(unitDefinition);

        // Commit to the run about three seconds out — far enough that a bomb
        // released on the way in still has time to fall, near enough that the
        // aircraft is not holding a straight line across half the map. Never
        // further than the weapon can reach.
        auto threeSeconds = unitDefinition.maxVelocity * 90_ss;
        geometry.commitDistance = rweMin(weaponMaxRange, rweMax(threeSeconds, geometry.turnRadius * 2_ss));

        // About 20 degrees of heading error is close enough to call it lined up.
        geometry.commitAngle = SimAngle(3600);

        geometry.runOutDistance = defaultAttackRunOutDistance(unitDefinition, weaponMaxRange);
        return geometry;
    }

    SimVector computeAttackRunTargetPoint(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateAttackRun& physics)
    {
        if (physics.phase == AirMovementStateAttackRun::Phase::Departing)
        {
            // Steer toward the far side of the run-out vector.
            auto distance = physics.runOutDistance;
            return physics.lastKnownTargetPos + (physics.runOutDirection * distance);
        }

        if (physics.phase == AirMovementStateAttackRun::Phase::Engaging)
        {
            // Fly through the target: aim at a point beyond it along the
            // run-out vector. Steering at the far point pulls an off-axis
            // approach back onto the line over the target (so the bombsight
            // can open) without orbiting above it.
            auto lookahead = unitDefinition.maxVelocity * 30_ss;
            return physics.lastKnownTargetPos + (physics.runOutDirection * lookahead);
        }

        // Approaching: aim directly at the target.
        return physics.lastKnownTargetPos;
    }

    SimVector computeNewAttackRunVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateAttackRun& physics)
    {
        auto targetPoint = computeAttackRunTargetPoint(unit, unitDefinition, physics);

        // Speed: always winding up towards the aircraft's best. A run never
        // brakes — flying slower does not help it hit anything.
        auto speed = physics.currentVelocity.length();
        speed = rweMin(unitDefinition.maxVelocity, speed + unitDefinition.acceleration);

        // Heading: an aircraft cannot slide sideways, it banks. Swing the
        // current heading towards the aim point by at most one tick's worth
        // of turn, so coming back for another pass is a arc of radius
        // speed / turnRate flown at full speed, rather than a stop and a
        // pivot on the spot.
        SimVector flatVelocity(physics.currentVelocity.x, 0_ss, physics.currentVelocity.z);
        auto currentHeading = flatVelocity.lengthSquared() > 0_ss
            ? UnitState::toRotation(flatVelocity)
            : unit.rotation;

        SimVector toTarget(targetPoint.x - unit.position.x, 0_ss, targetPoint.z - unit.position.z);
        auto desiredHeading = toTarget.lengthSquared() > 0_ss
            ? UnitState::toRotation(toTarget)
            : currentHeading;

        auto newHeading = turnTowards(currentHeading, desiredHeading, SimAngle(unitDefinition.turnRate.value));

        // Altitude is handled separately (the aircraft converges on its cruise
        // height as the ground rises and falls), so the run itself is level.
        return UnitState::toDirection(newHeading) * speed;
    }

    SimScalar defaultAttackRunOutDistance(const UnitDefinition& unitDefinition, SimScalar /*weaponMaxRange*/)
    {
        // Run out far enough past the target that the turn back can be flown
        // as one continuous arc and still end up pointing at the target: a
        // half circle is two radii across, so two and a half gives room to
        // straighten up. The weapon's range is deliberately not used; a bomb's
        // 1280 range would send the aircraft clean off the map before it turned.
        auto fromTurn = attackRunTurnRadius(unitDefinition) * 2.5_ssf;
        auto fromAltitude = unitDefinition.cruiseAltitude * 2_ss;
        return rweMax(250_ss, rweMin(900_ss, rweMax(fromTurn, fromAltitude)));
    }

    SimVector predictBombImpactPoint(const SimVector& bomberPosition, const SimVector& bomberVelocity, SimScalar groundY)
    {
        // The bomb's per-tick fall is governed by the ballistic gravity used
        // in updateProjectiles: dvy/dt = -112/(30*30) (per-tick²). Integrating
        // y(t) = h - (1/2) g t² with h = bomberPosition.y - groundY gives
        //   t_impact = sqrt(2h / g).
        auto h = bomberPosition.y - groundY;
        if (h <= 0_ss)
        {
            // Aircraft is at or below the ground — bomb impacts immediately
            // at the bomber's current XZ.
            return SimVector(bomberPosition.x, groundY, bomberPosition.z);
        }

        auto gravity = 112_ss / (30_ss * 30_ss);
        // Solve t for h - (1/2) g t² = 0 => t = sqrt(2 h / g).
        auto tSquared = (2_ss * h) / gravity;
        auto t = rweSqrt(tSquared);

        SimVector impact(
            bomberPosition.x + (bomberVelocity.x * t),
            groundY,
            bomberPosition.z + (bomberVelocity.z * t));
        return impact;
    }

    bool bombsightInReleaseWindow(
        const SimVector& bomberPosition,
        const SimVector& bomberVelocity,
        const SimVector& targetPosition,
        SimScalar releaseRadius)
    {
        auto impact = predictBombImpactPoint(bomberPosition, bomberVelocity, targetPosition.y);
        SimVector dxz(impact.x - targetPosition.x, 0_ss, impact.z - targetPosition.z);

        SimVector heading(bomberVelocity.x, 0_ss, bomberVelocity.z);
        if (heading.lengthSquared() == 0_ss)
        {
            return dxz.lengthSquared() <= (releaseRadius * releaseRadius);
        }
        heading = heading.normalized();

        // Split the miss into along-track (timing, which the bombsight controls
        // precisely) and across-track (line-up, which it cannot fix at release).
        // Timing must be within the blast; a run that is slightly wide still
        // drops rather than wasting the whole pass.
        auto along = (dxz.x * heading.x) + (dxz.z * heading.z);
        auto across = (dxz.x * heading.z) - (dxz.z * heading.x);
        auto acrossTolerance = rweMax(releaseRadius * 2_ss, 48_ss);
        return along * along <= releaseRadius * releaseRadius && across * across <= acrossTolerance * acrossTolerance;
    }

    bool stepAttackRunPhase(
        const SimVector& unitPosition,
        const SimVector& unitHeading,
        const SimVector& targetPosition,
        const AttackRunGeometry& geometry,
        AirMovementStateAttackRun& runState)
    {
        SimVector xzUnit(unitPosition.x, 0_ss, unitPosition.z);
        SimVector xzTarget(targetPosition.x, 0_ss, targetPosition.z);
        auto xzDistanceSquared = xzUnit.distanceSquared(xzTarget);
        auto commitDistanceSquared = geometry.commitDistance * geometry.commitDistance;

        SimVector toTarget(targetPosition.x - unitPosition.x, 0_ss, targetPosition.z - unitPosition.z);
        SimVector flatHeading(unitHeading.x, 0_ss, unitHeading.z);
        bool linedUp = true;
        if (toTarget.lengthSquared() > 0_ss && flatHeading.lengthSquared() > 0_ss)
        {
            linedUp = angleBetweenIsLessOrEqual(
                UnitState::toRotation(flatHeading),
                UnitState::toRotation(toTarget),
                geometry.commitAngle);
        }

        switch (runState.phase)
        {
            case AirMovementStateAttackRun::Phase::Approaching:
            {
                // Too close to turn onto the target: an aircraft inside its own
                // turn circle can only spiral around it, which looks like a
                // bomber circling forever without ever dropping. Extend away
                // first, then come back with room to line up.
                auto insideTurnCircle = geometry.turnRadius > 0_ss
                    && xzDistanceSquared < (geometry.turnRadius * 2_ss) * (geometry.turnRadius * 2_ss);
                if (insideTurnCircle && !linedUp)
                {
                    runState.phase = AirMovementStateAttackRun::Phase::Departing;
                    if (flatHeading.lengthSquared() > 0_ss)
                    {
                        runState.runOutDirection = flatHeading.normalized();
                    }
                    return false;
                }

                // Commit to the run once it is close enough and pointing the right way.
                if (xzDistanceSquared <= commitDistanceSquared && linedUp)
                {
                    runState.phase = AirMovementStateAttackRun::Phase::Engaging;
                    return true;
                }
                return false;
            }
            case AirMovementStateAttackRun::Phase::Engaging:
            {
                // Detect that the unit has flown past the target along
                // the run-out direction.
                SimVector toUnitFromTarget(unitPosition.x - targetPosition.x, 0_ss, unitPosition.z - targetPosition.z);
                auto passed = (toUnitFromTarget.x * runState.runOutDirection.x) + (toUnitFromTarget.z * runState.runOutDirection.z);
                if (passed > 0_ss)
                {
                    runState.phase = AirMovementStateAttackRun::Phase::Departing;
                    return false;
                }
                return true;
            }
            case AirMovementStateAttackRun::Phase::Departing:
            {
                SimVector toUnitFromTarget(unitPosition.x - targetPosition.x, 0_ss, unitPosition.z - targetPosition.z);
                auto runOut = rweMax(runState.runOutDistance, geometry.runOutDistance);
                auto runOutSquared = runOut * runOut;
                if (toUnitFromTarget.lengthSquared() >= runOutSquared)
                {
                    runState.phase = AirMovementStateAttackRun::Phase::Approaching;
                    runState.bombsDroppedThisPass = 0;
                }
                return false;
            }
        }
        return false;
    }

    Rectangle2x<SimScalar> toWorldXZRect(const MapTerrain& terrain, const DiscreteRect& footprintRect)
    {
        auto topLeftWorld = terrain.heightmapIndexToWorldCorner(footprintRect.x, footprintRect.y);
        return Rectangle2x<SimScalar>::fromTopLeft(
            topLeftWorld.x,
            topLeftWorld.z,
            SimScalar(footprintRect.width) * MapTerrain::HeightTileWidthInWorldUnits,
            SimScalar(footprintRect.height) * MapTerrain::HeightTileHeightInWorldUnits);
    }

    enum class Edge
    {
        Top,
        Left,
        Bottom,
        Right
    };

    Edge findClosestEdge(const Rectangle2x<SimScalar>& rect, const SimVector& p)
    {
        auto distanceLeft = rweAbs(rect.left() - p.x);
        auto distanceRight = rweAbs(rect.right() - p.x);
        auto distanceTop = rweAbs(rect.top() - p.z);
        auto distanceBottom = rweAbs(rect.bottom() - p.z);

        if (rweMin(distanceLeft, distanceRight) < rweMin(distanceTop, distanceBottom))
        {
            return distanceLeft < distanceRight ? Edge::Left : Edge::Right;
        }
        else
        {

            return distanceTop < distanceBottom ? Edge::Top : Edge::Bottom;
        }
    }

    SimVector findClosestPointOnPerimeter(const Rectangle2x<SimScalar>& rect, const SimVector& p)
    {
        bool collidesX = false;
        SimScalar x;
        if (p.x < rect.left())
        {
            x = rect.left();
        }
        else if (p.x > rect.right())
        {
            x = rect.right();
        }
        else
        {
            x = p.x;
            collidesX = true;
        }

        bool collidesZ = false;
        SimScalar z;
        if (p.z < rect.top())
        {
            z = rect.top();
        }
        else if (p.z > rect.bottom())
        {
            z = rect.bottom();
        }
        else
        {
            z = p.z;
            collidesZ = true;
        }

        // We are inside the rectangle so snap to closest edge
        if (collidesX && collidesZ)
        {
            auto closestEdge = findClosestEdge(rect, p);
            switch (closestEdge)
            {
                case Edge::Top:
                    z = rect.top();
                    break;
                case Edge::Bottom:
                    z = rect.bottom();
                    break;
                case Edge::Left:
                    x = rect.left();
                    break;
                case Edge::Right:
                    x = rect.right();
                    break;
            }
        }

        return SimVector(x, p.y, z);
    }

    SimVector findClosestPointToFootprintXZ(const MapTerrain& terrain, const DiscreteRect& footprintRect, const SimVector& p)
    {
        return findClosestPointOnPerimeter(toWorldXZRect(terrain, footprintRect), p);
    }

    SimVector findClosestPointToFootprintXZForUnit(const MapTerrain& terrain, const DiscreteRect& targetFootprintRect, const SimVector& p, int unitFootprintX, int unitFootprintZ)
    {
        auto targetWorldRect = toWorldXZRect(terrain, targetFootprintRect);
        auto footprintXWorld = SimScalar(unitFootprintX) * MapTerrain::HeightTileWidthInWorldUnits;
        auto footprintZWorld = SimScalar(unitFootprintZ) * MapTerrain::HeightTileHeightInWorldUnits;
        targetWorldRect.extents.x += footprintXWorld / 2_ss;
        targetWorldRect.extents.y += footprintZWorld / 2_ss;
        return findClosestPointOnPerimeter(targetWorldRect, p);
    }

    bool hasReachedGoal(const GameSimulation& sim, const MapTerrain& terrain, const UnitState& unit, const UnitDefinition& unitDefinition, const NavigationGoal& goal)
    {
        auto destination = match(
            goal,
            [&](const SimVector& pos) {
                // If the pathfinder found the point unreachable, getting as close
                // as we can is as good as arriving.
                if (auto moving = std::get_if<NavigationStateMoving>(&unit.navigationState.state); moving != nullptr && moving->reachableDestination)
                {
                    if (auto movingGoal = std::get_if<SimVector>(&moving->movementGoal); movingGoal != nullptr && *movingGoal == pos)
                    {
                        return std::make_optional(*moving->reachableDestination);
                    }
                }
                return std::make_optional(pos);
            },
            [&](const DiscreteRect& rect) {
                // As for points: an unreachable footprint is "reached" at the closest point we could get to.
                if (auto moving = std::get_if<NavigationStateMoving>(&unit.navigationState.state); moving != nullptr && moving->reachableDestination)
                {
                    if (auto movingGoal = std::get_if<DiscreteRect>(&moving->movementGoal); movingGoal != nullptr && *movingGoal == rect)
                    {
                        return std::make_optional(*moving->reachableDestination);
                    }
                }
                auto footprint = sim.getFootprintXZ(unitDefinition.movementCollisionInfo);
                return std::make_optional(findClosestPointToFootprintXZForUnit(terrain, rect, unit.position, footprint.first, footprint.second));
            },
            [&](const NavigationGoalLandingLocation&) {
                const auto& s = std::get_if<NavigationStateMovingToLandingSpot>(&unit.navigationState.state);
                if (s)
                {
                    return std::make_optional(s->landingLocation);
                }
                return std::optional<SimVector>();
            },
            [&](const UnitId&) {
                return std::optional<SimVector>();
            },
            [&](const FeatureId&) {
                return std::optional<SimVector>();
            });

        if (!destination)
        {
            return false;
        }

        SimVector xzPosition(unit.position.x, 0_ss, unit.position.z);
        SimVector xzDestination(destination->x, 0_ss, destination->z);
        auto distanceSquared = xzPosition.distanceSquared(xzDestination);

        // A ground unit stops on the spot, so eight units is close enough to
        // call it arrived. An aircraft is still carrying its speed and cannot
        // stop dead, so hold it to a few ticks of travel instead; asking for
        // eight units leaves it circling the spot, overshooting each time,
        // and orders that never finish.
        //
        // Landing is the exception: an aircraft touches down where it is, not
        // where it was aiming, so it has to actually be over the spot. Getting
        // this wrong puts it in the sea a few units short of the beach.
        auto tolerance = 8_ss;
        if (unitDefinition.canFly && !std::holds_alternative<NavigationGoalLandingLocation>(goal))
        {
            tolerance = rweMax(tolerance, unitDefinition.maxVelocity * 4_ss);
        }

        if (distanceSquared < (tolerance * tolerance))
        {
            return true;
        }

        return false;
    }

    std::string getAimScriptName(unsigned int weaponIndex)
    {
        switch (weaponIndex)
        {
            case 0:
                return "AimPrimary";
            case 1:
                return "AimSecondary";
            case 2:
                return "AimTertiary";
            default:
                throw std::logic_error("Invalid weapon index: " + std::to_string(weaponIndex));
        }
    }

    std::string getAimFromScriptName(unsigned int weaponIndex)
    {
        switch (weaponIndex)
        {
            case 0:
                return "AimFromPrimary";
            case 1:
                return "AimFromSecondary";
            case 2:
                return "AimFromTertiary";
            default:
                throw std::logic_error("Invalid weapon index: " + std::to_string(weaponIndex));
        }
    }

    std::string getFireScriptName(unsigned int weaponIndex)
    {
        switch (weaponIndex)
        {
            case 0:
                return "FirePrimary";
            case 1:
                return "FireSecondary";
            case 2:
                return "FireTertiary";
            default:
                throw std::logic_error("Invalid weapon index: " + std::to_string(weaponIndex));
        }
    }

    std::string getQueryScriptName(unsigned int weaponIndex)
    {
        switch (weaponIndex)
        {
            case 0:
                return "QueryPrimary";
            case 1:
                return "QuerySecondary";
            case 2:
                return "QueryTertiary";
            default:
                throw std::logic_error("Invalid wepaon index: " + std::to_string(weaponIndex));
        }
    }
}
