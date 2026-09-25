#pragma once

#include <optional>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitState.h>

namespace rwe
{
    class MapTerrain;
    struct UnitDefinition;

    /**
     * One tick's worth of air intent: where the aircraft is being sent, and
     * whether its weapons should be hot.
     *
     * This is the seam the air module is built around. The aircraft's own
     * long-lived state lives in `AirMovementState`; the orders and navigation
     * above it produce a command per tick, the module steps the state, and the
     * caller writes back what came out. It is deliberately the narrowest value
     * that covers `step`: identity of the thing being attacked stays on
     * `AirMovementStateAttackRun`, because the attack run needs to notice when
     * its target changes and no other state does.
     */
    struct AirFrameCommand
    {
        /** The point the aircraft is steering for this tick, at altitude, if any. */
        std::optional<SimVector> targetPosition;

        /** True when the aircraft's weapons should be treated as hot this tick. */
        bool weaponsHot{false};
    };

    /**
     * The air movement module: the per-tick state machine for aircraft, moved
     * out of `UnitBehaviorService` so that it can be read and tested in one
     * place.
     *
     * It is stateless — every method is a pure transform of the state passed
     * in — so it holds no `GameSimulation`, draws no random numbers and reads
     * no terrain. Where the original draws or asks the map, the caller does it
     * and passes the result in, which is also what keeps the draws at exactly
     * the tick they were on before.
     *
     * It builds on the pure air arithmetic in `UnitBehaviorService_util.h`
     * rather than duplicating it.
     */
    class AirMovement
    {
    public:
        /**
         * The velocity an aircraft movement state is carrying, or zero for one
         * that is not moving (landing).
         */
        static SimVector airVelocity(const AirMovementState& state);

        /**
         * The bank an aircraft is holding, which in the original is honest
         * aerodynamics rather than an animation: tan(roll) = BankScale times
         * the sideways acceleration over gravity.
         *
         * The acceleration is passed through a one-pole lag, and the constants
         * are chosen so the lag's gain cancels out of the division — which is
         * what gives the ramp on entering a turn and the wash-out on leaving
         * it, with no rate limit or clamp needed anywhere.
         *
         * Mutates `physics.bankAccum`, the lag's memory.
         */
        static SimScalar bankAngle(const UnitState& unit, const UnitDefinition& unitDefinition, UnitPhysicsInfoAir& physics, const SimVector& deltaVelocity);

        /** The original's attitude arithmetic shared by bank and pitch: atan2(-scale * lateral, g / (1 - lag)), wrapped to (-pi, pi]. */
        static SimScalar attitudeAngle(SimScalar scale, SimScalar lateral, SimScalar gravityOverLagGain);

        /** The pitch off the bank accumulator as it stands, scaled by PitchScale; call after bankAngle. */
        static SimScalar pitchAngle(const UnitState& unit, const UnitDefinition& unitDefinition, const UnitPhysicsInfoAir& physics);

        /**
         * The per-tick air step: state plus intent, one tick later.
         *
         * This is every air state's velocity update in one function. For a
         * unit in level flight the command's target position, when present,
         * replaces whatever the state was already holding; for every other
         * state the movement state owns its own goal. The dogfight's goal runs
         * away from the fighter by its own velocity here, in x and z only,
         * exactly as the original's goal object integrates itself.
         */
        static AirMovementState step(const AirMovementState& state, const AirFrameCommand& command, const UnitState& unit, const UnitDefinition& unitDefinition);

        /**
         * The whole air branch of `updateUnitSpeed`: step the movement state
         * and then recompute the bank from the change in velocity. The command
         * is taken from the state's own target, so this is the dispatch
         * `updateUnitSpeed` used to do inline.
         */
        static void updateVelocity(UnitPhysicsInfoAir& physics, const UnitState& unit, const UnitDefinition& unitDefinition);

        /** True once a climbing aircraft has reached its cruise height. */
        static bool takeoffReachedCruise(const AirMovementStateTakingOff& state, SimScalar unitY, SimScalar targetHeight);

        /** The level-flight state a finished takeoff becomes, keeping heading and speed. */
        static AirMovementStateFlying finishTakeoff(const AirMovementStateTakingOff& state);

        // --- attack run ---

        /**
         * The result of stepping an attack run's phase machine.
         *
         * `previousPhase` is carried out so the caller can put the strafing
         * pass's ninety-degree break and the run-out capture back in exactly
         * the order `attackTargetAir` had them, and with the random side drawn
         * at the same point.
         */
        struct AttackRunStep
        {
            bool weaponsHot{false};
            AirMovementStateAttackRun::Phase previousPhase{AirMovementStateAttackRun::Phase::Approaching};
        };

        /**
         * Starts an attack run on a target. `targetPositionAtAltitude` is the
         * target position lifted to cruise height, which is what the run
         * steers on so it does not dive; `runOutDistance` is chosen by the
         * caller because it depends on whether the weapon is dropped.
         */
        static AirMovementStateAttackRun beginAttackRun(
            const AttackTarget& target,
            const SimVector& targetPositionAtAltitude,
            SimAngle rotation,
            const SimVector& currentVelocity,
            bool strafingPass,
            SimScalar runOutDistance);

        /**
         * Advances an attack run one tick: refresh the remembered target
         * position, step the phase machine, and turn back early if the run-out
         * would leave the map. It does not fire, does not break and does not
         * capture the run-out heading — those are the caller's, in the order
         * they had before.
         */
        static AttackRunStep stepAttackRun(
            AirMovementStateAttackRun& run,
            const UnitState& unit,
            const UnitDefinition& unitDefinition,
            const MapTerrain& terrain,
            const SimVector& targetPositionAtAltitude,
            const SimVector& targetPosition,
            SimScalar weaponMaxRange);

        /**
         * Where the strafing pass's ninety-degree break flies to: one weapon
         * range out, a quarter turn to the given side of the current heading.
         */
        static SimVector strafeBreakWaypoint(
            const SimVector& unitPosition,
            const SimVector& velocity,
            SimAngle rotation,
            SimScalar weaponMaxRange,
            bool breakLeft);

        /**
         * On the tick an attack run commits (Approaching -> Engaging), fixes
         * the line through the target the run flies out along.
         */
        static void captureRunOutDirection(
            AirMovementStateAttackRun& run,
            AirMovementStateAttackRun::Phase previousPhase,
            const SimVector& unitPosition,
            const SimVector& targetPosition,
            SimAngle rotation);

        // --- idle circuits (the original's loiter) ---

        /** The goal's arrival tolerance, 0x80 at 0x4106C4 and 0x410211. */
        static constexpr SimScalar LoiterArrivalTolerance = 128_ss;

        /** The ring is weapon range plus this, 0xA0 at 0x41064B and 0x410175. */
        static constexpr SimScalar LoiterStandoff = 160_ss;

        /** What a unit with no weapon at all uses instead: 0x1400000, 320 world units. */
        static constexpr SimScalar LoiterUnarmedRadius = 320_ss;

        /** 0x5555, a third of a turn, at 0x410634. The search circuit's step. */
        static constexpr SimAngle LoiterSeekStep = SimAngle(0x5555);

        /** 0x4000, a quarter turn, at 0x410151. The guard circuit's step. */
        static constexpr SimAngle LoiterGuardStep = SimAngle(0x4000);

        /** rand(0x2000) on top of either, at 0x410625 and 0x410142. */
        static constexpr SimAngle LoiterStepJitter = SimAngle(0x2000);

        /** How far out an aircraft holds on a circuit. */
        static SimScalar loiterRadius(bool hasPrimaryWeapon, SimScalar primaryWeaponMaxRange);

        /** A fresh bearing for a circuit, drawn once when it is armed. */
        static SimAngle loiterEntryBearing(unsigned int draw);

        /** A point on the circuit's ring. The height is the caller's to set. */
        static SimVector loiterStation(const SimVector& anchor, SimAngle bearing, SimScalar radius);

        /** Whether the aircraft has reached its current station. */
        static bool loiterArrived(const SimVector& unitPosition, const SimVector& station);

        /** The next bearing, stepped back by the base amount plus the drawn jitter. */
        static SimAngle nextLoiterBearing(SimAngle bearing, SimAngle stepBase, unsigned int draw);

        // --- repair diversion ---

        /**
         * Whether a damaged aircraft should break off what it is doing and go
         * and find a repair pad. The pad search itself needs the simulation
         * and stays with the caller; this is the gate in front of it, and it
         * is exactly the one seven of the original's VTOL mission handlers
         * open with: airborne, in front of work willing to be abandoned, not
         * already locked onto another aircraft, and hurt.
         */
        static bool canDivertForRepair(bool canFly, bool isAirborne, bool inDogfight, bool frontOrderBreaksOff, bool hurtEnough);
    };
}
