#pragma once

#include <array>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * The seven numbered columns of the original's end-of-game chart, in the
     * order it lays them out and fills them: left to right across
     * `bitmaps/OUTCOME0.PCX`, which paints their headings, and one stage at a
     * time as the bars run up (the jump table at 0x4205D0). The eighth column
     * of the artwork, Name, is not one of these -- it is text rather than a
     * bar.
     */
    enum class EndGameStat
    {
        Kills = 0,
        Losses,
        EnergyProduced,
        MetalProduced,
        ExcessEnergy,
        ExcessMetal,
        Score
    };

    constexpr int EndGameStatCount = 7;

    /** One player's row. */
    struct EndGameStatRow
    {
        PlayerId player;
        std::string name;
        PlayerColorIndex color;
        std::array<int, EndGameStatCount> values;
    };

    struct EndGameStats
    {
        std::vector<EndGameStatRow> rows;

        /**
         * What a full bar means in each column: the largest value any player
         * reached, but never less than the floor the original starts the
         * column at -- ten for the two counts and a hundred for the rest
         * (0x41DCA4-0x41DD01). Without the floor a game where nobody killed
         * anything would draw one player's single kill as a full bar.
         */
        std::array<int, EndGameStatCount> maxima;
    };

    EndGameStats computeEndGameStats(const GameSimulation& simulation);
}
