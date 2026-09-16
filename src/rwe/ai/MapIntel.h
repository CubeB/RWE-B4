#pragma once

#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/grid/Point.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/SimVector.h>
#include <rwe/util/Index.h>
#include <vector>

namespace rwe
{
    /**
     * What kind of map this is, in the one sense that changes what an army
     * ought to be made of.
     */
    enum class MapCharacter
    {
        /** Water is scenery. A navy has nowhere useful to go. */
        Land,
        /** Both matter: a coastline worth holding, and ground worth walking. */
        Mixed,
        /** Mostly water. Expect islands, and expect to need a way across. */
        Water,
    };

    const char* mapCharacterName(MapCharacter character);

    /**
     * A place an 8x8 shipyard needing MinWaterDepth=30 could stand -- found by
     * testing depth alone against the raw heightmap, the same test
     * GameSimulation::canBeBuiltAt makes via isWaterDepthWithinBounds. This is
     * a nomination, not a validation: occupied cells, an enemy sighted on the
     * spot, and every other obstruction that can change during a game are
     * exactly what canBeBuiltAt still has to check before a site is actually
     * usable. MapIntel's job stops at "the water here is deep enough and
     * there is room", so a later ring-walk over candidates has something
     * short to walk instead of every cell on the map.
     */
    struct NavalSite
    {
        /** Top-left heightmap tile of the 8x8 footprint, as GameSimulation::canBeBuiltAt expects it. */
        Point tile;

        /** World-space centre of the footprint, for aiming a builder at it. */
        SimVector position;

        /**
         * The id (into waterRegionSizes, 1-based, matching waterRegions) of
         * the body of water this site opens onto. 0 would mean the site's own
         * centre tile came out dry, which should not happen given the depth
         * test that produced this site, but is checked rather than assumed.
         */
        int waterRegion;
    };

    /**
     * The 8x8 footprint and MinWaterDepth=30 both ARMSY and CORSY share --
     * see the verified shipped data this was read from. MapIntel does not
     * need this to vary per mod, because every side's shipyard already
     * agrees on it; a mod that changes it would need its own analysis, which
     * is outside what a load-time pass over immutable terrain can know.
     */
    constexpr unsigned int NavalShipyardFootprintX = 8;
    constexpr unsigned int NavalShipyardFootprintZ = 8;
    constexpr unsigned int NavalShipyardMinWaterDepth = 30;

    /**
     * What the AI is told about the map before anyone has moved.
     *
     * A human reads every one of these off the lobby's map preview -- how much
     * of it is water, and where the start positions are. Handing the AI the
     * same thing is not a cheat; withholding it would leave the AI unable to
     * make the one decision a player makes before the game even starts, which
     * is what to build first.
     *
     * What is deliberately NOT here is anything a player would have to scout
     * for: which start position the enemy actually took, what they are
     * building, or what stands anywhere on the ground. The AI learns those the
     * way a player does, through PerceptionManager and the fog.
     *
     * Computed once from map data that never changes, and never updated after,
     * so this is not simulation state: it is neither saved nor hashed. That is
     * the same exemption UnitSpatialIndex has and it holds for the same reason
     * -- nothing here can differ between two peers that loaded the same map.
     */
    struct MapIntel
    {
        /** False until analyseMap has run; every consumer must tolerate that. */
        bool valid{false};

        MapCharacter character{MapCharacter::Land};

        /** Share of the heightmap standing below sea level, 0 to 1. */
        float waterFraction{0.0f};

        /** Every start position the map declares, in the map's own order. */
        std::vector<SimVector> startPositions;

        /**
         * 0 where a heightmap cell is dry; otherwise the id of the connected
         * body of open water (any cell below sea level, 4-connected on the
         * raw heightmap) it belongs to. This is coarser than
         * ReachabilityMap's per-movement-class labelling -- it knows nothing
         * of a mover's footprint or its own minimum depth, only "is there
         * water here at all" -- which is what makes it cheap enough to keep
         * here rather than asking a real movement class for it. Use it to
         * ask "is this roughly the same sea", not "can a destroyer get
         * there": that finer question is ReachabilityMap's naval layer.
         */
        Grid<int> waterRegions;

        /** Tile count of each region in waterRegions, indexed by id - 1. */
        std::vector<int> waterRegionSizes;

        /**
         * Sites where an 8x8 shipyard could stand, found once at load. See
         * NavalSite for what "could" leaves for the caller to still check.
         */
        std::vector<NavalSite> shipyardSites;
    };

    /**
     * Which connected body of waterRegions a world position falls in, or 0 if
     * it is dry or off the map. Takes the terrain too, purely to convert the
     * world position to a heightmap tile the same way everything else does
     * (MapTerrain::worldToHeightmapCoordinate) rather than re-deriving that
     * conversion from waterRegions' own dimensions.
     */
    int waterRegionAt(const MapIntel& intel, const MapTerrain& terrain, const SimVector& position);

    /** True when both points are water and fall in the same connected body of it. False if either is dry, off the map, or they are different bodies. */
    bool sameWaterBody(const MapIntel& intel, const MapTerrain& terrain, const SimVector& a, const SimVector& b);

    /**
     * Above this share of water the map is no longer a land map: there is
     * enough sea that it shapes where an army can go. Below it, the water is
     * a lake or a river and a navy is a waste of metal.
     */
    constexpr float MixedMapWaterFraction = 0.12f;

    /**
     * Above this, the water is the map. Ground routes between start positions
     * are the exception rather than the rule, and getting across matters more
     * than what does the fighting once it is there.
     */
    constexpr float WaterMapWaterFraction = 0.40f;

    /**
     * Reads the map once: the water-fraction/character pass over the
     * heightmap, then a second pass over the same heightmap that labels
     * connected water and nominates shipyard sites. Two passes rather than
     * one, but each is still just a walk of the heightmap, so it stays cheap
     * enough to do at load; it is not meant to be called again.
     */
    MapIntel analyseMap(const MapTerrain& terrain, std::vector<SimVector> startPositions);

    /** Index of the declared start position nearest a point, if there are any. */
    std::optional<Index> nearestStartPosition(const MapIntel& intel, const SimVector& position);
}
