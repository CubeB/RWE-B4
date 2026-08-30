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

    /**
     * Speed for the next tick. maxSlope is the unit's movement class slope
     * limit; climbing slows the unit down in proportion to how close the
     * ground ahead is to that limit.
     */
    SimScalar computeNewGroundUnitSpeed(const MapTerrain& terrain, const UnitState& unit, const UnitDefinition& unitDefinition, const UnitPhysicsInfoGround& physics, unsigned int maxSlope);

    /** Multiplier in (0, 1] applied to a unit's top speed for the ground directly ahead of it. */
    SimScalar computeSlopeSpeedFactor(const MapTerrain& terrain, const UnitState& unit, unsigned int maxSlope);

    SimVector decelerate(SimVector currentVelocity, SimScalar deceleration);

    SimVector computeNewAirUnitVelocity(const UnitState& unit, const UnitDefinition& unitDefinition, const AirMovementStateFlying& physics);

    /**
     * The tightest circle the aircraft can fly at full speed, from its own
     * FBI stats: speed divided by turn rate. Everything about an attack run
     * — how far it runs out, when it commits to the run — is measured in
     * these, so a nimble fighter loops tightly and a heavy bomber sweeps wide.
     */
    SimScalar attackRunTurnRadius(const UnitDefinition& unitDefinition);

    /** The shape of one aircraft's attack pattern, derived from its stats and its weapon. */
    struct AttackRunGeometry
    {
        /** Tightest circle the aircraft can fly at speed. */
        SimScalar turnRadius;

        /**
         * How close, and how well lined up, before it stops manoeuvring and
         * holds a straight line through the target so the sight can settle.
         */
        SimScalar commitDistance;
        SimAngle commitAngle;

        /** How far past the target it flies before turning back for another pass. */
        SimScalar runOutDistance;
    };

    AttackRunGeometry computeAttackRunGeometry(const UnitDefinition& unitDefinition, SimScalar weaponMaxRange);

    /**
     * Velocity update for aircraft executing an attack run. The aircraft
     * never brakes — it holds speed and banks, turning its heading at no more
     * than its own turn rate, so reversing course is an arc rather than a
     * stop and a pivot on the spot.
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
     * Predicts where a bomb dropped right now would land, given the bomber's
     * current world position and per-tick velocity. Uses the same gravity
     * model as ProjectilePhysicsTypeBallistic / Bomb (-112 / (30*30) per tick).
     *
     * Returns the world XZ point of impact at altitude `groundY`, computed
     * deterministically with rweSqrt. Falls back to the bomber's XZ position
     * if the unit is at or below the ground (degenerate case).
     */
    SimVector predictBombImpactPoint(const SimVector& bomberPosition, const SimVector& bomberVelocity, SimScalar groundY);

    /**
     * Bombsight release predicate. Returns true if a bomb dropped now would
     * land within `releaseRadius` (XZ) of the target. The predicate is
     * deterministic and uses fixed-point math throughout.
     *
     * The release radius should typically be the weapon's damage radius plus
     * a tolerance — too small and bombers never find a release window; too
     * large and bombs miss visibly. The caller is responsible for picking it.
     */
    bool bombsightInReleaseWindow(
        const SimVector& bomberPosition,
        const SimVector& bomberVelocity,
        const SimVector& targetPosition,
        SimScalar releaseRadius);

    /**
     * Pure state-machine step for an aircraft attack run. Given the unit's
     * current XZ position and heading, the resolved target XZ position, the
     * geometry of its attack pattern and the current run state, this advances
     * the run phase and returns whether weapons should be hot this tick.
     *
     * The aircraft only commits to a run (Engaging) once it is both close
     * enough and lined up; if it finds itself inside its own turn circle it
     * extends away first rather than spiralling around the target, which is
     * what makes a bomber circle forever without ever dropping.
     *
     * The function does not mutate UnitState; the caller is responsible for
     * applying weapon target updates and writing the new run state back.
     *
     * Returns true if weapons should fire this tick (Engaging phase).
     */
    bool stepAttackRunPhase(
        const SimVector& unitPosition,
        const SimVector& unitHeading,
        const SimVector& targetPosition,
        const AttackRunGeometry& geometry,
        AirMovementStateAttackRun& runState);

    SimVector findClosestPointToFootprintXZ(const MapTerrain& terrain, const DiscreteRect& rect, const SimVector& p);

    SimVector findClosestPointToFootprintXZForUnit(const MapTerrain& terrain, const DiscreteRect& targetFootprintRect, const SimVector& p, int unitFootprintX, int unitFootprintZ);

    bool hasReachedGoal(const GameSimulation& sim, const MapTerrain& terrain, const UnitState& unit, const UnitDefinition& unitDefinition, const NavigationGoal& goal);

    std::string getAimScriptName(unsigned int weaponIndex);
    std::string getAimFromScriptName(unsigned int weaponIndex);
    std::string getFireScriptName(unsigned int weaponIndex);
    std::string getQueryScriptName(unsigned int weaponIndex);

}
