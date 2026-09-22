#pragma once

#include <rwe/grid/DiscreteRect.h>
#include <rwe/grid/Point.h>
#include <rwe/pathfinding/AStarPathFinder.h>
#include <rwe/pathfinding/AStarScratch.h>
#include <rwe/pathfinding/PathCost.h>
#include <rwe/pathfinding/UnitPath.h>
#include <memory>
#include <rwe/sim/MovementClassCollisionService.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <variant>

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
         * Whether a goal something is already standing on is relaxed, before
         * the search starts, to the nearest cell the unit could stand on.
         *
         * On, and a constant of the build for the reason the other two are:
         * two peers disagreeing about it would path differently. It is a
         * member only so that path_bench can measure with and without it.
         *
         * See beginSearch for what it is worth and why. Off restores the
         * behaviour where such a search runs to exhaustion unless the first
         * pass happens to rescue it.
         */
        bool relaxBlockedGoal{true};

        /**
         * How far out from a blocked goal to look for a cell the unit could
         * actually stand on, in grid cells.
         *
         * Eight, which is a crowd about seventeen cells across -- big enough
         * for the ring of units that gathers round an attack target, small
         * enough that the look costs at most a few hundred footprint tests
         * against a search that would otherwise cost tens of thousands of
         * expansions. Nothing standable inside it leaves the goal alone.
         * A constant of the build; a member so path_bench can sweep it.
         */
        int blockedGoalSearchRadius{8};


        /**
         * Diagnostics for `path_bench`, and for nothing else: they are only
         * counted, never read by the simulation, so they cannot change an
         * outcome and are neither saved nor hashed.
         */
        struct Counters
        {
            long long searches{0};
            /**
             * Ended because there was nowhere left to look.
             *
             * There is no longer a truncated count beside this one. A search
             * is not cut short any more: it is sliced, and what a slice runs
             * out of is the tick, not the search.
             */
            long long searchesExhausted{0};
            long long expansions{0};
            /** Requests still queued when the tick's budget ran out. */
            long long deferredRequests{0};
            /** Searches whose goal the first pass was able to relax. */
            long long searchesRelaxed{0};
            /** Cells the first pass stepped on, which is what it costs. */
            long long bugWalkSteps{0};
            /** Ticks a search was carried over rather than finished. */
            long long searchesSuspended{0};
            /** Searches thrown away because what asked for them had moved on. */
            long long searchesAbandoned{0};
            /** Ticks update() has run, which is what the rest are per. */
            long long ticks{0};
            /** Ticks that ended with anything still waiting. */
            long long ticksWithQueue{0};
            /** The deepest the queue has been. */
            long long maxQueue{0};
            /**
             * Expansions spent on searches that found nothing -- every
             * reachable cell looked at, no route there.
             *
             * Beside the total, this is what says whether a saturated budget
             * is the cost of routing an army or the cost of a handful of
             * units asking for somewhere they cannot get to. Those are
             * different faults with different fixes, and the totals alone
             * cannot tell them apart.
             */
            long long expansionsExhausted{0};
            /** And expansions thrown away when a goal moved mid-search. */
            long long expansionsAbandoned{0};
            /** The worst single search, which is what sets the worst wait. */
            long long maxSearchExpansions{0};
            /** Searches begun with something standing on the goal cell. */
            long long searchesGoalBlocked{0};
            /** Of those, the ones given a standable cell to aim at instead. */
            long long searchesGoalRelaxed{0};
            /** Searches where the first pass could not improve on the start at all. */
            long long searchesWalkStuck{0};
        };
        Counters counters;

        PathFindingService();
        ~PathFindingService();
        PathFindingService(PathFindingService&&) noexcept;
        PathFindingService& operator=(PathFindingService&&) noexcept;

        void update(GameSimulation& simulation);

        /**
         * Writes the counters above to the log every ten seconds, as deltas,
         * when RWE_PATH_PROFILE is set in the environment. Off otherwise, and
         * a pure observer either way -- it reads counters the simulation
         * never reads and writes nothing the simulation can see.
         *
         * What it is for: a unit whose search has not landed yet is walking
         * the straight-line stand-in, and a straight line into a wall is a
         * unit sliding along the wall. So "are units waiting for paths, and
         * for how long" is the question behind a complaint about units
         * sliding, and the queue depth is the only place it can be answered.
         */
        void logCounters(const GameSimulation& simulation);

        /** Throws away a search that is part way through. */
        void abandonSearch();

        /**
         * A unit has asked for a path. Returns true if the search already in
         * flight for that unit will answer it, in which case the caller must
         * leave the unit's place in the queue alone.
         *
         * A search that outlived its tick owns the request at the head of the
         * queue -- the save format reads the suspended search off the head, and
         * update() asserts the two agree when the search lands -- so a unit
         * that asks for the same place again while its search is part way
         * through must not be shuffled to the back of the queue. It has not
         * been served yet; the search that is running is its turn.
         *
         * If the unit's goal has moved on instead, the search in flight is for
         * somewhere the unit no longer wants to go: it is thrown away here, so
         * that no stale search is left for a save to serialise, and false is
         * returned so the caller queues the request like any other.
         */
        bool onPathRequested(const GameSimulation& simulation, UnitId unitId);

        /**
         * What a save needs to write down about a search in progress, and all
         * of it: five integers.
         *
         * A half-finished A\* cannot be serialised and would be silly to try,
         * but it does not have to be. A search is a pure function of the
         * world, the goal, and the cell it started from, and the first two are
         * in the save already -- the world because it is the save, and the
         * goal because it hangs off the unit's navigation state. The only
         * thing lost is the third: a suspended unit keeps walking its
         * straight-line stand-in, so by the time the save is written it is no
         * longer standing where its search began. Write that footprint down
         * and the search can be built again exactly, and then run forward the
         * number of expansions it had already done -- which is deterministic,
         * so what comes out is the state that was suspended, not an
         * approximation of it.
         *
         * The alternative was to drop the search on both sides of a save. That
         * works for a saved game, where nobody can watch both timelines, and
         * breaks a replay outright: keyframes go through the same path during
         * playback, where the recording they are being compared against never
         * dropped anything.
         *
         * Empty when no search is in progress, which is most ticks.
         */
        std::optional<DiscreteRect> suspendedSearchStart() const;
        std::size_t suspendedSearchExpansions() const;

        /**
         * Rebuilds a search suspended at save time. The request it belongs to
         * is the one at the head of the queue, as it was when the search
         * began, so it is not named here.
         */
        void restoreSuspendedSearch(const GameSimulation& simulation, const DiscreteRect& start, std::size_t expansions);

    private:
        /**
         * Working storage shared by every search, so that a search allocates
         * nothing. It is scratch in the strictest sense -- each search stamps
         * what it touches with its own number and reads nothing left behind by
         * the last one -- so sharing it cannot make one path depend on another.
         *
         * One search at a time is what makes that safe now that searches
         * outlive a tick: starting one stamps every cell the suspended one was
         * relying on, so update() never begins a search while it is holding a
         * suspended one. The original has the same shape for the same reason
         * -- its pathfinder is a singleton at gm+0x14207 with one set of map
         * entries, and the scheduler slices that one search (TOTALA-EXE.md
         * S:87).
         */
        AStarScratch scratch;

        /**
         * The search in progress, if there is one. Opaque here: the concrete
         * finder it holds is named only in the .cpp, so this header does not
         * have to reach the simulation the finder is built from.
         */
        struct ActiveSearch;
        std::unique_ptr<ActiveSearch> activeSearch;

        /**
         * `startOverride` is for a restored search and nothing else: it puts
         * the search back at the footprint it began from rather than the one
         * the unit has walked to since.
         */
        void beginSearch(const GameSimulation& simulation, UnitId unitId, const std::variant<SimVector, DiscreteRect>& destination, const std::optional<DiscreteRect>& startOverride = std::nullopt);

        /** Turns a finished search into waypoints. Leaves the finder idle. */
        UnitPath finishSearch(const GameSimulation& simulation);

        /** What the cheap first pass found, and what it was able to do with it. */
        struct FirstPass
        {
            /** The closest cell the walk stood on; only meaningful when it ran. */
            Point closest;
            /** Whether the walk ran at all. */
            bool walked{false};
            /** Whether it got closer than the start, which relaxes the goal. */
            bool relaxed{false};
            /**
             * What the walk cost. Charged to the tick's budget when the walk
             * is the whole answer, since no expansions are spent then and
             * nothing else would bound the tick.
             */
            unsigned int steps{0};
        };

        /**
         * Runs the cheap first pass. If it got closer to the goal than the
         * unit already is, relaxes the goal to what it reached. A goal in
         * another terrain region is the shape where the walk ran and could
         * get no closer, which is what the caller short-circuits on.
         */
        FirstPass relaxGoalToWhatIsReachable(UnitPathFinder& pathFinder, const Point& start, const Point& goal);

        /**
         * Whether the goal lies in a different terrain region from the start,
         * so that no route can exist whatever the units are doing. False when
         * the movement class has no registered terrain grid, which is every
         * ad-hoc class -- terrain is invisible to those searches anyway.
         */
        bool terrainUnreachable(const GameSimulation& simulation, MovementClassId movementClass, const Point& start, const Point& goal) const;

        SimVector getWorldCenter(const GameSimulation& simulation, const DiscreteRect& discreteRect);
    };
}
