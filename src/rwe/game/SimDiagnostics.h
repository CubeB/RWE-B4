#pragma once

#include <fstream>
#include <optional>
#include <rwe/sim/GameHash.h>

namespace rwe
{
    struct GameSimulation;

    /**
     * The two pure observers a determinism hunt runs on, shared by the game
     * scene and the headless arena so the same env switches mean the same
     * thing in both.
     *
     * RWE_HASH_LOG=<file> writes one line a tick, the tick and the sync hash;
     * a game and the replay of it should write the same file, and the first
     * line that differs is the tick a fault showed itself on.
     * RWE_STATE_DUMP=<first>:<last>:<prefix> (with RWE_STATE_DUMP_STEP) writes
     * the full saved state as JSON for each tick in the range.
     */
    class SimDiagnostics
    {
    public:
        SimDiagnostics();

        /** Whether a hash log was asked for, which is also when the hash has to be computed in a replay. */
        bool hashLogEnabled() const;

        /** Computes and records the tick's hash and, if asked for, its state dump. Returns the hash. */
        GameHash record(const GameSimulation& simulation, unsigned int tick);

    private:
        std::optional<std::ofstream> hashLog;
    };
}
