#pragma once

#include <map>
#include <optional>
#include <rwe/ai/AiSideUnits.h>
#include <rwe/ai/AiBuildTree.h>
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
     * One of ours that is not a standing building: a mobile unit, finished
     * or half-built, or a building frame that is not up yet.
     *
     * The complement of StandingBuilding, kept for the same reason and
     * diffed the same way. Nothing tracked these before, so an entire
     * production run could be destroyed as fast as it was made and no part
     * of the AI would ever hear about it -- which is exactly what a scout
     * ship parked off a shipyard does. See ROADMAP Phase 2, the all-water
     * entry.
     */
    struct StandingUnit
    {
        std::string unitType;
        SimVector position;
        bool underConstruction;
    };

    /** One of those, gone since last tick. */
    struct LostUnit
    {
        std::string unitType;
        SimVector position;
        GameTime lostAt;
        /**
         * Whether it died as a nanoframe.
         *
         * This is the whole signal on a harassed production site: a frame
         * spawns with zero hit points (hit points are trunc(progress *
         * maxdamage), TOTALA-EXE.md S:23 and the takeDamage path), so any
         * damage at all kills it, and a factory that keeps feeding hulls to
         * one gun loses the price of every one of them. A finished unit
         * dying is an ordinary fight; a frame dying where it was born is a
         * siege.
         */
        bool underConstruction;
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
     * How long a mobile or nanoframe loss stays worth reacting to, in
     * ticks. Much shorter than LossMemoryTicks because it answers a
     * different question: a building lost steers what gets rebuilt for a
     * while, where a unit lost is only evidence that something is shooting
     * at a place *now*. A shipyard refills a destroyer frame in about
     * twenty-nine ticks, so thirty seconds covers tens of cycles and still
     * clears within a second or two of the gun leaving.
     */
    constexpr unsigned int UnitLossMemoryTicks = 30u * 30u;

    /** Unit losses remembered at once. A lost battle should not crowd out the rest. */
    constexpr std::size_t MaxRememberedUnitLosses = 16;

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
        /**
         * What each of our builders is allowed to build, read out of the
         * game's own build menus. Set once before the first tick like
         * mapIntel, and for the same reason: it describes the data, not the
         * game in progress. See AiBuildTree.h.
         */
        AiBuildTree buildTree;
        /**
         * How much more a level-two assault unit is worth per metal than the
         * best of the level-one ones this side builds. Worked out once from
         * the unit data, beside the side units, because it describes the
         * game's units rather than the game in progress.
         *
         * This is what decides whether teching is worth its factory, and the
         * two sides are nothing alike: Arm's Zeus is 1.44 times a Peewee,
         * which does not repay a 2007-metal lab inside a normal game, while
         * Core's Can is 9.4 times an A.K. and repays it in under one unit.
         * See §15.7.
         */
        float advancedArmyValueRatio{0.0f};

        // --- Economy ---
        Metal currentMetal{0};
        Energy currentEnergy{0};
        Metal metalStorage{0};
        Energy energyStorage{0};
        bool metalStalled{false};
        bool energyStalled{false};
        /**
         * Last tick's production and demand for each resource.
         *
         * Stored because "am I rich" cannot be answered from the stockpile
         * alone: a base can be sitting on full energy storage and still be
         * one metal maker away from never producing anything again. Income
         * against demand is the question that matters, and nothing on the
         * blackboard could answer it before.
         */
        Metal metalIncome{0};
        Energy energyIncome{0};
        Metal metalDemand{0};
        Energy energyDemand{0};
        /**
         * Metal per second that our builders and factories will go on
         * drawing, worked out from the frames they are actually on rather
         * than read from last tick's demand. The two differ exactly when it
         * matters: the planner runs the moment a builder falls idle, and
         * last tick's demand still has that builder's finished job in it.
         * Judged against that, a 705-metal lab was "unaffordable" with 627
         * in the bank and nothing else drawing, and ten solar collectors
         * went up in its place.
         */
        Metal metalCommitted{0};

        // --- Own units ---
        std::map<std::string, int> ownedCompletedCounts;
        /**
         * Everything that exists or has been asked for, by type: complete
         * units, frames on the ground, and build orders a builder is still
         * walking to. The last is what keeps two builders from planning the
         * same thing -- a frame appears only when the builder arrives, and
         * the second builder falls idle long before that.
         */
        std::map<std::string, int> ownedTotalCounts;
        int idleBuilderCount{0};
        std::optional<UnitId> commanderUnitId;
        std::optional<SimVector> commanderPosition;
        /**
         * Whether the build pass has already given the commander something
         * to do this tick.
         *
         * A PlayerCommand takes at least a tick to reach the unit's own
         * order queue, so a later pass in the same tick cannot tell a
         * commander that was just handed a job from one that is genuinely
         * idle -- it would issue over the top of an order that has not
         * landed yet. Written by AiPlayerController around the build pass,
         * because that is the one place that knows where that pass's
         * commands begin.
         */
        bool commanderTasked{false};
        /** Where the commander first stood. Buildings are laid out around this, not around the wandering commander. */
        std::optional<SimVector> homePosition;
        std::optional<SimVector> baseAnchor;
        /** Complete, idle builders including the commander, in id order. */
        std::vector<UnitId> idleBuilders;
        /** Complete factories (immobile builders), in id order. */
        std::vector<UnitId> factories;
        /**
         * Complete metal makers, in id order: anything that can be switched
         * off and turns energy into metal. Found by what the unit does rather
         * than by name, so a mod's own maker is picked up too.
         */
        std::vector<UnitId> metalMakers;
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
        /**
         * Complete warships -- destroyer, submarine, and the scout ship --
         * in id order. Kept out of combatUnits entirely: every one of
         * ArmyManager's gather/attack/raid rules is written in terms of
         * combatUnits, so a hull that landed in there would be rallied and
         * marched at a land target the moment the phase called for it.
         * Ordered by ArmyManager::updateNavy, which is also why the scout
         * ship is here rather than in scoutUnits: ScoutManager's routes were
         * built for the ground the rest of the AI walks on, and a hull put
         * in there would have been marched at dry land.
         *
         * ScoutManager may now BORROW one of these (see navalScoutUnitId)
         * while the enemy is unfound, because sendScout knows the difference
         * between a scout that walks and one that floats. The hull it takes
         * is skipped by updateNavy for as long as it is scouting.
         */
        std::vector<UnitId> navalCombatUnits;
        /**
         * Our building frames that no builder is on, in id order: the one
         * that started them died, or was sent elsewhere. Half the metal is
         * already in them, and a frame left alone rots away (TOTALA-EXE.md
         * s93), so they come before anything new.
         */
        std::vector<UnitId> orphanedFrames;

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
        /**
         * Everything of ours that is not a standing building, keyed by raw
         * unit id: mobile units, and frames of any kind. Diffed against the
         * next tick's exactly as standingBuildings is.
         */
        std::map<unsigned int, StandingUnit> standingUnits;
        /** Mobile and nanoframe losses, most recent first, aged out after UnitLossMemoryTicks. */
        std::vector<LostUnit> recentUnitLosses;
        /**
         * Our factories that have lately lost a frame where it was born, in
         * id order. Memory only: it says something was killing our
         * production, not that anything still is.
         */
        std::vector<UnitId> harassedFactories;
        /**
         * Of those, the ones with an armed enemy sitting on them right now,
         * in id order. This is the live signal -- a factory here is being
         * besieged, and topping its queue up only feeds the gun another
         * frame.
         *
         * Written by PerceptionManager, because the memory half comes from
         * EconomyManager and the enemy half from this pass, and read by
         * BuildManager and ArmyManager.
         */
        std::vector<UnitId> besiegedFactories;
        /**
         * A building of our own that BuildManager has ordered reclaimed --
         * the extractor a moho is about to replace. Its disappearance is not
         * a loss: read as one, it would put the base on a war footing and
         * ask for the very extractor back that was just taken down on
         * purpose. Written by BuildManager, read where losses are diffed.
         */
        std::optional<UnitId> ownReclaimTarget;

        // --- Enemy ---
        /** Keyed by the enemy unit's raw id so iteration is deterministic. */
        std::map<unsigned int, KnownEnemy> knownEnemies;
        /** Centroid of the enemy's known buildings, if any have been seen. */
        std::optional<SimVector> enemyBasePosition;
        /** How many aircraft we currently believe the enemy has. */
        int knownEnemyAirCount{0};
        /** The most ARMED enemy aircraft known at once, all game. Aircraft are seen in glimpses, so the count of the moment undersells the raid that is coming back. */
        int enemyArmedAirPeak{0};
        /** Set by BuildManager while the factories are held for the first moho and reactor; for the debug panel and the log. */
        bool tierTwoReserveActive{false};
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
        /**
         * A warship borrowed for scouting while the enemy has not been found.
         *
         * On an island map nothing else can go and look: the ground scout
         * cannot leave its island and the air plant is rarely up early. Left
         * unborrowed, knownEnemies stays empty for the whole game, so the
         * Attack phase -- which wants (enemyBasePosition ||
         * !knownEnemies.empty()) as well as the army size -- never fires, and
         * everything hanging off it stays switched off, the army ferry
         * included. Measured on Hundred Isles before this existed: a side
         * holding a scout ship, three destroyers and a transport never saw
         * the enemy once in nine hundred seconds.
         *
         * ArmyManager::updateNavy skips this hull, the way the land side
         * skips scoutUnitId, so two managers never order the same ship.
         */
        std::optional<UnitId> navalScoutUnitId;
        /**
         * Whether the fleet is out. Set when navalAttackFleetSize hulls have
         * gathered at the yard, cleared when fewer than half that are left.
         *
         * It has to be remembered rather than worked out each pass. Asked
         * afresh, "are enough hulls together" stops being true the moment the
         * first of them sails, and what that looked like from the other side
         * of the water, watched in a replay, was single ships arriving one
         * after another to be sunk one after another.
         */
        bool navalSortieActive{false};

        /**
         * The commander is being shot at, or has something armed close
         * enough to start. Set by ArmyManager and kept for a few seconds past
         * the last cause so it does not flicker; while it stands the
         * commander is ArmyManager's to move and BuildManager leaves it
         * alone, and whatever can reach it goes to it.
         */
        bool commanderInDanger{false};
        /** In danger AND running from it, rather than standing to fight. Only then is it taken off BuildManager's hands. */
        bool commanderFleeing{false};
        GameTime commanderDangerUntil{0};
        unsigned int commanderLastHitPoints{0};
        /** What is threatening it, if that is known: the nearest armed enemy seen lately. */
        std::optional<UnitId> commanderThreat;
        /**
         * What the commander was last sent to fight by the safety rule, so
         * that rule can call off a chase it started -- and only one it
         * started: the other rules that hand the commander an attack order
         * own theirs.
         */
        std::optional<UnitId> commanderEngagedTarget;

        /**
         * Enemy units our radar can see and our eyes cannot, by unit id, with
         * how far each stood from the base on the last pass. A player reads a
         * blip's drift off the minimap; this is the same reading.
         */
        std::map<unsigned int, SimScalar> radarContactDistance;
        /**
         * Where an attack is coming from, when radar says one is: the middle
         * of the contacts that are inside the warning ring and closing. The
         * army forms up facing it rather than standing at the rally point
         * until the first shot lands.
         */
        std::optional<SimVector> incomingAttackFrom;
        /** When the contacts were last read -- once a second, since a blip's drift over one tick is noise. */
        GameTime radarSampleAt{0};
        /** The warning stands until here: a column that pauses has not gone home. */
        GameTime incomingAttackUntil{0};
        /** Where each scout is heading, keyed by raw unit id, so two scouts do not chase the same ground. */
        std::map<unsigned int, SimVector> scoutTargets;
        /** Units booked onto a transport, keyed by raw unit id; the other managers leave them alone. */
        std::set<unsigned int> ferryPassengers;
        /** Set when there is somewhere worth going that ground units cannot walk to. */
        bool wantsTransport{false};
        /**
         * The two halves of wantsTransport, kept apart because their union
         * cannot be read back. Observational only: nothing branches on
         * these, they exist so a log line can say which of the two is true.
         * hasExpansionSite is what the BUILDER ferry needs -- a metal patch
         * on walkable ground the base cannot reach -- and enemyAcrossWater
         * is what the ARMY ferry needs. Folded together they are useless for
         * diagnosis: a twenty-game run with eight ferries and one with none
         * both read "wants transport yes" on every line.
         */
        bool hasExpansionSite{false};
        bool enemyAcrossWater{false};
        std::optional<SimVector> rallyPoint;
        std::optional<SimVector> attackTarget;
        int armySize{0};
        /**
         * The units that set out on the attack now under way, by raw id.
         * Formed from everything at the rally point when the attack was
         * called, and never added to: a unit built after that gathers for
         * the next wave. Empty outside an attack.
         */
        std::set<unsigned int> attackGroup;
        /**
         * Set by the army pass when the wave has fallen below the retreat
         * size; the strategic pass then ends the attack. Two flags rather
         * than one test because the strategic pass runs first in the tick,
         * and on the tick an attack is called the group has not been
         * formed yet.
         */
        bool waveSpent{false};
        /**
         * A detachment sent at the enemy's outlying economy rather than at
         * the main target, by raw id. Drawn from the units gathering for the
         * next wave, never from the wave that is already out.
         */
        std::set<unsigned int> raidGroup;
        /** Where the raid is headed, if one is out. */
        std::optional<SimVector> raidTarget;

        /**
         * A build site away from the base that wants protection, and the
         * builder placing it there. Written by BuildManager the pass it
         * sends a builder to a site far enough out to be worth escorting --
         * an outpost tower, most often, since a builder placing one goes
         * there alone otherwise -- and read by ArmyManager, which is what
         * actually detaches a guard. Cleared (by ArmyManager) once the
         * builder is no longer there to protect: the job finished, the
         * builder died, or the request timed out.
         */
        struct BuildSiteGuardRequest
        {
            SimVector position;
            UnitId builderId;
            GameTime requestedAt;
            /**
             * What the influence map said could reach this site when the
             * request was made.
             *
             * Kept because there is only one slot: without it the slot goes
             * to whichever distant build order came last, so a tower going up
             * under fire loses its guard to the next extractor started in an
             * empty corner of the map. With it, the slot is held by the most
             * threatened site instead. Zero when the threat test is switched
             * off, which is what makes that case fall back to recency.
             */
            float threat{0.0f};
        };
        std::optional<BuildSiteGuardRequest> buildSiteGuardRequest;
        /**
         * The units detached to stand over a remote build site, by raw id.
         * Modelled on raidGroup: drawn from the units gathering for the next
         * wave and never from the wave that is out, and returned to the
         * reserve the same way once buildSiteGuardRequest is cleared.
         */
        std::set<unsigned int> guardGroup;
    };
}
