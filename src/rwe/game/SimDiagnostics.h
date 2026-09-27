#pragma once

#include <chrono>
#include <fstream>
#include <optional>
#include <rwe/sim/GameHash.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * A delay RWE_SIM_LAG asked to put behind every tick, from a tick onwards,
     * so a peer whose machine cannot keep up can be reproduced on one that can.
     * Wall clock only: it changes when ticks run, never what they contain.
     */
    struct SimLag
    {
        std::chrono::milliseconds delay{0};
        unsigned int fromTick{0};
    };

    /**
     * Parses RWE_SIM_LAG: "<ms>", or "<ms>@<fromTick>" to start part way
     * through. Nothing for unset or unreadable input -- the switch is a test
     * aid, and a spec that cannot be read is better ignored loudly than
     * guessed at.
     */
    std::optional<SimLag> parseSimLag(const char* spec);

    /**
     * The observers a determinism hunt runs on, shared by the game scene and
     * the headless arena so the same env switches mean the same thing in both.
     *
     * RWE_HASH_LOG=<file> writes one line a tick, the tick and the sync hash;
     * a game and the replay of it should write the same file, and the first
     * line that differs is the tick a fault showed itself on.
     * RWE_STATE_DUMP=<first>:<last>:<prefix> (with RWE_STATE_DUMP_STEP) writes
     * the full saved state as JSON for each tick in the range.
     * RWE_SIM_LAG=<ms>[@<fromTick>] sleeps that long after every tick from that
     * tick onwards, for reproducing a peer that cannot keep up. It costs wall
     * time and nothing else.
     *
     * RWE_DESYNC_AT=<tick> is the odd one out: it is the only switch here that
     * changes what the peer says rather than only watching it. Set on one peer
     * of a network game, it makes that peer report a wrong sync hash from that
     * tick onwards -- the simulation is untouched, so this counterfeits a
     * desync rather than causing one, which is what makes it safe to leave in.
     * It exists because the desync report is otherwise only reachable by
     * finding a real desync, and a diagnostic nobody can fire is a diagnostic
     * nobody has read.
     */
    class SimDiagnostics
    {
    public:
        SimDiagnostics();

        /** Whether a hash log was asked for, which is also when the hash has to be computed in a replay. */
        bool hashLogEnabled() const;

        /** Computes and records the tick's hash and, if asked for, its state dump. Returns the hash. */
        GameHash record(const GameSimulation& simulation, unsigned int tick);

        /** The delay to sleep after this tick under RWE_SIM_LAG, if it applies. */
        std::optional<std::chrono::milliseconds> simulationLagForTick(unsigned int tick) const;

    private:
        std::optional<std::ofstream> hashLog;

        /** The lag RWE_SIM_LAG asked for, if it was set and read. */
        std::optional<SimLag> simLag;

        /**
         * The tick RWE_DESYNC_AT asked for, if it asked for one.
         *
         * Reported wrong from that tick *onwards*, not on that tick alone: two
         * simulations that have diverged stay diverged, so a single wrong tick
         * followed by agreement would be a shape no real desync takes, and the
         * report would be being tested against something that cannot happen.
         */
        std::optional<unsigned int> desyncFromTick;
    };
}
