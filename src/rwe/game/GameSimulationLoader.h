#pragma once

#include <array>
#include <memory>
#include <optional>
#include <rwe/AudioService.h>
#include <rwe/ColorPalette.h>
#include <rwe/MeshService.h>
#include <rwe/PathMapping.h>
#include <rwe/TextureService.h>
#include <rwe/ai/AiBuildTree.h>
#include <rwe/ai/MapIntel.h>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/game/GameMediaDatabase.h>
#include <rwe/game/GameParameters.h>
#include <rwe/grid/Grid.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/TdfBlock.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/LosTables.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/sim/MapTerrain.h>
#include <rwe/sim/MissionRules.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/UnitModelDefinition.h>
#include <rwe/sim/WeaponDefinition.h>
#include <rwe/collections/SimpleVectorMap.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rwe
{
    /**
     * Everything the simulation needs from the map files, read without a
     * renderer: the terrain, the map's economy and wind settings, the features
     * the TNT and the OTA place, and the raw tile indices. The GL path turns
     * the last of those into a texture array; a headless run has no use for it
     * and never looks.
     */
    struct MapData
    {
        MapTerrain terrain;
        unsigned char surfaceMetal;
        int minWindSpeed;
        int maxWindSpeed;
        int tidalStrength;
        int killMul;
        int timeMul;
        std::vector<std::pair<Point, std::string>> features;
        Grid<std::size_t> tileData;
    };

    /**
     * The parsed game data. Most of it the simulation needs; the media
     * database and builder GUIs are the interface's, and a headless load
     * leaves the media database nearly empty rather than fetching art it will
     * never draw.
     */
    struct GameDataMaps
    {
        BuilderGuisDatabase builderGuisDatabase;
        GameMediaDatabase gameMediaDatabase;
        MovementClassDatabase movementClassDatabase;
        std::unordered_map<std::string, UnitDefinition> unitDefinitions;
        std::unordered_map<std::string, UnitModelDefinition> modelDefinitions;
        std::unordered_map<std::string, WeaponDefinition> weaponDefinitions;
        SimpleVectorMap<FeatureDefinition, FeatureDefinitionIdTag> featureDefinitions;
        std::unordered_map<std::string, FeatureDefinitionId> featureNameIndex;
        LosTables losTables;

        /**
         * The data set's unit types in TA's load order, 0-based:
         * `tadUnitLoadOrder` over the `units` directory's FBI listing.
         * `unitDefinitions` is an unordered_map and carries no order, and a
         * demo's 0x09/0x2c type indices are load-order positions, so the
         * listing is captured here while it is still in hand.
         */
        std::vector<std::string> unitLoadOrder;
    };

    /**
     * The services a load may use. The presentation pointers may be null: a
     * headless load passes no texture or audio service and a mesh service
     * whose graphics context is null, and every media fetch is skipped in
     * place. The parsing, and so the simulation, is the same either way.
     */
    struct GameLoadServices
    {
        AbstractVirtualFileSystem* vfs;
        const PathMapping* pathMapping;
        const ColorPalette* palette;
        const ColorPalette* guiPalette;
        TdfBlock* audioLookup;
        MeshService* meshService;
        TextureService* textureService;
        AudioService* audioService;
    };

    /**
     * A game loaded up to the point the interface takes over: the simulation
     * with its terrain, features, players and AI controllers, plus the map
     * intelligence and build tree the AI needs and the per-slot start
     * positions. No window, no GL, no scene.
     */
    struct LoadedGame
    {
        GameSimulation simulation;
        GameParameters gameParameters;
        OtaRecord ota;
        std::optional<PlayerId> localPlayerId;
        std::array<std::optional<PlayerId>, 10> gamePlayers;
        std::array<std::optional<int>, 10> startPositionForSlot;
        MapIntel mapIntel;
        AiBuildTree buildTree;
        GameDataMaps dataMaps;
    };

    /** Reads the map's terrain, economy, wind and features. No GL, no textures. */
    MapData readMapData(TntArchive& tnt, const OtaRecord& ota, unsigned int schemaIndex);

    /**
     * Builds the simulation for a game: definitions, movement classes, feature
     * placement, players, AI controllers, the RNG seed and the start-position
     * deal. This is the half of LoadingScene::createGameScene that never
     * touched the GPU, so the arena can run the same code path with no window.
     */
    LoadedGame loadGameSimulation(
        const GameLoadServices& services,
        const GameParameters& gameParameters,
        MapData mapData,
        const OtaRecord& ota);

    /** What spawnMissionUnits did, for the log and the tests. */
    struct MissionSpawnResult
    {
        /** The units made, in the order of the schema's [unitN] blocks. */
        std::vector<UnitId> spawned;
        /** One line for each [unitN] that was not spawned, and why. */
        std::vector<std::string> skipped;
    };

    /**
     * A mission's starting units, as the original lays them out at mission
     * start (0x488310): each [unitN] of the schema becomes a finished unit of
     * `Player` N's slot N-1. A building is snapped to the build grid for its
     * footprint and set on the ground (0x47DDC0, bmcode 0); a mobile unit is
     * put where the file says and on the ground. Hit points are the maximum
     * times HealthPercentage over a hundred (0x48848E) and the heading is
     * Angle in degrees (0x436EF9).
     *
     * Two departures because RWE has nothing to spawn into: a name no unit
     * definition carries is skipped, as the original skips it, and so is a
     * unit whose slot has no player in it, where the original logs "Player
     * number %d invalid" and makes the unit anyway. And one because RWE
     * keeps one unit to a cell where the original lets them overlap: a
     * mobile unit whose spot is taken goes to the nearest free one within
     * eight cells, and a building whose spot is taken is not made.
     */
    MissionSpawnResult spawnMissionUnits(GameSimulation& simulation, const OtaSchema& schema, const std::array<std::optional<PlayerId>, 10>& slotPlayers);

    /**
     * A mission's rules as the builder at 0x48E010 makes them from the
     * [GlobalHeader], in its order: the eleven victory keys, then the seven
     * defeat keys, with DestroyAllUnits standing in when no victory rule is
     * given and AllUnitsKilled when no defeat rule is. Type names are
     * compared without case, so they are kept in upper case; ANYTYPE becomes
     * the empty name. The Passes rules keep N >> 4, a cell, and the timers N
     * seconds as ticks.
     *
     * `hasUnits` is whether the mission has any [units]: without them the
     * rules are switched off for good (0x488547), as in the original.
     */
    MissionRules buildMissionRules(
        const OtaMissionRules& rules,
        const MapTerrain& terrain,
        bool hasUnits,
        const std::optional<PlayerId>& human,
        const std::optional<PlayerId>& computer,
        const std::string& humanCommander,
        const std::string& computerCommander);

    /**
     * Gives the simulation the mission's rules: slot 0 as P0, slot 1 as P1,
     * and each one's commander from its side. For a new mission only; a
     * loaded save brings its own.
     */
    void installMissionRules(
        GameSimulation& simulation,
        const OtaRecord& ota,
        const OtaSchema& schema,
        const std::array<std::optional<PlayerId>, 10>& slotPlayers,
        const std::unordered_map<std::string, SideData>& sideData);
}
