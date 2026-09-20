#include <catch2/catch_test_macros.hpp>
#include <rwe/game/EndGameStats.h>
#include <rwe/grid/Grid.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/MapTerrain.h>

namespace rwe
{
    namespace
    {
        MapTerrain makeChartTerrain()
        {
            Grid<unsigned char> heights(32, 32, static_cast<unsigned char>(0));
            return MapTerrain(std::move(heights), 0_ss);
        }

        PlayerId addChartPlayer(GameSimulation& sim, const std::string& name, unsigned int colorIndex)
        {
            GamePlayerInfo p{
                std::optional<std::string>(name),
                GamePlayerType::Human,
                PlayerColorIndex(colorIndex),
                GamePlayerStatus::Alive,
                std::string("ARM"),
                Metal(0.0f),
                Energy(0.0f),
                Metal(1000.0f),
                Energy(1000.0f),
                Metal(0.0f),
                Energy(0.0f),
            };
            return sim.addPlayer(p);
        }

        int valueOf(const EndGameStats& stats, Index row, EndGameStat stat)
        {
            return stats.rows.at(row).values.at(static_cast<int>(stat));
        }

        int maxOf(const EndGameStats& stats, EndGameStat stat)
        {
            return stats.maxima.at(static_cast<int>(stat));
        }
    }

    TEST_CASE("the end-of-game chart reads the seven columns the original draws", "[endgame]")
    {
        GameSimulation sim(makeChartTerrain(), 0u, 0, 0);
        auto us = addChartPlayer(sim, "us", 0);
        auto them = addChartPlayer(sim, "them", 1);

        auto& a = sim.getPlayer(us);
        a.unitsKilled = 12;
        a.unitsLost = 3;
        a.energyProduced = Energy(4000.0f);
        a.metalProduced = Metal(900.0f);
        a.energyExcess = Energy(250.0f);
        a.metalExcess = Metal(40.0f);

        auto& b = sim.getPlayer(them);
        b.unitsKilled = 3;
        b.unitsLost = 12;
        b.energyProduced = Energy(1500.0f);
        b.metalProduced = Metal(300.0f);

        SECTION("each column is the figure it names")
        {
            auto stats = computeEndGameStats(sim);
            REQUIRE(stats.rows.size() == 2);
            REQUIRE(stats.rows.at(0).name == "us");
            REQUIRE(stats.rows.at(1).name == "them");
            REQUIRE(stats.rows.at(0).color.value == 0);
            REQUIRE(stats.rows.at(1).color.value == 1);

            REQUIRE(valueOf(stats, 0, EndGameStat::Kills) == 12);
            REQUIRE(valueOf(stats, 0, EndGameStat::Losses) == 3);
            REQUIRE(valueOf(stats, 0, EndGameStat::EnergyProduced) == 4000);
            REQUIRE(valueOf(stats, 0, EndGameStat::MetalProduced) == 900);
            REQUIRE(valueOf(stats, 0, EndGameStat::ExcessEnergy) == 250);
            REQUIRE(valueOf(stats, 0, EndGameStat::ExcessMetal) == 40);
        }

        SECTION("a bar is measured against the best anyone managed")
        {
            auto stats = computeEndGameStats(sim);
            REQUIRE(maxOf(stats, EndGameStat::Kills) == 12);
            REQUIRE(maxOf(stats, EndGameStat::EnergyProduced) == 4000);
        }

        SECTION("but never against less than the column's floor")
        {
            // Ten for the two counts, a hundred for the rest, so a game where
            // nobody managed anything does not draw one kill as a full bar.
            GameSimulation quiet(makeChartTerrain(), 0u, 0, 0);
            addChartPlayer(quiet, "us", 0);
            quiet.getPlayer(PlayerId(0)).unitsKilled = 1;

            auto stats = computeEndGameStats(quiet);
            REQUIRE(maxOf(stats, EndGameStat::Kills) == 10);
            REQUIRE(maxOf(stats, EndGameStat::Losses) == 10);
            REQUIRE(maxOf(stats, EndGameStat::ExcessMetal) == 100);
            REQUIRE(maxOf(stats, EndGameStat::Score) == 100);
        }

        SECTION("the score is kills by killmul plus seconds by timemul")
        {
            // The default map says killmul=50 and timemul=0, which scores a
            // game on kills alone.
            sim.killMul = 50;
            sim.timeMul = 0;
            sim.gameTime = GameTime(60u * static_cast<unsigned int>(SimTicksPerSecond));

            auto stats = computeEndGameStats(sim);
            REQUIRE(valueOf(stats, 0, EndGameStat::Score) == 600);
            REQUIRE(valueOf(stats, 1, EndGameStat::Score) == 150);
        }

        SECTION("a timemul puts the clock in it as well")
        {
            sim.killMul = 10;
            sim.timeMul = 2;
            sim.gameTime = GameTime(90u * static_cast<unsigned int>(SimTicksPerSecond));

            auto stats = computeEndGameStats(sim);
            REQUIRE(valueOf(stats, 0, EndGameStat::Score) == (12 * 10) + (90 * 2));
        }

        SECTION("a negative timemul cannot take a score below nothing")
        {
            sim.killMul = 1;
            sim.timeMul = -100;
            sim.gameTime = GameTime(60u * static_cast<unsigned int>(SimTicksPerSecond));

            auto stats = computeEndGameStats(sim);
            REQUIRE(valueOf(stats, 0, EndGameStat::Score) == 0);
        }
    }
}
