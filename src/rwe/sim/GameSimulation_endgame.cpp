#include "GameSimulation.h"

#include <rwe/sim/SimTicksPerSecond.h>
#include <variant>

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
