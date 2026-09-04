#include "PathCost.h"

namespace rwe
{
    PathCost::PathCost(const OctileDistance& distance, unsigned int turnCount)
        : distance(distance), turnCount(turnCount)
    {
    }

    PathCost::HeapKey PathCost::heapKey() const
    {
        return HeapKey{distance.asFloat(), turnCount};
    }

    bool PathCost::operator==(const PathCost& rhs) const
    {
        return distance == rhs.distance && turnCount == rhs.turnCount;
    }

    bool PathCost::operator!=(const PathCost& rhs) const
    {
        return !(rhs == *this);
    }

    bool PathCost::operator<(const PathCost& rhs) const
    {
        // The two lengths, then the tie-break on turns. Written out with the
        // floats taken once rather than as two OctileDistance comparisons,
        // which took them four times: this is the comparison the open heap
        // makes on every sift step, so it is one of the hottest lines in the
        // simulation.
        auto a = distance.asFloat();
        auto b = rhs.distance.asFloat();

        if (a < b)
        {
            return true;
        }

        if (b < a)
        {
            return false;
        }

        return turnCount < rhs.turnCount;
    }

    bool PathCost::operator>(const PathCost& rhs) const
    {
        return rhs < *this;
    }

    bool PathCost::operator<=(const PathCost& rhs) const
    {
        return !(rhs < *this);
    }

    bool PathCost::operator>=(const PathCost& rhs) const
    {
        return !(*this < rhs);
    }

    PathCost PathCost::operator+(const PathCost& rhs) const
    {
        return PathCost(distance + rhs.distance, turnCount + rhs.turnCount);
    }
}
