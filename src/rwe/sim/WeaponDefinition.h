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

        /**
         * How wide a spread the weapon shoots, in 16-bit angle units, with
         * zero meaning perfect. The original draws a fresh error for the
         * heading and another for the pitch on every shot and adds them to the
         * aim it just solved (TotalA.exe 0x49D6D7).
         */
        SimAngle accuracy;

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

        /**
         * TA noexplode, bit 22 of `wdef+0x111`, and the whole of what makes the
         * D-gun look like a beam. It does not stop the projectile detonating --
         * it stops the detonation *consuming* it. The first act of the
         * detonation routine (0x499EDE) is to mark the round dead, and this
         * flag skips that line and nothing else, so the round keeps its
         * position and its velocity, moves on the next tick and tests the next
         * cell. A disintegrator ploughing into a hillside therefore goes off
         * once a tick for the rest of its life: not one blast but up to
         * thirty-six of them, strung out along two hundred and forty world
         * units. That is the trail the player sees, and it is why the thing is
         * good against a clump rather than against one unit.
         */
        bool noExplode{false};

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

        /**
         * TA paralyzer, bit 7 of `wdef+0x111` (parsed at 0x42EB95). A hit from
         * one of these does no damage at all: the number the `[DAMAGE]` table
         * gives is a count of ticks to stun the victim for, which is what the
         * game data's own comment says and what the EMP missile's 1800 against
         * every CORE unit means -- sixty seconds, not instant death.
         */
        bool paralyzer{false};

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

        /**
         * Whether this weapon's blast hurts wreckage and scenery. Lasers do
         * not damage wreckage -- the community's standing advice for a
         * blocked assault is to force-attack the wrecks with anything else --
         * so this is cleared for beam render types at load.
         */
        bool damagesFeatures{true};

        /**
         * TA interceptor, bit 30 of `wdef+0x111`. The weapon shoots at other
         * projectiles rather than at units: the auto-target scan hands it the
         * projectile search at 0x49D120 instead of the unit one, and its blast
         * detonates every projectile inside its `areaofeffect` (0x49A664).
         */
        bool interceptor{false};

        /**
         * TA targetable, bit 29 of `wdef+0x111`. The other half of that pair --
         * what marks a projectile as something an interceptor may shoot at.
         * Four weapons carry it, and they are the four a launcher stockpiles:
         * the two nuclear missiles and the two big EMP rounds. Nothing else
         * does, so an anti-nuke will not spend itself on a Diplomat's rocket.
         */
        bool targetable{false};

        /**
         * TA coverage, `wdef+0xE0`, and an interceptor's real engagement rule --
         * the weapon's own `range` is 32000, which is the whole map. It is the
         * half-extent of a square: 0x49D18D tests |dx| and |dz| against it as an
         * unsigned compare against twice the coverage and never looks at Y, so
         * what an anti-nuke defends is a box, not a dome. Both that ship say
         * 2000.
         */
        SimScalar coverage{0};
    };
}
