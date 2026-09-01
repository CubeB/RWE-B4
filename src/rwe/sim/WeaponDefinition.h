#pragma once

#include <optional>
#include <rwe/sim/Energy.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/ProjectilePhysicsType.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <string>

namespace rwe
{
    struct WeaponDefinition
    {
        ProjectilePhysicsType physicsType;

        SimScalar maxRange;

        SimScalar reloadTime;

        /** The number of shots in a burst. */
        int burst;

        /** The amount of time between shots in the same burst, in seconds. */
        SimScalar burstInterval;

        /** Maximum angle deviation of projectiles shot in a burst. */
        SimAngle sprayAngle;

        SimAngle tolerance;

        SimAngle pitchTolerance;

        /** Projectile velocity in pixels/tick. */
        SimScalar velocity;

        /** If true, the weapon only fires on command and does not auto-target. */
        bool commandFire;

        std::unordered_map<std::string, unsigned int> damage;

        SimScalar damageRadius;

        /**
         * Fraction of the full damage still dealt at the very edge of the blast
         * (TA EdgeEffectiveness). Absent from the weapon TDF means zero, so an
         * ordinary weapon falls off to nothing at its blast radius.
         */
        SimScalar edgeEffectiveness;

        /** Number of ticks projectiles fired from this weapon live for */
        std::optional<GameTime> weaponTimer;

        /** The range in frames that projectile lifetime may randomly vary by. */
        std::optional<GameTime> randomDecay;

        /** If true, projectile does not explode when hitting the ground but instead continues travelling. */
        bool groundBounce;

        Energy energyPerShot;

        /**
         * TA metalpershot, which sits beside energypershot and is taken the same
         * way: the original tests both against the player's stores before the
         * shot and simply does not fire if either is short (0x49E3ED), then takes
         * them as the shot leaves (0x49E51F). Only eight weapons ship one and
         * they are the expensive ones -- 200 metal for an anti-nuke, 2000 for a
         * nuclear missile.
         */
        Metal metalPerShot{0};

        /**
         * TA stockpile. The weapon is not paid for out of the economy as it
         * fires; rounds are built one at a time beforehand and firing spends one
         * out of the stock (0x49E3D5, 0x49E447). A stockpiled weapon never gets
         * a reload timer -- the original skips setting one -- so it is ready
         * again the moment another round is finished.
         */
        bool stockpile{false};

        /** Percent chance that an impact sets flammable features in the blast alight. */
        unsigned int fireStarter{0};

        /** TA waterweapon: torpedoes and depth charges, which travel in the water and can only hit what is in it. */
        bool waterWeapon{false};

        /** TA toairweapon: the flak guns, which will engage nothing that is not in the air. */
        bool toAirWeapon{false};

        /**
         * TA turret: the gun sits on a mount that can swing round on its own, so
         * the unit shoots wherever its aim script points. A weapon without one is
         * bolted to the hull -- aircraft cannon, torpedo tubes, gunship rockets --
         * and the hull itself has to be brought to bear before it will fire.
         *
         * A weapon TDF that says nothing means no turret, as in the original,
         * and that is what the parser hands over. The default here is the other
         * way round on purpose, so that a definition built in code rather than
         * read from a file keeps the free-swinging behaviour rather than
         * quietly refusing to shoot.
         */
        bool turret{true};

        /**
         * TA vlaunch: the missile leaves the tube straight up and turns onto the
         * target afterwards, so which way the launcher faces does not matter.
         */
        bool verticalLaunch{false};
    };
}
