#pragma once

#include <rwe/sim/PlayerId.h>
#include <rwe/sim/ProjectileId.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitWeapon.h>
#include <optional>
#include <string>

namespace rwe
{
    /**
     * Everything a weapon round needs to come into existence, and nothing
     * else: `createProjectileFromWeapon` and `spawnProjectile` each set every
     * one of these fields on, or from, the `Projectile` that results, so the
     * descriptor carries exactly what the spawn consumes rather than a
     * positional list that re-types it. It is a spawn parameter object, in the
     * shape of the order structures of `UnitOrder.h`.
     *
     * One of the two weapon doors is `weapon`, a weapon already resolved off
     * the firing unit, and the other is `weaponType`, a type to look up
     * directly -- which is what a death blast names (`explodeAs` strings, not
     * unit weapons) and what the interception tests spawn by. Exactly one
     * must be filled in.
     */
    struct ProjectileSpawn
    {
        PlayerId owner;
        /** Weapon resolved off the unit that fires; its `weaponType` is the one used. */
        const UnitWeapon* weapon{};
        /** Weapon type to look up directly, when no unit weapon is at hand. */
        std::string weaponType{};
        SimVector position;
        SimVector direction;
        SimScalar distanceToTarget;
        std::optional<UnitId> targetUnit{};
        std::optional<UnitId> attacker{};
        std::optional<SimVector> inheritedVelocity{};
        std::optional<SimVector> targetPosition{};
        std::optional<ProjectileId> targetProjectile{};
    };
}
