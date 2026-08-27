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

        // Sight and radar radii (in TA "elmos"). The fog-of-war system in
        // Phase 2+ will use these to compute per-player visibility grids.
        // Both default to 0; LOS code must treat 0 as "no signal".
        unsigned int sightDistance{0};
        unsigned int radarDistance{0};

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

        bool commander;

        unsigned int maxDamage;

        bool bmCode;

        bool floater;
        bool canHover;

        bool canFly;

        unsigned int cruiseAlt;

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
