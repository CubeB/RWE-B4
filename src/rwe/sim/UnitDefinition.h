#pragma once

#include <array>
#include <rwe/grid/Grid.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/MovementClassId.h>
#include <rwe/sim/SimAngle.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitMovementOrders.h>
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

    /**
     * Whether a unit's Category list names the given category. The original
     * interns every category name to a bit and asks a bitmask; the answer is
     * the same either way and the lists are half a dozen words long, so we
     * compare the words. An empty name never matches, which is how a unit
     * with no bad target category is spelled.
     */
    bool categoryListContains(const std::string& categoryList, const std::string& category);

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
         * The two speed thresholds the move-rate script callbacks are banded
         * by, in the same game units/tick as maxVelocity. At or below the
         * first, a moving unit is calling MoveRate1; between the two,
         * MoveRate2; above the second, MoveRate3. The original defaults both
         * to twice MaxVelocity (0x42C1E6, 0x42C206) so that a unit naming
         * neither never leaves the first band.
         */
        SimScalar moveRate1;
        SimScalar moveRate2;

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
        bool canStop{false};
        bool canPatrol{false};
        bool canReclamate{false};
        /**
         * TA canload. Exactly four units name it -- armatlas, armtship,
         * cortship and corvalk -- and it is what puts LOAD and UNLOAD on the
         * order menu. Kept separate from being able to carry anything, which
         * is what transportCapacity says.
         */
        bool canLoad{false};
        /**
         * TA candgun, bit 14 of the unit definition's second capability word
         * (parsed at 0x42CAA3). Only the two commanders name it, and it is what
         * puts the D-gun on their order menu; the weapon behind the button is an
         * ordinary `commandfire` weapon with an enormous damage number.
         */
        bool canDgun{false};

        /**
         * Can hide from enemy sight. Only this shows the CLOAK button. The
         * original has no Cloakable key at all and derives this from CloakCost
         * being greater than zero; nothing in the shipped data writes the key,
         * so honouring it alone gives the button to nothing.
         */
        bool cloakable{false};

        /**
         * Whether the standing move order and standing fire order buttons are
         * offered for this unit at all. The original gathers the buttons for a
         * selection out of these two flags and refuses to cycle an order the
         * unit never advertised, so a transport cannot be told to hold fire and
         * a solar collector is offered nothing.
         */
        bool mobileStandOrders{false};
        bool fireStandOrders{false};

        /**
         * What the two standing orders start on when the unit is built. The
         * original copies them out of the definition once, in the same routine
         * that gives a fresh unit its type index and frees its weapons; from
         * then on they are the unit's own state and the buttons move them.
         * They are what stops a nuke silo launching at the first thing that
         * wanders past and what keeps the Commander from strolling off.
         */
        UnitMovementOrders standingMoveOrder{UnitMovementOrders::Roam};
        UnitFireOrders standingFireOrder{UnitFireOrders::FireAtWill};

        /** If true, the unit is considered a commander for victory conditions. */
        bool commander;

        unsigned int maxHitPoints;

        /**
         * TA DamageModifier, in 16.16 fixed point exactly as the original
         * stores it, and only consulted while the unit's script has declared
         * it armoured. The original parses the key to fixed point at load
         * time and multiplies in integers, so keeping the same representation
         * keeps the rounding identical.
         */
        int damageModifier{0x10000};

        bool isMobile;

        bool floater;
        bool canHover;

        /**
         * How far below the water surface the hull rides, in whole world
         * units. The original uses it in two places (TotalA.exe 0x43D72E and
         * 0x43DBA9); see the findings doc for why only the second of them has
         * any effect on shipped data.
         */
        unsigned int waterLine{0};

        /**
         * Hit points a second the unit mends itself at, zero for no self
         * repair. See GameSimulation::updateSelfRepair.
         */
        unsigned int healTime{0};

        bool canFly;

        /** Units it can carry at once; 0 for anything that is not a transport. */
        unsigned int transportCapacity{0};
        /** Largest footprint (in tiles) it can carry; 0 means no limit. */
        unsigned int transportSize{0};
        bool isTransport() const { return transportCapacity > 0 || transportSize > 0; }
        unsigned int effectiveTransportCapacity() const { return transportCapacity > 0 ? transportCapacity : 1u; }

        /**
         * Refuses to ride in anything. This is the first question the
         * original's load predicate asks (0x489AA3), before it looks at the
         * transport at all, so a unit that sets it cannot be carried by
         * anything however much room there is. The Sumo is the one unit in
         * the shipped data that does.
         */
        bool cantBeTransported{false};

        /**
         * An aircraft repair pad or a carrier: the four units that name it
         * are ARMASP, CORASP, ARMCARRY and CORCARRY. A damaged aircraft that
         * has run out of orders goes and lands on the nearest one rather than
         * setting down where it stands.
         */
        bool isAirBase{false};

        /**
         * Casts no shadow. Fifteen units in the shipped data set it, mostly
         * the map props that are not really objects standing on the ground.
         */
        bool noShadow{false};

        /** Distance above the ground that the unit flies at. */
        SimScalar cruiseAltitude;

        /**
         * How hard the aircraft leans into a turn, as a multiple of a true
         * coordinated bank. One unless the FBI overrides it; the construction
         * aircraft use 1.5.
         */
        SimScalar bankScale{1_ss};

        /**
         * Set on the gunships. The Brawler and the Rapier are the only two
         * units in the original data that carry it, and it makes them hold a
         * standoff ring and work their way around a target rather than
         * running in past it the way a bomber does.
         */
        bool hoverAttack{false};

        /**
         * How far past the target a bomber carries its run before turning
         * back for another, and part of how early it lets the bombs go.
         * Only the four bombers set it: Thunder 120, Phoenix 180, Shadow 220,
         * Hurricane 290. Zero for everything else.
         */
        SimScalar attackRunLength{0_ss};

        /**
         * How far the unit will stray from where it was told to be in order
         * to fight. 640 for most things, 1280 for aircraft and ships.
         */
        SimScalar maneuverLeashLength{0_ss};

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

        /**
         * How wide an arc this building settles in when it goes up, centred
         * on the stock facing. The original reads it straight out of the FBI
         * and it varies by a factor of thirty-two: a vehicle plant barely
         * moves at 1024, a light laser tower swings the full 32768. Zero for
         * the shipyards, the aircraft plants and everything mobile, which go
         * up square.
         */
        SimAngle buildAngle{0};

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
        Energy tidalGenerator;

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

        /**
         * The category each weapon slot would rather not shoot at, indexed
         * the way the weapons are: primary, secondary, tertiary. A candidate
         * whose own category list names it is still a legal target, it is
         * just kept until nothing better is left.
         */
        std::array<std::string, 3> badTargetCategory;

        /** The category the unit will not leave its post to go after. */
        std::string noChaseCategory;

        /**
         * Whether the unit is worth shooting at unbidden. False on the
         * passive buildings, which is why an idle tank ignores an enemy
         * solar collector it is standing next to.
         */
        bool shootMe{false};

        /**
         * A crawling bomb. An attack order on one of these does not fire a
         * weapon -- neither the Roach nor the Invader has one -- it closes to
         * `kamikazeDistance` of the target and self-destructs. The original
         * floors the distance at sixteen world units (0x403364).
         */
        bool kamikaze{false};

        unsigned int kamikazeDistance{0};

        /**
         * TA immunetoparalyzer, bit 26 of `def+0x241` (parsed at 0x42C7FA and
         * tested at 0x489E39). Both commanders set it, which is why an EMP
         * missile cannot simply switch a game off.
         */
        bool immuneToParalyzer{false};

        // Sight / radar / sonar radii in TA "elmos". 0 = no LOS / no radar /
        // no sonar. Consumed by the visibility pass in GameSimulation:
        // sight is capped at 8 vision cells, radar range is extended by twice
        // the detecting unit's altitude, and sonar range is flat.
        unsigned int sightDistance{0};
        unsigned int radarDistance{0};
        unsigned int sonarDistance{0};

        /**
         * Radii inside which this unit erases everyone else's radar and sonar
         * contacts. A jammer hides what is in the bubble rather than producing
         * false contacts, it has to be switched on, and it leaves its own
         * owner's picture alone.
         */
        unsigned int radarDistanceJam{0};
        unsigned int sonarDistanceJam{0};

        /** Never becomes a radar or a sonar contact for anybody. */
        bool stealth{false};

        /**
         * Energy per second to stay cloaked, standing still and moving. The
         * original truncates both to whole numbers before it spends them and
         * takes nothing at all when the player cannot afford the whole amount,
         * so a unit short of energy decloaks rather than running a deficit.
         */
        Energy cloakCost{0};
        Energy cloakCostMoving{0};

        /**
         * How close a live enemy has to come to break the cloak. It is a
         * proximity fuse rather than a refusal to cloak: coming inside it
         * decloaks the unit for three seconds, counted from the last tick an
         * enemy was that close.
         */
        unsigned int minCloakDistance{80};

        /** Whether the unit is built with its cloak already asked for. */
        bool initCloaked{false};
    };
}
