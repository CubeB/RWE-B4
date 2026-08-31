#pragma once

#include <rwe/sim/GameTime.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <variant>

namespace rwe
{
    struct ProjectilePhysicsTypeLineOfSight
    {
    };
    struct ProjectilePhysicsTypeBallistic
    {
    };
    struct ProjectilePhysicsTypeTracking
    {
        /**
         * Rate at which the projectile turns to face its target in world angular units/tick.
         */
        SimScalar turnRate;
    };
    /**
     * Bomb dropped from a bomber. Same physics as Ballistic (gravity-affected),
     * but bombs are released using a "bombsight" predicate rather than the
     * ground-unit aim-then-fire flow: when the predicted ballistic impact
     * point matches the target, the bomb is released and inherits the
     * aircraft's velocity (rather than being launched along the firing-piece
     * heading). Discriminator in TA weapondefs is the `dropped=1` flag, which
     * normally appears alongside `ballistic=1` (see ARM STILBOMB and similar).
     */
    struct ProjectilePhysicsTypeBomb
    {
    };

    /**
     * A missile with a motor: TA's `selfprop=1`. It carries its own heading,
     * pitch and speed, and derives its velocity from those every tick rather
     * than steering the velocity vector (0x49B9AE). The motor burns for a
     * fixed number of ticks and the missile then coasts under gravity, which
     * is why a missile fired at the very edge of its range arrives unguided.
     */
    struct ProjectilePhysicsTypeSelfPropelled
    {
        /** Speed off the rail in world units/tick; zero for a missile that has to build its speed up. */
        SimScalar startVelocity;

        /** Added to the speed every tick the motor burns. */
        SimScalar acceleration;

        /** A ceiling the motor works up to, not a speed the missile is launched at. */
        SimScalar maxVelocity;

        /** How far the heading and the pitch may each step in one tick. */
        SimAngle turnRate;

        /** Whether the missile steers at all. Two-phase missiles steer regardless once they have turned over. */
        bool guidance;

        /** Whether the missile keeps chasing the unit it was fired at, or finishes at the place it was aimed. */
        bool tracks;

        /** Fly the launch blind, then turn over and burn again for flightTime. */
        bool twoPhase;

        /** Leave the tube pointing straight up and standing still. */
        bool vLaunch;

        /** How long the second phase's motor burns for. */
        GameTime flightTime;

        /** Detonate on running out of motor, or on losing the target behind, instead of coasting on. */
        bool burnBlow;

        /** Hold a fixed altitude until close to the aim point, then come down on it. */
        bool cruise;

        /** Take the burn time from the weapon's range rather than from its weaponTimer. */
        bool autoRange;
    };

    using ProjectilePhysicsType = std::variant<ProjectilePhysicsTypeLineOfSight, ProjectilePhysicsTypeBallistic, ProjectilePhysicsTypeTracking, ProjectilePhysicsTypeBomb, ProjectilePhysicsTypeSelfPropelled>;
}
