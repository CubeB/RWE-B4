#pragma once

#include <optional>
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
    };

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
     * Reads the map once. Walks the heightmap a single time, so it is cheap
     * enough to do at load; it is not meant to be called again.
     */
    MapIntel analyseMap(const MapTerrain& terrain, std::vector<SimVector> startPositions);

    /** Index of the declared start position nearest a point, if there are any. */
    std::optional<Index> nearestStartPosition(const MapIntel& intel, const SimVector& position);
}
