#pragma once

#include <string>

namespace rwe
{
    struct UnitFbi
    {
        std::string unitName;
        std::string objectName;
        std::string soundCategory;
        std::string movementClass;

        std::string name;
        std::string description;

        // Coarse class hint from TA: KBOT / TANK / SHIP / PLANE / COMMANDER
        // / METAL / ENERGY / etc. Used by the AI's UnitClassifier (Phase 2)
        // to bucket units into roles. Empty if not present in the FBI.
        std::string tedClass;

        // Space-separated category list, e.g. "LEVEL1 KBOT WEAPON CONSTRUCT".
        // The AI's UnitClassifier matches on substrings of this.
        // Stored verbatim (uppercase or whatever case the FBI used) — the
        // classifier normalises at use time.
        std::string category;

        /**
         * The category each weapon slot would rather not shoot at. The
         * original writes a single category name here and asks whether the
         * candidate's own Category list mentions it; a match does not veto
         * the target, it drops it to the back of the queue.
         */
        std::string wpriBadTargetCategory;
        std::string wsecBadTargetCategory;
        std::string wspeBadTargetCategory;

        /** The category the unit will not leave its post to go after. */
        std::string noChaseCategory;

        /**
         * Whether the unit is worth shooting without being told to. Every
         * mobile unit and every defence sets it; the fifty-four that do not
         * are the passive buildings, which is why nothing in TA spontaneously
         * opens fire on a solar collector.
         */
        bool shootMe{false};
        bool isFeature{false};

        /**
         * A crawling bomb: an attack order sends it at the target and it blows
         * itself up on arrival. `kamikaze` is bit 28 of `def+0x241` in the
         * original (parsed at 0x42CB18), `kamikazedistance` the word at
         * `def+0x218` (0x42CB29). The Roach and the Invader are the only two
         * units in the shipped data that set them, and neither has a weapon.
         */
        bool kamikaze{false};

        unsigned int kamikazeDistance{0};

        bool immuneToParalyzer{false};

        // Sight and radar radii (in TA "elmos"). The fog-of-war system in
        // Phase 2+ will use these to compute per-player visibility grids.
        // Both default to 0; LOS code must treat 0 as "no signal".
        unsigned int sightDistance{0};
        unsigned int radarDistance{0};
        unsigned int sonarDistance{0};

        /** Radii inside which this unit erases enemy radar and sonar contacts. */
        unsigned int radarDistanceJam{0};
        unsigned int sonarDistanceJam{0};

        /** Never shows up on anyone else's radar or sonar at all. */
        bool stealth{false};

        /**
         * Energy a second to stay cloaked, standing still and moving. The
         * original has no Cloakable key: a unit can cloak if and only if
         * CloakCost is greater than zero, and CloakCostMoving defaults to
         * CloakCost rather than to nothing.
         */
        float cloakCost{0.0f};
        float cloakCostMoving{0.0f};

        /**
         * How close an enemy has to come to break the cloak. Defaults to 80
         * when the FBI is silent, which is the original's own default rather
         * than a guess.
         */
        unsigned int minCloakDistance{80};

        /** Whether the unit is built already cloaked. */
        bool initCloaked{false};

        unsigned int turnRate;
        float maxVelocity;
        float acceleration;
        float brakeRate;

        unsigned int footprintX;
        unsigned int footprintZ;
        unsigned int maxSlope;
        unsigned int maxWaterSlope;
        unsigned int minWaterDepth;
        unsigned int maxWaterDepth;

        bool canAttack;
        bool canMove;
        bool canGuard;
        bool canCapture;
        bool canResurrect;
        bool canStop;
        bool canPatrol;
        bool canReclamate;
        bool canLoad;
        bool canDgun;
        bool cloakable;

        /**
         * Whether this unit offers the standing move order and standing fire
         * order buttons. They sit in the same run of capability flags as
         * CanAttack and CanMove and the original treats them the same way:
         * silence means no button, which is why a solar collector names
         * neither and a light laser tower names only the fire one. The four
         * transports set them to zero outright.
         */
        bool mobileStandOrders{false};
        bool fireStandOrders{false};

        /**
         * The order each button starts on, 0 to 2 — hold position, maneuver,
         * roam, and hold fire, return fire, fire at will. The original
         * defaults both to 2 when the key is absent, which in the shipped
         * data only ever lands on buildings: every mobile unit names a
         * StandingMoveOrder, and all but the Commander, the Sentinel, the
         * Yorktown and a vehicle plant name 1.
         */
        unsigned int standingMoveOrder{2};
        unsigned int standingFireOrder{2};

        bool commander;

        unsigned int maxDamage;

        float damageModifier;

        bool bmCode;

        bool floater;
        bool canHover;

        /**
         * How far the hull sits below the water surface, in whole world units.
         * The original keeps it in a byte and reads it with the integer
         * reader, so a fractional value in a unit file (only ARMTL has one)
         * truncates (TotalA.exe 0x42C24A, stored 0x42C259).
         */
        unsigned int waterLine;

        /**
         * How fast the unit mends itself, in hit points a second. Only the
         * two commanders name it (TotalA.exe reader 0x48AF3D).
         */
        unsigned int healTime;

        bool canFly;
        /** How many units it can carry; 0 unless it is a transport. */
        unsigned int transportCapacity;
        /** Largest footprint it can carry (TA: the unit's footprint must not exceed this). */
        unsigned int transportSize;

        /**
         * Refuses to be picked up by anything. The first thing the original's
         * load predicate asks (0x489AA3), ahead of the transport's own
         * capacity and size, so a unit that names it cannot be carried by any
         * transport at all.
         */
        bool cantBeTransported;

        /**
         * An aircraft repair pad or a carrier. Four units name it: ARMASP,
         * CORASP, ARMCARRY, CORCARRY. A damaged aircraft with nothing else to
         * do goes and lands on one.
         */
        bool isAirBase;

        /** Casts no shadow. Fifteen units name it, mostly the map props. */
        bool noShadow;

        /**
         * Whether the unit's cached bitmap carries a height plane. Every
         * shipped unit says 1 except CORFAV and CORTRUCK, and the original
         * draws a finished unit without one unsorted and unshaded.
         */
        bool zBuffer{true};

        unsigned int cruiseAlt;

        /**
         * The gunships. Exactly two units in the original data set it, the
         * Brawler and the Rapier, and it makes them work a target from a
         * standoff ring instead of running in past it.
         */
        bool hoverAttack;

        /**
         * How far past the target a bomber carries its run before turning
         * back. Only the four bombers set it: Thunder 120, Phoenix 180,
         * Shadow 220, Hurricane 290.
         */
        unsigned int attackRunLength;

        /** How far the unit will stray to fight. 640 or 1280 in the original data. */
        unsigned int maneuverLeashLength;

        /**
         * How hard an aircraft leans into a turn, as a multiple of a true
         * coordinated bank. One in the original unless the FBI says
         * otherwise, and the construction aircraft ship 1.5.
         */
        float bankScale;

        /**
         * The two speed thresholds that pick which of the MoveRate1 / MoveRate2
         * / MoveRate3 script callbacks fires while the unit is moving. Both
         * default in the original to twice MaxVelocity (0x42C1E6, 0x42C206), a
         * speed nothing can reach, so a unit that names neither is always in the
         * first band -- which is why the Atlas, which names neither, starts its
         * exhaust from MoveRate1. Seven units name the first and one the second.
         */
        float moveRate1{0.0f};
        float moveRate2{0.0f};

        std::string weapon1;
        std::string weapon2;
        std::string weapon3;

        std::string explodeAs;
        std::string selfDestructAs;

        bool builder;
        unsigned int buildTime;
        unsigned int buildCostEnergy;
        unsigned int buildCostMetal;

        unsigned int workerTime;

        unsigned int buildDistance;

        /**
         * How wide an arc a finished building is allowed to settle in, as a
         * fraction of a 16-bit turn. Eighty of the original's units name
         * one, from 1024 on a vehicle plant to 32768 on a light laser tower;
         * anything that stays silent goes up dead square.
         */
        unsigned int buildAngle{0};

        bool onOffable;
        bool activateWhenBuilt;

        float energyMake;
        float metalMake;
        float energyUse;
        float metalUse;

        float makesMetal;
        float extractsMetal;

        unsigned int energyStorage;
        unsigned int metalStorage;

        unsigned int windGenerator;
        unsigned int tidalGenerator;

        bool hideDamage;
        bool showPlayerName;

        std::string yardMap;

        std::string corpse;
    };
}
