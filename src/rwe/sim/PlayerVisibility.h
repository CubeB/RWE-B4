#pragma once

#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/LosTables.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitId.h>
#include <unordered_set>
#include <vector>

namespace rwe
{
    /**
     * Ground height per vision cell, in the two flavours the line of sight
     * march needs. The asymmetry is deliberate and errs towards revealing:
     * a cell is easier to see than it is to hide behind.
     */
    struct VisionHeightGrid
    {
        /** Max-biased height: decides whether a cell can be seen. */
        Grid<unsigned char> reveal;

        /** Min-biased height: decides whether a cell blocks sight past it. */
        Grid<unsigned char> occlude;
    };

    /**
     * What one player can see of the map, plus the player's radar and sonar
     * coverage.
     *
     * The grids are kept at a coarser resolution than the heightmap
     * (VisionCellSizeInTiles heightmap tiles per cell, i.e. 32 world units)
     * and are indexed in projected space - see GameSimulation::visionCellAt
     * for the transform, which anything reading these grids must apply.
     */
    struct PlayerVisibility
    {
        static constexpr int VisionCellSizeInTiles = 2;

        /** A radar or sonar dish: one of the player's units, and its reach. */
        struct RadarDetector
        {
            SimVector position;

            /** Squared reach, in world units, measured in the map plane. */
            SimScalar rangeSquared;

            /**
             * Sonar rather than radar. The original keeps the two contacts
             * apart because they answer different questions - sonar only ever
             * finds a unit at or below the waterline, radar only one whose
             * model rises above it - and because a jammer erases one without
             * touching the other.
             */
            bool sonar{false};
        };

        /** A radar or sonar jammer: anyone else's unit, and the radius it blanks. */
        struct RadarJammer
        {
            SimVector position;

            /** Squared radius, in world units, measured in the map plane. */
            SimScalar rangeSquared;

            /** Jams sonar contacts rather than radar ones. */
            bool sonar{false};
        };

        /** Cells that have been seen at some point. Never cleared. */
        Grid<unsigned char> explored;

        /**
         * Reference count per cell: how many of the player's units currently
         * see it. Non-zero means visible. Rebuilt every tick.
         */
        Grid<unsigned char> visible;

        /**
         * The player's active radar and sonar dishes. Rebuilt every tick.
         * Radar reveals no ground at all - it only makes units detectable.
         */
        std::vector<RadarDetector> radarDetectors;

        /**
         * The jammers working against this player: every other player's active
         * jammer, rebuilt every tick. The original skips only the viewer's own,
         * so an ally's jammer blanks your radar just as an enemy's does.
         */
        std::vector<RadarJammer> radarJammers;

        /**
         * Units currently inside one of those dishes' reach, kept apart the
         * way the original keeps them apart: `unit+0x110` bit 8 for radar and
         * bit 9 for sonar, set by two different arms of the same visitor at
         * 0x467840 and erased by two different jammers.
         *
         * They are kept apart here because the two are read for different
         * things. Radar feeds the picture and nothing else -- the minimap dot,
         * and no simulation decision at all. Sonar is read by the can-see
         * predicate at 0x465AC0, where it lifts the veto that would otherwise
         * hide anything below the waterline.
         *
         * Both are rebuilt every tick and only ever queried by id, never
         * iterated, so their ordering does not reach the simulation.
         *
         * Derived state, and deliberately so: they are recomputed from the
         * unit list and the dish list at the top of every tick, so they are
         * neither saved, nor hashed, nor dumped. Nothing here is state a load
         * would have to restore -- the first tick after a load rebuilds it --
         * which is what keeps them outside the four-places rule.
         */
        std::unordered_set<UnitId> radarContacts;
        std::unordered_set<UnitId> sonarContacts;

        PlayerVisibility() = default;
        PlayerVisibility(int width, int height);

        bool contains(const Point& cell) const;
        bool isExplored(const Point& cell) const;
        bool isVisible(const Point& cell) const;

        /** How many of the player's units see the cell; 0 outside the grid. */
        int visibleCount(const Point& cell) const;

        void clearCurrent();

        /**
         * Reveals what a unit standing on the centre cell, with its eye
         * eyeHeight above the map's zero, can see out to radius cells.
         *
         * The centre cell is always revealed. Beyond that, sight follows the
         * authored rays of tables.tableForRadius(radius), each replicated by
         * the four ninety degree rotations; cells that lie on no ray are
         * never revealed. Along each ray a running horizon is carried, so a
         * ridge shadows everything behind it rather than each cell being
         * tested against a fresh line.
         *
         * Every cell revealed by this call counts once towards the visible
         * reference count, however many rays reach it.
         */
        void revealWithLineOfSight(
            const Point& center,
            int radius,
            const VisionHeightGrid& heights,
            int eyeHeight,
            const LosTables& tables);

        /**
         * Reveals a flat disc of radius cells around the centre, paying no
         * attention at all to what the ground in between is doing.
         *
         * This is the Circular sight option. The original does not ray-trace
         * in that mode either: it blits one of ten hand-drawn mask sprites
         * from anims/vismasks.gaf over the grid. RWE has no reader for those
         * masks, so the disc dx^2 + dy^2 <= radius^2 stands in for the
         * artwork; the shapes differ only around the rim.
         *
         * As with the line-of-sight march, the centre cell is always revealed
         * and every revealed cell counts once towards the reference count.
         */
        void revealCircle(const Point& center, int radius);

        /**
         * Lights every cell that has ever been explored.
         *
         * This is the whole of the Permanent sight option. Everything
         * downstream -- the fog renderer, canSeeUnit, the target scan -- reads
         * the visible grid, so promoting remembered ground into it brings the
         * units standing on that ground back with it, which is exactly what
         * the option is for.
         *
         * A cell already lit by a live unit keeps its reference count; a
         * merely remembered one is given a count of one. Nothing ever
         * subtracts from these counts -- the grid is rebuilt from nothing
         * every tick -- so handing out a count here costs nothing later.
         */
        void makeExploredVisible();

        /**
         * Marks the whole map explored, which is what the Mapped option does
         * before the game starts. Explored, not visible: mapping hands over
         * the ground and never the units standing on it.
         */
        void exploreAll();

    private:
        /**
         * Which pass of revealWithLineOfSight last touched each cell, so that
         * a unit whose rays cross the same cell twice only counts once.
         */
        Grid<unsigned int> seenStamp;
        unsigned int currentStamp{0};

        void beginReveal();
        void revealCell(int x, int y);
    };

    /**
     * The vision cell containing a point given in heightmap space (that is,
     * in heightmap tiles rather than world units), with terrainHeight the
     * height of the ground there.
     *
     * This is the shared implementation of the projected-space transform
     * documented on GameSimulation::visionCellAt; read that comment first.
     * The returned cell may lie outside the grid.
     */
    Point heightmapToVisionCell(SimScalar tileX, SimScalar tileZ, int terrainHeight);

    /**
     * Builds the two-height vision grid from the heightmap.
     *
     * Cells are placed in projected space, so a sample's row is skewed up the
     * map by half its height (heightmapToVisionCell). Within a cell,
     * reveal = max(seaLevel, (2*max + min) / 3) and
     * occlude = max(seaLevel, (max + 2*min) / 3) over the samples that land
     * in it. Cells that the skew leaves without any sample - the ground
     * hidden behind a cliff face - fall back to the plan-view block at the
     * same index.
     */
    VisionHeightGrid computeVisionHeights(const Grid<unsigned char>& heightmap, unsigned char seaLevel);
}
