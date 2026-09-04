#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <rwe/pathfinding/AStarScratch.h>
#include <utility>
#include <vector>

namespace rwe
{
    /**
     * The maximum number of elements in the open list to expand
     * before giving up on a path search.
     */
    const unsigned int MaxOpenListQueries = 1000;

    /** The most successors a vertex on an eight-connected grid can have. */
    const unsigned int MaxSuccessors = 8;

    // AStarScratch keeps both of a cell's positions in an int16, which these
    // bounds are what make safe. The search closes one vertex per pop, and
    // pushes at most MaxSuccessors per pop plus the start.
    static_assert(MaxOpenListQueries < 32767, "closed list index must fit an int16");
    static_assert((MaxOpenListQueries * MaxSuccessors) + 1 < 32767, "open heap index must fit an int16");

    template <typename T, typename Cost = float>
    struct AStarVertexInfo
    {
        Cost costToReach;
        T vertex;
        std::optional<const AStarVertexInfo<T, Cost>*> predecessor;
    };

    enum class AStarPathType
    {
        Complete,
        Partial
    };

    template <typename T, typename Cost>
    struct AStarPathInfo
    {
        AStarPathType type;
        std::vector<T> path;
        /**
         * Every vertex the search closed, in the order it closed them, each
         * paired with the entry it was reached from.
         *
         * A flat vector rather than the hash map this used to be: nothing
         * needs to look a closed vertex up by value once the search is over
         * (the search itself asks the scratch grid instead), and the search
         * reserves MaxOpenListQueries entries up front, so the predecessor
         * pointers into it stay put for the life of the result.
         */
        std::vector<std::pair<T, AStarVertexInfo<T, Cost>>> closedVertices;
        /**
         * True for a Partial result when the search ran out of vertices to
         * expand, i.e. the goal is genuinely unreachable, as opposed to the
         * search giving up because it hit its expansion budget.
         */
        bool exhausted{false};
    };

    /**
     * A* over an eight-connected grid of vertices addressed by integer x and y.
     *
     * The open list is a binary heap whose sift rules are those of the MinHeap
     * this class used to hold, so equal-cost nodes still break their tie the
     * same way and the routes that come out are unchanged. What has gone is
     * the hashing: the heap position of a vertex and its closed-list position
     * both live in AStarScratch, indexed by grid position.
     */
    template <typename T, typename Cost = float>
    class AStarPathFinder
    {
    public:
        using VertexInfo = AStarVertexInfo<T, Cost>;

        /** One step out of a vertex, as filled in by getSuccessors. */
        struct Successor
        {
            Cost costToReach;
            T vertex;
        };

    private:
        /**
         * A vertex waiting to be expanded.
         *
         * Deliberately leaner than the VertexInfo that ends up in the closed
         * list, because the heap copies these on every sift: the predecessor
         * is a closed-list index rather than a pointer, and the scratch cell
         * index is carried along so a sift never has to recompute it.
         */
        struct OpenNode
        {
            /**
             * The estimated total cost, in the precomputed form the heap
             * orders by. Working it out once per offered vertex rather than
             * on every comparison is worth having: the heap makes about
             * twenty comparisons for each entry it takes.
             */
            typename Cost::HeapKey priority;
            Cost costToReach;
            T vertex;
            int32_t predecessor{-1};
            int32_t cellIndex{-1};
        };

        AStarScratch* scratch;
        std::unique_ptr<AStarScratch> ownedScratch;
        std::vector<OpenNode> openHeap;

    public:
        /**
         * Searches share the caller's scratch when it supplies one, which is
         * what keeps a search free of allocation. Without one (tests, one-off
         * tools) the pathfinder brings its own.
         */
        explicit AStarPathFinder(AStarScratch* scratch = nullptr)
            : scratch(scratch)
        {
            if (this->scratch == nullptr)
            {
                ownedScratch = std::make_unique<AStarScratch>();
                this->scratch = ownedScratch.get();
            }
        }

        virtual ~AStarPathFinder() = default;

        AStarPathInfo<T, Cost> findPath(const T& start)
        {
            scratch->beginSearch();

            openHeap.clear();

            std::vector<std::pair<T, VertexInfo>> closedVertices;
            // The search closes at most one vertex per pop, so this is enough
            // for the whole search and the predecessor pointers below can
            // never be invalidated by a reallocation.
            closedVertices.reserve(MaxOpenListQueries);

            {
                OpenNode startNode;
                startNode.priority = estimateCostToGoal(start).heapKey();
                startNode.costToReach = Cost();
                startNode.vertex = start;
                startNode.predecessor = -1;
                startNode.cellIndex = scratch->toIndex(start.x, start.y);
                heapPushOrDecrease(startNode);
            }

            std::optional<Cost> closestCost;
            int32_t closestIndex = -1;

            Successor successors[MaxSuccessors];

            unsigned int openListPopsPerformed = 0;

            while (!openHeap.empty() && openListPopsPerformed < MaxOpenListQueries)
            {
                auto current = openHeap.front();
                heapPop();
                openListPopsPerformed += 1;

                auto currentIndex = static_cast<int32_t>(closedVertices.size());
                std::optional<const VertexInfo*> predecessor;
                if (current.predecessor >= 0)
                {
                    predecessor = &closedVertices[current.predecessor].second;
                }
                closedVertices.push_back({current.vertex, VertexInfo{current.costToReach, current.vertex, predecessor}});
                if (current.cellIndex >= 0)
                {
                    scratch->at(current.cellIndex).closedIndex = static_cast<int16_t>(currentIndex);
                }

                if (isGoal(current.vertex))
                {
                    return AStarPathInfo<T, Cost>{AStarPathType::Complete, walkPath(closedVertices.back().second), std::move(closedVertices), false};
                }

                auto estimatedCostToGoal = estimateCostToGoal(current.vertex);
                if (!closestCost || estimatedCostToGoal < *closestCost)
                {
                    closestCost = estimatedCostToGoal;
                    closestIndex = currentIndex;
                }

                std::optional<T> predecessorVertex;
                if (predecessor)
                {
                    predecessorVertex = (*predecessor)->vertex;
                }

                auto successorCount = getSuccessors(current.vertex, predecessorVertex, current.costToReach, successors);
                for (unsigned int i = 0; i < successorCount; ++i)
                {
                    const auto& s = successors[i];
                    auto cellIndex = scratch->toIndex(s.vertex.x, s.vertex.y);
                    if (cellIndex >= 0 && scratch->at(cellIndex).closedIndex >= 0)
                    {
                        continue;
                    }

                    OpenNode node;
                    node.priority = (s.costToReach + estimateCostToGoal(s.vertex)).heapKey();
                    node.costToReach = s.costToReach;
                    node.vertex = s.vertex;
                    node.predecessor = currentIndex;
                    node.cellIndex = cellIndex;
                    heapPushOrDecrease(node);
                }
            }

            assert(closestIndex >= 0);
            auto exhausted = openHeap.empty();
            return AStarPathInfo<T, Cost>{AStarPathType::Partial, walkPath(closedVertices[closestIndex].second), std::move(closedVertices), exhausted};
        }

    protected:
        virtual bool isGoal(const T& vertex) = 0;

        virtual Cost estimateCostToGoal(const T& vertex) = 0;

        /**
         * Writes the vertex's successors into out, which has room for
         * MaxSuccessors of them, and returns how many were written.
         * Filling a caller-owned buffer rather than returning a vector keeps
         * two heap allocations out of every node expansion.
         */
        virtual unsigned int getSuccessors(const T& vertex, const std::optional<T>& predecessor, const Cost& costToReach, Successor* out) = 0;

        AStarScratch& getScratch() const
        {
            return *scratch;
        }

    private:
        void setCellOpenIndex(const OpenNode& node, std::size_t position)
        {
            if (node.cellIndex >= 0)
            {
                scratch->at(node.cellIndex).openIndex = static_cast<int16_t>(position);
            }
        }

        void clearCellOpenIndex(int32_t cellIndex)
        {
            if (cellIndex >= 0)
            {
                scratch->at(cellIndex).openIndex = -1;
            }
        }

        void heapSiftUp(std::size_t position, const OpenNode& element)
        {
            while (position > 0)
            {
                auto parentPosition = (position - 1) / 2;
                if (!(element.priority < openHeap[parentPosition].priority))
                {
                    break;
                }

                openHeap[position] = openHeap[parentPosition];
                setCellOpenIndex(openHeap[position], position);
                position = parentPosition;
            }

            openHeap[position] = element;
            setCellOpenIndex(openHeap[position], position);
        }

        void heapSiftDown(std::size_t position, const OpenNode& element)
        {
            auto firstLeafPosition = openHeap.size() / 2;
            while (position < firstLeafPosition) // while non-leaf
            {
                auto smallestChildPosition = (position * 2) + 1;
                auto rightChildPosition = (position * 2) + 2;
                if (rightChildPosition < openHeap.size()
                    && openHeap[rightChildPosition].priority < openHeap[smallestChildPosition].priority)
                {
                    smallestChildPosition = rightChildPosition;
                }

                if (element.priority < openHeap[smallestChildPosition].priority)
                {
                    break;
                }

                openHeap[position] = openHeap[smallestChildPosition];
                setCellOpenIndex(openHeap[position], position);
                position = smallestChildPosition;
            }

            openHeap[position] = element;
            setCellOpenIndex(openHeap[position], position);
        }

        void heapPop()
        {
            // Only the front entry's cell is wanted, not the entry, so this
            // reads the index rather than copying the node out.
            auto firstCellIndex = openHeap.front().cellIndex;
            auto lastElement = openHeap.back();
            openHeap.pop_back();
            clearCellOpenIndex(lastElement.cellIndex);

            if (!openHeap.empty())
            {
                clearCellOpenIndex(firstCellIndex);
                heapSiftDown(0, lastElement);
            }
        }

        void heapPushOrDecrease(const OpenNode& item)
        {
            auto existingPosition = item.cellIndex >= 0 ? scratch->at(item.cellIndex).openIndex : -1;
            if (existingPosition < 0)
            {
                openHeap.emplace_back();
                heapSiftUp(openHeap.size() - 1, item);
                return;
            }

            if (!(item.priority < openHeap[existingPosition].priority))
            {
                return;
            }

            heapSiftUp(static_cast<std::size_t>(existingPosition), item);
        }

        std::vector<T> walkPath(const VertexInfo& info)
        {
            std::vector<T> items;
            std::optional<const VertexInfo*> v = &info;
            while (v)
            {
                items.push_back((*v)->vertex);
                v = (*v)->predecessor;
            }

            std::reverse(items.begin(), items.end());
            return items;
        }
    };
}
