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

        // Sight and radar radii (in TA "elmos"). The fog-of-war system in
        // Phase 2+ will use these to compute per-player visibility grids.
        // Both default to 0; LOS code must treat 0 as "no signal".
        unsigned int sightDistance{0};
        unsigned int radarDistance{0};
        unsigned int sonarDistance{0};

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
        bool cloakable;

        bool commander;

        unsigned int maxDamage;

        bool bmCode;

        bool floater;
        bool canHover;

        bool canFly;
        /** How many units it can carry; 0 unless it is a transport. */
        unsigned int transportCapacity;
        /** Largest footprint it can carry (TA: the unit's footprint must not exceed this). */
        unsigned int transportSize;

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

        bool hideDamage;
        bool showPlayerName;

        std::string yardMap;

        std::string corpse;
    };
}
