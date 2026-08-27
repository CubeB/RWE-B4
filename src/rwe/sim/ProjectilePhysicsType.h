#pragma once

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

    using ProjectilePhysicsType = std::variant<ProjectilePhysicsTypeLineOfSight, ProjectilePhysicsTypeBallistic, ProjectilePhysicsTypeTracking, ProjectilePhysicsTypeBomb>;
}
