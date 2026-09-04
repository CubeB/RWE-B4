#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <rwe/sim/PlayerId.h>
#include <rwe/sim/UnitId.h>
#include <vector>

namespace rwe
{
    /**
     * A uniform grid of unit positions, so a target scan looks at the units
     * near the searcher instead of at every unit in the game.
     *
     * The original needs one of these for the same reason and has one: the
     * radius visitor behind radar, jamming and the mincloak proximity fuse
     * (0x47E890, TOTALA-EXE.md §34) walks a **128 world unit** cell grid over
     * the query's bounding box and then does a real circular test on what the
     * cells hand back. The cell size here is that one.
     *
     * Why this exists: at 800 units in a fight, chooseTarget was 10.0ms of the
     * 11.8ms the whole behaviour pass cost per tick, because each of the ~694
     * searches a tick walked all 800 units. That is the O(n^2) the profile
     * shows. Narrowing the walk to the searcher's neighbourhood is the whole
     * of the fix.
     *
     * Two properties matter for a lockstep simulation and both are held:
     *
     * - **It is derived state.** Rebuilt from the unit list, never saved,
     *   never hashed, and it feeds no decision the unit list could not have
     *   answered by itself.
     *
     * - **It does not decide anything.** A query is deliberately a *superset*
     *   of the answer: the positions it holds are as of the rebuild, so they
     *   go stale by up to one tick of movement as the pass that queries it
     *   moves units about. The query is therefore widened by `margin`, and
     *   the exact distance test that follows is made against the unit's live
     *   position, exactly as it was when this walked the whole list.
     */
    class UnitSpatialIndex
    {
    public:
        static constexpr float CellSize = 128.0f;

        struct Entry
        {
            float x;
            float z;
            UnitId id;

            // Carried here so a search can drop its own side without going
            // near the unit itself. It is the one piece of unit state in the
            // index that can change under it -- capture is the only thing
            // that rewrites an owner -- and that rebuilds the index.
            PlayerId owner;

            Entry() = default;
            Entry(float x, float z, UnitId id, PlayerId owner) : x(x), z(z), id(id), owner(owner) {}
        };

        /** Throws away the contents and re-sizes the grid to cover the map. */
        void reset(float left, float top, float width, float height, float queryMargin)
        {
            originX = left;
            originZ = top;
            margin = queryMargin;
            gridWidth = std::max(1, static_cast<int>(std::ceil(width / CellSize)));
            gridHeight = std::max(1, static_cast<int>(std::ceil(height / CellSize)));

            pendingCell.clear();
            pendingEntry.clear();
            cellStart.assign(static_cast<std::size_t>(gridWidth) * static_cast<std::size_t>(gridHeight) + 1, 0);
        }

        void insert(UnitId id, PlayerId owner, float x, float z)
        {
            auto cell = cellIndexOf(x, z);
            pendingCell.push_back(cell);
            pendingEntry.emplace_back(x, z, id, owner);
            ++cellStart[cell + 1];
        }

        /** Counting-sorts what was inserted into per-cell runs. */
        void build()
        {
            for (std::size_t i = 1; i < cellStart.size(); ++i)
            {
                cellStart[i] += cellStart[i - 1];
            }

            cursor.assign(cellStart.begin(), cellStart.end() - 1);
            entries.resize(pendingEntry.size());
            for (std::size_t i = 0; i < pendingEntry.size(); ++i)
            {
                entries[cursor[pendingCell[i]]++] = pendingEntry[i];
            }
        }

        /**
         * Calls f(entry) for every unit that could be within radius of the
         * given point. Cells are laid out row by row, so the cells a query
         * wants out of one row are one contiguous run of entries and come
         * out as a single span -- which is what makes the scan a walk of a
         * packed array rather than a pointer chase per candidate.
         *
         * The reach is widened by the margin the index was reset with, and
         * the entry positions are the ones recorded at the rebuild, so what
         * comes out is a superset and the caller must still make the real
         * test against live positions.
         */
        template <typename F>
        void forEachNear(float x, float z, float radius, F&& f) const
        {
            if (cellStart.empty())
            {
                // Never reset, so there is no grid to walk. Nothing in the
                // engine gets here -- getUnitSpatialIndex always builds
                // before it hands one out -- but a query into an empty grid
                // should come back empty rather than off the end of it.
                return;
            }

            auto reach = radius + margin;
            auto x0 = clampCell(cellCoordinate(x - reach, originX), gridWidth);
            auto x1 = clampCell(cellCoordinate(x + reach, originX), gridWidth);
            auto z0 = clampCell(cellCoordinate(z - reach, originZ), gridHeight);
            auto z1 = clampCell(cellCoordinate(z + reach, originZ), gridHeight);

            for (int cz = z0; cz <= z1; ++cz)
            {
                auto row = static_cast<std::size_t>(cz) * static_cast<std::size_t>(gridWidth);
                auto begin = cellStart[row + static_cast<std::size_t>(x0)];
                auto end = cellStart[row + static_cast<std::size_t>(x1) + 1];
                for (auto i = begin; i < end; ++i)
                {
                    f(entries[i]);
                }
            }
        }

        /**
         * The ids of every unit not belonging to the excluded player that
         * could be within radius of the point, in ascending id order.
         *
         * Dropping the searcher's own side here rather than after the lookup
         * is worth doing: in a two-sided fight it halves what the sort and
         * the caller's per-unit tests have to get through, and a unit never
         * shoots at its own regardless.
         *
         * The order is the point of the method. A VectorMap id is its slot in
         * the map (index << 8 | generation) and only one live unit holds a
         * slot, so ascending id is exactly the order iterating the unit list
         * visits units in. A caller that walked the whole list and drew a
         * random number per surviving candidate can therefore swap in this
         * search and get the same numbers on the same units.
         *
         * The result is a reference to a buffer the index reuses, so it is
         * good until the next call. Nothing queries this while iterating a
         * previous result.
         *
         * The distance is flat, and only ever conservative: the caller's real
         * test may be in three dimensions and against live positions, and
         * both of those can only shrink the answer.
         */
        const std::vector<UnitId>& collectEnemiesNear(float x, float z, float radius, PlayerId excludedOwner) const
        {
            auto reach = radius + margin;
            auto reachSquared = reach * reach;

            scratch.clear();
            forEachNear(x, z, radius, [&](const Entry& e) {
                if (e.owner == excludedOwner)
                {
                    return;
                }
                auto dx = e.x - x;
                auto dz = e.z - z;
                if ((dx * dx) + (dz * dz) <= reachSquared)
                {
                    scratch.push_back(e.id);
                }
            });

            std::sort(scratch.begin(), scratch.end());
            return scratch;
        }

        float getMargin() const
        {
            return margin;
        }

    private:
        static int cellCoordinate(float v, float origin)
        {
            return static_cast<int>(std::floor((v - origin) / CellSize));
        }

        static int clampCell(int c, int dimension)
        {
            return std::clamp(c, 0, dimension - 1);
        }

        std::uint32_t cellIndexOf(float x, float z) const
        {
            auto cx = clampCell(cellCoordinate(x, originX), gridWidth);
            auto cz = clampCell(cellCoordinate(z, originZ), gridHeight);
            return static_cast<std::uint32_t>((static_cast<std::size_t>(cz) * static_cast<std::size_t>(gridWidth)) + static_cast<std::size_t>(cx));
        }

        float originX{0.0f};
        float originZ{0.0f};
        float margin{0.0f};
        int gridWidth{1};
        int gridHeight{1};

        std::vector<std::uint32_t> cellStart;
        std::vector<Entry> entries;

        // Scratch, kept between rebuilds so a tick does no allocation.
        std::vector<std::uint32_t> pendingCell;
        std::vector<Entry> pendingEntry;
        std::vector<std::uint32_t> cursor;
        mutable std::vector<UnitId> scratch;
    };
}
