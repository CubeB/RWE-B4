#pragma once

#include <rwe/sim/GameTime.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <variant>

namespace rwe
{
    struct Projectile
    {
        std::string weaponType;

        PlayerId owner;

        /**
         * The unit that fired this projectile, if any.
         * Used to credit kills for the COB VeteranLevel query.
         * std::nullopt for projectiles spawned outside of a firing unit
         * (e.g., the explodeAs projectile spawned during killUnit).
         */
        std::optional<UnitId> attacker;

        SimVector position;
        SimVector previousPosition;

        SimVector origin;

        /** Velocity in game pixels/tick */
        SimVector velocity;

        /** The last time the projectile emitted smoke. */
        GameTime lastSmoke;

        std::unordered_map<std::string, unsigned int> damage;

        std::optional<GameTime> dieOnFrame;

        SimScalar damageRadius;

        bool groundBounce;

        bool isDead{false};

        /** The game time at which the projectile was created. */
        GameTime createdAt;

        /** The unit that this projectile is tracking, if any. */
        std::optional<UnitId> targetUnit;

        /**
         * Where the shot was aimed. A self-propelled missile that does not
         * track falls back on this once it has turned over, and a cruise
         * missile measures its run-in against it.
         */
        std::optional<SimVector> targetPosition;

        /**
         * Attitude and speed of a self-propelled missile. TA turns these and
         * works the velocity out from them, so a missile flies where it points
         * rather than where it was going.
         */
        SimAngle heading{0};
        SimAngle pitch{0};
        SimScalar speed{0};

        /** The tick the motor stops on. Unlike dieOnFrame this does not kill the projectile. */
        std::optional<GameTime> motorOutFrame;

        /** A two-phase missile has finished its blind launch and is steering. */
        bool secondPhase{false};

        /** The motor has stopped and the rest of the flight is a ballistic coast. */
        bool motorOut{false};

        SimVector getBackPosition(SimScalar duration) const;

        SimVector getPreviousBackPosition(SimScalar duration) const;

        unsigned int getDamage(const std::string& unitType) const;
    };
}
