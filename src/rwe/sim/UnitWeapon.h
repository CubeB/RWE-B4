#pragma once

#include <rwe/cob/CobThread.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/ProjectileId.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/WeaponDefinition.h>
#include <variant>

namespace rwe
{
    struct UnitWeaponStateIdle
    {
    };

    /**
     * An interceptor is aimed at a projectile rather than at a unit or a place:
     * the auto-target scan hands it 0x49D120's projectile search and aims at
     * what that returns (0x408B31, 0x48A0A0).
     */
    using UnitWeaponAttackTarget = std::variant<UnitId, SimVector, ProjectileId>;

    struct UnitWeaponStateAttacking
    {
        struct IdleInfo
        {
        };

        struct AimInfo
        {
            const CobThread* thread;
            SimAngle lastHeading;
            SimAngle lastPitch;
        };

        struct FireInfo
        {
            SimAngle heading;
            SimAngle pitch;

            SimVector targetPosition;

            std::optional<int> firingPiece;

            /** Counts how many shots the weapon has fired so far in the current burst. */
            int burstsFired;

            /** When the next shot of the burst can be fired */
            GameTime readyTime;
        };

        /** The target the weapon is currently trying to shoot at. */
        UnitWeaponAttackTarget target;

        using AttackInfo = std::variant<IdleInfo, AimInfo, FireInfo>;
        AttackInfo attackInfo;

        explicit UnitWeaponStateAttacking(const UnitWeaponAttackTarget& target) : target(target)
        {
        }
    };

    using UnitWeaponState = std::variant<UnitWeaponStateIdle, UnitWeaponStateAttacking>;

    /** Rounds a stockpiled weapon will hold before it stops building more (0x402CCA). */
    constexpr int MaxStockedRounds = 200;

    /** The original's stockpile order runs one tick in five (0x402C84). */
    constexpr int StockpileStepTicks = 5;

    /** A step the economy could not pay for is retried after this long (0x402C9C). */
    constexpr int StockpileStallRetryTicks = 10;

    /** How long a launcher with a full magazine waits before looking again (0x402CD0). */
    constexpr int StockpileFullRetryTicks = 300;

    struct UnitWeapon
    {
        std::string weaponType;

        /** The game time at which the weapon next becomes ready to fire. */
        GameTime readyTime{0};

        /** Offset from aim point to firing point, compensation for ballistics calculations. */
        SimScalar ballisticZOffset{0};

        /**
         * Rounds built and waiting, for a `stockpile` weapon. The original keeps
         * this in a byte on the unit's weapon record (unit+0x1E for weapon one)
         * and refuses to start another round once it reaches 200 (0x402CCA).
         */
        int stockedRounds{0};

        /** Rounds ordered but not yet built, the count the fire button shows as "+N". */
        int queuedRounds{0};

        /**
         * Ticks of the round currently under construction that have been paid
         * for. The original's order handler holds this on the order rather than
         * the weapon (0x402BD4, [order+0x3E]), but it belongs to the round, and
         * the round belongs to the weapon.
         */
        int stockpileProgress{0};

        /** Ticks still to wait before the next step of the build (0x402C84, 0x402C9C). */
        int stockpileStepDelay{0};

        /** The internal state of the weapon. */
        UnitWeaponState state{UnitWeaponStateIdle()};
    };
}
