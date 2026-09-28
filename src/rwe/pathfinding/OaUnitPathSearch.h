#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <rwe/grid/Point.h>
#include <rwe/pathfinding/UnitPath.h>
#include <rwe/sim/MovementClassId.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;
    class UnitPathFinder;

    /**
     * The original's phase-two path search, as OpenAnnihilation reimplements
     * it, driven the way `PathFindingService` drives a search: begin, step a
     * budget at a time, take the result.
     *
     * What it reproduces is the search algorithm alone -- the restricted
     * successor fan, the turn and straight-run costs, the inadmissible
     * weighted heuristic, the wall-follow seed that relaxes the goal, and the
     * corners-only route. The scheduler that picks which unit searches, the
     * adaptive heuristic weight, the movement-class movement map with its
     * occupancy history, and the sight rule that makes unexplored ground
     * passable are deliberately not here; see TOTALA-EXE-MOVEMENT.md S:87.
     *
     * Cell classification is delegated to RWE's own walkability and
     * rough-terrain tests (the same ones the default backend uses), so a cell
     * the default search refuses is refused here too.
     */
    class OaUnitPathSearch
    {
    public:
        OaUnitPathSearch(
            const GameSimulation* simulation,
            UnitId self,
            std::optional<MovementClassId> movementClass,
            unsigned int footprintX,
            unsigned int footprintZ,
            const Point& goalCell,
            int32_t goalTolerance);

        ~OaUnitPathSearch();
        OaUnitPathSearch(OaUnitPathSearch&&) noexcept;
        OaUnitPathSearch& operator=(OaUnitPathSearch&&) noexcept;

        /** Seeds the search at the footprint top-left cell `start`. */
        void beginSearch(const Point& start);

        /**
         * Expands at most `maxExpansions` nodes and returns how many it did.
         * A search that runs out resumes exactly where it stopped, and the
         * sequence of expansions does not depend on how they are sliced.
         */
        unsigned int stepSearch(unsigned int maxExpansions);

        bool isSearchFinished() const;

        /** Nodes expanded so far, which is what a save writes down. */
        std::size_t expansionsSoFar() const;

        /**
         * Whether the goal was relaxed -- the wall-follow seed got closer to
         * it than the start but not onto it -- so the route ends at the
         * nearest place the unit can stand rather than at the destination.
         */
        bool goalWasRelaxed() const;

        /**
         * The finished route as world-space waypoints, beginning with the
         * corner behind the unit. An empty route means the destination could
         * not be reached, and the path then begins and ends where the unit
         * is standing.
         */
        UnitPath takeResult(const GameSimulation& simulation) const;

    private:
        static uint32_t classifyThunk(void* context, int32_t x, int32_t z);
        uint32_t classify(int32_t x, int32_t z);

        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
