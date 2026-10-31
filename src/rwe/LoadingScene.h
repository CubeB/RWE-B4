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
#include <rwe/game/GameSimulationLoader.h>
#include <rwe/game/MapTerrainGraphics.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/sidedatatdf/SideData.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/render/TextureArrayRegion.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
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

        void update(int millisecondsElapsed) override;

        void render() override;

    private:
        /**
         * MLR-002: the loading presentation is drawn once before the
         * expensive construction starts, so the skirmish screen does not sit
         * frozen on screen through it. This is RWE's own frame scheduling --
         * the reference establishes the loading presentation concurrent with
         * match construction, not a mandatory order for the first frame.
         */
        bool loadPending{true};
        int framesUntilLoad{1};

        void loadGame();

        std::unique_ptr<GameScene> createGameScene(const std::string& mapName, unsigned int schemaIndex);


        struct LoadMapResult
        {
            MapData data;
            MapTerrainGraphics terrainGraphics;
        };

        LoadMapResult loadMap(const std::string& mapName, const rwe::OtaRecord& ota, unsigned int schemaIndex);

        std::vector<TextureArrayRegion> getTileTextures(TntArchive& tnt);

        const SideData& getSideData(const std::string& side) const;

        std::optional<AudioService::SoundHandle> lookUpSound(const std::string& key);
    };
}
