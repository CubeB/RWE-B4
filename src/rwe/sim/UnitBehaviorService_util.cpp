#include "UnitBehaviorService_util.h"
#include <algorithm>

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
        // TODO: make this smarter
        return unitInfo.state->position;
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
        auto currentDirection = currentVelocity.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));
        if (currentDirection == SimVector(0_ss, 0_ss, 0_ss))
        {
            return SimVector(0_ss, 0_ss, 0_ss);
        }
        auto newVelocity = currentVelocity - (currentDirection * deceleration);
        return newVelocity;
    }

    SimVector computeNewAirUnitVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateFlying& physics)
    {
        if (!physics.targetPosition)
        {
            return decelerate(physics.currentVelocity, unitDefinition.acceleration);
        }

        auto rawDirection = *physics.targetPosition - unit.position;
        auto distanceSquared = rawDirection.lengthSquared();
        auto direction = rawDirection.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));

        auto currentSpeedSquared = physics.currentVelocity.lengthSquared();
        auto decelerationDistance = currentSpeedSquared / (2_ss * unitDefinition.acceleration);

        if (distanceSquared > (decelerationDistance * decelerationDistance))
        {
            auto targetVelocity = direction * unitDefinition.maxVelocity;
            auto velocityDelta = targetVelocity - physics.currentVelocity;
            auto deltaDirection = velocityDelta.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));

            auto newVelocity = physics.currentVelocity + (deltaDirection * unitDefinition.acceleration);
            if (newVelocity.lengthSquared() > (unitDefinition.maxVelocity * unitDefinition.maxVelocity))
            {
                newVelocity = newVelocity.normalized() * unitDefinition.maxVelocity;
            }
            return newVelocity;
        }
        else
        {
            return decelerate(physics.currentVelocity, unitDefinition.acceleration);
        }
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
            // Hold heading: aim a fixed lookahead distance ahead of the unit
            // along the run-out vector. This keeps the aircraft committed to the
            // line through the target rather than orbiting above it.
            auto lookahead = unitDefinition.maxVelocity * 30_ss;
            return unit.position + (physics.runOutDirection * lookahead);
        }

        // Approaching: aim directly at the target.
        return physics.lastKnownTargetPos;
    }

    SimVector computeNewAttackRunVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateAttackRun& physics)
    {
        auto targetPoint = computeAttackRunTargetPoint(unit, unitDefinition, physics);

        auto rawDirection = targetPoint - unit.position;
        auto direction = rawDirection.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));

        // Always accelerate toward max velocity along the desired direction.
        // Unlike computeNewAirUnitVelocity, no deceleration near the target.
        auto targetVelocity = direction * unitDefinition.maxVelocity;
        auto velocityDelta = targetVelocity - physics.currentVelocity;
        auto deltaDirection = velocityDelta.normalizedOr(SimVector(0_ss, 0_ss, 0_ss));

        auto newVelocity = physics.currentVelocity + (deltaDirection * unitDefinition.acceleration);
        if (newVelocity.lengthSquared() > (unitDefinition.maxVelocity * unitDefinition.maxVelocity))
        {
            newVelocity = newVelocity.normalized() * unitDefinition.maxVelocity;
        }
        return newVelocity;
    }

    SimScalar defaultAttackRunOutDistance(const UnitDefinition& unitDefinition, SimScalar weaponMaxRange)
    {
        // Run out at least one weapon range past the target, with a floor based
        // on the cruise altitude so units that hover high don't loop too tight.
        auto altitudeFloor = unitDefinition.cruiseAltitude * 8_ss;
        return rweMax(weaponMaxRange, altitudeFloor);
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
        return dxz.lengthSquared() <= (releaseRadius * releaseRadius);
    }

    bool stepAttackRunPhase(
        const SimVector& unitPosition,
        const SimVector& targetPosition,
        SimScalar weaponMaxRange,
        AirMovementStateAttackRun& runState)
    {
        SimVector xzUnit(unitPosition.x, 0_ss, unitPosition.z);
        SimVector xzTarget(targetPosition.x, 0_ss, targetPosition.z);
        auto xzDistanceSquared = xzUnit.distanceSquared(xzTarget);
        auto maxRangeSquared = weaponMaxRange * weaponMaxRange;

        switch (runState.phase)
        {
            case AirMovementStateAttackRun::Phase::Approaching:
            {
                if (xzDistanceSquared <= maxRangeSquared)
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
                auto runOutSquared = runState.runOutDistance * runState.runOutDistance;
                if (toUnitFromTarget.lengthSquared() >= runOutSquared)
                {
                    runState.phase = AirMovementStateAttackRun::Phase::Approaching;
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

        if (distanceSquared < (8_ss * 8_ss))
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
