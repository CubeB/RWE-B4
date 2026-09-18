#pragma once

#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/MovementClassDefinition.h>
#include <rwe/sim/SimVector.h>
#include <rwe/sim/UnitDefinition.h>
#include <vector>

namespace rwe
{
    struct GameSimulation;

    /**
     * Which heightmap tiles a unit of a given movement class can walk (or
     * float) to from a base, ignoring units and buildings. Anything a mover
     * cannot reach is an island (or the far side of a river, or dry land to
     * a ship) and needs some other way across.
     *
     * The terrain never changes during a game, so the expensive part -
     * labelling every tile a movement class can stand on with the connected
     * region it belongs to - is done once per movement class and then
     * reused. Re-homing the map on a different base anchor afterwards is a
     * handful of lookups.
     *
     * There are three independent labellings -- ground, naval and commander --
     * rather than one that gets replaced. BuildManager, TransportManager and
     * ScoutManager use the original unsuffixed API
     * (rebuild/isValid/isReachable/isWalkable/reachableTileCount/
     * walkableTileCount) exactly as before, for whatever ground movement
     * class they pass it -- that behaviour is unchanged down to the call
     * signature. A navy needs its own labelling answered at the same time
     * without evicting that one, so rebuildNaval and its isNaval-/naval-
     * prefixed counterparts label and query a second, separate cache.
     *
     * The third exists because a commander is not the builder the ground
     * layer is labelled for. The ground labelling is built for the side's
     * constructor, and ARMCOM's TANKDS2 wades to water depth 100 and climbs
     * slope 32 where ARMCK's TANKSH2 stops at 12 and 15 -- so asking the
     * constructor's labelling about a commander calls every island across
     * water unreachable while the commander walks there. rebuildCommander
     * and its isCommander- prefixed counterparts answer for that class
     * instead. Calling any one of them never invalidates the others; each still only re-floods the whole
     * heightmap when it is asked for a movement class different from the one
     * it last labelled.
     */
    class ReachabilityMap
    {
    public:
        /**
         * Labels the terrain the first time it is called for a movement class,
         * then homes the map on the given position. Cheap on every call after
         * the first unless the movement class changes.
         */
        void rebuild(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from);

        bool isValid() const { return ground.components.getWidth() > 0; }

        /** True when a unit of the mover's class at the base can reach this point. */
        bool isReachable(const GameSimulation& sim, const SimVector& position) const;

        /** True when a unit of the mover's class can stand here at all. */
        bool isWalkable(const GameSimulation& sim, const SimVector& position) const;

        int reachableTileCount() const { return ground.reachableTiles; }
        int walkableTileCount() const { return ground.walkableTiles; }

        /**
         * The naval counterpart of rebuild(): labels and homes a second,
         * independent cache -- typically for a floating movement class, a
         * destroyer's or a submarine's -- entirely separate from the one
         * rebuild() maintains. Calling this never invalidates a labelling
         * done by rebuild(), and vice versa: an AiPlayerController holding a
         * single ReachabilityMap can answer "can my tank get there" and "can
         * my destroyer get there" in the same tick without re-flooding the
         * heightmap every time the question changes.
         */
        void rebuildNaval(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from);

        bool isNavalValid() const { return naval.components.getWidth() > 0; }

        /** True when a unit of the naval mover's class at its base can reach this point. */
        bool isNavalReachable(const GameSimulation& sim, const SimVector& position) const;

        /** True when a unit of the naval mover's class can stand (float) here at all. */
        bool isNavalWalkable(const GameSimulation& sim, const SimVector& position) const;

        int navalReachableTileCount() const { return naval.reachableTiles; }
        int navalWalkableTileCount() const { return naval.walkableTiles; }

        /**
         * The commander's counterpart of rebuild(): a third independent
         * cache, for the commander's own movement class rather than the
         * constructor's the ground layer is labelled for. Everything said
         * above about rebuildNaval's independence holds here too.
         */
        void rebuildCommander(const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from);

        bool isCommanderValid() const { return commander.components.getWidth() > 0; }

        /** True when the commander, from its base, can reach this point. */
        bool isCommanderReachable(const GameSimulation& sim, const SimVector& position) const;

        /** True when the commander can stand here at all. */
        bool isCommanderWalkable(const GameSimulation& sim, const SimVector& position) const;

        int commanderReachableTileCount() const { return commander.reachableTiles; }
        int commanderWalkableTileCount() const { return commander.walkableTiles; }

    private:
        /**
         * One movement class's labelling: which tiles it can stand on, which
         * connected region each belongs to, and which of those regions count
         * as home given where its base is. Two of these coexist -- ground
         * and naval -- rather than one replacing the other.
         */
        struct Layer
        {
            /** 0 where a unit cannot stand; otherwise the id of the connected region. */
            Grid<int> components;
            /** Tile count of each region, indexed by id - 1. */
            std::vector<int> componentSizes;
            /** The regions the base can walk in. Normally one; more only when the base straddles a gap. */
            std::vector<int> homeComponents;
            /** The base's own tile counts as home even where a unit could not stand on it. */
            std::optional<Point> anchorTile;
            /** The movement class the labelling was done for, so it is not redone needlessly. */
            std::optional<MovementClassDefinition> labelledFor;
            int walkableTiles{0};
            int reachableTiles{0};
        };

        /** Shared body of rebuild()/rebuildNaval(): relabels the given layer only if its movement class changed, then re-homes it. */
        void rebuildLayer(Layer& layer, const GameSimulation& sim, const UnitDefinition::MovementCollisionInfo& mover, const SimVector& from) const;

        /** The one-off flood fill that labels every tile the layer's movement class can stand on with a region id. */
        void labelComponents(Layer& layer, const GameSimulation& sim, const MovementClassDefinition& mc) const;

        /** Works out which regions count as home, given where the base is. */
        void setAnchor(Layer& layer, const GameSimulation& sim, const SimVector& from) const;

        /**
         * The component-grid cell a mover standing CENTRED on this position
         * occupies -- which is its footprint's top-left tile, not the tile its
         * centre happens to fall in.
         *
         * labelComponents labels cell (x,y) by asking whether the footprint
         * STARTING at (x,y) is walkable, so the grid is indexed by top-left
         * tiles. Every query here is handed a unit's position, which is its
         * CENTRE. Reading the grid at the centre's tile therefore asks about a
         * footprint offset by half a footprint -- a whole tile each way for
         * anything 2x2 -- and on a coast that shifted block is very often open
         * water.
         *
         * Measured on Hundred Isles before this existed: every combat unit the
         * AI owned was refused as a ferry passenger, 168,076 refusals in six
         * games with not one other gate ever firing, and 63% of the sampled
         * refusals were standing on ground this map called unwalkable while
         * they were plainly standing on it.
         *
         * The conversion is GameSimulation::computeFootprintRegion's, followed
         * exactly rather than re-derived -- subtract half the footprint in
         * world units, then round to NEAREST -- so the two cannot drift.
         */
        Point footprintOriginTile(const Layer& layer, const GameSimulation& sim, const SimVector& position) const;

        bool isReachable(const Layer& layer, const GameSimulation& sim, const SimVector& position) const;
        bool isWalkable(const Layer& layer, const GameSimulation& sim, const SimVector& position) const;

        Layer ground;
        Layer naval;
        Layer commander;
    };
}
