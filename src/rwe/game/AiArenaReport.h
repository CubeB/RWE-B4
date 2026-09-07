#pragma once

#include <filesystem>
#include <map>
#include <rwe/sim/PlayerId.h>
#include <string>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Records how a computer-versus-computer game went, so a change to the AI
     * can be judged by playing it rather than by arguing about it.
     *
     * This exists because reasoning about AI changes has already been wrong
     * once: gating the whole metal search on exploration looked obviously
     * correct and measurably starved the opening, and it was two three-minute
     * runs that caught it, not thought. Twenty games and a win count is the
     * only honest way to tell whether a change helped.
     *
     * Pure observation. It reads the simulation and never touches it, so it
     * cannot affect the outcome it is measuring -- which matters more here
     * than usual, because the whole point is to compare two runs.
     */
    class AiArenaReport
    {
    public:
        /**
         * `sampleIntervalTicks` is how often a row is written. One row per
         * player per sample; a row is cheap and the interesting shape is the
         * curve, not the endpoint.
         */
        explicit AiArenaReport(unsigned int sampleIntervalTicks);

        /** Call once per simulation tick; samples on the interval. */
        void update(const GameSimulation& sim);

        /**
         * Writes the sampled rows as CSV and returns a one-line summary
         * suitable for a batch script to grep. Writing does not stop the
         * report being used again.
         */
        std::string write(const std::filesystem::path& csvPath, const GameSimulation& sim);

    private:
        struct Row
        {
            unsigned int tick;
            int player;
            std::string side;
            std::string status;
            float metal;
            float energy;
            float maxMetal;
            float maxEnergy;
            float metalIncome;
            float energyIncome;
            float metalDemand;
            float energyDemand;
            int units;
            int buildings;
            int army;
            int builders;
            int unitsLost;
            int buildingsLost;
        };

        /**
         * Every unit each player has ever owned, and whether it was a
         * building. Losses cannot be read off a count -- a count falls when a
         * unit dies and rises when one is built, and the two cancel -- so the
         * ids are kept and the dead ones counted.
         */
        std::map<int, std::map<unsigned int, bool>> everOwned;

        unsigned int sampleIntervalTicks;
        std::vector<Row> rows;

        void sample(const GameSimulation& sim);
    };
}
