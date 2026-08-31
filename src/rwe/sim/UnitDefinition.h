#pragma once

#include <rwe/grid/Grid.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/MovementClassId.h>
#include <rwe/sim/SimScalar.h>
#include <string>
#include <variant>

namespace rwe
{
    enum class YardMapCell
    {
        GroundPassableWhenOpen,
        WaterPassableWhenOpen,
        GroundNoFeature,
        GroundGeoPassableWhenOpen,
        Geo,
        Ground,
        GroundPassableWhenClosed,
        Water,
        GroundPassable,
        WaterPassable,
        Passable
    };

    struct UnitDefinition
    {
        struct NamedMovementClass
        {
            MovementClassId movementClassId;
        };
        struct AdHocMovementClass
        {
            unsigned int footprintX;
            unsigned int footprintZ;
            unsigned int maxSlope;
            unsigned int maxWaterSlope;
            unsigned int minWaterDepth;
            unsigned int maxWaterDepth;
        };
        using MovementCollisionInfo = std::variant<NamedMovementClass, AdHocMovementClass>;

        // FIXME: these two things should probably be in unit media info??
        // They are not needed for sim.
        std::string unitName;
        std::string unitDescription;

        std::string objectName;

        MovementCollisionInfo movementCollisionInfo;

        /**
         * Rate at which the unit turns in world angular units/tick.
         */
        SimScalar turnRate;

        /**
         * Maximum speed the unit can travel forwards in game units/tick.
         */
        SimScalar maxVelocity;

        /**
         * Speed at which the unit accelerates in game units/tick.
         */
        SimScalar acceleration;

        /**
         * Speed at which the unit brakes in game units/tick.
         */
        SimScalar brakeRate;

        bool canAttack;
        bool canMove;
        bool canGuard;
        bool canCapture;
        /** Can hide from enemy sight (TA Cloakable). Only this shows the CLOAK button. */
        bool cloakable{false};

        /** If true, the unit is considered a commander for victory conditions. */
        bool commander;

        unsigned int maxHitPoints;

        bool isMobile;

        bool floater;
        bool canHover;

        bool canFly;

        /** Units it can carry at once; 0 for anything that is not a transport. */
        unsigned int transportCapacity{0};
        /** Largest footprint (in tiles) it can carry; 0 means no limit. */
        unsigned int transportSize{0};
        bool isTransport() const { return transportCapacity > 0 || transportSize > 0; }
        unsigned int effectiveTransportCapacity() const { return transportCapacity > 0 ? transportCapacity : 1u; }

        /** Distance above the ground that the unit flies at. */
        SimScalar cruiseAltitude;

        /**
         * How hard the aircraft leans into a turn, as a multiple of a true
         * coordinated bank. One unless the FBI overrides it; the construction
         * aircraft use 1.5.
         */
        SimScalar bankScale{1_ss};

        std::string weapon1;
        std::string weapon2;
        std::string weapon3;

        std::string explodeAs;
        std::string selfDestructAs;

        bool builder;
        unsigned int buildTime;
        Energy buildCostEnergy;
        Metal buildCostMetal;

        unsigned int workerTimePerTick;

        SimScalar buildDistance;

        bool onOffable;
        bool activateWhenBuilt;

        Energy energyMake;
        Metal metalMake;
        Energy energyUse;
        Metal metalUse;

        Metal makesMetal;
        Metal extractsMetal;

        Energy energyStorage;
        Metal metalStorage;

        Energy windGenerator;

        std::optional<Grid<YardMapCell>> yardMap;
        bool yardMapContainsGeo;

        std::string corpse;

        bool hideDamage;
        bool showPlayerName;

        std::string soundCategory;

        // ---- AI/LOS classification fields (parsed from FBI) ----
        // These are read-only metadata — they do not affect physics or
        // command processing. Future LOS and AI Classifier subsystems
        // will read them.

        // Coarse TA class hint, e.g. "KBOT", "TANK", "SHIP", "PLANE",
        // "COMMANDER", "METAL", "ENERGY". Empty if absent in the FBI.
        std::string tedClass;

        // Verbatim TA category list. Space-separated tokens such as
        // "LEVEL1 KBOT WEAPON CONSTRUCT". Empty if absent.
        std::string category;

        // Sight / radar radii in TA "elmos". 0 = no LOS / no radar.
        // Consumed by the future fog-of-war subsystem; today RWE has no
        // LOS implementation so these values are stored but not used.
        unsigned int sightDistance{0};
        unsigned int radarDistance{0};
    };
}
