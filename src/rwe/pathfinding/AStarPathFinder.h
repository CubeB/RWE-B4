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
    /** The most successors a vertex on an eight-connected grid can have. */
    const unsigned int MaxSuccessors = 8;

    /**
     * How much of a search one uninterrupted call will do. It is not a cap on
     * a search -- a search has none -- it is what findPath passes when the
     * caller wants an answer now rather than a slice of one.
     */
    const unsigned int UnlimitedExpansions = ~0u;

    template <typename T, typename Cost = float>
    struct AStarVertexInfo
    {
        Cost costToReach;
        T vertex;
        /**
         * Where this vertex was reached from, as a position in the closed
         * list, or -1 for the vertex the search started at.
         *
         * It used to be a pointer into that list, kept valid by reserving the
         * per-search cap up front so the vector could never reallocate. With
         * the cap gone there is no size to reserve -- a search runs until it
         * finds the goal or empties its open list -- so the link is an index
         * and the list is free to grow.
         */
        int32_t predecessor{-1};
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

        /**
         * Everything a half-finished search has to keep hold of.
         *
         * These were locals of findPath while a search began and ended inside
         * one call. They are members now because a search is sliced: the
         * scheduler gives it what is left of the tick's budget, and if that
         * runs out before the goal is found the search stays exactly here and
         * is carried on next tick. The scratch grid holds the rest of the
         * state, which is why nothing else may start a search on the same
         * scratch while one is suspended -- beginSearch would stamp every one
         * of these cells stale.
         */
        std::vector<std::pair<T, VertexInfo>> closedVertices;
        std::optional<Cost> closestCost;
        int32_t closestIndex{-1};
        bool searchActive{false};
        bool searchFinished{false};
        bool searchExhausted{false};
        AStarPathType resultType{AStarPathType::Partial};
        int32_t resultIndex{-1};

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

        /**
         * Runs a whole search and hands back the answer, for every caller
         * that has no budget to spend and no tick to be interrupted by.
         */
        AStarPathInfo<T, Cost> findPath(const T& start)
        {
            beginSearch(start);
            stepSearch(UnlimitedExpansions);
            return takeResult();
        }

        /**
         * Seeds a search at start. Stamps the scratch, which invalidates any
         * search still suspended on it, so the caller owes it to whoever is
         * holding one not to call this until that one is done.
         */
        void beginSearch(const T& start)
        {
            scratch->beginSearch();

            openHeap.clear();
            closedVertices.clear();
            // The finished search's list is moved out rather than reused, so
            // its capacity goes with it. A thousand is what the old cap used
            // to reserve and is still about what a typical search closes, so
            // most searches get their one allocation and no growth after it.
            closedVertices.reserve(1024);
            closestCost.reset();
            closestIndex = -1;
            searchActive = true;
            searchFinished = false;
            searchExhausted = false;
            resultType = AStarPathType::Partial;
            resultIndex = -1;

            OpenNode startNode;
            startNode.priority = estimateCostToGoal(start).heapKey();
            startNode.costToReach = Cost();
            startNode.vertex = start;
            startNode.predecessor = -1;
            startNode.cellIndex = scratch->toIndex(start.x, start.y);
            heapPushOrDecrease(startNode);
        }

        /**
         * Expands at most maxExpansions vertices and returns how many it
         * actually did, which is what the caller deducts from its budget.
         *
         * There is no cap of the search's own. It stops when it reaches the
         * goal, when the open list empties -- which is what proves a goal
         * unreachable rather than merely unfound -- or when the slice runs
         * out, and only the last of those leaves it resumable. This is the
         * structural piece the original has and RWE did not: 0x40EEAF slices
         * its A* at a hundred expansions a tick and carries the same search
         * on next tick, so a long route completes over several ticks and
         * "nothing is ever truncated" (TOTALA-EXE.md S:87). RWE truncated at
         * a thousand expansions and handed back a partial path the unit
         * walked and then re-requested from.
         */
        unsigned int stepSearch(unsigned int maxExpansions)
        {
            assert(searchActive);

            Successor successors[MaxSuccessors];
            unsigned int expansions = 0;

            while (!searchFinished && expansions < maxExpansions)
            {
                if (openHeap.empty())
                {
                    // Nowhere left to look: the goal cannot be reached at all.
                    assert(closestIndex >= 0);
                    searchFinished = true;
                    searchExhausted = true;
                    resultType = AStarPathType::Partial;
                    resultIndex = closestIndex;
                    break;
                }

                auto current = openHeap.front();
                heapPop();
                expansions += 1;

                auto currentIndex = static_cast<int32_t>(closedVertices.size());
                closedVertices.push_back({current.vertex, VertexInfo{current.costToReach, current.vertex, current.predecessor}});
                if (current.cellIndex >= 0)
                {
                    scratch->at(current.cellIndex).closedIndex = currentIndex;
                }

                if (isGoal(current.vertex))
                {
                    searchFinished = true;
                    searchExhausted = false;
                    resultType = AStarPathType::Complete;
                    resultIndex = currentIndex;
                    break;
                }

                auto estimatedCostToGoal = estimateCostToGoal(current.vertex);
                if (!closestCost || estimatedCostToGoal < *closestCost)
                {
                    closestCost = estimatedCostToGoal;
                    closestIndex = currentIndex;
                }

                std::optional<T> predecessorVertex;
                if (current.predecessor >= 0)
                {
                    predecessorVertex = closedVertices[current.predecessor].second.vertex;
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

            return expansions;
        }

        /** True once the search has an answer and wants no more budget. */
        bool isSearchFinished() const
        {
            return searchFinished;
        }

        bool isSearchActive() const
        {
            return searchActive;
        }

        /**
         * Vertices expanded so far, which is exactly the size of the closed
         * list. A save writes this down and a load steps the rebuilt search
         * that far to land back where it was.
         */
        std::size_t expansionsSoFar() const
        {
            return closedVertices.size();
        }

        /** Takes the finished search's answer, leaving the finder idle. */
        AStarPathInfo<T, Cost> takeResult()
        {
            assert(searchFinished);
            assert(resultIndex >= 0);

            auto path = walkPath(resultIndex);
            AStarPathInfo<T, Cost> info{resultType, std::move(path), std::move(closedVertices), searchExhausted};

            // closedVertices was moved from, so it is unspecified rather than
            // empty; say what it is before the next search asks.
            closedVertices.clear();
            openHeap.clear();
            searchActive = false;
            searchFinished = false;

            return info;
        }

        /**
         * Throws a half-finished search away. The scratch keeps whatever it
         * stamped, which costs nothing: the next beginSearch stamps over it.
         */
        void abandonSearch()
        {
            openHeap.clear();
            closedVertices.clear();
            closestCost.reset();
            closestIndex = -1;
            searchActive = false;
            searchFinished = false;
            resultIndex = -1;
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
                scratch->at(node.cellIndex).openIndex = static_cast<int32_t>(position);
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

        std::vector<T> walkPath(int32_t index)
        {
            std::vector<T> items;
            for (auto i = index; i >= 0; i = closedVertices[i].second.predecessor)
            {
                items.push_back(closedVertices[i].second.vertex);
            }

            std::reverse(items.begin(), items.end());
            return items;
        }
    };
}
