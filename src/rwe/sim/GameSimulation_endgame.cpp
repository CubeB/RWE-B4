#include "GameSimulation.h"

#include <rwe/sim/SimTicksPerSecond.h>
#include <variant>
#include <algorithm>
#include <cmath>
#include <rwe/sim/SimRandom.h>
#include <rwe/sim/SimulationOwnership.h>

namespace rwe
{
    void GameSimulation::updateDroppedPlayers()
    {
        // BT-232, static byte-tactics reading at 97d7ac59: the remote-player
        // removal wipes every live unit with cause 3. The agreed tick is RWE's
        // lockstep mechanism; packet arrival must never decide when units die.
        for (unsigned int i = 0; i < players.size(); ++i)
        {
            auto& player = players[i];
            if (!player.removeUnitsAt || gameTime < *player.removeUnitsAt)
            {
                continue;
            }
            player.removeUnitsAt.reset();
            player.status = GamePlayerStatus::Dead;
            player.removedFromGame = true;
            player.defeatCountdown = -2;
            // Snapshot the ids: an explosion can affect other units while the
            // wipe is in progress. selfDestructUnit already ignores dead ones.
            std::vector<UnitId> owned;
            for (const auto& [id, unit] : units)
            {
                if (unit.owner == PlayerId(i) && !unit.isDead())
                {
                    owned.push_back(id);
                }
            }
            for (auto id : owned)
            {
                selfDestructUnit(id);
            }
        }
    }

    namespace
    {
        /**
         * The first positive observation sets the word to this, and each later
         * one takes one off until it goes below zero (0x464F80; the same word
         * as a mission's, at g+0x39239). Six observations thirty ticks apart
         * make the hundred and fifty ticks between the first sighting of a
         * result and the latch.
         */
        constexpr int EndCountdownStart = 4;
    }

    void GameSimulation::updateEndCountdown()
    {
        // A mission's rules run their own countdown with the same numbers.
        if (missionRules || players.size() < 2)
        {
            return;
        }

        // Once a second, on the settle's tick, for the local player's pass.
        if (gameTime.value % SimTicksPerSecond != 0)
        {
            return;
        }

        updatePlayerDefeats();

        // Latched for good.
        if (!std::holds_alternative<WinStatusUndecided>(gameStatus))
        {
            return;
        }

        // A false observation neither resets nor advances the word.
        auto observed = computeWinStatus();
        if (std::holds_alternative<WinStatusUndecided>(observed))
        {
            return;
        }

        if (endCountdown < 0)
        {
            endCountdown = EndCountdownStart;
            return;
        }

        --endCountdown;
        if (endCountdown < 0)
        {
            gameStatus = observed;
        }
    }

    void GameSimulation::wipeCommanderArmy(PlayerId playerId)
    {
        // BT-83 supplement: direct 30000 damage, cause 3. Retain the
        // existing cause-3 death path (its Killed ladder is tracked in #409).
        if (!simulatesLocally(*this, playerId))
        {
            return;
        }
        std::vector<UnitId> owned;
        for (const auto& [id, unit] : units)
        {
            if (unit.owner == playerId && unit.isAlive())
            {
                owned.push_back(id);
            }
        }
        for (auto id : owned)
        {
            auto& unit = getUnitState(id);
            if (unit.isDead())
            {
                continue;
            }
            const auto& definition = unitDefinitions.at(unit.unitType);
            const auto tier = std::min<unsigned int>(25u, veterancyLevel(veterancyRules(definition.veterancy, communityPatchMode), unit.kills));
            const auto damage = (25u - tier) * 30000u * 4u / 100u;
            if (unit.hitPoints > damage)
            {
                unit.hitPoints -= damage;
            }
            else
            {
                selfDestructUnit(id);
            }
        }
        const auto living = std::any_of(units.begin(), units.end(), [&](const auto& entry) {
            return entry.second.owner == playerId && entry.second.isAlive();
        });
        getPlayer(playerId).status = living ? GamePlayerStatus::Alive : GamePlayerStatus::Dead;
    }

    void GameSimulation::updatePlayerDefeats()
    {
        // The original checks its local human. All RWE seats share one
        // deterministic world; the documented adaptation applies to each.
        for (unsigned int i = 0; i < players.size(); ++i)
        {
            auto& player = players[i];
            if (player.status != GamePlayerStatus::Dead || player.defeatCountdown == -2
                || player.removedFromGame || !simulatesLocally(*this, PlayerId(i)))
            {
                continue;
            }
            if (player.defeatCountdown == -1)
            {
                player.defeatCountdown = EndCountdownStart;
            }
            else if (--player.defeatCountdown < 0)
            {
                if (commanderDeathMode == CommanderDeathMode::Deathmatch)
                {
                    player.defeatCountdown = respawnDefeatedPlayer(PlayerId(i)) ? -1 : 0;
                }
                else
                {
                    player.defeatCountdown = -2;
                }
            }
        }
    }

    bool GameSimulation::respawnDefeatedPlayer(PlayerId playerId)
    {
        // #699 supplement, 0x4651B2: integer candidates within 10% margins,
        // nine widely spaced footprint-origin probes, then the last attempt.
        auto& player = getPlayer(playerId);
        auto definition = unitDefinitions.find(player.commanderType);
        if (definition == unitDefinitions.end())
        {
            return false;
        }
        const auto& def = definition->second;
        const auto mc = getAdHocMovementClass(def.movementCollisionInfo);
        const int width = static_cast<int>(terrain.getWidthInWorldUnits().value);
        const int height = static_cast<int>(terrain.getHeightInWorldUnits().value);
        const int marginX = width / 10;
        const int marginZ = height / 10;
        auto draw = [&](int range) { return range < 2 ? 0 : static_cast<int>(randomBelow(rng, static_cast<unsigned int>(range))); };
        SimVector position;
        for (int attempt = 0; attempt < 9999; ++attempt)
        {
            auto x = marginX + draw(width - 2 * marginX);
            auto z = marginZ + draw(height - 2 * marginZ);
            position = terrain.topLeftCoordinateToWorld(SimVector(intToSimScalar(x), 0_ss, intToSimScalar(z)));
            bool suitable = true;
            for (int dz = -1; dz <= 1 && suitable; ++dz)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    auto probe = position + SimVector(intToSimScalar(dx * terrain.getHeightMap().getWidth()), 0_ss,
                        intToSimScalar(dz * terrain.getHeightMap().getHeight()));
                    auto cell = terrain.worldToHeightmapCoordinate(probe);
                    if (cell.x < 0 || cell.y < 0 || !canBeBuiltAt(mc, std::nullopt, false,
                            static_cast<unsigned int>(cell.x), static_cast<unsigned int>(cell.y), 0u))
                    {
                        suitable = false;
                        break;
                    }
                }
            }
            auto cell = occupiedGrid.tryGet(terrain.worldToHeightmapCoordinate(position));
            suitable = suitable && cell && !cell->get().featureId;
            suitable = suitable && (!lavaWorld || terrain.getHeightAt(position.x, position.z) > terrain.getSeaLevel());
            if (suitable)
            {
                break;
            }
        }
        auto created = trySpawnCompletedUnit(player.commanderType, playerId, position, std::nullopt);
        if (!created)
        {
            return false;
        }
        player.status = GamePlayerStatus::Alive;
        player.hasBaseStorage = true;
        ++player.respawnCount;
        // The normal settle applies the seat's income multiplier once.
        addResourceDelta(*created, player.startingEnergy, player.startingMetal);
        return true;
    }

    bool GameSimulation::isEndCountdownFreezingEconomy() const
    {
        // The per-player economy call runs only while the latch is clear and
        // the word is still negative (0x401360 from 0x464F80): from the first
        // positive observation on, nobody settles any more.
        return endCountdown >= 0 || !std::holds_alternative<WinStatusUndecided>(gameStatus);
    }

    WinStatus GameSimulation::decidedStatus() const
    {
        // A mission's outcome is its own latch.
        if (missionRules)
        {
            return computeWinStatus();
        }

        return gameStatus;
    }
}
