#include "EndGameStats.h"

#include <algorithm>

namespace rwe
{
    namespace
    {
        /**
         * The floors the original seeds its column maxima with before it walks
         * the players raising them: ten for kills and losses, a hundred for the
         * four economy columns and for the score.
         */
        constexpr std::array<int, EndGameStatCount> statFloors{10, 10, 100, 100, 100, 100, 100};

        int truncate(float v)
        {
            return static_cast<int>(v);
        }
    }

    EndGameStats computeEndGameStats(const GameSimulation& simulation)
    {
        EndGameStats stats;
        stats.maxima = statFloors;

        auto seconds = static_cast<int>(simulation.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond));

        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            const auto& player = simulation.players[i];

            EndGameStatRow row{PlayerId(i), std::string(), player.color, {}};
            row.name = player.name.value_or("Player " + std::to_string(i + 1));

            auto kills = static_cast<int>(player.unitsKilled);

            // 0x41DDBE. Each term is truncated to an integer on its own before
            // they are added -- the original converts twice, once per
            // multiply -- and the total is floored at zero, which is what makes
            // a negative timemul cost a slow player points without ever taking
            // them below nothing.
            auto score = truncate(static_cast<float>(kills) * static_cast<float>(simulation.killMul))
                + truncate(static_cast<float>(seconds) * static_cast<float>(simulation.timeMul));
            row.values[static_cast<int>(EndGameStat::Kills)] = kills;
            row.values[static_cast<int>(EndGameStat::Losses)] = static_cast<int>(player.unitsLost);
            row.values[static_cast<int>(EndGameStat::EnergyProduced)] = truncate(player.energyProduced.value);
            row.values[static_cast<int>(EndGameStat::MetalProduced)] = truncate(player.metalProduced.value);
            row.values[static_cast<int>(EndGameStat::ExcessEnergy)] = truncate(player.energyExcess.value);
            row.values[static_cast<int>(EndGameStat::ExcessMetal)] = truncate(player.metalExcess.value);
            row.values[static_cast<int>(EndGameStat::Score)] = std::max(0, score);

            for (int c = 0; c < EndGameStatCount; ++c)
            {
                stats.maxima[c] = std::max(stats.maxima[c], row.values[c]);
            }

            stats.rows.push_back(std::move(row));
        }

        return stats;
    }
}
