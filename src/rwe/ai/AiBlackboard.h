#pragma once

#include <map>
#include <optional>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/MapIntel.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <set>
#include <string>
#include <vector>

namespace rwe
{
    enum class GamePhase
    {
        Opening,
        Boom,
        Attack,
        Defend,
        Tech,
        Endgame,
    };

    const char* gamePhaseName(GamePhase phase);

    /** What the AI remembers about an enemy unit it has seen. */
    struct KnownEnemy
    {
        UnitId unitId;
        std::string unitType;
        SimVector lastKnownPosition;
        GameTime lastSeen;
        bool isBuilding;
        bool isArmed;
        bool isAir;
    };

    /** A completed building of ours, remembered so that losing it can be noticed. */
    struct StandingBuilding
    {
        std::string unitType;
        SimVector position;
    };

    /** One of ours that was standing last tick and is not standing now. */
    struct LostBuilding
    {
        std::string unitType;
        SimVector position;
        GameTime lostAt;
    };

    /**
     * How long the memory of enemy aircraft keeps anti-air worth building,
     * in ticks. Aircraft are fast and rarely sit still to be counted, so the
     * AI would otherwise put up a tower, lose sight of the bomber, and drop
     * the tower off its wanted list before the bomber came back. Five minutes
     * is long enough to cover a raid cycle.
     */
    constexpr unsigned int AirThreatMemoryTicks = 300u * 30u;

    /**
     * How long a loss stays worth reacting to, in ticks. Long enough that a
     * raid which flattens three buildings is still being answered while the
     * replacements go up, short enough that the AI is not still avenging
     * something from five minutes ago.
     */
    constexpr unsigned int LossMemoryTicks = 60u * 30u;

    /** Losses remembered at once. A razed base should not crowd out everything else. */
    constexpr std::size_t MaxRememberedLosses = 8;

    /**
     * Shared scratch state between the AI's managers. Rebuilt from the sim
     * every tick by the perception and economy passes; the managers only
     * ever read it and emit commands.
     */
    struct AiBlackboard
    {
        GamePhase phase{GamePhase::Opening};
        GameTime now{0};

        // --- Map ---
        /**
         * What the map looks like, and where it declares its start positions.
         * Unlike everything else here this is set once, before the first
         * tick, and never refreshed -- it describes ground that does not
         * change. See MapIntel.h for why the AI is allowed to know it.
         */
        MapIntel mapIntel;

        // --- Side ---
        bool sideUnitsResolved{false};
        AiSideUnits sideUnits;

        // --- Economy ---
        Metal currentMetal{0};
        Energy currentEnergy{0};
        Metal metalStorage{0};
        Energy energyStorage{0};
        bool metalStalled{false};
        bool energyStalled{false};

        // --- Own units ---
        std::map<std::string, int> ownedCompletedCounts;
        std::map<std::string, int> ownedTotalCounts;
        int idleBuilderCount{0};
        std::optional<UnitId> commanderUnitId;
        std::optional<SimVector> commanderPosition;
        /** Where the commander first stood. Buildings are laid out around this, not around the wandering commander. */
        std::optional<SimVector> homePosition;
        std::optional<SimVector> baseAnchor;
        /** Complete, idle builders including the commander, in id order. */
        std::vector<UnitId> idleBuilders;
        /** Complete factories (immobile builders), in id order. */
        std::vector<UnitId> factories;
        /** Complete, mobile, armed, non-builder units, in id order. */
        std::vector<UnitId> combatUnits;
        /** Complete scout planes and scout vehicles, in id order. */
        std::vector<UnitId> scoutUnits;
        /**
         * Complete mobile anti-air, in id order, kept out of combatUnits.
         *
         * These do not join the army and are not counted towards the attack
         * threshold. Anti-air that marches off with the attack is not cover,
         * and an AI that counted it as army would attack earlier for having
         * built defences.
         */
        std::vector<UnitId> antiAirUnits;
        /** Complete mobile transports, in id order. */
        std::vector<UnitId> transports;

        // --- Ground ---
        /** Whether the reachability grid has been built yet. */
        bool groundReachabilityValid{false};
        /** True when the map has ground the base cannot walk to (islands, far banks). */
        bool hasUnreachableGround{false};

        // --- Losses ---
        /**
         * Our completed buildings as of last tick, keyed by raw unit id.
         * Diffed against this tick's to notice what has been destroyed --
         * nothing else tells the AI, because the counts alone cannot say
         * whether a solar collector is missing because it blew up or because
         * one was never built.
         */
        std::map<unsigned int, StandingBuilding> standingBuildings;
        /** What we have lost lately, most recent first, aged out after LossMemoryTicks. */
        std::vector<LostBuilding> recentLosses;

        // --- Enemy ---
        /** Keyed by the enemy unit's raw id so iteration is deterministic. */
        std::map<unsigned int, KnownEnemy> knownEnemies;
        /** Centroid of the enemy's known buildings, if any have been seen. */
        std::optional<SimVector> enemyBasePosition;
        /** How many aircraft we currently believe the enemy has. */
        int knownEnemyAirCount{0};
        /** When we last actually had eyes on one. Never reset, so the memory outlives the sighting. */
        std::optional<GameTime> lastEnemyAirSeenAt;
        /**
         * Whether aircraft are worth defending against: one is in our picture
         * now, or one was within AirThreatMemoryTicks. This is what turns the
         * anti-air build on, and it is the AI's only piece of opponent
         * modelling -- it reacts to what the enemy actually has rather than
         * to a fixed plan.
         */
        bool enemyAirThreat{false};
        /** Known enemies within the defend radius of our base, in id order. */
        std::vector<UnitId> enemiesNearBase;

        // --- Army ---
        /** A combat unit pressed into scouting while there is no dedicated scout. */
        std::optional<UnitId> scoutUnitId;
        /** Where each scout is heading, keyed by raw unit id, so two scouts do not chase the same ground. */
        std::map<unsigned int, SimVector> scoutTargets;
        /** Units booked onto a transport, keyed by raw unit id; the other managers leave them alone. */
        std::set<unsigned int> ferryPassengers;
        /** Set when there is somewhere worth going that ground units cannot walk to. */
        bool wantsTransport{false};
        std::optional<SimVector> rallyPoint;
        std::optional<SimVector> attackTarget;
        int armySize{0};
    };
}
