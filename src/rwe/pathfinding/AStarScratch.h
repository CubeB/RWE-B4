#pragma once

#include <cstdint>
#include <vector>

namespace rwe
{
    /**
     * Per-cell working storage for a grid A* search, kept alive between
     * searches so that a search allocates nothing.
     *
     * The search used to keep its open list index and its closed set in
     * std::unordered_map. At 800 units on Painted Desert that was the largest
     * single cost in the whole simulation tick: every sift step of the open
     * heap wrote a hash entry, and every successor cost a hash lookup to ask
     * whether it was closed, so a node expansion did upwards of a hundred hash
     * operations against a table that never fit in cache. Flat arrays over the
     * grid answer the same questions with one indexed load.
     *
     * Cells are not cleared between searches -- on a 512x512 map that would be
     * a megabyte of memset for a search that touches a few thousand cells.
     * Each cell instead carries the number of the search its contents belong
     * to, and a cell from an older search reads as fresh.
     */
    struct AStarScratch
    {
        /** Bits in Cell::flags: an answer, and whether it has been worked out yet. */
        static constexpr uint8_t WalkableKnown = 1u << 0;
        static constexpr uint8_t WalkableValue = 1u << 1;
        static constexpr uint8_t RoughKnown = 1u << 2;
        static constexpr uint8_t RoughValue = 1u << 3;
        static constexpr uint8_t WaterKnown = 1u << 4;
        static constexpr uint8_t WaterValue = 1u << 5;

        /**
         * Eight bytes, and every field the search wants about a cell is in
         * them. Worth the narrow types: the open list of a thousand-vertex
         * search spans a few thousand cells, and a sift step reaches into one
         * of them at random, so whether the set fits in L1 decides what a sift
         * costs. The indices are narrow safely -- see the static asserts in
         * AStarPathFinder, which bound both against the search's own limits.
         */
        struct Cell
        {
            /** The search these fields belong to; anything else means "untouched". */
            uint16_t stamp{0};
            /** Terrain answers already worked out for this cell this search. */
            uint8_t flags{0};
            uint8_t unused{0};
            /** Position in the open heap, or -1 when this cell is not open. */
            int16_t openIndex{-1};
            /** Position in the closed list, or -1 when this cell is not closed. */
            int16_t closedIndex{-1};
        };

        std::vector<Cell> cells;
        int width{0};
        int height{0};
        /**
         * Zero means "no search has run yet", which no cell can claim, so the
         * first search after a resize sees every cell as untouched.
         */
        uint16_t currentStamp{0};

        /**
         * Points the scratch at a grid of the given size. Resizing throws away
         * everything, which is fine: it happens once when a map is loaded.
         */
        void configure(int w, int h)
        {
            if (w == width && h == height)
            {
                return;
            }

            width = w;
            height = h;
            cells.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), Cell());
            currentStamp = 0;
        }

        void beginSearch()
        {
            ++currentStamp;
            if (currentStamp == 0)
            {
                // Every sixty-five thousand searches -- around nine minutes of
                // a busy game -- the stamp wraps back onto stamps still in the
                // array, so start again from clean. A megabyte of memset at
                // that interval does not show up anywhere.
                cells.assign(cells.size(), Cell());
                currentStamp = 1;
            }
        }

        /** The cell index for a grid position, or -1 if it lies off the grid. */
        int32_t toIndex(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= width || y >= height)
            {
                return -1;
            }

            return (y * width) + x;
        }

        /** The cell at an index known to be valid, reset if it is stale. */
        Cell& at(int32_t index)
        {
            Cell& cell = cells[static_cast<std::size_t>(index)];
            if (cell.stamp != currentStamp)
            {
                cell = Cell();
                cell.stamp = currentStamp;
            }
            return cell;
        }
    };
}
