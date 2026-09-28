#pragma once

#include <string>

namespace rwe
{
    /**
     * Which path search a game runs.
     *
     * A game option and not a local preference: two lockstep peers that
     * disagree path their units differently, and no sync hash catches a
     * disagreement in a build constant. It is recorded in the replay and save
     * headers the way `LineOfSightMode` and `MappingMode` are, and `rwe.cfg`'s
     * `pathfinding` key only supplies the default a fresh game starts from.
     */
    enum class PathfindingBackend
    {
        /**
         * RWE's own A*: an admissible octile heuristic and an eight-way
         * successor fan over the terrain and occupancy test. The default, and
         * unchanged by this option existing.
         */
        RweAStar = 0,

        /**
         * The original's phase-two search, as OpenAnnihilation reimplements it
         * (`src/sim/ground-orders/`): a restricted successor fan, turn and
         * straight-run costs, and an inadmissible weighted heuristic. See
         * `TOTALA-EXE-MOVEMENT.md` S:87.
         */
        OpenAnnihilation = 1,
    };

    /** The rwe.cfg spelling of a backend: `rwe` or `oa`. */
    const char* pathfindingBackendName(PathfindingBackend backend);

    /**
     * Parses a config value, falling back when it names nothing known.
     * `open-annihilation` and `openannihilation` are accepted alongside `oa`.
     */
    PathfindingBackend pathfindingBackendFromString(const std::string& value, PathfindingBackend fallback);
}
