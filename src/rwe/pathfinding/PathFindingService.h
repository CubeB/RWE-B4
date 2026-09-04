#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/grid/Point.h>
#include <rwe/pathfinding/AStarPathFinder.h>
#include <rwe/pathfinding/AStarScratch.h>
#include <rwe/pathfinding/PathCost.h>
#include <rwe/pathfinding/UnitPath.h>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>

namespace rwe
{
    struct GameSimulation;
    class UnitPathFinder;

    class PathFindingService
    {
    public:
        AStarPathInfo<Point, PathCost> lastPathDebugInfo;

        /**
         * Node expansions the service will spend in one tick, over all the
         * searches it runs. It is a constant of the build rather than a
         * setting: two peers running different numbers would path their units
         * on different ticks and fall out of step. It lives here as a member
         * only so that `path_bench` can sweep it.
         */
        int expansionBudgetPerTick{4000};

        /**
         * Whether the cheap first pass runs and relaxes the goal. On, and a
         * constant of the build for the same reason the budget is: two peers
         * disagreeing about it would path differently. It is a member only so
         * that path_bench can measure with and without it.
         */
        bool relaxGoalWithFirstPass{true};

        /**
         * Diagnostics for `path_bench`, and for nothing else: they are only
         * counted, never read by the simulation, so they cannot change an
         * outcome and are neither saved nor hashed.
         */
        struct Counters
        {
            long long searches{0};
            /** Cut short by the per-search cap, with the goal still reachable. */
            long long searchesTruncated{0};
            /** Ended because there was nowhere left to look. */
            long long searchesExhausted{0};
            long long expansions{0};
            /** Requests still queued when the tick's budget ran out. */
            long long deferredRequests{0};
            /** Searches whose goal the first pass was able to relax. */
            long long searchesRelaxed{0};
            /** Cells the first pass stepped on, which is what it costs. */
            long long bugWalkSteps{0};
        };
        Counters counters;

        void update(GameSimulation& simulation);

    private:
        /**
         * Working storage shared by every search, so that a search allocates
         * nothing. It is scratch in the strictest sense -- each search stamps
         * what it touches with its own number and reads nothing left behind by
         * the last one -- so sharing it cannot make one path depend on another.
         */
        AStarScratch scratch;

        UnitPath findPath(const GameSimulation& simulation, UnitId unitId, const SimVector& destination);

        void relaxGoalToWhatIsReachable(UnitPathFinder& pathFinder, const Point& start, const Point& goal);
        UnitPath findPath(const GameSimulation& simulation, UnitId unitId, const DiscreteRect& destination);

        SimVector getWorldCenter(const GameSimulation& simulation, const DiscreteRect& discreteRect);
    };
}
