#pragma once

#include <memory>
#include <rwe/AudioService.h>
#include <rwe/ai/AiTuningProfile.h>
#include <rwe/CursorService.h>
#include <rwe/LoadingNetworkService.h>
#include <rwe/SceneContext.h>
#include <rwe/TextureService.h>
#include <rwe/game/BuilderGuisDatabase.h>
#include <rwe/game/GameScene.h>
#include <rwe/game/MapTerrainGraphics.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/io/featuretdf/FeatureTdf.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/render/TextureArrayRegion.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/LosTables.h>
#include <rwe/sim/Metal.h>
#include <rwe/sim/MovementClassDatabase.h>
#include <rwe/sim/SimVector.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiLightBar.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/MeshService.h>
#include <rwe/game/GameParameters.h>

namespace rwe
 {
    // PlayerInfo, PlayerControllerType and GameParameters live in
    // rwe/game/GameParameters.h so the save-file code can see them without
    // pulling in the whole loading scene.


    class LoadingScene : public Scene
    {
    private:
        SceneContext sceneContext;

        std::unique_ptr<UiPanel> panel;

        UiRenderService scaledUiRenderService;
        UiRenderService nativeUiRenderService;

        TdfBlock* audioLookup;

        AudioService::LoopToken bgm;

        GameParameters gameParameters;

        std::vector<UiLightBar*> bars;

        LoadingNetworkService networkService;

        UiFactory uiFactory;

    public:
        LoadingScene(
            const SceneContext& sceneContext,
            TdfBlock* audioLookup,
            AudioService::LoopToken&& bgm,
            GameParameters gameParameters);

        void init() override;

        void render() override;

    private:
        std::unique_ptr<GameScene> createGameScene(const std::string& mapName, unsigned int schemaIndex);


        struct LoadMapResult
        {
            MapTerrain terrain;
            unsigned char surfaceMetal;
            int minWindSpeed;
            int maxWindSpeed;
            int tidalStrength;
            std::vector<std::pair<Point, std::string>> features;
            MapTerrainGraphics terrainGraphics;
        };

        LoadMapResult loadMap(const std::string& mapName, const rwe::OtaRecord& ota, unsigned int schemaIndex);

        std::vector<TextureArrayRegion> getTileTextures(TntArchive& tnt);

        const SideData& getSideData(const std::string& side) const;

        struct DataMaps
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

        DataMaps loadDefinitions(MeshService& meshService, const std::unordered_set<std::string>& requiredFeatures);

        void preloadSound(GameMediaDatabase& meshDb, const std::string& soundName);

        void preloadSound(GameMediaDatabase& meshDb, const std::optional<std::string>& soundName);

        std::optional<AudioService::SoundHandle> lookUpSound(const std::string& key);

        std::optional<std::vector<std::vector<GuiEntry>>> loadBuilderGui(const std::string& unitName);

        void loadFeature(MeshService& meshService, GameMediaDatabase& gameMediaDatabase, const std::unordered_map<std::string, FeatureTdf>& tdfs, DataMaps& dataMaps, const std::string& initialFeatureName);
        void loadFeatureMedia(MeshService& meshService, std::unordered_map<std::string, UnitModelDefinition>& modelDefinitions, GameMediaDatabase& gameMediaDatabase, const FeatureTdf& tdf);
    };
}
