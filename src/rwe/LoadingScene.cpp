#include "LoadingScene.h"
#include <rwe/game/SaveFile.h>
#include <algorithm>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/SpanStream.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/atlas_util.h>
#include <rwe/game/GameNetworkService.h>
#include <rwe/game/InGameSoundsInfo.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/io/ota/ota.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/io/tnt/TntArchive.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    const Viewport MenuUiViewport(0, 0, 640, 480);

    GameParameters::GameParameters(const std::string& mapName, unsigned int schemaIndex)
        : mapName(mapName),
          schemaIndex(schemaIndex)
    {
    }

    LoadingScene::LoadingScene(
        const SceneContext& sceneContext,
        TdfBlock* audioLookup,
        AudioService::LoopToken&& bgm,
        GameParameters gameParameters)
        : sceneContext(sceneContext),
          scaledUiRenderService(sceneContext.graphics, sceneContext.shaders, &MenuUiViewport, sceneContext.viewport),
          nativeUiRenderService(sceneContext.graphics, sceneContext.shaders, sceneContext.viewport),
          audioLookup(audioLookup),
          bgm(std::move(bgm)),
          gameParameters(std::move(gameParameters)),
          uiFactory(sceneContext.textureService, sceneContext.audioService, audioLookup, sceneContext.vfs, sceneContext.pathMapping, sceneContext.viewport->width(), sceneContext.viewport->height())
    {
    }

    void LoadingScene::init()
    {
        setCrashScene("LoadingScene");
        auto backgroundSprite = sceneContext.textureService->getBitmapRegion(
            "Loadgame2bg",
            0,
            0,
            640,
            480);

        panel = std::make_unique<UiPanel>(0, 0, 640, 480, backgroundSprite);

        std::vector<std::string> categories{
            "Textures",
            "Terrain",
            "Units",
            "Animation",
            "3D Data",
            "Explosions"};

        auto font = sceneContext.textureService->getGafEntry("anims/hattfont12.gaf", "Haettenschweiler (120)");

        auto barSpriteSeries = sceneContext.textureService->getGuiTexture("", "LIGHTBAR");

        auto barSprite = barSpriteSeries
            ? (*barSpriteSeries)->sprites[0]
            : sceneContext.textureService->getDefaultSpriteSeries()->sprites[0];

        for (Index i = 0; i < getSize(categories); ++i)
        {
            int y = 136 + (i * 42);
            auto label = std::make_unique<UiLabel>(90, y, 100, 12, categories[i], font);
            panel->appendChild(std::move(label));

            auto bar = std::make_unique<UiLightBar>(205, y, 351, 21, barSprite);
            bar->setPercentComplete(i / 5.0f); // for demo/debugging purposes
            bars.push_back(bar.get());
            panel->appendChild(std::move(bar));
        }

        // set up network
        //
        // Not for a recording. A replay of a network game carries the peers it
        // was played against in its header, addresses and all, and every one of
        // their commands in its body -- so there is nobody to connect to and
        // nothing to ask them for. Left in, opening such a recording tried to
        // resolve a host that was long gone and threw before the first frame.
        // See the matching test in createGameScene.
        for (Index i = 0; i < getSize(gameParameters.players) && !gameParameters.replayFile; ++i)
        {
            const auto& p = gameParameters.players[i];
            if (!p)
            {
                continue;
            }
            const auto address = std::visit(GetNetworkAddressVisitor(), p->controller);
            if (!address)
            {
                continue;
            }

            networkService.addEndpoint(i, address->first, address->second);
        }
        networkService.start(gameParameters.localNetworkPort);

        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(createGameScene(gameParameters.mapName, gameParameters.schemaIndex)));

        // wait for other players before starting
        networkService.setDoneLoading();
        if (gameParameters.rejoinAtTick == 0)
        {
            networkService.waitForAllToBeReady();
        }
        else
        {
            // A peer rejoining a game in progress has nobody to agree a start
            // with: the game started long ago, and the peers still in it
            // answer this handshake with "already in game" for ever. What it
            // waits for instead is the rejoin tick, which it reaches by
            // winding the recording forward -- and the peers that stayed are
            // stalled there waiting for exactly that.
            LOG_INFO << "Rejoining at tick " << gameParameters.rejoinAtTick
                     << "; not waiting for the other players to finish loading";
        }
    }

    void LoadingScene::render()
    {
        panel->render(scaledUiRenderService);
    }


    std::unique_ptr<GameScene> LoadingScene::createGameScene(const std::string& mapName, unsigned int schemaIndex)
    {
        auto atlasInfo = createTextureAtlases(sceneContext.vfs, sceneContext.graphics, sceneContext.palette);
        MeshService meshService(sceneContext.vfs, sceneContext.graphics, std::move(atlasInfo.textureAtlasMap), std::move(atlasInfo.teamTextureAtlasMap), std::move(atlasInfo.colorAtlasMap));

        auto otaRaw = sceneContext.vfs->readFile(std::string("maps/").append(mapName).append(".ota"));
        if (!otaRaw)
        {
            throw std::runtime_error("Failed to read OTA file");
        }
        std::string otaStr(otaRaw->begin(), otaRaw->end());
        auto ota = parseOta(parseTdfFromString(otaStr));

        auto mapInfo = loadMap(mapName, ota, schemaIndex);

        GameLoadServices services{
            sceneContext.vfs,
            sceneContext.pathMapping,
            sceneContext.palette,
            sceneContext.guiPalette,
            audioLookup,
            &meshService,
            sceneContext.textureService,
            sceneContext.audioService};

        auto loaded = loadGameSimulation(services, gameParameters, std::move(mapInfo.data), ota);

        auto minimap = sceneContext.textureService->getMinimap(mapName);

        GameCameraState worldCameraState;

        worldCameraState.density = sceneContext.sceneManager->contentScale();

        auto playerCommandService = std::make_unique<PlayerCommandService>();

        std::vector<GameNetworkService::EndpointInfo> endpointInfos;

        for (Index i = 0; i < getSize(gameParameters.players); ++i)
        {
            const auto& params = gameParameters.players[i];
            if (!params || !loaded.gamePlayers[i])
            {
                continue;
            }
            auto playerId = *loaded.gamePlayers[i];
            playerCommandService->registerPlayer(playerId);

            // Nobody is remote in a recording, whatever the header says: every
            // player's commands come out of the file, nothing is sent to
            // anyone, and there is no address to resolve. They stay network
            // players in every other respect, which is what keeps them human
            // players no AI drives and leaves the local seat where the
            // recording had it.
            auto isRemote = !gameParameters.replayFile
                && std::get_if<PlayerControllerTypeNetwork>(&params->controller) != nullptr;

            // Only a peer running its own simulation has a sync hash to
            // report: this machine, and the machines on the other end of the
            // network. That is exactly the set GameScene pushes a hash for
            // every tick, and the set has to match, because a hash buffer
            // nobody ever fills stalls the comparison for every player at
            // once -- which is how desync detection came to be quietly off in
            // any game with a computer player in it. A computer player's
            // commands come out of this very simulation, so it has no second
            // opinion to offer and gets no buffer.
            if (playerId == *loaded.localPlayerId || isRemote)
            {
                // On a peer rejoining a game in progress, every hash stream
                // starts at the rejoin tick: this peer has none for the ticks
                // it missed, and that is also where the peers that stayed
                // resume sending to it. In an ordinary game it is tick 1,
                // which is the default.
                playerCommandService->registerHashSource(
                    playerId,
                    gameParameters.rejoinAtTick > 0 ? SceneTime(gameParameters.rejoinAtTick) : SceneTime(1));
            }

            if (isRemote)
            {
                auto& endpoint = endpointInfos.emplace_back(playerId, networkService.getEndpoint(i));

                // Both streams from a peer that stayed resume where this one
                // rejoins, and everything below that it already has from the
                // catch-up. A sequence number is an absolute position, so
                // saying so is the whole of it: a packet carrying sets from
                // further back is skipped down to here rather than refused.
                if (gameParameters.rejoinAtTick > 0)
                {
                    auto resumeAt = gameParameters.rejoinAtTick - 1;
                    endpoint.nextCommandToSend = SequenceNumber(resumeAt);
                    endpoint.nextCommandToReceive = SequenceNumber(resumeAt);
                    endpoint.nextHashToSend = GameTime(resumeAt);
                    endpoint.nextHashToReceive = GameTime(resumeAt);
                }
            }
        }

        auto resumeFromSequence = gameParameters.rejoinAtTick > 0
            ? SequenceNumber(gameParameters.rejoinAtTick - 1)
            : SequenceNumber(0);
        auto gameNetworkService = std::make_unique<GameNetworkService>(*loaded.localPlayerId, std::stoi(gameParameters.localNetworkPort), endpointInfos, playerCommandService.get(), resumeFromSequence);

        auto minimapDots = sceneContext.textureService->getGafEntry("anims/FX.GAF", "radlogo");
        if (minimapDots->sprites.size() != 10)
        {
            throw std::runtime_error("Incorrect number of frames in anims/FX.GAF radlogo");
        }
        auto minimapDotHighlight = sceneContext.textureService->getGafEntry("anims/FX.GAF", "radlogohigh")->sprites.at(0);

        InGameSoundsInfo sounds;
        sounds.immediateOrders = lookUpSound("IMMEDIATEORDERS");
        sounds.specialOrders = lookUpSound("SPECIALORDERS");
        sounds.setFireOrders = lookUpSound("SETFIREORDERS");
        sounds.setMoveOrders = lookUpSound("SETMOVEORDERS");
        sounds.nextBuildMenu = lookUpSound("NEXTBUILDMENU");
        sounds.buildButton = lookUpSound("BUILDBUTTON");
        sounds.ordersButton = lookUpSound("ORDERSBUTTON");
        sounds.addBuild = lookUpSound("ADDBUILD");
        sounds.okToBuild = lookUpSound("OKTOBUILD");
        sounds.notOkToBuild = lookUpSound("NOTOKTOBUILD");
        sounds.selectMultipleUnits = lookUpSound("SelectMultipleUnits");
        sounds.panel = lookUpSound("PANEL");
        sounds.options = lookUpSound("OPTIONS");
        sounds.victoryCondition = lookUpSound("Victory Condition");

        auto consoleFont = sceneContext.textureService->getFont("fonts/CONSOLE.FNT");
        // The original loads exactly two in-game fonts (0x42A320): COMIX for
        // the world text -- the speech console, the countdown -- and SMLFONT
        // for the tiny build-button overlays. The game-screen setup at
        // 0x497FBE selects COMIX where the message queue lives.
        auto speechFont = sceneContext.textureService->getFont("fonts/COMIX.FNT");

        std::optional<std::ofstream> stateLogStream;
        if (gameParameters.stateLogFile)
        {
            stateLogStream = std::ofstream(*gameParameters.stateLogFile, std::ios::binary);
        }

        // A mission brings its own units in place of the commanders, and its
        // own rules in place of the skirmish ones. They are put into the
        // simulation before it moves into the scene, and the camera goes to
        // the first of the local player's units.
        std::optional<SimVector> missionCameraPos;
        // Not when loading a save: the saved simulation already holds the
        // mission's units, as it holds a skirmish's commanders, and spawning
        // them here would put a second set beside the first.
        if (gameParameters.mission && !gameParameters.loadFromSaveFile)
        {
            auto result = spawnMissionUnits(loaded.simulation, ota.schemas.at(schemaIndex), loaded.gamePlayers);
            installMissionRules(loaded.simulation, ota, ota.schemas.at(schemaIndex), loaded.gamePlayers, *sceneContext.sideData);
            LOG_INFO << "Mission: " << result.spawned.size() << " units placed, " << result.skipped.size() << " not";
            for (const auto& line : result.skipped)
            {
                LOG_WARN << "Mission unit not placed: " << line;
            }
            for (auto unitId : result.spawned)
            {
                const auto& unit = loaded.simulation.getUnitState(unitId);
                if (!missionCameraPos || (loaded.localPlayerId && unit.owner == *loaded.localPlayerId))
                {
                    missionCameraPos = unit.position;
                    if (loaded.localPlayerId && unit.owner == *loaded.localPlayerId)
                    {
                        break;
                    }
                }
            }
        }

        auto gameScene = std::make_unique<GameScene>(
            sceneContext,
            std::move(playerCommandService),
            std::move(loaded.dataMaps.gameMediaDatabase),
            worldCameraState,
            atlasInfo.textureAtlas,
            std::move(atlasInfo.teamTextureAtlases),
            atlasInfo.paletteIndexAtlas,
            std::move(atlasInfo.teamPaletteIndexAtlases),
            atlasInfo.shadeTableTexture,
            atlasInfo.alphaTableTexture,
            std::move(loaded.simulation),
            std::move(mapInfo.terrainGraphics),
            std::move(loaded.dataMaps.builderGuisDatabase),
            std::move(gameNetworkService),
            minimap,
            minimapDots,
            minimapDotHighlight,
            std::move(sounds),
            consoleFont,
            speechFont,
            gameParameters,
            *loaded.localPlayerId,
            audioLookup,
            std::move(stateLogStream));

        if (gameParameters.loadFromSaveFile)
        {
            // A resumed game: the world state comes off disk instead of the
            // starting commanders. The simulation was built above with the
            // map's features and no units, which is what the loader expects.
            auto save = readSaveFile(*gameParameters.loadFromSaveFile);
            if (!save)
            {
                throw std::runtime_error("Could not read save file: " + *gameParameters.loadFromSaveFile);
            }
            gameScene->applyLoadedGame(*save);
            return gameScene;
        }

        const auto& schema = ota.schemas.at(schemaIndex);

        std::optional<SimVector> humanStartPos;

        if (missionCameraPos)
        {
            humanStartPos = missionCameraPos;
        }

        // The battle harness wants every start position and no commanders.
        std::vector<PlayerId> battlePlayers;
        std::vector<SimVector> battleSpawns;

        for (Index i = 0; i < getSize(gameParameters.players) && !gameParameters.mission; ++i)
        {
            const auto& player = gameParameters.players[i];
            if (!player)
            {
                continue;
            }

            // The lobby refuses to start a game whose slots the map cannot
            // seat, so this is the backstop for the command line and the
            // harnesses. It still ends the game, but it says which map and
            // which slot rather than naming a key.
            auto startPos = findStartPosition(schema, *loaded.startPositionForSlot[i]);
            if (!startPos)
            {
                throw std::runtime_error(
                    "Map \"" + mapName + "\" has no start position for player slot " + std::to_string(*loaded.startPositionForSlot[i])
                    + " (schema " + std::to_string(schemaIndex) + " declares " + std::to_string(countStartPositions(schema)) + ")");
            }

            auto worldStartPos = gameScene->getTerrain().topLeftCoordinateToWorld(SimVector(SimScalar(startPos->xPos), 0_ss, SimScalar(startPos->zPos)));
            worldStartPos.y = gameScene->getTerrain().getHeightAt(worldStartPos.x, worldStartPos.z);

            if (*loaded.gamePlayers[i] == *loaded.localPlayerId)
            {
                humanStartPos = worldStartPos;
            }

            if (gameParameters.battleTestUnitsPerSide)
            {
                // No commander: the harness fills the field itself, and a
                // commander standing in it would only distort the fight.
                battlePlayers.push_back(*loaded.gamePlayers[i]);
                battleSpawns.push_back(worldStartPos);
                continue;
            }

            const auto& sideData = getSideData(player->side);
            gameScene->spawnCompletedUnit(sideData.commander, *loaded.gamePlayers[i], worldStartPos);
        }

        // No `battlePlayers.size() >= 2` guard here on purpose: a one-player
        // battle test used to fall through this silently and leave an empty
        // map with nothing to explain it. enableBattleTest says so instead.
        if (gameParameters.battleTestUnitsPerSide)
        {
            gameScene->enableBattleTest(
                *gameParameters.battleTestUnitsPerSide,
                gameParameters.battleTestUnitTypes,
                battlePlayers,
                battleSpawns);
        }

        if (!humanStartPos)
        {
            throw std::runtime_error("No human player!");
        }

        gameScene->setCameraPosition(Vector3f(simScalarToFloat(humanStartPos->x), 0.0f, simScalarToFloat(humanStartPos->z)));

        if (gameParameters.rejoinFromReplayFile)
        {
            // Rejoining a game in progress. The recording of it so far is
            // wound through before the first live tick; see beginRejoin.
            auto catchUp = readReplayFile(*gameParameters.rejoinFromReplayFile);
            if (!catchUp)
            {
                throw std::runtime_error("Could not read rejoin file: " + *gameParameters.rejoinFromReplayFile);
            }
            gameScene->beginRejoin(std::move(*catchUp), gameParameters.rejoinAtTick);
        }
        else if (gameParameters.replayFile)
        {
            auto replay = readReplayFile(*gameParameters.replayFile);
            if (!replay)
            {
                throw std::runtime_error("Could not read replay file: " + *gameParameters.replayFile);
            }
            gameScene->enableReplayPlayback(std::move(*replay));
        }
        else if (gameParameters.recordReplayFile)
        {
            gameScene->enableReplayRecording(*gameParameters.recordReplayFile, replayHeaderFromParameters(gameParameters));
        }

        // Independent of the replay branch above: a replay and a demo can be
        // recorded in the same game.
        if (gameParameters.recordDemoFile)
        {
            gameScene->enableDemoRecording(*gameParameters.recordDemoFile, loaded.dataMaps.unitLoadOrder);
        }

        return gameScene;
    }


    LoadingScene::LoadMapResult LoadingScene::loadMap(const std::string& mapName, const OtaRecord& ota, unsigned int schemaIndex)
    {
        auto tntBytes = sceneContext.vfs->readFile("maps/" + mapName + ".tnt");
        if (!tntBytes)
        {
            throw std::runtime_error("Failed to load map bytes");
        }

        rwe::SpanStream tntStream(tntBytes->data(), tntBytes->size());
        TntArchive tnt(&tntStream);

        auto tileTextures = getTileTextures(tnt);

        auto data = readMapData(tnt, ota, schemaIndex);

        MapTerrainGraphics terrainGraphics(
            std::move(tileTextures),
            std::move(data.tileData));

        return LoadMapResult{std::move(data), std::move(terrainGraphics)};
    }

    std::vector<TextureArrayRegion> LoadingScene::getTileTextures(TntArchive& tnt)
    {
        static const unsigned int tileWidth = 32;
        static const unsigned int tileHeight = 32;
        static const unsigned int mipMapLevels = 5;
        static const auto tilesPerTextureArray = 256;

        std::vector<TextureArrayRegion> tileTextures;

        std::vector<Color> textureArrayBuffer;
        textureArrayBuffer.reserve(tileWidth * tileHeight * tilesPerTextureArray);

        std::vector<SharedTextureArrayHandle> textureArrayHandles;

        // read the tile graphics into textures
        {
            tnt.readTiles([&](const char* tile) {
                if (textureArrayBuffer.size() == tileWidth * tileHeight * tilesPerTextureArray)
                {
                    SharedTextureArrayHandle handle(sceneContext.graphics->createTextureArray(tileWidth, tileHeight, mipMapLevels, textureArrayBuffer));
                    textureArrayHandles.push_back(std::move(handle));
                    textureArrayBuffer.clear();
                }

                for (unsigned int i = 0; i < (tileWidth * tileHeight); ++i)
                {
                    auto index = static_cast<unsigned char>(tile[i]);
                    textureArrayBuffer.push_back((*sceneContext.palette)[index]);
                }
            });
        }
        textureArrayHandles.emplace_back(sceneContext.graphics->createTextureArray(tileWidth, tileHeight, mipMapLevels, textureArrayBuffer));

        // populate the list of texture regions referencing the textures
        for (unsigned int i = 0; i < tnt.getHeader().numberOfTiles; ++i)
        {
            assert(textureArrayHandles.size() > i / tilesPerTextureArray);
            auto textureIndex = i / tilesPerTextureArray;
            auto tileIndex = i % tilesPerTextureArray;
            tileTextures.emplace_back(textureArrayHandles[textureIndex], tileIndex);
        }

        return tileTextures;
    }


    const SideData& LoadingScene::getSideData(const std::string& side) const
    {
        auto it = sceneContext.sideData->find(side);
        if (it == sceneContext.sideData->end())
        {
            throw std::runtime_error("Missing side data for " + side);
        }

        return it->second;
    }

    std::optional<AudioService::SoundHandle> LoadingScene::lookUpSound(const std::string& key)
    {
        auto soundBlock = audioLookup->findBlock(key);
        if (!soundBlock)
        {
            return std::nullopt;
        }

        auto soundName = soundBlock->get().findValue("sound");
        if (!soundName)
        {
            return std::nullopt;
        }

        return sceneContext.audioService->loadSound(*soundName);
    }
}
