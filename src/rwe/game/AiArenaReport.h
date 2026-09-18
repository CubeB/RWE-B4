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
     * Two outputs, because they answer different questions. The sampled rows
     * say how the economy moved; the events say what was built and when, and
     * that is the one a person reads when they want to know why the AI did
     * something. tools/arena-report.py turns both into a page.
     *
     * Pure observation. It reads the simulation and never touches it, so it
     * cannot affect the outcome it is measuring -- which matters more here
     * than usual, because the whole point is to compare two runs.
     */
    class AiArenaReport
    {
    public:
        /**
         * `sampleIntervalTicks` is how often an economy row is written.
         * Events are recorded every tick regardless: a build order that lands
         * two seconds earlier is exactly the sort of thing being looked for.
         */
        explicit AiArenaReport(unsigned int sampleIntervalTicks);

        /** Call once per simulation tick. */
        void update(const GameSimulation& sim);

        /**
         * Writes the economy rows and the events as two CSVs beside each
         * other, and returns a one-line summary for a batch script to grep.
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
         * One unit's whole life. Losses cannot be read off a count -- a count
         * falls when a unit dies and rises when one is built, and the two
         * cancel -- so every unit is remembered by id.
         */
        struct UnitRecord
        {
            int player;
            std::string unitType;
            bool isBuilding;
            /**
             * What the unit is for, decided here where the definition is to
             * hand rather than guessed from the name later. The timeline
             * colours by this, and "did it build army or economy" is the
             * question a person asks first.
             */
            std::string category;
            /** When it was first seen. A nanoframe counts: that is when the AI decided. */
            unsigned int bornTick;
            /** Unset while it still stands. */
            unsigned int diedTick;
            bool dead;
            /** Set once it has finished building, so "started" and "finished" can be told apart. */
            unsigned int completedTick;
            bool completed;
        };

        unsigned int sampleIntervalTicks;
        std::vector<Row> rows;
        std::map<unsigned int, UnitRecord> units;

        void sample(const GameSimulation& sim);
        void trackUnits(const GameSimulation& sim);
    };
}
