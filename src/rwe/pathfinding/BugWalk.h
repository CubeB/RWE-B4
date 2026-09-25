#pragma once

#include <functional>
#include <rwe/grid/Point.h>

namespace rwe
{
    /**
     * The cheap first pass the original runs before it searches at all.
     *
     * `0x40E160` is not a search: it is a greedy walk towards the target that
     * follows a wall when it meets one, with no heap, no allocation and no
     * iteration cap. What the pathfinder wants from it is not a route but two
     * answers -- how close it is possible to get, and whether searching is
     * worth doing at all.
     *
     * The walk goes along x and then along z. At a wall two tracers set off
     * round it in opposite directions, a step each in turn, each scanning all
     * eight directions from the wall side; the first to come back onto the
     * greedy line nearer the goal takes the walk on from there, and if the
     * two meet head on there is no way round. Transcribed from
     * `0x40E2AC`-`0x40E600`.
     *
     * Both answers are then used to relax the goal rather than to truncate the search
     * (`0x40DCA8`): every cell at least as close as the walk managed counts as
     * arrival, so an A* aimed at an unreachable place finishes at the nearest
     * spot instead of exhausting the map, and one aimed at a reachable place
     * is unaffected because the walk reached it and nothing but the goal
     * qualifies.
     *
     * Three deliberate differences from the original, all recorded in
     * TOTALA-EXE.md section 87:
     *
     *  - it takes a step limit. The original has none, and a map with the
     *    wrong shape of wall on it could make one tick arbitrarily expensive;
     *    running out of steps here simply reports the closest cell reached so
     *    far, which is a safe answer rather than a wrong one.
     *  - closeness is measured with the caller's own metric, so that the
     *    number handed back means the same thing to the search that consumes
     *    it. The original uses `18*max + 7*min`, which orders cells the same
     *    way as an octile distance but does not share its units.
     *  - the start is not asked whether it is walkable. The original gives up
     *    at once if it is not (`0x40E1AD`); RWE's walkability counts other
     *    units, and a unit pressed against another would learn nothing.
     */
    struct BugWalkResult
    {
        /** The closest cell the walk stood on, by the caller's metric. */
        Point closest;

        /** Whether that cell is the goal itself. */
        bool reachedGoal{false};

        /** Steps taken, which is what the walk costs the tick's budget. */
        unsigned int steps{0};
    };

    /**
     * Walks greedily from `start` towards `goal`, following walls it meets.
     *
     * `walkable` is asked about a cell at most once per visit and must be
     * cheap; `distanceToGoal` orders cells by closeness, smaller being
     * nearer. Neither is called after the step limit is reached.
     */
    BugWalkResult bugWalk(
        const Point& start,
        const Point& goal,
        const std::function<bool(const Point&)>& walkable,
        const std::function<unsigned int(const Point&)>& distanceToGoal,
        unsigned int stepLimit);
}
