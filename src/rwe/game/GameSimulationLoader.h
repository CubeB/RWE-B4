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
#include <rwe/sim/MapTerrain.h>
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
}
