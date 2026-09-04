#pragma once

#include <rwe/pathfinding/OctileDistance.h>

namespace rwe
{
    struct PathCost
    {
        /**
         * What the open heap orders by, worked out once at the point a vertex
         * is offered to the heap rather than on every comparison.
         *
         * A binary heap compares an entry far more often than it makes one --
         * roughly twenty times, at the depths a thousand-vertex search reaches
         * -- and every one of those comparisons was rebuilding both lengths
         * out of their straight and diagonal parts. The ordering is exactly
         * PathCost::operator<, which is the length as a float and then the
         * turn count to break the tie.
         */
        struct HeapKey
        {
            float distance{0.0f};
            unsigned int turnCount{0};

            bool operator<(const HeapKey& rhs) const
            {
                if (distance < rhs.distance)
                {
                    return true;
                }

                if (rhs.distance < distance)
                {
                    return false;
                }

                return turnCount < rhs.turnCount;
            }
        };

        OctileDistance distance;
        unsigned int turnCount{0};

        PathCost() = default;
        PathCost(const OctileDistance& distance, unsigned int turnCount);

        HeapKey heapKey() const;

        bool operator==(const PathCost& rhs) const;

        bool operator!=(const PathCost& rhs) const;

        bool operator<(const PathCost& rhs) const;

        bool operator>(const PathCost& rhs) const;

        bool operator<=(const PathCost& rhs) const;

        bool operator>=(const PathCost& rhs) const;

        PathCost operator+(const PathCost& rhs) const;
    };
}
