#pragma once

#include <rwe/math/Vector2x.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitState.h>
#include <string>

namespace rwe
{
    SimAngle angleTo(const Vector2x<SimScalar>& lhs, const Vector2x<SimScalar>& rhs);

    SimVector toDirection(SimAngle heading, SimAngle pitch);

    SimVector rotateDirectionXZ(const SimVector& direction, SimAngle angle);

    SimScalar getTurnRadius(SimScalar speed, SimScalar turnRate);

    std::optional<SimVector> findLandingLocation(const GameSimulation& sim, ConstUnitInfo unitInfo);

    std::pair<SimAngle, SimAngle> computeHeadingAndPitch(SimAngle rotation, const SimVector& from, const SimVector& to, SimScalar speed, SimScalar gravity, SimScalar zOffset, ProjectilePhysicsType projectileType);

    std::pair<SimAngle, SimAngle> computeLineOfSightHeadingAndPitch(SimAngle rotation, const SimVector& from, const SimVector& to);

    std::pair<SimAngle, SimAngle> computeBallisticHeadingAndPitch(SimAngle rotation, const SimVector& from, const SimVector& to, SimScalar speed, SimScalar gravity, SimScalar zOffset);

    std::optional<std::pair<SimAngle, SimAngle>> computeFiringAngles(SimScalar speed, SimScalar gravity, SimScalar targetX, SimScalar targetY);

    SteeringInfo seek(const UnitState& unit, const UnitDefinition& unitDefinition, const SimVector& destination);

    SteeringInfo arrive(const UnitState& unit, const UnitDefinition& unitDefinition, const UnitPhysicsInfoGround& physics, const SimVector& destination);

    SimScalar computeNewGroundUnitSpeed(const MapTerrain& terrain, const UnitState& unit, const UnitDefinition& unitDefinition, const UnitPhysicsInfoGround& physics);

    SimVector decelerate(SimVector currentVelocity, SimScalar deceleration);

    SimVector computeNewAirUnitVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateFlying& physics);

    /**
     * Velocity update for aircraft executing an attack run. Unlike
     * computeNewAirUnitVelocity, this does NOT decelerate near the target —
     * the aircraft is meant to fly through the target, not stop on it.
     */
    SimVector computeNewAttackRunVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateAttackRun& physics);

    /**
     * Computes the target XZ position the aircraft should be steering toward
     * during the given attack run phase.
     */
    SimVector computeAttackRunTargetPoint(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateAttackRun& physics);

    /** Default run-out distance (units to fly past the target before looping). */
    SimScalar defaultAttackRunOutDistance(const UnitDefinition& unitDefinition, SimScalar weaponMaxRange);

    /**
     * Pure state-machine step for an aircraft attack run. Given the unit's
     * current XZ position, the resolved target XZ position, the weapon's max
     * range, and the current run state, this advances the run phase and
     * returns whether weapons should be hot this tick.
     *
     * The function does not mutate UnitState; the caller is responsible for
     * applying weapon target updates and writing the new run state back.
     *
     * Returns true if weapons should fire this tick (Engaging phase).
     */
    bool stepAttackRunPhase(
        const SimVector& unitPosition,
        const SimVector& targetPosition,
        SimScalar weaponMaxRange,
        AirMovementStateAttackRun& runState);

    SimVector findClosestPointToFootprintXZ(const MapTerrain& terrain, const DiscreteRect& rect, const SimVector& p);

    SimVector findClosestPointToFootprintXZForUnit(const MapTerrain& terrain, const DiscreteRect& targetFootprintRect, const SimVector& p, int unitFootprintX, int unitFootprintZ);

    bool hasReachedGoal(const GameSimulation& sim, const MapTerrain& terrain, const UnitState& unit, const UnitDefinition& unitDefinition, const NavigationGoal& goal);

    std::string getAimScriptName(unsigned int weaponIndex);
    std::string getAimFromScriptName(unsigned int weaponIndex);
    std::string getFireScriptName(unsigned int weaponIndex);
    std::string getQueryScriptName(unsigned int weaponIndex);

}
