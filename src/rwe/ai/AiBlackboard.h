#pragma once

#include <map>
#include <optional>
#include <rwe/sim/Energy.h>
#include <rwe/sim/GameTime.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
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
    };

    /**
     * Shared scratch state between the AI's managers. Rebuilt from the sim
     * every tick by the perception and economy passes; the managers only
     * ever read it and emit commands.
     */
    struct AiBlackboard
    {
        GamePhase phase{GamePhase::Opening};
        GameTime now{0};

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

        // --- Enemy ---
        /** Keyed by the enemy unit's raw id so iteration is deterministic. */
        std::map<unsigned int, KnownEnemy> knownEnemies;
        /** Centroid of the enemy's known buildings, if any have been seen. */
        std::optional<SimVector> enemyBasePosition;
        /** Known enemies within the defend radius of our base, in id order. */
        std::vector<UnitId> enemiesNearBase;

        // --- Army ---
        std::optional<UnitId> scoutUnitId;
        std::optional<SimVector> rallyPoint;
        std::optional<SimVector> attackTarget;
        int armySize{0};
    };
}
