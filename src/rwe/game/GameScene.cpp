#include "GameScene.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/game/SaveFile.h>
#include <rwe/io/gui/gui.h>
#include <rwe/game/save_util.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/MainMenuScene.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/Mesh.h>
#include <rwe/camera_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/game/OrderButtons.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/matrix_util.h>
#include <rwe/render/render_prof.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/resource_io.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/util/Index.h>
#include <rwe/util/match.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    /** True for particles drawn among the world's geometry rather than over the finished frame. */
    bool particleDrawsInWorld(const Particle& particle)
    {
        auto sprite = std::get_if<ParticleRenderTypeSprite>(&particle.renderType);
        return sprite != nullptr && sprite->inWorld;
    }

    bool isValidUnitType(const GameSimulation& simulation, const std::string& unitType)
    {
        return simulation.unitDefinitions.find(unitType) != simulation.unitDefinitions.end();
    }

    std::optional<std::reference_wrapper<const std::vector<GuiEntry>>> getBuilderGui(const BuilderGuisDatabase& db, const std::string& unitType, unsigned int page)
    {
        const auto& pages = db.tryGetBuilderGui(unitType);
        if (!pages)
        {
            return std::nullopt;
        }

        const auto& unwrappedPages = pages->get();

        if (page >= unwrappedPages.size())
        {
            return std::nullopt;
        }

        return unwrappedPages[page];
    }

    /** If the unit has no build gui, this will be zero. */
    unsigned int getBuildPageCount(const BuilderGuisDatabase& db, const std::string& unitType)
    {
        const auto& pages = db.tryGetBuilderGui(unitType);
        if (!pages)
        {
            return 0;
        }

        return pages->get().size();
    }

    bool unitCanAttack(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.canAttack;
    }

    bool unitCanMove(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.canMove;
    }

    bool unitCanGuard(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.canGuard;
    }

    bool unitIsBuilder(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.builder;
    }

    bool unitIsBuilder(const GameSimulation& sim, std::optional<UnitId> singleSelectedUnit)
    {
        if (!singleSelectedUnit)
        {
            return false;
        }
        const auto& unit = sim.getUnitState(*singleSelectedUnit);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.builder;
    }

    /**
     * Whether clicking `target` with `flyer` selected should send the
     * aircraft down onto a repair pad rather than merely moving it there.
     *
     * Three arms of the original's order dispatcher -- 0x43F735, 0x43F959
     * and 0x43FAEF -- test the same pair, the mover being `canfly` and the
     * thing under the cursor being `isairbase`, and all three jump to the
     * same place: 0x43FB1B, the VTOL_LANDING mission. The cursor agrees
     * (0x43EA83 gives cursor 13 for exactly that pair), and the FAQ's
     * account of it is the same gesture from the player's side: "Select the
     * plane, click on Move and then click on the repair pad."
     */
    bool unitShouldLandOnAirBase(const GameSimulation& sim, UnitId flyer, UnitId target)
    {
        if (flyer == target)
        {
            return false;
        }
        const auto& flyerState = sim.getUnitState(flyer);
        if (!sim.unitDefinitions.at(flyerState.unitType).canFly)
        {
            return false;
        }
        const auto& targetState = sim.getUnitState(target);
        if (!targetState.isOwnedBy(flyerState.owner))
        {
            return false;
        }
        return unitIsAnUsableAirBase(targetState, sim.unitDefinitions.at(targetState.unitType));
    }

    bool unitIsBeingBuilt(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unit.isBeingBuilt(unitDefinition);
    }

    bool unitIsDamaged(const GameSimulation& sim, UnitId unitId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unit.isAlive() && !unit.isBeingBuilt(unitDefinition) && unit.hitPoints < unitDefinition.maxHitPoints;
    }

    bool unitIsSelectableBy(const GameSimulation& sim, UnitId unitId, PlayerId playerId)
    {
        const auto& unit = sim.getUnitState(unitId);
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unit.isSelectableBy(unitDefinition, playerId);
    }

    bool unitIsOwnedByPlayerAndIsBuilder(const GameSimulation& sim, PlayerId playerId, std::optional<UnitId> singleSelectedUnit)
    {
        if (!singleSelectedUnit)
        {
            return false;
        }
        const auto& unit = sim.getUnitState(*singleSelectedUnit);
        if (!unit.isOwnedBy(playerId))
        {
            return false;
        }
        const auto& unitDefinition = sim.unitDefinitions.at(unit.unitType);
        return unitDefinition.builder;
    }

    bool shouldShowAllBuildBoxes(const GameSimulation& sim, PlayerId localPlayerId, std::optional<UnitId> singleSelectedUnit, std::optional<UnitId> hoveredUnit)
    {
        return unitIsBuilder(sim, singleSelectedUnit) || unitIsOwnedByPlayerAndIsBuilder(sim, localPlayerId, hoveredUnit);
    }

    Line3x<SimScalar> floatToSimLine(const Line3f& line)
    {
        return Line3x<SimScalar>(floatToSimVector(line.start), floatToSimVector(line.end));
    }

    Matrix4f computeView(const Vector3f& cameraPosition)
    {
        auto translation = Matrix4f::translation(-cameraPosition);
        auto rotation = Matrix4f::rotationToAxes(Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, -1.0f), Vector3f(0.0f, 1.0f, 0.0f));
        return rotation * translation;
    }

    Matrix4f computeInverseView(const Vector3f& cameraPosition)
    {
        auto translation = Matrix4f::translation(cameraPosition);
        auto rotation = Matrix4f::rotationToAxes(Vector3f(1.0f, 0.0f, 0.0f), Vector3f(0.0f, 0.0f, -1.0f), Vector3f(0.0f, 1.0f, 0.0f)).transposed();
        return translation * rotation;
    }

    Matrix4f computeProjection(float width, float height)
    {
        float halfWidth = width / 2.0f;
        float halfHeight = height / 2.0f;

        auto cabinet = Matrix4f::cabinetProjection(0.0f, 0.5f);

        auto ortho = Matrix4f::orthographicProjection(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            -1000.0f,
            1000.0f);

        return ortho * cabinet;
    }

    Matrix4f computeInverseProjection(float width, float height)
    {
        float halfWidth = width / 2.0f;
        float halfHeight = height / 2.0f;

        auto inverseCabinet = Matrix4f::cabinetProjection(0.0f, -0.5f);

        auto inverseOrtho = Matrix4f::inverseOrthographicProjection(
            -halfWidth,
            halfWidth,
            -halfHeight,
            halfHeight,
            -1000.0f,
            1000.0f);

        return inverseCabinet * inverseOrtho;
    }

    Matrix4f computeViewProjectionMatrix(const GameCameraState& cameraState, int screenWidth, int screenHeight)
    {
        auto view = computeView(cameraState.getRoundedPosition());
        auto projection = computeProjection(cameraState.scaleDimension(screenWidth), cameraState.scaleDimension(screenHeight));
        return projection * view;
    }

    Matrix4f computeInverseViewProjectionMatrix(const GameCameraState& cameraState, int screenWidth, int screenHeight)
    {
        auto inverseView = computeInverseView(cameraState.getRoundedPosition());
        auto inverseProjection = computeInverseProjection(cameraState.scaleDimension(screenWidth), cameraState.scaleDimension(screenHeight));
        return inverseView * inverseProjection;
    }

    const Rectangle2f GameScene::minimapViewport = Rectangle2f::fromTopLeft(0.0f, 0.0f, GuiSizeLeft, GuiSizeLeft);

    GameScene::GameScene(
        const SceneContext& sceneContext,
        std::unique_ptr<PlayerCommandService>&& playerCommandService,
        GameMediaDatabase&& meshDatabase,
        const GameCameraState& cameraState,
        SharedTextureHandle unitTextureAtlas,
        std::vector<SharedTextureHandle>&& unitTeamTextureAtlases,
        GameSimulation&& simulation,
        MapTerrainGraphics&& terrainGraphics,
        BuilderGuisDatabase&& builderGuisDatabase,
        std::unique_ptr<GameNetworkService>&& gameNetworkService,
        const std::shared_ptr<Sprite>& minimap,
        const std::shared_ptr<SpriteSeries>& minimapDots,
        const std::shared_ptr<Sprite>& minimapDotHighlight,
        InGameSoundsInfo sounds,
        const std::shared_ptr<SpriteSeries>& guiFont,
        const std::shared_ptr<SpriteSeries>& speechFont,
        const GameParameters& gameParameters,
        PlayerId localPlayerId,
        TdfBlock* audioLookup,
        std::optional<std::ofstream>&& stateLogStream)
        : sceneContext(sceneContext),
          worldViewport(CroppedViewport(this->sceneContext.viewport, GuiSizeLeft, GuiSizeTop, GuiSizeRight, GuiSizeBottom)),
          playerCommandService(std::move(playerCommandService)),
          worldCameraState(cameraState),
          gameMediaDatabase(std::move(meshDatabase)),
          unitTextureAtlas(unitTextureAtlas),
          unitTeamTextureAtlases(std::move(unitTeamTextureAtlases)),
          worldUiRenderService(this->sceneContext.graphics, this->sceneContext.shaders, &this->worldViewport),
          chromeUiRenderService(this->sceneContext.graphics, this->sceneContext.shaders, this->sceneContext.viewport),
          simulation(std::move(simulation)),
          terrainGraphics(std::move(terrainGraphics)),
          builderGuisDatabase(std::move(builderGuisDatabase)),
          gameNetworkService(std::move(gameNetworkService)),
          minimap(minimap),
          minimapDots(minimapDots),
          minimapDotHighlight(minimapDotHighlight),
          minimapRect(minimapViewport.scaleToFit(this->minimap->bounds)),
          sounds(std::move(sounds)),
          guiFont(guiFont),
          speechFont(speechFont),
          localPlayerId(localPlayerId),
          uiFactory(sceneContext.textureService, sceneContext.audioService, audioLookup, sceneContext.vfs, sceneContext.pathMapping, sceneContext.viewport->width(), sceneContext.viewport->height()),
          soundModeSetting(static_cast<SoundMode>(sceneContext.globalConfig->soundMode)),
          unitSpeechSetting(static_cast<UnitSpeechLevel>(sceneContext.globalConfig->unitSpeech)),
          gammaSetting(sceneContext.globalConfig->gamma),
          shadingEnabled(sceneContext.globalConfig->shading),
          antiAliasEnabled(sceneContext.globalConfig->antiAlias),
          shadowsEnabled(sceneContext.globalConfig->shadows),
          scrollSpeedSetting(sceneContext.globalConfig->scrollSpeed),
          gameParameters(gameParameters),
          audioLookup(audioLookup),
          stateLogStream(std::move(stateLogStream))
    {
    }

    GameScene::~GameScene()
    {
        // The audio service outlives us and holds a callback into this
        // object; without handing it back, the mixer reports a finished
        // channel into freed memory the moment the NEXT game renders its
        // first frame. Destroying the handle is not enough -- see
        // Subscription, whose destructor deliberately does nothing.
        audioSub->unsubscribe();
    }

    void GameScene::enableBattleTest(unsigned int unitsPerSide, const std::vector<std::string>& unitTypes, const std::vector<PlayerId>& players, const std::vector<SimVector>& spawns)
    {
        // Every one of the checks below is otherwise a way to end up staring
        // at an empty field with nothing on screen to say why, so each of
        // them says what is wrong while there is still someone to read it.
        // The harness throws rather than carrying on: the caller turns an
        // exception into a message box and a line on stderr.
        if (players.size() < 2 || spawns.size() != players.size())
        {
            throw std::runtime_error("Battle test needs two sides with start positions, but this map and player list gave " + std::to_string(players.size()) + ". Pick a map with at least two start positions.");
        }
        if (unitTypes.empty())
        {
            throw std::runtime_error("Battle test was given no unit type to spawn");
        }

        battleTestUnitsPerSide = static_cast<int>(unitsPerSide);
        battleTestUnitTypes.clear();
        battleTestPlayers = players;
        battleTestSpawns = spawns;
        battleTestSpawnCounter.assign(players.size(), 0u);
        battleTestFootprint.clear();
        battleTestAlive.assign(players.size(), 0);

        for (std::size_t i = 0; i < players.size(); ++i)
        {
            // The definitions are keyed by the FBI's UnitName in upper case,
            // so accept --unit-type armah as readily as ARMAH.
            auto unitType = toUpper(unitTypes.at(i % unitTypes.size()));

            // A name the data does not define -- or one whose model or script
            // failed to load -- takes the game down on the first spawn, deep
            // inside a map::at, with nothing to say which name was wrong.
            auto definitionIt = simulation.unitDefinitions.find(unitType);
            if (definitionIt == simulation.unitDefinitions.end())
            {
                throw std::runtime_error("Battle test: the data defines no unit called '" + unitType + "'. Run with --list-units to see what it does define.");
            }
            if (simulation.unitModelDefinitions.find(definitionIt->second.objectName) == simulation.unitModelDefinitions.end())
            {
                throw std::runtime_error("Battle test: unit '" + unitType + "' names a model, " + definitionIt->second.objectName + ", that did not load");
            }
            if (simulation.unitScriptDefinitions.find(unitType) == simulation.unitScriptDefinitions.end())
            {
                throw std::runtime_error("Battle test: unit '" + unitType + "' has no script loaded");
            }

            // How big the unit is decides how far apart the spawn slots go;
            // runBattleTest works the spacing out per frame, since it also
            // depends on how many the slider is asking for.
            auto [footprintX, footprintZ] = simulation.getFootprintXZ(definitionIt->second.movementCollisionInfo);
            auto footprint = static_cast<int>(std::max(footprintX, footprintZ));
            battleTestFootprint.push_back(footprint);

            battleTestUnitTypes.push_back(unitType);

            LOG_INFO << "Battle test: player " << i << " fields " << unitType
                     << " (" << footprint << " tiles square)"
                     << " from " << simScalarToFloat(spawns[i].x) << ", " << simScalarToFloat(spawns[i].z);
        }

        LOG_INFO << "Battle test: " << battleTestUnitsPerSide << " units a side across " << players.size() << " players";

        // Start the heartbeat's clock here, or the first line reports every
        // millisecond since the process started as one frame.
        battleTestLastLogTime = sceneContext.timeService->getTicks();
        battleTestFramesSinceLog = 0;

        // The whole point is watching the fight, so the map is open from the
        // start rather than lit a unit at a time.
        fogOfWarEnabled = false;
    }

    void GameScene::runBattleTest()
    {
        if (battleTestPlayers.size() < 2 || battleTestSpawns.size() != battleTestPlayers.size())
        {
            return;
        }

        auto wanted = battleTestUnitsPerSide;

        // Every few seconds the harness looks again at anything that has come
        // to a stop, so this is also where the heartbeat lands.
        ++battleTestFramesSinceLog;
        auto now = sceneContext.timeService->getTicks();
        auto sweepDue = now - battleTestLastLogTime >= 5000;

        // Count what each player still has standing, keeping the ids as well
        // as the tally: the slider has to be able to take units away again,
        // not only put them there. The ones with nothing left to do are worth
        // knowing about separately -- see the sweep below.
        battleTestAlive.assign(battleTestPlayers.size(), 0);
        std::vector<std::vector<UnitId>> living(battleTestPlayers.size());
        std::vector<std::vector<UnitId>> idle(battleTestPlayers.size());
        for (const auto& [unitId, unit] : simulation.units)
        {
            if (unit.isDead())
            {
                continue;
            }
            for (std::size_t i = 0; i < battleTestPlayers.size(); ++i)
            {
                if (unit.owner == battleTestPlayers[i])
                {
                    ++battleTestAlive[i];
                    living[i].push_back(unitId);
                    if (sweepDue && unit.orders.empty())
                    {
                        idle[i].push_back(unitId);
                    }
                }
            }
        }

        for (std::size_t i = 0; i < battleTestPlayers.size(); ++i)
        {
            // A few at a time, in either direction: putting two hundred units
            // on the field in one tick stalls the frame and tells you nothing
            // about the fight, and deleting two hundred in one tick floods
            // the event queue for no better reason.
            auto budget = 8;

            // Dragging the slider down takes the newest away first. Those are
            // the ones still standing in the spawn block rather than fighting,
            // so the battle in the middle of the map is left alone. They go
            // quietly -- no wreck, no explosion -- because this is the harness
            // removing them, not the enemy killing them.
            while (battleTestAlive[i] > wanted && budget > 0 && !living[i].empty())
            {
                --budget;
                simulation.quietlyKillUnit(living[i].back());
                living[i].pop_back();
                --battleTestAlive[i];
                ++battleTestCulled;
            }

            const auto& home = battleTestSpawns[i];
            const auto& enemy = battleTestSpawns[(i + 1) % battleTestSpawns.size()];
            const auto& unitType = battleTestUnitTypes.at(i);
            const auto& definition = simulation.unitDefinitions.at(unitType);
            auto movementClass = simulation.getAdHocMovementClass(definition.movementCollisionInfo);
            auto footprint = battleTestFootprint.at(i);

            // Twice as many slots as units asked for, laid out square. The
            // slack matters: a slot blocked by a tree, or by a unit that has
            // not moved off yet, then costs a turn in the queue rather than a
            // place on the field. A block sized exactly to the count can
            // never fill.
            auto slots = std::max(4, wanted * 2);
            auto columns = 1;
            while (columns * columns < slots)
            {
                ++columns;
            }
            auto rows = (slots + columns - 1) / columns;

            const auto& terrain = simulation.terrain;
            auto tile = static_cast<int>(MapTerrain::HeightTileWidthInWorldUnits.value);
            auto mapWidth = static_cast<int>(simScalarToFloat(terrain.rightCutoffInWorldUnits() - terrain.leftInWorldUnits()));
            auto mapDepth = static_cast<int>(simScalarToFloat(terrain.bottomCutoffInWorldUnits() - terrain.topInWorldUnits()));

            // Lanes as wide as the unit itself wherever the map has room for
            // them, which means slots two footprints apart. Anything tighter
            // is a block nothing can leave: a three-tile Swatter cannot walk
            // down a one-tile lane, so the pathfinder finds no route off an
            // interior slot, gives up, and the middle of the block stands
            // there for the length of the run while only its edge feeds the
            // fight. Watched at a hundred a side, that was two thirds of the
            // army.
            //
            // Where the map has not got the room -- five hundred Swatters a
            // side want more ground than Coast To Coast has, twice over --
            // the block closes up rather than hanging off the edge, down to a
            // floor of one tile of clearance. That trades a mobile fight for
            // a full field, which at least is the count that was asked for,
            // and the heartbeat's alive figures say which of the two you got.
            auto pitch = std::min({footprint * 2 * tile, mapWidth / columns, mapDepth / rows});
            pitch = std::max(pitch, (footprint + 1) * tile);

            // Send the stopped ones off again. A unit whose route was blocked
            // when it was asked -- by the crowd it spawned in, or by ground
            // its movement class will not take -- drops the order and stands
            // there for the rest of the run, and a battle test with a growing
            // pool of statues in the corner is measuring the wrong thing. The
            // crowd is different a few seconds later, so asking again is
            // usually enough. Only for units still a long way from the enemy:
            // one that has arrived has honestly finished its order, and
            // re-issuing there would just churn.
            if (sweepDue)
            {
                auto farEnough = intToSimScalar(pitch * 4);
                for (auto unitId : idle[i])
                {
                    auto unit = tryGetUnit(unitId);
                    if (!unit)
                    {
                        continue;
                    }
                    if (unit->get().position.distanceSquared(enemy) < farEnough * farEnough)
                    {
                        continue;
                    }
                    unit->get().addOrder(MoveOrder(enemy));
                    ++battleTestReordered;
                }
            }

            // Keep the block on the map. Start positions sit near an edge as
            // often as not -- Coast To Coast puts one of its two within a
            // third of the map's width of the corner -- and a block centred
            // blindly on such a spawn hangs half of itself over the side,
            // where no unit can ever be placed. At five hundred a side that
            // alone left one player a quarter short for the whole run, every
            // attempt failing on a slot that was never on the map.
            auto halfWidth = intToSimScalar(columns * pitch) / 2_ss;
            auto halfDepth = intToSimScalar(rows * pitch) / 2_ss;
            auto centre = home;
            auto lowX = terrain.leftInWorldUnits() + halfWidth;
            auto highX = terrain.rightCutoffInWorldUnits() - halfWidth;
            centre.x = lowX < highX ? rweMax(lowX, rweMin(highX, home.x)) : (terrain.leftInWorldUnits() + terrain.rightCutoffInWorldUnits()) / 2_ss;
            auto lowZ = terrain.topInWorldUnits() + halfDepth;
            auto highZ = terrain.bottomCutoffInWorldUnits() - halfDepth;
            centre.z = lowZ < highZ ? rweMax(lowZ, rweMin(highZ, home.z)) : (terrain.topInWorldUnits() + terrain.bottomCutoffInWorldUnits()) / 2_ss;

            while (battleTestAlive[i] < wanted && budget > 0)
            {
                --budget;

                // The counter keeps climbing whether or not the last attempt
                // found room, so a blocked cell moves the next one along
                // instead of trying the same spot for ever -- which is what a
                // straight retry does, and it looks exactly like everything
                // spawning in one place.
                auto slot = static_cast<int>(battleTestSpawnCounter[i]++ % static_cast<unsigned int>(columns * rows));
                auto column = (slot % columns) - (columns / 2);
                auto rank = (slot / columns) - (rows / 2);
                auto position = SimVector(
                    centre.x + (intToSimScalar(pitch) * intToSimScalar(column)),
                    centre.y,
                    centre.z + (intToSimScalar(pitch) * intToSimScalar(rank)));
                position.y = simulation.terrain.getHeightAt(position.x, position.z);

                // The occupancy test inside trySpawnUnit never asks whether
                // the unit could move off the spot again. A crater wall is
                // empty ground as far as that is concerned, so slots on one
                // took units quite happily and then kept them: fourteen
                // Swatters a side sat out an entire run on the same rock,
                // too steep to leave, counted all the while towards the
                // hundred the harness thought it had in the fight. Ask the
                // movement class first -- the same question the game asks
                // before it puts a building down.
                auto footprintRect = simulation.computeFootprintRegion(position, definition.movementCollisionInfo);
                if (footprintRect.x < 0 || footprintRect.y < 0 || !simulation.canBeBuiltAt(movementClass, std::nullopt, false, static_cast<unsigned int>(footprintRect.x), static_cast<unsigned int>(footprintRect.y)))
                {
                    ++battleTestSpawnsBlocked;
                    continue;
                }

                // Completed, not a nanoframe: spawnUnit leaves a unit under
                // construction, and an unbuilt Peewee cannot walk, so they
                // simply piled up on the spawn.
                auto unit = spawnCompletedUnit(unitType, battleTestPlayers[i], position);
                if (!unit)
                {
                    ++battleTestSpawnsBlocked;
                    continue;
                }

                unit->get().fireOrders = UnitFireOrders::FireAtWill;
                unit->get().addOrder(MoveOrder(enemy));
                ++battleTestAlive[i];
                ++battleTestSpawned;
            }
        }

        // A heartbeat in the log every few seconds. A run of this thing
        // normally ends under taskkill /F, which lets nothing flush, so the
        // only evidence that survives is what was already on disk --
        // SimpleLogger writes and flushes a line at a time, so this does.
        //
        // On the wall clock rather than a frame count, because the frame rate
        // is the thing being pushed: counting frames went quiet for minutes
        // at a time at five hundred a side, which is exactly the run whose
        // log matters. The frames since the last line are worth having for
        // the same reason -- that ratio is the answer to "does it behave at
        // five hundred".
        if (sweepDue)
        {
            auto elapsed = now - battleTestLastLogTime;
            battleTestLastLogTime = now;
            std::string aliveText;
            for (std::size_t i = 0; i < battleTestAlive.size(); ++i)
            {
                if (i != 0)
                {
                    aliveText += " v ";
                }
                aliveText += std::to_string(battleTestAlive[i]);
            }
            LOG_INFO << "Battle test: wanted " << wanted << " a side, alive " << aliveText
                     << ", spawned " << battleTestSpawned
                     << ", blocked " << battleTestSpawnsBlocked
                     << ", culled " << battleTestCulled
                     << ", re-ordered " << battleTestReordered
                     << ", " << battleTestFramesSinceLog << " frames in " << elapsed << "ms";
            battleTestFramesSinceLog = 0;
        }
    }

    void GameScene::init()
    {
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        currentPanel = uiFactory.panelFromGuiFile(sidePrefix + "MAIN2");

        sceneContext.audioService->reserveChannels(reservedChannelsCount);
        gameNetworkService->start();

        recreateWorldRenderTextures();
    }

    float computeSoundCeiling(int soundCount)
    {
        assert(soundCount > 0);
        if (soundCount <= 4)
        {
            return soundCount;
        }

        if (soundCount <= 8)
        {
            return 4 + ((soundCount - 4) * 0.5f);
        }

        if (soundCount <= 16)
        {
            return (6 + ((soundCount - 8) * 0.25f));
        }

        return 8;
    }

    int computeSoundVolume(int soundCount)
    {
        soundCount = std::max(soundCount, 1);
        auto headRoom = computeSoundCeiling(soundCount);
        return std::clamp(static_cast<int>(headRoom * 128) / soundCount, 1, 128);
    }

#ifdef RWE_ENABLE_RENDERPROF
    namespace
    {
        // Per-phase frame timing, reported every two seconds, in the same
        // shape as the sim's SIMPROF line so the two can be read together.
        // The slots live in render/render_prof.h so RenderService and
        // SceneManager can add their own without a second mechanism; they are
        // zeroed rather than erased so a reference held at a call site stays
        // good.
        std::chrono::steady_clock::time_point renderProfLastReport = std::chrono::steady_clock::now();
        int renderProfFrames = 0;

        void renderProfReport()
        {
            ++renderProfFrames;
            auto now = std::chrono::steady_clock::now();
            auto span = std::chrono::duration<double, std::milli>(now - renderProfLastReport).count();
            if (span < 2000.0)
            {
                return;
            }
            auto frames = renderProfFrames == 0 ? 1 : renderProfFrames;
            std::string line;
            for (auto& [name, total] : renderProfTotals)
            {
                line += " " + name + "=" + std::to_string(static_cast<int>(total / frames * 1000.0)) + "us";
                total = 0.0;
            }
            for (auto& [name, total] : renderProfCounts)
            {
                line += " " + name + "=" + std::to_string(static_cast<int>(total / frames));
                total = 0.0;
            }
            LOG_INFO << "RENDERPROF frames=" << renderProfFrames
                     << " fps=" << static_cast<int>(renderProfFrames * 1000.0 / span)
                     << line;
            renderProfFrames = 0;
            renderProfLastReport = now;
        }
    }
#endif

    void GameScene::render()
    {
        {
            RWE_RENDERPROF("frame");

            if (guiVisible)
            {
                RWE_RENDERPROF("ui");
                renderUi();
            }

            sceneContext.graphics->enableDepthBuffer();

            {
                RWE_RENDERPROF("world");
                renderWorld();
            }
            sceneContext.graphics->disableDepthBuffer();

            if (guiVisible)
            {
                RWE_RENDERPROF("overlay");
                renderOverlay();
            }
        }
#ifdef RWE_ENABLE_RENDERPROF
        renderProfReport();
#endif

        // oh yeah also regulate sound.
        // Never call into the mixer while holding playingUnitChannelsLock:
        // the mixer's audio thread holds its track lock while it reports a
        // finished track, and that report wants our lock in turn. Finished
        // tracks are therefore announced here, on this thread, and the
        // volume pass works from a copy of the set.
        sceneContext.audioService->dispatchFinishedChannels();
        std::vector<int> channels;
        int volume;
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            channels.assign(playingUnitChannels.begin(), playingUnitChannels.end());
            volume = computeSoundVolume(playingUnitChannels.size());
        }
        for (auto channel : channels)
        {
            sceneContext.audioService->setVolume(channel, volume);
        }
    }

    void GameScene::renderUi()
    {
        renderMinimap();

        const auto& localSideData = sceneContext.sideData->at(getPlayer(localPlayerId).side);

        // render top bar
        const auto& intGafName = localSideData.intGaf;
        auto topPanelBackground = sceneContext.textureService->tryGetGafEntry("anims/" + intGafName + ".GAF", "PANELTOP");
        auto bottomPanelBackground = sceneContext.textureService->tryGetGafEntry("anims/" + intGafName + ".GAF", "PANELBOT");
        float topXBuffer = GuiSizeLeft;
        if (topPanelBackground)
        {
            const auto& sprite = *(*topPanelBackground)->sprites.at(0);
            chromeUiRenderService.drawSpriteAbs(topXBuffer, 0, sprite);
            topXBuffer += sprite.bounds.width();
        }
        if (bottomPanelBackground)
        {
            while (topXBuffer < sceneContext.viewport->width())
            {
                const auto& sprite = *(*bottomPanelBackground)->sprites.at(0);
                chromeUiRenderService.drawSpriteAbs(topXBuffer, 0.0f, sprite);
                topXBuffer += sprite.bounds.width();
            }
        }

        auto logos = sceneContext.textureService->tryGetGafEntry("textures/LOGOS.GAF", "32xlogos");
        if (logos)
        {
            auto playerColorIndex = getPlayer(localPlayerId).color;
            const auto& rect = localSideData.logo.toDiscreteRect();
            chromeUiRenderService.drawSpriteAbs(rect.x, rect.y, rect.width, rect.height, *(*logos)->sprites.at(playerColorIndex.value));
        }

        // A stalled resource flashes its bar red, as in TA.
        const bool stallFlashOn = ((sceneContext.timeService->getTicks() / 250) % 2) == 0;
        const Color stallColor(255, 40, 40);

        // draw energy bar
        {
            const auto& rect = localSideData.energyBar.toDiscreteRect();
            const auto& localPlayer = getPlayer(localPlayerId);
            auto rectWidth = localPlayer.maxEnergy == Energy(0) ? 0 : (rect.width * std::max(Energy(0), localPlayer.energy).value) / localPlayer.maxEnergy.value;
            const auto& colorIndex = localSideData.energyColor;
            const auto& color = sceneContext.palette->at(colorIndex);
            if (localPlayer.energyStalled && stallFlashOn)
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rect.width, rect.height, stallColor);
            }
            else
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rectWidth, rect.height, color);
            }
        }
        {
            const auto& rect = localSideData.energy0;
            chromeUiRenderService.drawText(rect.x1, rect.y1, formatResource(Energy(0)), *guiFont);
        }
        {
            const auto& rect = localSideData.energyMax;
            auto text = formatResource(getPlayer(localPlayerId).maxEnergy);
            chromeUiRenderService.drawTextAlignRight(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.energyNum;
            auto text = formatResource(std::max(Energy(0), getPlayer(localPlayerId).energy));
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.energyProduced;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousEnergyProductionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(83, 223, 79));
        }
        {
            const auto& rect = localSideData.energyConsumed;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousDesiredEnergyConsumptionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(255, 71, 0));
        }

        // draw metal bar
        {
            const auto& rect = localSideData.metalBar.toDiscreteRect();
            const auto& localPlayer = getPlayer(localPlayerId);
            auto rectWidth = localPlayer.maxMetal == Metal(0) ? 0 : (rect.width * std::max(Metal(0), localPlayer.metal).value) / localPlayer.maxMetal.value;
            const auto& colorIndex = localSideData.metalColor;
            const auto& color = sceneContext.palette->at(colorIndex);
            if (localPlayer.metalStalled && stallFlashOn)
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rect.width, rect.height, stallColor);
            }
            else
            {
                chromeUiRenderService.fillColor(rect.x, rect.y, rectWidth, rect.height, color);
            }
        }
        {
            const auto& rect = localSideData.metal0;
            chromeUiRenderService.drawText(rect.x1, rect.y1, "0", *guiFont);
        }
        {
            const auto& rect = localSideData.metalMax;
            auto text = formatResource(getPlayer(localPlayerId).maxMetal);
            chromeUiRenderService.drawTextAlignRight(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.metalNum;
            auto text = formatResource(std::max(Metal(0), getPlayer(localPlayerId).metal));
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont);
        }
        {
            const auto& rect = localSideData.metalProduced;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousMetalProductionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(83, 223, 79));
        }
        {
            const auto& rect = localSideData.metalConsumed;
            auto text = formatResourceDelta(getPlayer(localPlayerId).previousDesiredMetalConsumptionBuffer);
            chromeUiRenderService.drawText(rect.x1, rect.y1, text, *guiFont, Color(255, 71, 0));
        }

        renderHelpOverlay();

        renderGameOverOverlay();

        // render bottom bar
        float bottomXBuffer = GuiSizeLeft;
        if (bottomPanelBackground)
        {
            while (bottomXBuffer < sceneContext.viewport->width())
            {
                const auto& sprite = *(*bottomPanelBackground)->sprites.at(0);
                chromeUiRenderService.drawSpriteAbs(bottomXBuffer, worldViewport.bottom(), sprite);
                bottomXBuffer += sprite.bounds.width();
            }
        }

        auto extraBottom = sceneContext.viewport->height() - 480;
        if (hoveredUnit)
        {
            const auto& unit = getUnit(*hoveredUnit);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (logos)
            {
                const auto& rect = localSideData.logo2.toDiscreteRect();
                const auto& color = *(*logos)->sprites.at(getPlayer(unit.owner).color.value);
                chromeUiRenderService.drawSpriteAbs(rect.x, extraBottom + rect.y, rect.width, rect.height, color);
            }

            {
                const auto& rect = localSideData.unitName;
                const auto& playerName = getPlayer(unit.owner).name;
                const auto& text = unitDefinition.showPlayerName && playerName ? *playerName : unitDefinition.unitName;
                chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, text, *guiFont);
            }

            if (unit.isOwnedBy(localPlayerId) || !unitDefinition.hideDamage)
            {
                const auto& rect = localSideData.damageBar.toDiscreteRect();
                chromeUiRenderService.drawHealthBar2(rect.x, extraBottom + rect.y, rect.width, rect.height, static_cast<float>(unit.hitPoints) / static_cast<float>(unitDefinition.maxHitPoints));
            }

            auto stockpileWeapon = simulation.tryGetStockpileWeapon(*hoveredUnit);

            // The four rates, the kills line and the mission line are all
            // behind one ownership test in the original (0x46B119 compares
            // the unit's owner with the local player and jumps past the lot),
            // so an enemy shows you its name and its health and nothing else.
            if (unit.isOwnedBy(localPlayerId))
            {
                // Each rate is max(value, 0) before it is formatted: the
                // fcomp against the zero at 0x4FD568 in front of all four
                // sprintf calls. Metal takes one decimal place and energy
                // none, which is not tidying on RWE's part -- the format
                // strings really are "+%.1f"/"-%.1f" for metal (0x50788C,
                // 0x50787C) and "+%.0f"/"-%.0f" for energy (0x507884,
                // 0x507874).
                {
                    const auto& rect = localSideData.unitMetalMake;
                    auto text = "+" + formatResourceDelta(std::max(Metal(0), unit.getMetalMake()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(83, 223, 79));
                }
                {
                    const auto& rect = localSideData.unitMetalUse;
                    auto text = "-" + formatResourceDelta(std::max(Metal(0), unit.getMetalUse()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(255, 71, 0));
                }
                {
                    const auto& rect = localSideData.unitEnergyMake;
                    auto text = "+" + formatResourceDelta(std::max(Energy(0), unit.getEnergyMake()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(83, 223, 79));
                }
                {
                    const auto& rect = localSideData.unitEnergyUse;
                    auto text = "-" + formatResourceDelta(std::max(Energy(0), unit.getEnergyUse()));
                    chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont, Color(255, 71, 0));
                }

                // Kills, and Veteran from the fifth kill on. There is no
                // rectangle for this in SIDEDATA.TDF: 0x46B2D6 takes the
                // damage bar's own left edge and its bottom plus two, so the
                // line hangs off the bar it belongs to. Colour is interface
                // slot 0x0F, which resolves to white.
                if (auto caption = killsCaption(unit.kills); !caption.empty())
                {
                    const auto& bar = localSideData.damageBar.toDiscreteRect();
                    chromeUiRenderService.drawText(
                        static_cast<float>(bar.x),
                        static_cast<float>(extraBottom + bar.y + bar.height + 2),
                        caption,
                        *guiFont,
                        Color(255, 255, 255));
                }

                {
                    // Not composed here and not composed there either: every
                    // mission record carries its display name at +0x00 and
                    // 0x439DF0 fetches the one belonging to the unit's
                    // current mission.
                    const auto& rect = localSideData.missionText;
                    auto weaponQueued = stockpileWeapon && stockpileWeapon->get().queuedRounds > 0;
                    auto text = missionDisplayName(unitActivity(unit, unit.isBeingBuilt(unitDefinition), weaponQueued));
                    chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, text, *guiFont);
                }
            }

            // The second name-and-bar slot: what this unit's current order is
            // pointed at. 0x46B445 asks 0x439D20 for a weapon build first and
            // falls through to 0x439DD0, the current mission's target unit, so
            // a builder shows what it is building and how far along it is, a
            // guard shows what it is guarding, and a launcher shows the round
            // on the way. Unlike the block above this is not owner-gated --
            // only the weapon branch is (0x46B471) -- though the bar still
            // honours hidedamage.
            {
                auto weaponPercent = 0;
                if (stockpileWeapon && unit.isOwnedBy(localPlayerId))
                {
                    const auto& weapon = stockpileWeapon->get();
                    const auto& weaponDefinition = simulation.weaponDefinitions.at(weapon.weaponType);

                    // ticksPaid * 100 / (reloadtime * 30), the arithmetic of
                    // 0x439D41-0x439D65 exactly: an integer percentage, and
                    // zero means there is nothing to show.
                    auto totalTicks = std::max(1, static_cast<int>(deltaSecondsToTicks(weaponDefinition.reloadTime).value));
                    weaponPercent = std::clamp(weapon.stockpileProgress * 100 / totalTicks, 0, 100);
                }

                if (weaponPercent > 0)
                {
                    {
                        const auto& rect = localSideData.unitName2;
                        chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, "Weapon", *guiFont);
                    }

                    const auto& bar = localSideData.damageBar2.toDiscreteRect();
                    chromeUiRenderService.drawHealthBar2(
                        static_cast<float>(bar.x),
                        static_cast<float>(extraBottom + bar.y),
                        static_cast<float>(bar.width),
                        static_cast<float>(bar.height),
                        static_cast<float>(weaponPercent) / 100.0f);
                }
                else if (auto targetId = unitOrderTargetUnit(unit); targetId)
                {
                    if (auto target = tryGetUnit(*targetId); target && unitIsDetectableByLocalPlayer(*targetId, target->get()))
                    {
                        const auto& targetUnit = target->get();
                        const auto& targetDefinition = simulation.unitDefinitions.at(targetUnit.unitType);

                        {
                            const auto& rect = localSideData.unitName2;
                            chromeUiRenderService.drawTextCenteredX(rect.x1, extraBottom + rect.y1, targetDefinition.unitName, *guiFont);
                        }

                        if (targetUnit.isOwnedBy(localPlayerId) || !targetDefinition.hideDamage)
                        {
                            const auto& bar = localSideData.damageBar2.toDiscreteRect();
                            chromeUiRenderService.drawHealthBar2(
                                static_cast<float>(bar.x),
                                static_cast<float>(extraBottom + bar.y),
                                static_cast<float>(bar.width),
                                static_cast<float>(bar.height),
                                std::clamp(static_cast<float>(targetUnit.hitPoints) / static_cast<float>(targetDefinition.maxHitPoints), 0.0f, 1.0f));
                        }
                    }
                }
            }
        }
        // Resolved rather than dereferenced: the id is picked during update
        // and read again here, so the feature can be reclaimed in between --
        // which used to assert inside getFeature and take the game down.
        else if (auto hoveredFeatureState = tryGetHoveredFeature(simulation, hoveredFeature); hoveredFeatureState)
        {
            const auto& feature = hoveredFeatureState->get();
            const auto& featureDefinition = simulation.getFeatureDefinition(feature.featureName);
            const auto& featureMediaInfo = gameMediaDatabase.getFeature(feature.featureName);

            {
                const auto& rect = localSideData._name;
                auto text = featureMediaInfo.description;
                if (featureDefinition.reclaimable)
                {
                    text += " ";
                    if (featureDefinition.metal > 0)
                    {
                        text += " M:" + formatResource(Metal(featureDefinition.metal));
                    }

                    if (featureDefinition.energy > 0)
                    {
                        text += " E:" + formatResource(Energy(featureDefinition.energy));
                    }
                }
                chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont);
            }
        }
        else if (auto hoveredBuildButtonUnitType = getUnitBuildButtonUnderCursor(); hoveredBuildButtonUnitType)
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(*hoveredBuildButtonUnitType);

            {
                const auto& rect = localSideData._name;
                auto text = unitDefinition.unitName + "  M:" + formatResource(unitDefinition.buildCostMetal) + " E:" + formatResource(unitDefinition.buildCostEnergy);
                chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, text, *guiFont);
            }

            {
                const auto& rect = localSideData.description;
                chromeUiRenderService.drawText(rect.x1, extraBottom + rect.y1, unitDefinition.unitDescription, *guiFont);
            }
        }

        currentPanel->render(chromeUiRenderService);
    }

    void GameScene::renderOverlay()
    {
        // These overlays must render AFTER renderWorld, otherwise the world
        // pass overwrites the center of the screen where they sit.

        // Speed indicator: TA shows "+N" / "-N" relative to default 1.0x speed.
        if (!gameSpeed.isDefault())
        {
            int offset = gameSpeed.displayOffset();
            std::string speedText = (offset > 0 ? "+" : "") + std::to_string(offset);
            float centerX = static_cast<float>(sceneContext.viewport->width()) / 2.0f;
            chromeUiRenderService.drawTextCenteredX(centerX, GuiSizeTop + 8, speedText, *guiFont);
        }

        renderConsole();

        // The menu screens are drawn over the game view -- the original folds
        // its options out across the world rather than tucking them behind
        // it. This has to be the overlay pass: renderUi runs before the world
        // and anything it draws outside the sidebar is painted over.
        for (auto& panel : gameMenuPanels)
        {
            panel->render(chromeUiRenderService);
        }

        if (paused)
        {
            float centerX = static_cast<float>(sceneContext.viewport->width()) / 2.0f;
            float centerY = static_cast<float>(sceneContext.viewport->height()) / 2.0f;
            // TA's own title from anims/IGTITLES.GAF, centred on the screen.
            auto title = gameMediaDatabase.getSpriteSeries("IGTITLES", "igpaused");
            if (title && !(*title)->sprites.empty())
            {
                const auto& sprite = *(*title)->sprites.front();
                chromeUiRenderService.drawSpriteAbs(std::floor(centerX - (sprite.bounds.width() / 2.0f)), std::floor(centerY - (sprite.bounds.height() / 2.0f)), sprite);
            }
            else
            {
                chromeUiRenderService.drawTextCenteredX(centerX, centerY, "PAUSED", *guiFont);
            }
        }
    }

    void GameScene::renderMinimap()
    {
        // draw minimap
        chromeUiRenderService.drawSpriteAbs(minimapRect, *minimap);
        if (fogSprite)
        {
            chromeUiRenderService.drawSpriteAbs(minimapRect, *fogSprite);
        }

        auto cameraInverse = computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        auto worldToMinimap = worldToMinimapMatrix(simulation.terrain, minimapRect);

        // draw minimap dots
        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unitIsDetectableByLocalPlayer(unitId, unit) || unit.carriedBy)
            {
                // Units riding in a transport are inside it: only the
                // transport shows on the minimap.
                continue;
            }
            auto minimapPos = worldToMinimap * simVectorToFloat(unit.position);
            minimapPos.x = std::floor(minimapPos.x);
            minimapPos.y = std::floor(minimapPos.y);
            auto ownerId = unit.owner;
            auto colorIndex = getPlayer(ownerId).color;
            chromeUiRenderService.drawSprite(minimapPos.x, minimapPos.y, *minimapDots->sprites[colorIndex.value]);
        }
        // highlight the minimap dot for the hovered unit
        if (hoveredUnit)
        {
            const auto& unit = getUnit(*hoveredUnit);
            auto minimapPos = worldToMinimap * simVectorToFloat(unit.position);
            minimapPos.x = std::floor(minimapPos.x);
            minimapPos.y = std::floor(minimapPos.y);
            chromeUiRenderService.drawSprite(minimapPos.x, minimapPos.y, *minimapDotHighlight);
        }

        // draw the detection rings of every selected unit
        renderMinimapDetectionRings(worldToMinimap);

        // draw minimap viewport rectangle
        {
            auto transform = worldToMinimap * cameraInverse;
            auto bottomLeft = transform * Vector3f(-1.0f, -1.0f, 0.0f);
            auto topRight = transform * Vector3f(1.0f, 1.0f, 0.0f);

            chromeUiRenderService.drawBoxOutline(
                std::round(bottomLeft.x),
                std::round(topRight.y),
                std::round(topRight.x - bottomLeft.x),
                std::round(bottomLeft.y - topRight.y),
                Color(247, 227, 103));
        }
    }

    namespace
    {
        /**
         * Liang-Barsky. The original's circle routine clips every segment to
         * the surface it is drawing on before it hands it to the Bresenham
         * (0x4C00C3 onward), which is what keeps a ring bigger than the
         * minimap inside the minimap. RWE draws its rings into the whole
         * chrome viewport, so without this a large ring paints over the top
         * bar and the panel.
         */
        bool clipSegmentToRect(Vector2f& a, Vector2f& b, const Rectangle2f& rect)
        {
            auto dx = b.x - a.x;
            auto dy = b.y - a.y;
            float t0 = 0.0f;
            float t1 = 1.0f;

            const float p[4] = {-dx, dx, -dy, dy};
            const float q[4] = {a.x - rect.left(), rect.right() - a.x, a.y - rect.top(), rect.bottom() - a.y};

            for (int i = 0; i < 4; ++i)
            {
                if (p[i] == 0.0f)
                {
                    if (q[i] < 0.0f)
                    {
                        return false;
                    }
                    continue;
                }

                auto t = q[i] / p[i];
                if (p[i] < 0.0f)
                {
                    t0 = std::max(t0, t);
                }
                else
                {
                    t1 = std::min(t1, t);
                }
            }

            if (t0 > t1)
            {
                return false;
            }

            auto ax = a.x;
            auto ay = a.y;
            a = Vector2f(ax + (t0 * dx), ay + (t0 * dy));
            b = Vector2f(ax + (t1 * dx), ay + (t1 * dy));
            return true;
        }

        /**
         * One ring as a list of clipped segments, ready for a single
         * drawLines call. The original's circle is a closed 32-gon of
         * one-pixel Bresenham lines with no thickness and no smoothing
         * (0x4C0070), and its dashed twin (0x4C01A0) draws alternate
         * segments with the starting parity taken from the minimap's blink
         * bit.
         */
        std::vector<Vector2f> buildMinimapRing(const Vector2f& centre, float radius, const Rectangle2f& clip, bool dashed, int parity)
        {
            const int segments = 32;
            std::vector<Vector2f> points;
            points.reserve(segments * 2);

            for (int i = 0; i < segments; ++i)
            {
                if (dashed && (i % 2) != parity)
                {
                    continue;
                }

                auto angleA = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(segments);
                auto angleB = (2.0f * Pif * static_cast<float>(i + 1)) / static_cast<float>(segments);
                Vector2f a(centre.x + (std::cos(angleA) * radius), centre.y + (std::sin(angleA) * radius));
                Vector2f b(centre.x + (std::cos(angleB) * radius), centre.y + (std::sin(angleB) * radius));
                if (clipSegmentToRect(a, b, clip))
                {
                    points.push_back(a);
                    points.push_back(b);
                }
            }

            return points;
        }
    }

    void GameScene::renderMinimapDetectionRings(const Matrix4f& worldToMinimap)
    {
        // 0x466DC0, the minimap render, per unit and gated on the selection
        // bit -- so every selected unit draws its rings, not just one.
        //
        // Four ranges get a ring: RadarDistance, SonarDistance and the two
        // jammer radii. SightDistance does not. It sits one slot away in the
        // definition (+0x202 against +0x204) and the routine steps over it
        // deliberately, which is easy to disbelieve until you read it -- the
        // rings are about what the unit tells you, not about what it can see.
        //
        // An onoffable unit that is switched off draws nothing: turning a
        // radar off takes its ring away with it.
        //
        // The colours are bytes out of the interface colour table at
        // cfg+0xDCB, which S:50 resolved: it is filled from a different base
        // (0x4AC7D0 writes it as cfg+0x519+0x8B2) by nearest-matching
        // GUIPAL.PAL into the screen palette, and the two slots in play here
        // are 0x0A for the detectors and 0x0C for the jammers.
        auto mapWidth = simScalarToFloat(simulation.terrain.rightCutoffInWorldUnits() - simulation.terrain.leftInWorldUnits());
        if (mapWidth <= 0.0f)
        {
            return;
        }
        auto worldUnitsToMinimapPixels = static_cast<float>(minimapRect.width()) / mapWidth;

        for (const auto& unitId : selectedUnits)
        {
            const auto& unit = getUnit(unitId);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

            auto centre = worldToMinimap * simVectorToFloat(unit.position);

            // The anti-missile coverage rings sit outside the on/off gate:
            // 0x466F6A jumps past the four detection rings and lands at
            // 0x46707C, which is where the coverage block starts. So a
            // switched-off radar loses its ring and a switched-off launcher
            // keeps its.
            renderMinimapCoverageRings(unit, unitDefinition, centre, worldUnitsToMinimapPixels);

            if (unitDefinition.onOffable && !unit.activated)
            {
                continue;
            }

            // The colours are the interface map's slots 0x0A and 0x0C,
            // resolved through the runtime GUIPAL nearest-match (0x4AC7D0):
            // detectors draw bright green, jammers orange-red.
            const std::pair<unsigned int, Color> rings[] = {
                {unitDefinition.radarDistance, Color(83, 223, 79)},
                {unitDefinition.sonarDistance, Color(83, 223, 79)},
                {unitDefinition.radarDistanceJam, Color(255, 71, 0)},
                {unitDefinition.sonarDistanceJam, Color(255, 71, 0)},
            };

            for (const auto& [range, color] : rings)
            {
                if (range == 0)
                {
                    continue;
                }

                // The horizontal scale is used for both axes, even though the
                // minimap is not square on every map.
                auto radius = static_cast<float>(range) * worldUnitsToMinimapPixels;
                if (radius < 1.0f)
                {
                    continue;
                }

                chromeUiRenderService.drawLines(
                    buildMinimapRing(Vector2f(centre.x, centre.y), radius, minimapRect, false, 0),
                    color);
            }
        }
    }

    void GameScene::renderMinimapCoverageRings(const UnitState& unit, const UnitDefinition& unitDefinition, const Vector3f& centre, float worldUnitsToMinimapPixels)
    {
        // 0x46707C: one ring per weapon slot whose weapon carries the
        // interceptor flag, radius coverage - 512, in interface colour 0x0F
        // (white). The minus 512 is literal in the binary and unexplained --
        // half a map square shaved off, so the drawn circle understates the
        // guaranteed intercept area rather than overstating it.
        //
        // Where RWE differs, and it is a data question rather than a choice:
        // the original gates the whole block on antiweapons, FBI flags bit 29,
        // which is a pure display flag -- nothing in its simulation reads it.
        // RWE has never parsed it, so the gate here is the interceptor weapon
        // itself. In the shipped data the two are the same set: ARMAMD and
        // CORFMD are the only units with antiweapons=1 and AMD_ROCKET and
        // FMD_ROCKET the only weapons with interceptor=1.
        for (const auto& weapon : unit.weapons)
        {
            if (!weapon)
            {
                continue;
            }

            const auto& weaponDefinition = simulation.weaponDefinitions.at(weapon->weaponType);
            if (!weaponDefinition.interceptor)
            {
                continue;
            }

            auto coverage = simScalarToFloat(weaponDefinition.coverage) - 512.0f;
            if (coverage <= 0.0f)
            {
                continue;
            }

            auto radius = coverage * worldUnitsToMinimapPixels;
            if (radius < 1.0f)
            {
                continue;
            }

            // Dashed while the launcher has a round in the magazine, solid
            // while it is empty: 0x4670D8 tests the magazine byte at
            // weaponSlot+0x0E and picks the dashed circle 0x4C01A0 over the
            // plain one 0x4C0070. Sixteen of the thirty-two segments are
            // drawn, and the parity comes from the minimap's blink bit, so
            // the gaps chase round the ring.
            // Eight frames a half-cycle: the counter at cfg+0x142EF is
            // reloaded with 7 and the bit flipped when it runs out
            // (0x466580), once per pass of the main loop, and RWE's scene
            // clock runs at the same thirty a second.
            const unsigned int minimapBlinkTicks = 8;
            auto dashed = weapon->stockedRounds > 0;
            auto parity = dashed && ((sceneTime.value / minimapBlinkTicks) % 2 == 1) ? 1 : 0;

            chromeUiRenderService.drawLines(
                buildMinimapRing(Vector2f(centre.x, centre.y), radius, minimapRect, dashed, parity),
                Color(255, 255, 255));
        }
    }

    void GameScene::renderBuildBoxes(const UnitState& unit, const Color& outerColor, const Color& innerColor)
    {
        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        for (const auto& order : unit.orders)
        {
            if (const auto buildOrder = std::get_if<BuildOrder>(&order))
            {
                const auto& unitType = buildOrder->unitType;
                const auto& unitDefinition = simulation.unitDefinitions.at(unitType);
                auto mc = simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo);
                auto footprintRect = simulation.computeFootprintRegion(buildOrder->position, unitDefinition.movementCollisionInfo);

                auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(footprintRect.x, footprintRect.y);
                topLeftWorld.y = simulation.terrain.getHeightAt(
                    topLeftWorld.x + ((SimScalar(footprintRect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
                    topLeftWorld.z + ((SimScalar(footprintRect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));

                auto topLeftUi = worldToUi * simVectorToFloat(topLeftWorld);
                auto boxWidth = footprintRect.width * simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
                auto boxHeight = footprintRect.height * simScalarToFloat(MapTerrain::HeightTileHeightInWorldUnits);

                // Two nested one-pixel outlines with the darker line INSIDE.
                // The exact colours came out of the binary at last: the
                // "unwritable" interface colour table is written by 0x4AC7D0
                // addressing it from a different base, a runtime nearest-match
                // of GUIPAL.PAL into the screen palette. A selected owner's
                // queue draws bright green over dark cyan; anyone else's
                // draws bright blue over navy -- the teal tint in the
                // screenshot was cyan, not a green.
                worldUiRenderService.drawBoxOutline(topLeftUi.x, topLeftUi.y, boxWidth, boxHeight, outerColor, 1.0f);
                if (boxWidth > 2.0f && boxHeight > 2.0f)
                {
                    worldUiRenderService.drawBoxOutline(topLeftUi.x + 1.0f, topLeftUi.y + 1.0f, boxWidth - 2.0f, boxHeight - 2.0f, innerColor, 1.0f);
                }
            }
        }
    }

    void GameScene::renderCloakRadius(const UnitState& unit)
    {
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
        if (!unitDefinition.cloakable || !unit.cloaked)
        {
            return;
        }

        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());

        // Projected a point at a time rather than drawn as a screen-space
        // circle, so the ring lies on the ground the way the build boxes do
        // instead of standing up to face the camera.
        auto radius = static_cast<float>(unitDefinition.minCloakDistance);
        auto centre = simVectorToFloat(unit.position);

        constexpr int segments = 48;
        std::vector<Vector2f> points;
        points.reserve(segments);
        for (int i = 0; i < segments; ++i)
        {
            auto angle = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(segments);
            auto worldPoint = Vector3f(
                centre.x + (std::cos(angle) * radius),
                centre.y,
                centre.z + (std::sin(angle) * radius));
            auto uiPoint = worldToUi * worldPoint;
            points.emplace_back(uiPoint.x, uiPoint.y);
        }

        worldUiRenderService.drawLineLoop(points, Color(255, 255, 255));
    }

    namespace
    {
        uint64_t buildBoxKey(const DiscreteRect& rect)
        {
            // Grid coordinates, so two build orders can only collide here if
            // they are for the same square, in which case one sweep is right.
            return (static_cast<uint64_t>(static_cast<uint32_t>(rect.x)) << 32)
                | static_cast<uint64_t>(static_cast<uint32_t>(rect.y));
        }

        /** Ten ticks, a third of a second (0x438C00). */
        const unsigned int BuildBoxSweepTicks = 10;
    }

    void GameScene::updateBuildBoxAppearances()
    {
        // Note when each build order turns up, whatever put it there: the
        // local player's click, an ally's, or the AI's. This runs every frame
        // rather than only while the boxes are being drawn, because the sweep
        // has to start when the building was placed and not when somebody
        // happened to hold shift down.
        std::unordered_map<uint64_t, GameTime> stillThere;
        for (const auto& [_, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            for (const auto& order : unit.orders)
            {
                const auto buildOrder = std::get_if<BuildOrder>(&order);
                if (buildOrder == nullptr)
                {
                    continue;
                }

                const auto& unitDefinition = simulation.unitDefinitions.at(buildOrder->unitType);
                auto key = buildBoxKey(simulation.computeFootprintRegion(buildOrder->position, unitDefinition.movementCollisionInfo));
                auto existing = buildBoxAppearedAt.find(key);
                stillThere[key] = existing == buildBoxAppearedAt.end() ? simulation.gameTime : existing->second;
            }
        }

        // Anything no longer queued is dropped, so the same spot built on
        // twice gets its sweep twice.
        buildBoxAppearedAt = std::move(stillThere);
    }

    void GameScene::renderBuildBoxSweep(const Matrix4f& worldToUi, const DiscreteRect& footprintRect, unsigned int age, bool ownerSelected)
    {
        // 0x438C00. Four full-length lines -- two vertical, two horizontal --
        // sweeping inwards across the footprint: dx = width * t / 10 and
        // dy = height * t / 10, so at t=0 they lie on the box, at t=5 all four
        // meet in the middle, and at t=10 they have crossed and come to rest
        // back on the footprint the other way round. It is linear, there is no
        // overshoot, and it plays once.
        //
        // Each line is drawn twice: colour A overhangs the corners by a pixel,
        // colour B does not, which is where the little nubs come from. The
        // colours are the queued box's own, now exact: guicolours[3]/[10]
        // for a selected owner, [1]/[9] otherwise, through the runtime
        // GUIPAL nearest-match (0x4AC7D0).
        auto colorA = ownerSelected ? Color(0, 128, 128) : Color(0, 0, 128);
        auto colorB = ownerSelected ? Color(83, 223, 79) : Color(84, 84, 252);

        auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(footprintRect.x, footprintRect.y);
        topLeftWorld.y = simulation.terrain.getHeightAt(
            topLeftWorld.x + ((SimScalar(footprintRect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
            topLeftWorld.z + ((SimScalar(footprintRect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));

        auto topLeftUi = worldToUi * simVectorToFloat(topLeftWorld);
        auto width = footprintRect.width * simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
        auto height = footprintRect.height * simScalarToFloat(MapTerrain::HeightTileHeightInWorldUnits);

        auto t = static_cast<float>(std::min(age, BuildBoxSweepTicks)) / static_cast<float>(BuildBoxSweepTicks);
        auto dx = width * t;
        auto dy = height * t;

        const float thickness = 2.0f;
        auto x0 = topLeftUi.x;
        auto y0 = topLeftUi.y;

        for (auto pass = 0; pass < 2; ++pass)
        {
            auto color = pass == 0 ? colorA : colorB;
            auto overhang = pass == 0 ? 1.0f : 0.0f;

            worldUiRenderService.fillColor(x0 + dx, y0 - overhang, thickness, height + (overhang * 2.0f), color);
            worldUiRenderService.fillColor(x0 + width - dx - thickness, y0 - overhang, thickness, height + (overhang * 2.0f), color);
            worldUiRenderService.fillColor(x0 - overhang, y0 + dy, width + (overhang * 2.0f), thickness, color);
            worldUiRenderService.fillColor(x0 - overhang, y0 + height - dy - thickness, width + (overhang * 2.0f), thickness, color);
        }
    }

    void GameScene::renderPlacementSweeps()
    {
        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());

        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            auto ownerSelected = selectedUnits.find(unitId) != selectedUnits.end();

            for (const auto& order : unit.orders)
            {
                const auto buildOrder = std::get_if<BuildOrder>(&order);
                if (buildOrder == nullptr)
                {
                    continue;
                }

                const auto& unitDefinition = simulation.unitDefinitions.at(buildOrder->unitType);
                auto footprintRect = simulation.computeFootprintRegion(buildOrder->position, unitDefinition.movementCollisionInfo);
                auto it = buildBoxAppearedAt.find(buildBoxKey(footprintRect));
                if (it == buildBoxAppearedAt.end())
                {
                    continue;
                }

                auto age = simulation.gameTime.value - it->second.value;
                if (age > BuildBoxSweepTicks)
                {
                    continue;
                }

                renderBuildBoxSweep(worldToUi, footprintRect, age, ownerSelected);
            }
        }
    }

    void GameScene::drawWaypointTrail(const Matrix4f& worldToUi, const SimVector& from, const SimVector& to)
    {
        // The original does not draw a line between waypoints at all. It walks
        // the segment planting a small four-armed star -- `pathicon` in
        // anims/CURSORS.GAF, eleven pixels square, hotspot dead centre, and
        // green with the bright end of the ramp at the tips -- every 48 world
        // units, and slides the whole string of them along by 1.6 world units
        // a tick. 1.6 x 30 is 48, so after a second every star has arrived
        // where its neighbour was and the march is seamless.
        //
        // Where we differ: the original takes the phase from the age of the
        // order being drawn, so two orders queued a few ticks apart march very
        // slightly out of step with one another. RWE's orders do not record
        // when they were issued, and giving them one would mean carrying it
        // through the hash, the dump and the network protocol for an effect
        // nobody can see, so the phase comes off the global clock and every
        // segment marches together.
        const float spacing = 48.0f;
        const float speedPerTick = spacing / 30.0f;

        auto fromF = simVectorToFloat(from);
        auto toF = simVectorToFloat(to);
        auto along = toF - fromF;
        auto length = along.length();
        if (length < 0.001f)
        {
            return;
        }
        auto direction = along / length;

        const auto& frames = sceneContext.cursor->getCursor(CursorType::PathIcon)->sprites;
        if (frames.empty())
        {
            return;
        }
        const auto& icon = *frames.front();

        // The march wraps every 30 ticks, which is what keeps this in step
        // with the simulation rather than with the frame rate.
        auto phase = static_cast<float>(simulation.gameTime.value % 30u) * speedPerTick;

        // The straight line between the two order positions, height included:
        // the trail does not follow the ground, so on a slope it cuts through
        // the hill rather than draping over it. That is the original's
        // behaviour, not an omission.
        for (auto travelled = phase; travelled < length; travelled += spacing)
        {
            auto point = worldToUi * (fromF + (direction * travelled));
            worldUiRenderService.drawSprite(point.x, point.y, icon);
        }
    }

    void GameScene::renderUnitOrders(UnitId unitId, bool drawLines)
    {
        const auto& unit = getUnit(unitId);
        auto pos = unit.position;
        auto worldToUi = worldUiRenderService.getInverseViewProjectionMatrix()
            * computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        for (const auto& order : unit.orders)
        {
            auto nextPos = match(
                order,
                [&](const BuildOrder& o) { return o.position; },
                [&](const MoveOrder& o) { return o.destination; },
                [&](const AttackOrder& o) { return match(
                                                o.target,
                                                [&](const SimVector& v) { return v; },
                                                [&](const UnitId& u) {
                                                    auto unitOption = tryGetUnit(u);
                                                    if (!unitOption)
                                                    {
                                                        return pos;
                                                    }
                                                    return unitOption->get().position;
                                                }); },
                [&](const BuggerOffOrder&) { return pos; },
                [&](const CompleteBuildOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const GuardOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const RepairOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const PatrolOrder& o) { return o.destination; },
                [&](const CaptureOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const LoadOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const UnloadOrder& o) { return o.destination; },
                [&](const LandOnAirBaseOrder& o) {
                    auto unitOption = tryGetUnit(o.target);
                    if (!unitOption)
                    {
                        return pos;
                    }
                    return unitOption->get().position;
                },
                [&](const DgunOrder& o) {
                    return match(
                        o.target,
                        [&](const UnitId& u) {
                            auto unitOption = tryGetUnit(u);
                            if (!unitOption)
                            {
                                return pos;
                            }
                            return unitOption->get().position;
                        },
                        [&](const SimVector& v) { return v; });
                },
                [&](const ResurrectOrder& o) {
                    const auto& f = simulation.tryGetFeature(o.target);
                    if (!f)
                    {
                        return pos;
                    }
                    return f->get().position;
                },
                [&](const ReclaimOrder& o) {
                    return match(
                        o.target,
                        [&](const UnitId& u) {
                            auto unitOption = tryGetUnit(u);
                            if (!unitOption)
                            {
                                return pos;
                            }
                            return unitOption->get().position;
                        },
                        [&](const FeatureId& f) {
                            auto featureOption = simulation.tryGetFeature(f);
                            if (!featureOption)
                            {
                                return pos;
                            }
                            return featureOption->get().position;
                        });
                });

            auto waypointIcon = match(
                order,
                [&](const BuildOrder&) { return std::optional<CursorType>(); },
                [&](const MoveOrder&) { return std::optional<CursorType>(CursorType::Move); },
                [&](const AttackOrder&) { return std::optional<CursorType>(CursorType::Attack); },
                [&](const BuggerOffOrder&) { return std::optional<CursorType>(); },
                [&](const CompleteBuildOrder&) { return std::optional<CursorType>(CursorType::Repair); },
                [&](const GuardOrder&) { return std::optional<CursorType>(CursorType::Guard); },
                [&](const ReclaimOrder&) { return std::optional<CursorType>(CursorType::Reclaim); },
                [&](const ResurrectOrder&) { return std::optional<CursorType>(CursorType::Reclaim); },
                [&](const RepairOrder&) { return std::optional<CursorType>(CursorType::Repair); },
                [&](const PatrolOrder&) { return std::optional<CursorType>(CursorType::Patrol); },
                [&](const CaptureOrder&) { return std::optional<CursorType>(CursorType::Capture); },
                [&](const LoadOrder&) { return std::optional<CursorType>(CursorType::Load); },
                [&](const UnloadOrder&) { return std::optional<CursorType>(CursorType::Unload); },
                // The original has no cursor of its own for the D-gun; the
                // attack cursor is what CURSORS.GAF offers and what it uses.
                [&](const DgunOrder&) { return std::optional<CursorType>(CursorType::Attack); },
                // The original has a landing cursor of its own -- 0x43EA83
                // gives cursor 13 for an aircraft over an air base -- but RWE
                // has no sprite loaded for it, so the move cursor stands in.
                [&](const LandOnAirBaseOrder&) { return std::optional<CursorType>(CursorType::Move); });

            // draw waypoint icons
            if (waypointIcon)
            {
                auto timeInMillis = sceneContext.timeService->getTicks();
                unsigned int frameRateInSeconds = 2;
                unsigned int millisPerFrame = 1000 / frameRateInSeconds;

                const auto& frames = sceneContext.cursor->getCursor(*waypointIcon)->sprites;
                auto frameIndex = (timeInMillis / millisPerFrame) % frames.size();

                auto uiDest = worldToUi * simVectorToFloat(nextPos);
                worldUiRenderService.drawSprite(uiDest.x, uiDest.y, *(frames[frameIndex]), Color(255, 255, 255, 100));
            }

            if (drawLines)
            {
                auto drawLine = match(
                    order,
                    [&](const BuildOrder&) { return true; },
                    [&](const MoveOrder&) { return true; },
                    [&](const AttackOrder&) { return false; },
                    [&](const BuggerOffOrder&) { return false; },
                    [&](const CompleteBuildOrder&) { return true; },
                    [&](const GuardOrder&) { return true; },
                    [&](const ReclaimOrder&) { return true; },
                    [&](const ResurrectOrder&) { return true; },
                    [&](const RepairOrder&) { return true; },
                    [&](const PatrolOrder&) { return true; },
                    [&](const CaptureOrder&) { return true; },
                    [&](const LoadOrder&) { return true; },
                    [&](const UnloadOrder&) { return true; },
                    // No line, for the same reason an attack draws none.
                    [&](const DgunOrder&) { return false; },
                    [&](const LandOnAirBaseOrder&) { return true; });

                if (drawLine)
                {
                    drawWaypointTrail(worldToUi, pos, nextPos);
                }
            }

            pos = nextPos;
        }
    }

    void GameScene::renderWorld()
    {
        // The window can be resized (or dragged between monitors of
        // different scale) at any moment; the world is drawn through an
        // offscreen buffer, which has to follow the viewport or the view
        // ends up squeezed into a corner of the old size.
        if (worldRenderTextureSize != std::pair<unsigned int, unsigned int>{worldViewport.width(), worldViewport.height()})
        {
            recreateWorldRenderTextures();
        }

        {
            RWE_RENDERPROF("w.fog");
            updateFogSprite();
        }

        sceneContext.graphics->bindFrameBuffer(worldFrameBuffer.frameBuffer.get());
        sceneContext.graphics->setViewport(
            0,
            0,
            worldViewport.width() * worldRenderTextureScale,
            worldViewport.height() * worldRenderTextureScale);

        sceneContext.graphics->clear();

        const auto& viewProjectionMatrix = computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        RenderService worldRenderService(sceneContext.graphics, sceneContext.shaders, &viewProjectionMatrix);
        worldRenderService.setShadingEnabled(shadingEnabled);

        sceneContext.graphics->disableDepthBuffer();

        // Fog of war is applied by the terrain shader: remembered ground goes
        // grey, unknown ground black, along the ragged boundary that TA's own
        // fog tiles have been rasterised into fogOverlayTexture. That texture
        // covers a window around the camera, not the whole map. With the fog
        // switched off it is rasterised from a fully revealed grid and comes
        // out empty, so the same path draws the plain terrain.
        std::optional<FogOverlay> fogOverlay;
        if (fogOverlayTexture.isValid())
        {
            fogOverlay = FogOverlay{fogOverlayTexture.get(), fogOverlayBounds.left(), fogOverlayBounds.top(), fogOverlayBounds.width(), fogOverlayBounds.height()};
        }
        {
            RWE_RENDERPROF("w.terrain");
            worldRenderService.drawMapTerrain(terrainGraphics, worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), fogOverlay);
        }

        SpriteBatch flatFeatureBatch;
        SpriteBatch flatFeatureShadowBatch;
        {
            RWE_RENDERPROF("w.flatfeat.build");
            for (const auto& f : simulation.features)
            {
                if (!positionIsExploredByLocalPlayer(f.second.position))
                {
                    continue;
                }
                const auto& featureDefinition = simulation.getFeatureDefinition(f.second.featureName);
                if (!featureDefinition.isStanding())
                {
                    auto fogged = !positionIsVisibleToLocalPlayer(f.second.position);
                    drawFeature(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, simulation.gameTime, fogged, flatFeatureBatch);
                    drawFeatureShadow(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, fogged, flatFeatureShadowBatch);
                }
            }
        }
        {
            RWE_RENDERPROF("w.flatfeat.draw");
            RWE_RENDERPROF_COUNT("n.flatfeat", flatFeatureBatch.sprites.size() + flatFeatureShadowBatch.sprites.size());
            worldRenderService.drawSpriteBatch(flatFeatureShadowBatch);
            worldRenderService.drawSpriteBatch(flatFeatureBatch);
        }

        {
            RWE_RENDERPROF("w.wake");
            wakeBatch.lines.clear();
            wakeBatch.triangles.clear();
            for (const auto& particle : particles)
            {
                drawWakeParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, wakeBatch);
            }
            RWE_RENDERPROF_COUNT("n.particles", particles.size());
            RWE_RENDERPROF_COUNT("n.waketri", wakeBatch.triangles.size());
            worldRenderService.drawBatch(wakeBatch, viewProjectionMatrix);
        }

        ColoredMeshBatch terrainOverlayBatch;

        if (occupiedGridVisible)
        {
            drawOccupiedGrid(worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), simulation.terrain, simulation.occupiedGrid, terrainOverlayBatch);
        }
        if (pathfindingVisualisationVisible)
        {
            drawPathfindingVisualisation(simulation.terrain, simulation.pathFindingService.lastPathDebugInfo, terrainOverlayBatch);
        }

        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && movementClassGridVisible)
        {
            const auto& unit = simulation.getUnitState(*selectedUnit);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            match(
                unitDefinition.movementCollisionInfo,
                [&](const UnitDefinition::NamedMovementClass& c) {
                    const auto& grid = simulation.movementClassCollisionService.getGrid(c.movementClassId);
                    drawMovementClassCollisionGrid(simulation.terrain, grid, worldCameraState.getRoundedPosition(), worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()), terrainOverlayBatch);
                },
                [&](const auto&) {

                });
        }

        {
            RWE_RENDERPROF("w.terrainoverlay");
            worldRenderService.drawBatch(terrainOverlayBatch, viewProjectionMatrix);
        }

        auto interpolationFraction = static_cast<float>(millisecondsBuffer) / static_cast<float>(SimMillisecondsPerTick);
        ColoredMeshesBatch selectionRectBatch;
        {
            RWE_RENDERPROF("w.selection");
            for (const auto& selectedUnitId : selectedUnits)
            {
                const auto& unit = getUnit(selectedUnitId);
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                drawSelectionRect(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, interpolationFraction, selectionRectBatch);
            }
            worldRenderService.drawLineLoopsBatch(selectionRectBatch);
        }

        auto seaLevel = simulation.terrain.getSeaLevel();

        // What is off screen costs nothing from here on. Working out the
        // transform of every piece of every unit on the map, and handing the
        // driver a draw call for each, was the largest cost in the frame at
        // eight hundred units, and a screenful is a fraction of them.
        auto viewCull = makeViewCullTest(viewProjectionMatrix);

        UnitShadowMeshBatch unitShadowMeshBatch;
        {
            RWE_RENDERPROF("w.shadow.build");
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unitId, unit))
                {
                    continue;
                }
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                if (!shadowsEnabled || !unitCastsShadow(unitDefinition))
                {
                    continue;
                }
                const auto& modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);

                auto groundHeight = simulation.terrain.getHeightAt(unit.position.x, unit.position.z);
                if (unitDefinition.floater || unitDefinition.canHover)
                {
                    groundHeight = rweMax(groundHeight, seaLevel);
                }

                // The cull goes on where the shadow lands rather than on where
                // the unit is: an aircraft's shadow sits on the ground under it,
                // which can be well inside the view while the aircraft itself is
                // above the top of it.
                auto shadowPosition = Vector3f(simScalarToFloat(unit.position.x), simScalarToFloat(groundHeight), simScalarToFloat(unit.position.z));
                if (!viewCull.couldBeVisible(shadowPosition, ViewCullModelRadius))
                {
                    continue;
                }

                drawUnitShadow(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, modelDefinition, interpolationFraction, simScalarToFloat(groundHeight), unitTextureAtlas.get(), unitTeamTextureAtlases, unitShadowMeshBatch);

                if (unit.isBeingBuilt(unitDefinition))
                {
                    // The frame is see-through while it is built, so the shadow
                    // would show through it. Keep only the part cast outside the
                    // model's own outline.
                    drawUnitSilhouette(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, modelDefinition, interpolationFraction, unitTextureAtlas.get(), unitTeamTextureAtlases, unitShadowMeshBatch.cutouts);
                }
            }
            for (const auto& [_, feature] : simulation.features)
            {
                const auto& position = feature.position;
                if (!positionIsExploredByLocalPlayer(position))
                {
                    continue;
                }
                auto groundHeight = simulation.terrain.getHeightAt(position.x, position.z);
                if (position.y >= seaLevel && groundHeight < seaLevel)
                {
                    groundHeight = seaLevel;
                }

                auto shadowPosition = Vector3f(simScalarToFloat(position.x), simScalarToFloat(groundHeight), simScalarToFloat(position.z));
                if (!viewCull.couldBeVisible(shadowPosition, ViewCullModelRadius))
                {
                    continue;
                }

                drawFeatureMeshShadow(simulation.unitModelDefinitions, gameMediaDatabase, viewProjectionMatrix, feature, simScalarToFloat(groundHeight), unitTextureAtlas.get(), unitTeamTextureAtlases, unitShadowMeshBatch);
            }
        }
        {
            RWE_RENDERPROF("w.shadow.draw");
            RWE_RENDERPROF_COUNT("n.shadowmesh", unitShadowMeshBatch.meshes.size() + unitShadowMeshBatch.cutouts.size());
            worldRenderService.drawUnitShadowMeshBatch(unitShadowMeshBatch);
        }

        sceneContext.graphics->enableDepthBuffer();

        UnitMeshBatch unitMeshBatch;
        {
            RWE_RENDERPROF("w.unit.build");
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unitId, unit))
                {
                    continue;
                }
                if (!viewCull.couldBeVisible(simVectorToFloat(unit.position), ViewCullModelRadius))
                {
                    continue;
                }
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                const auto& unitModelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);
                drawUnit(gameMediaDatabase, viewProjectionMatrix, unit, unitDefinition, unitModelDefinition, getPlayer(unit.owner).color, unitId.value, simulation.gameTime.value, interpolationFraction, unitTextureAtlas.get(), unitTeamTextureAtlases, unitMeshBatch);
            }
            for (const auto& [_, feature] : simulation.features)
            {
                if (!positionIsExploredByLocalPlayer(feature.position))
                {
                    continue;
                }
                if (!viewCull.couldBeVisible(simVectorToFloat(feature.position), ViewCullModelRadius))
                {
                    continue;
                }
                drawMeshFeature(simulation.unitModelDefinitions, gameMediaDatabase, viewProjectionMatrix, feature, unitTextureAtlas.get(), unitTeamTextureAtlases, unitMeshBatch);
            }
            for (const auto& d : debris)
            {
                if (d.shard)
                {
                    continue;
                }
                auto position = d.position + (d.velocity * interpolationFraction);
                auto rotation = d.rotation + (d.angularVelocity * interpolationFraction);
                auto matrix = Matrix4f::translation(position) * Matrix4f::rotationZXY(rotation);
                drawDebrisPiece(gameMediaDatabase, viewProjectionMatrix, d.objectName, d.pieceName, matrix, d.color, unitTextureAtlas.get(), unitTeamTextureAtlases, unitMeshBatch);
            }
        }
        {
            RWE_RENDERPROF("w.unit.draw");
            RWE_RENDERPROF_COUNT("n.unitmesh", unitMeshBatch.meshes.size() + unitMeshBatch.buildingMeshes.size() + unitMeshBatch.cloakedMeshes.size());
            worldRenderService.drawUnitMeshBatch(unitMeshBatch, simScalarToFloat(seaLevel));
        }

        // Construction wireframe: the visible polygon edges of each nanoframe,
        // drawn with the depth test on so the model hides its own back. The
        // original outlines every primitive of every piece in its second build
        // colour, a triangle wave down palette entries 160..175 and back that
        // comes round about every half second, offset per unit.
        {
            RWE_RENDERPROF("w.wireframe");
            // The direction from the scene towards the camera, in world space.
            auto inverseViewProjection = computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
            auto toCamera = ((inverseViewProjection * Vector3f(0.0f, 0.0f, -1.0f)) - (inverseViewProjection * Vector3f(0.0f, 0.0f, 0.0f))).normalized();

            ColoredMeshBatch wireframeBatch;
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unitIsVisibleToLocalPlayer(unitId, unit))
                {
                    continue;
                }
                if (!viewCull.couldBeVisible(simVectorToFloat(unit.position), ViewCullModelRadius))
                {
                    continue;
                }
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                if (!unit.isBeingBuilt(unitDefinition))
                {
                    continue;
                }
                const auto& modelDefinition = simulation.unitModelDefinitions.at(unitDefinition.objectName);
                auto wireframeColor = buildCycleColorB(unitId.value, simulation.gameTime.value);
                drawUnitWireframe(gameMediaDatabase, unit, unitDefinition, modelDefinition, interpolationFraction, toCamera, wireframeColor, wireframeBatch);
            }
            // Lines cannot go below one pixel, so a lighter blend reads as a finer wire.
            worldRenderService.drawBatch(wireframeBatch, viewProjectionMatrix, 0.65f);
        }

        ColoredMeshBatch lineProjectilesBatch;
        SpriteBatch spriteProjectilesBatch;
        UnitMeshBatch meshProjectilesBatch;
        {
            RWE_RENDERPROF("w.projectiles");
            drawProjectiles(simulation, localPlayerVisibility(), gameMediaDatabase, viewProjectionMatrix, simulation.projectiles, simulation.gameTime, interpolationFraction, unitTextureAtlas.get(), unitTeamTextureAtlases, lineProjectilesBatch, spriteProjectilesBatch, meshProjectilesBatch);
            worldRenderService.drawBatch(lineProjectilesBatch, viewProjectionMatrix);
            worldRenderService.drawUnitMeshBatch(meshProjectilesBatch, simScalarToFloat(seaLevel));
            worldRenderService.drawSpriteBatch(spriteProjectilesBatch);
        }

        sceneContext.graphics->disableDepthWrites();

        SpriteBatch featureBatch;
        SpriteBatch featureShadowBatch;
        {
            RWE_RENDERPROF("w.feature.build");
            for (const auto& f : simulation.features)
            {
                if (!positionIsExploredByLocalPlayer(f.second.position))
                {
                    continue;
                }
                const auto& featureDefinition = simulation.getFeatureDefinition(f.second.featureName);
                if (featureDefinition.isStanding())
                {
                    auto fogged = !positionIsVisibleToLocalPlayer(f.second.position);
                    drawFeature(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, simulation.gameTime, fogged, featureBatch);
                    drawFeatureShadow(gameMediaDatabase, f.second, featureDefinition, viewProjectionMatrix, fogged, featureShadowBatch);
                }
            }
        }
        {
            RWE_RENDERPROF("w.feature.draw");
            RWE_RENDERPROF_COUNT("n.featuresprite", featureBatch.sprites.size() + featureShadowBatch.sprites.size());
            worldRenderService.drawSpriteBatch(featureShadowBatch);
            worldRenderService.drawSpriteBatch(featureBatch);
        }

        // Particles that belong in the world rather than over it: drawn here,
        // while the depth test is still on, so what is in front of them hides
        // them. An aircraft's exhaust comes out from under the hull, and the
        // hull should cover it.
        {
            RWE_RENDERPROF("w.particles");
            SpriteBatch worldSpriteParticlesBatch;
            for (const auto& particle : particles)
            {
                if (!particleDrawsInWorld(particle))
                {
                    continue;
                }
                drawSpriteParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, worldSpriteParticlesBatch);
            }
            worldRenderService.drawSpriteBatch(worldSpriteParticlesBatch);
        }

        // Nano spray keeps depth testing (with writes still off) so the unit
        // doing the lathing occludes the part of the stream behind it. Drawn
        // without it, a construction aircraft hovering over its work has the
        // spray painted across the top of the fuselage.
        ColoredMeshBatch nanoParticlesBatch;
        {
            RWE_RENDERPROF("w.nano");
            for (const auto& particle : particles)
            {
                drawNanoParticle(simulation.gameTime, interpolationFraction, particle, nanoParticlesBatch);
            }
            for (const auto& d : debris)
            {
                if (d.shard)
                {
                    drawDebrisShard(d.position + (d.velocity * interpolationFraction), nanoParticlesBatch);
                }
            }
            worldRenderService.drawBatch(nanoParticlesBatch, viewProjectionMatrix);
        }

        sceneContext.graphics->disableDepthTest();

        {
            RWE_RENDERPROF("w.flashes");
            sceneContext.graphics->bindFrameBufferColorBuffer(dodgeMask.get());
            sceneContext.graphics->clearColor();
            worldRenderService.drawFlashes(simulation.gameTime, flashes);
            sceneContext.graphics->bindFrameBufferColorBuffer(worldFrameBuffer.texture.get());
        }

        sceneContext.graphics->unbindFrameBuffer();
        auto viewportPos = worldViewport.toOtherViewport(*sceneContext.viewport, 0, worldViewport.height());
        sceneContext.graphics->setViewport(
            viewportPos.x,
            sceneContext.viewport->height() - viewportPos.y,
            worldViewport.width(),
            worldViewport.height());

        sceneContext.graphics->disableDepthBuffer();
        {
            RWE_RENDERPROF("w.post");
            auto quadMesh = sceneContext.graphics->createUnitTexturedQuadFlipped(Rectangle2f::fromTLBR(1.0f, 0.0f, 0.0f, 1.0f));
            sceneContext.graphics->bindShader(sceneContext.shaders->worldPost.handle.get());
            sceneContext.graphics->setUniformInt(sceneContext.shaders->worldPost.dodgeMask, 1);
            sceneContext.graphics->setUniformFloat(sceneContext.shaders->worldPost.gamma, static_cast<float>(gammaSetting) / 100.0f);
            sceneContext.graphics->bindTexture(worldFrameBuffer.texture.get());
            sceneContext.graphics->setActiveTextureSlot1();
            sceneContext.graphics->bindTexture(dodgeMask.get());
            sceneContext.graphics->setActiveTextureSlot0();
            sceneContext.graphics->drawTriangles(quadMesh);

            SpriteBatch spriteParticlesBatch;
            for (const auto& particle : particles)
            {
                if (particleDrawsInWorld(particle))
                {
                    continue;
                }
                drawSpriteParticle(gameMediaDatabase, simulation.gameTime, viewProjectionMatrix, particle, spriteParticlesBatch);
            }
            worldRenderService.drawSpriteBatch(spriteParticlesBatch);
        }
        sceneContext.graphics->enableDepthTest();

        sceneContext.graphics->enableDepthWrites();

        // The sweep over a freshly placed building plays whether or not shift
        // is held: it is the acknowledgement of the click, and the original
        // shows it as soon as the order exists.
        RWE_RENDERPROF("w.worldui");
        renderPlacementSweeps();

        // in-world UI/overlay rendering
        if (isShiftDown())
        {
            auto singleSelectedUnit = getSingleSelectedUnit();

            // if unit is a builder, show all other buildings being built
            if (shouldShowAllBuildBoxes(simulation, localPlayerId, singleSelectedUnit, hoveredUnit))
            {
                for (const auto& [_, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(localPlayerId))
                    {
                        renderBuildBoxes(unit, Color(84, 84, 252), Color(0, 0, 128));
                    }
                }
            }

            // draw orders + lines for hovered unit
            if (hoveredUnit && getUnit(*hoveredUnit).isOwnedBy(localPlayerId))
            {
                renderUnitOrders(*hoveredUnit, true);

                // v3.1 feature 4: the cloaked unit under the cursor shows the
                // radius inside which an enemy will strip its cloak.
                renderCloakRadius(getUnit(*hoveredUnit));
            }

            // draw orders for all selected units
            for (const auto& selectedUnitId : selectedUnits)
            {
                renderBuildBoxes(getUnit(selectedUnitId), Color(83, 223, 79), Color(0, 128, 128));

                if (selectedUnitId != hoveredUnit)
                {
                    // draw lines if only one unit is selected--hovered unit is drawn aleady
                    renderUnitOrders(selectedUnitId, singleSelectedUnit == selectedUnitId);
                }
            }
        }

        if (healthBarsVisible)
        {
            for (const auto& [_, unit] : simulation.units)
            {
                if (!unit.isOwnedBy(localPlayerId) || unit.carriedBy)
                {
                    // only draw healthbars on units we own, and not on cargo
                    continue;
                }

                if (unit.hitPoints == 0)
                {
                    // Do not show health bar when the unit has zero health.
                    // This can happen when the unit is still a freshly created nanoframe.
                    continue;
                }

                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);

                auto uiPos = worldUiRenderService.getInverseViewProjectionMatrix()
                    * viewProjectionMatrix
                    * simVectorToFloat(unit.position);
                worldUiRenderService.drawHealthBar(uiPos.x, uiPos.y, static_cast<float>(unit.hitPoints) / static_cast<float>(unitDefinition.maxHitPoints));
            }
        }

        // Radar contacts get nothing here. The original's world render never
        // walks the unit list at all: it consumes a list rebuilt each frame by
        // 0x48BAE0, which admits a unit only if it is the viewer's own or
        // passes the can-see predicate 0x465AC0 -- and that predicate does not
        // look at the radar bits. A contact you only have on radar is a dot on
        // the minimap and nothing whatever in the main view, which is why it
        // cannot be clicked there either.

        // Self-destruct countdowns: seconds remaining, drawn above the unit.
        for (const auto& [_, unit] : simulation.units)
        {
            if (!unit.selfDestructTime || !unit.isAlive())
            {
                continue;
            }

            auto ticksLeft = *unit.selfDestructTime > simulation.gameTime
                ? (*unit.selfDestructTime - simulation.gameTime).value
                : 0u;
            auto secondsLeft = (ticksLeft + SimTicksPerSecond - 1) / SimTicksPerSecond;

            auto uiPos = worldUiRenderService.getInverseViewProjectionMatrix()
                * viewProjectionMatrix
                * simVectorToFloat(unit.position);
            // A red-framed badge so the countdown reads at a glance.
            const float badgeWidth = 22.0f;
            const float badgeHeight = 16.0f;
            auto badgeX = uiPos.x - (badgeWidth / 2.0f);
            auto badgeY = uiPos.y - 34.0f;
            worldUiRenderService.fillColor(badgeX, badgeY, badgeWidth, badgeHeight, Color(0, 0, 0, 200));
            worldUiRenderService.drawBoxOutline(badgeX, badgeY, badgeWidth, badgeHeight, Color(255, 40, 40), 2.0f);
            worldUiRenderService.drawTextCentered(uiPos.x, badgeY + (badgeHeight / 2.0f), std::to_string(secondsLeft), *guiFont);
        }

        // Draw build box outline when a unit is selected to be built.
        // The original's cursor box is two nested one-pixel rectangles in
        // ONE colour -- guicolours[10]/[4], which the runtime nearest-match
        // mapping (0x4AC7D0, GUIPAL.PAL into the screen palette) lands on
        // palette 233 bright green and 213 red.
        if (hoverBuildInfo)
        {
            Color color = hoverBuildInfo->isValid ? Color(83, 223, 79) : Color(171, 23, 0);

            auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(hoverBuildInfo->rect.x, hoverBuildInfo->rect.y);

            // The same height the building will stand at, which in the
            // original is the same number rather than merely the same rule:
            // the box reads what the legality test cached, or calls the
            // height routine itself. Taking the terrain under the centre
            // instead drew the box on the sea floor while the building went
            // to the surface, which is the gap a play-test saw under a tidal
            // generator.
            if (auto buildCursor = std::get_if<BuildCursorMode>(&cursorMode.getValue()); buildCursor != nullptr)
            {
                const auto& buildingDefinition = simulation.unitDefinitions.at(buildCursor->unitType);
                topLeftWorld.y = simulation.computeBuildHeight(buildingDefinition, hoverBuildInfo->rect);
            }
            else
            {
                topLeftWorld.y = simulation.terrain.getHeightAt(
                    topLeftWorld.x + ((SimScalar(hoverBuildInfo->rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss),
                    topLeftWorld.z + ((SimScalar(hoverBuildInfo->rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss));
            }

            auto topLeftUi = worldUiRenderService.getInverseViewProjectionMatrix()
                * viewProjectionMatrix
                * simVectorToFloat(topLeftWorld);
            worldUiRenderService.drawBoxOutline(
                topLeftUi.x,
                topLeftUi.y,
                hoverBuildInfo->rect.width * simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits),
                hoverBuildInfo->rect.height * simScalarToFloat(MapTerrain::HeightTileHeightInWorldUnits),
                color,
                2.0f);
        }

        // Draw bandbox selection rectangle
        if (auto normalCursorMode = std::get_if<NormalCursorMode>(&cursorMode.getValue()))
        {
            if (auto selectingState = std::get_if<NormalCursorMode::SelectingState>(&normalCursorMode->state))
            {
                const auto& start = selectingState->startPosition;
                const auto cameraPosition = worldCameraState.getRoundedPosition();
                Point cameraRelativeStart(start.x - cameraPosition.x, start.y - cameraPosition.z);

                auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, getMousePosition());
                auto rect = DiscreteRect::fromPoints(cameraRelativeStart, worldViewportPos);

                worldUiRenderService.drawBoxOutline(rect.x, rect.y, rect.width, rect.height, Color(255, 255, 255));
                if (rect.width > 2 && rect.height > 2)
                {
                    worldUiRenderService.drawBoxOutline(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, Color(0, 0, 0));
                }
            }
        }

        if (cursorTerrainDotVisible)
        {
            // draw a dot where we think the cursor intersects terrain
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            auto intersect = simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
            if (intersect)
            {
                auto cursorTerrainPos = worldUiRenderService.getInverseViewProjectionMatrix()
                    * viewProjectionMatrix
                    * simVectorToFloat(*intersect);
                worldUiRenderService.fillColor(cursorTerrainPos.x - 2, cursorTerrainPos.y - 2, 4, 4, Color(0, 0, 255));

                intersect->y = simulation.terrain.getHeightAt(intersect->x, intersect->z);

                auto heightTestedTerrainPos = worldUiRenderService.getInverseViewProjectionMatrix()
                    * viewProjectionMatrix
                    * simVectorToFloat(*intersect);
                worldUiRenderService.fillColor(heightTestedTerrainPos.x - 2, heightTestedTerrainPos.y - 2, 4, 4, Color(255, 0, 0));
            }
        }

        sceneContext.graphics->enableDepthBuffer();

        sceneContext.graphics->setViewport(0, 0, sceneContext.viewport->width(), sceneContext.viewport->height());
    }

    const char* stateToString(const UnitBehaviorState& state)
    {
        return match(
            state,
            [&](const UnitBehaviorStateIdle&) {
                return "idle";
            },
            [&](const UnitBehaviorStateBuilding&) {
                return "building";
            },
            [&](const UnitBehaviorStateReclaiming&) {
                return "reclaiming";
            },
            [&](const UnitBehaviorStateResurrecting&) {
                return "resurrecting";
            },
            [&](const UnitBehaviorStateCreatingUnit&) {
                return "creating unit";
            });
    }

    const char* cobAxisToString(const CobAxis& axis)
    {
        switch (axis)
        {
            case CobAxis::X:
                return "x-axis";
            case CobAxis::Y:
                return "Y-axis";
            case CobAxis::Z:
                return "z-axis";
            default:
                throw std::logic_error("invalid axis");
        }
    }

    std::string blockedStatusToString(const CobEnvironment::BlockedStatus& status)
    {
        return match(
            status.condition,
            [&](const CobEnvironment::BlockedStatus::Move& m) {
                return "wait-for-move piece " + std::to_string(m.object) + " along " + cobAxisToString(m.axis);
            },
            [&](const CobEnvironment::BlockedStatus::Turn& t) {
                return "wait-for-turn piece " + std::to_string(t.object) + " around " + cobAxisToString(t.axis);
            });
    }

    void renderUnitInfoSection(const UnitState& unit)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("Unit Info"))
        {
            ImGui::LabelText("State", "%s", stateToString(unit.behaviourState));

            ImGui::LabelText("x", "%f", unit.position.x.value);
            ImGui::LabelText("y", "%f", unit.position.y.value);
            ImGui::LabelText("z", "%f", unit.position.z.value);
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::CollapsingHeader("COB Scripts"))
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Static Variables"))
            {
                for (Index i = 0; i < getSize(unit.cobEnvironment->_statics); ++i)
                {
                    ImGui::Text("%lld: %d", i, unit.cobEnvironment->_statics[i]);
                }
                ImGui::TreePop();
            }

            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
            if (ImGui::TreeNode("Threads"))
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("All"))
                {

                    for (Index i = 0; i < getSize(unit.cobEnvironment->threads); ++i)
                    {
                        const auto& thread = *unit.cobEnvironment->threads[i];
                        ImGui::Text("%lld: %s (%u)", i, thread.name.c_str(), thread.signalMask);
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Ready"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->readyQueue); ++i)
                    {
                        ImGui::Text("%s", unit.cobEnvironment->readyQueue[i]->name.c_str());
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Blocked"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->blockedQueue); ++i)
                    {
                        const auto& pair = unit.cobEnvironment->blockedQueue[i];
                        ImGui::Text("%s, %s", pair.second->name.c_str(), blockedStatusToString(pair.first).c_str());
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Sleeping"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->sleepingQueue); ++i)
                    {
                        const auto& pair = unit.cobEnvironment->sleepingQueue[i];
                        ImGui::Text("%s, wake time: %d", pair.second->name.c_str(), pair.first.value);
                    }
                    ImGui::TreePop();
                }

                ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                if (ImGui::TreeNode("Finished"))
                {
                    for (Index i = 0; i < getSize(unit.cobEnvironment->finishedQueue); ++i)
                    {
                        ImGui::Text("%s", unit.cobEnvironment->finishedQueue[i]->name.c_str());
                    }
                    ImGui::TreePop();
                }

                ImGui::TreePop();
            }
        }
    }

    void GameScene::renderDebugWindow()
    {
        if (!showDebugWindow)
        {
            return;
        }

        ImGui::Begin("Game Debug", &showDebugWindow);
        ImGui::Checkbox("Health bars", &healthBarsVisible);
        ImGui::Checkbox("Fog of war", &fogOfWarEnabled);

        if (!battleTestPlayers.empty())
        {
            ImGui::Separator();
            ImGui::Text("Battle test");
            ImGui::SliderInt("Units per side", &battleTestUnitsPerSide, 0, 500);
            int total = 0;
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unit.isDead())
                {
                    ++total;
                }
            }

            // Per side as well as the total: the two numbers drifting apart
            // is how you see that one side cannot get out of its own spawn.
            std::string aliveText;
            for (std::size_t i = 0; i < battleTestAlive.size(); ++i)
            {
                if (i != 0)
                {
                    aliveText += " v ";
                }
                aliveText += std::to_string(battleTestAlive[i]);
            }
            ImGui::Text("alive: %s (%d in the world)", aliveText.c_str(), total);
            ImGui::Text("spawned %u, blocked %u, culled %u, re-ordered %u", battleTestSpawned, battleTestSpawnsBlocked, battleTestCulled, battleTestReordered);
        }

        if (!simulation.aiControllers.empty() && ImGui::CollapsingHeader("AI players"))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                auto it = simulation.aiControllers.find(PlayerId(i));
                if (it == simulation.aiControllers.end() || !it->second)
                {
                    continue;
                }
                const auto& ai = *it->second;
                const auto& bb = ai.getBlackboard();
                ImGui::Text("Player %d (%s)", static_cast<int>(i), aiDifficultyName(ai.getProfile().difficulty));
                ImGui::Text("  phase: %s", gamePhaseName(bb.phase));
                ImGui::Text("  metal %.0f/%.0f%s  energy %.0f/%.0f%s",
                    bb.currentMetal.value, bb.metalStorage.value, bb.metalStalled ? " (stalled)" : "",
                    bb.currentEnergy.value, bb.energyStorage.value, bb.energyStalled ? " (stalled)" : "");
                ImGui::Text("  builders idle %d, factories %d, army %d, scout %s", bb.idleBuilderCount, static_cast<int>(bb.factories.size()), bb.armySize, bb.scoutUnitId ? "yes" : "no");
                ImGui::Text("  known enemies %d, near base %d, enemy base %s", static_cast<int>(bb.knownEnemies.size()), static_cast<int>(bb.enemiesNearBase.size()), bb.enemyBasePosition ? "known" : "unknown");
                if (bb.attackTarget)
                {
                    ImGui::Text("  attacking %.0f, %.0f", bb.attackTarget->x.value, bb.attackTarget->z.value);
                }
                for (const auto& [type, count] : bb.ownedTotalCounts)
                {
                    ImGui::Text("    %s x%d", type.c_str(), count);
                }
            }
        }
        if (ImGui::Checkbox("GUI", &guiVisible))
        {
            if (guiVisible)
            {
                worldViewport.setInset(GuiSizeLeft, GuiSizeTop, GuiSizeRight, GuiSizeBottom);
            }
            else
            {
                worldViewport.setInset(0, 0, 0, 0);
            }
            recreateWorldRenderTextures();
        }
        ImGui::Separator();
        ImGui::Checkbox("Cursor terrain dot", &cursorTerrainDotVisible);
        ImGui::Checkbox("Occupied grid", &occupiedGridVisible);
        ImGui::Checkbox("Pathfinding visualisation", &pathfindingVisualisationVisible);
        ImGui::Checkbox("Movement class grid", &movementClassGridVisible);
        ImGui::Separator();
        if (ImGui::Button("Unit spawner window..."))
        {
            showUnitSpawnerWindow = true;
        }
        renderUnitPlacer();
        ImGui::Separator();
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            ImGui::LabelText("Unit sounds", "%lld", getSize(playingUnitChannels));
            ImGui::LabelText("Sound volume", "%d", computeSoundVolume(getSize(playingUnitChannels)));
        }

        if (ImGui::CollapsingHeader("Selected Unit"))
        {
            ImGui::Indent();
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                const auto& unit = getUnit(*selectedUnit);
                renderUnitInfoSection(unit);
            }
            else
            {
                ImGui::Text("None");
            }
            ImGui::Unindent();
        }

        auto mouseTerrainCoordinate = getMouseTerrainCoordinate();

        if (mouseTerrainCoordinate)
        {
            ImGui::LabelText("mouse terrain x", "%f", mouseTerrainCoordinate->x.value);
            ImGui::LabelText("mouse terrain y", "%f", mouseTerrainCoordinate->y.value);
            ImGui::LabelText("mouse terrain z", "%f", mouseTerrainCoordinate->z.value);
        }

        ImGui::End();

        renderUnitSpawnerWindow();
    }

    void GameScene::onKeyDown(const SDL_KeyboardEvent& keysym)
    {
        // The game menu owns the keyboard while it is up. Tab and F2 toggle
        // it (the original's keys: Tab opens GAME OPTIONS in single player,
        // F2 anywhere) and Escape closes it.
        if (keysym.key == SDLK_TAB || keysym.key == SDLK_F2)
        {
            toggleGameMenu();
            return;
        }
        if (isGameMenuOpen())
        {
            if (keysym.key == SDLK_ESCAPE)
            {
                closeGameMenu();
                return;
            }
            for (auto& panel : gameMenuPanels)
            {
                panel->keyDown(KeyEvent(keysym.key));
            }
            return;
        }

        // Suppress UI panel key activation when Ctrl is held — Ctrl+letter is
        // hotkey territory (Ctrl+A select-all, Ctrl+S stop, Ctrl+D self-destruct,
        // etc.), and the panel's letter-bound buttons (e.g. ATTACK on plain "A")
        // would otherwise also fire.
        if (!isCtrlDown())
        {
            currentPanel->keyDown(KeyEvent(keysym.key));
        }

        if (keysym.key == SDLK_UP)
        {
            up = true;
        }
        else if (keysym.key == SDLK_DOWN)
        {
            down = true;
        }
        else if (keysym.key == SDLK_LEFT)
        {
            left = true;
        }
        else if (keysym.key == SDLK_RIGHT)
        {
            right = true;
        }
        else if (keysym.key == SDLK_LCTRL)
        {
            leftCtrlDown = true;
        }
        else if (keysym.key == SDLK_RCTRL)
        {
            rightCtrlDown = true;
        }
        else if (keysym.key == SDLK_LSHIFT)
        {
            leftShiftDown = true;
        }
        else if (keysym.key == SDLK_RSHIFT)
        {
            rightShiftDown = true;
        }
        else if (keysym.key == SDLK_ESCAPE)
        {
            handleEscapeDown();
        }
        else if (keysym.key == SDLK_F10)
        {
            showDebugWindow = !showDebugWindow;
        }
        else if (keysym.key == SDLK_F1)
        {
            helpVisible = !helpVisible;
        }
        else if (keysym.scancode == SDL_SCANCODE_GRAVE)
        {
            healthBarsVisible = !healthBarsVisible;
        }
        else if (keysym.key == SDLK_COMMA || keysym.key == SDLK_PERIOD)
        {
            // ',' and '.' page the selected builder's build menu, the
            // original's default paging keys.
            if (auto selectedUnit = getSingleSelectedUnit())
            {
                auto pages = getBuildPageCount(builderGuisDatabase, getUnit(*selectedUnit).unitType);
                if (pages > 1)
                {
                    auto& guiInfo = getGuiInfo(*selectedUnit);
                    auto step = keysym.key == SDLK_PERIOD ? 1u : pages - 1u;
                    guiInfo.currentBuildPage = (guiInfo.currentBuildPage + step) % pages;
                    guiInfo.section = UnitGuiInfo::Section::Build;
                    const auto& unit = getUnit(*selectedUnit);
                    if (auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage))
                    {
                        setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                    }
                }
            }
        }
        else if (keysym.key >= SDLK_F5 && keysym.key <= SDLK_F8)
        {
            // F5-F8 recall the camera bookmarks; with Ctrl held they store
            // the current view instead, as the original does.
            auto slot = static_cast<std::size_t>(keysym.key - SDLK_F5);
            if (isCtrlDown())
            {
                cameraBookmarks[slot] = worldCameraState.position;
            }
            else if (cameraBookmarks[slot])
            {
                setCameraPosition(*cameraBookmarks[slot]);
            }
        }
        else if (keysym.key == SDLK_F3)
        {
            // Jump to the last place one of our units took a hit.
            if (lastAttackPosition)
            {
                setCameraPosition(Vector3f(simScalarToFloat(lastAttackPosition->x), 0.0f, simScalarToFloat(lastAttackPosition->z)));
            }
        }
        else if (keysym.key == SDLK_F12)
        {
            consoleMessages.clear();
        }
        else if (keysym.key == SDLK_N && !isCtrlDown())
        {
            // Walk the player's own units one keypress at a time, selecting
            // and centering each in turn.
            std::optional<UnitId> first;
            std::optional<UnitId> next;
            bool passedCursor = !nextUnitCursor.has_value();
            for (const auto& [unitId, unit] : simulation.units)
            {
                if (!unit.isAlive() || !unit.isOwnedBy(localPlayerId))
                {
                    continue;
                }
                if (!first)
                {
                    first = unitId;
                }
                if (passedCursor)
                {
                    next = unitId;
                    break;
                }
                if (unitId == *nextUnitCursor)
                {
                    passedCursor = true;
                }
            }
            if (!next)
            {
                next = first;
            }
            if (next)
            {
                nextUnitCursor = next;
                clearUnitSelection();
                selectAdditionalUnit(*next);
                const auto& unit = getUnit(*next);
                setCameraPosition(Vector3f(simScalarToFloat(unit.position.x), 0.0f, simScalarToFloat(unit.position.z)));
            }
        }
        else if (keysym.key == SDLK_T)
        {
            startTrack();
        }
        else if (keysym.key == SDLK_C)
        {
            if (isCtrlDown())
            {
                if (!isShiftDown())
                {
                    clearUnitSelection();
                }

                // Select commanders (edge case: debug mode allows spawning multiple commanders)
                std::optional<UnitId> commanderUnitId;
                for (const auto& [unitId, unit] : simulation.units)
                {
                    const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                    if (unitDefinition.commander && unit.isOwnedBy(localPlayerId))
                    {
                        selectAdditionalUnit(unitId);
                        // For multiple commanders, OTA selects all but always tracks only the last spawned (it won't cycle with repeated ctrl-c)
                        commanderUnitId = unitId;
                    }
                }

                if (commanderUnitId)
                {
                    startTrackInternal({*commanderUnitId});
                }
            }
        }
        else if (keysym.key == SDLK_EQUALS || keysym.key == SDLK_KP_PLUS)
        {
            // Speed up: locally-issued, host-authoritatively applied via lockstep.
            // Any client may emit; processPlayerCommand drops it unless issued by host.
            localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{gameSpeed.increased().index()});
        }
        else if (keysym.key == SDLK_MINUS || keysym.key == SDLK_KP_MINUS)
        {
            // Slow down: see SDLK_EQUALS comment.
            localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{gameSpeed.decreased().index()});
        }
        else if (keysym.key == SDLK_PAUSE)
        {
            // Toggle the local paused flag immediately so the tick loop can
            // resume on unpause; the lockstep-routed command handler is what
            // drives the tick loop and would otherwise never run while paused.
            // Pause/unpause is scene state (not deterministic sim state), so
            // toggling locally is fine; the command still goes through the
            // command stream so peers stay in sync.
            paused = !paused;
            if (paused)
            {
                localPlayerCommandBuffer.push_back(PlayerPauseGameCommand{});
            }
            else
            {
                localPlayerCommandBuffer.push_back(PlayerUnpauseGameCommand{});
            }
        }
        else if (keysym.key == SDLK_A && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+A selects every unit the player owns, anywhere on the map
            // -- the original's special case, not merely what is on screen.
            selectAllWhere([](const UnitState&, const UnitDefinition& d) { return d.canMove; });
        }
        else if (keysym.key == SDLK_S && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+S is select-on-screen in the original; stop lives on the
            // plain S quickkey through the orders panel.
            selectAllOnScreen();
        }
        else if (keysym.key == SDLK_D && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+D: self-destruct selected units (TA behaviour).
            // Routes through the deterministic command queue so the
            // explosion happens at the same game tick on all peers.
            unsigned int toggled = 0;
            for (const auto& unitId : selectedUnits)
            {
                const auto& unit = tryGetUnit(unitId);
                if (unit && unit->get().isAlive() && unit->get().isOwnedBy(localPlayerId))
                {
                    localPlayerSelfDestructUnit(unitId);
                    ++toggled;
                }
            }
            LOG_INFO << "Self-destruct toggled for " << toggled << " selected unit(s)";
        }
        else if (keysym.key == SDLK_Z && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+Z selects every owned unit of the same type as anything
            // already selected. Attack-ground stays on the A quickkey.
            std::unordered_set<std::string> types;
            for (const auto& unitId : selectedUnits)
            {
                if (auto unit = tryGetUnit(unitId))
                {
                    types.insert(unit->get().unitType);
                }
            }
            if (!types.empty())
            {
                selectAllWhere([&types](const UnitState& u, const UnitDefinition&) { return types.find(u.unitType) != types.end(); });
            }
        }
        else if (isCtrlDown() && !isShiftDown()
            && (keysym.key == SDLK_W || keysym.key == SDLK_F || keysym.key == SDLK_P
                || keysym.key == SDLK_V || keysym.key == SDLK_B || keysym.key == SDLK_R))
        {
            // The original's Ctrl+letter selections are data-driven: it
            // formats "CTRL_%c" and matches the FBI Category tokens, and the
            // shipped data defines CTRL_W (weapons), CTRL_V (vtols), CTRL_F
            // (factories), CTRL_B (builders), CTRL_R (radars) and CTRL_P.
            char letter = static_cast<char>(std::toupper(keysym.key));
            selectAllByCategoryToken(std::string("CTRL_") + letter);
        }
        else
        {
            // Control groups: keys 1-0 map to groups 0-9.
            // Ctrl+digit  → bind current selection to group (replace).
            // Shift+digit → add current selection to group.
            // Digit alone → recall group (replace current selection).
            // Ctrl+Shift+digit is treated the same as Shift+digit (add).
            std::optional<int> groupIndex;
            if (keysym.key >= SDLK_1 && keysym.key <= SDLK_9)
            {
                groupIndex = keysym.key - SDLK_1; // 0-8
            }
            else if (keysym.key == SDLK_0)
            {
                groupIndex = 9; // '0' maps to group index 9
            }

            if (groupIndex)
            {
                auto idx = *groupIndex;
                if (isCtrlDown() && isShiftDown())
                {
                    // Ctrl+Shift+digit: add selection to control group.
                    for (const auto& unitId : selectedUnits)
                    {
                        controlGroups[idx].insert(unitId);
                    }
                }
                else if (isCtrlDown())
                {
                    // Ctrl+digit: bind (replace) control group with current selection.
                    controlGroups[idx] = selectedUnits;
                }
                else if (isShiftDown())
                {
                    // Shift+digit: add current selection to control group.
                    for (const auto& unitId : selectedUnits)
                    {
                        controlGroups[idx].insert(unitId);
                    }
                }
                else
                {
                    // Digit alone: recall control group.
                    // Filter out any units that are now dead or no longer owned
                    // by the local player so stale IDs do not pollute the set.
                    std::unordered_set<UnitId> liveUnits;
                    for (const auto& unitId : controlGroups[idx])
                    {
                        auto unitRef = tryGetUnit(unitId);
                        if (unitRef && unitRef->get().isAlive() && unitRef->get().isOwnedBy(localPlayerId))
                        {
                            liveUnits.insert(unitId);
                        }
                    }
                    // Prune the stored group to remove dead entries.
                    controlGroups[idx] = liveUnits;
                    replaceUnitSelection(liveUnits);
                }
            }
        }
    }

    void GameScene::onKeyUp(const SDL_KeyboardEvent& keysym)
    {
        currentPanel->keyUp(KeyEvent(keysym.key));

        if (keysym.key == SDLK_UP)
        {
            up = false;
        }
        else if (keysym.key == SDLK_DOWN)
        {
            down = false;
        }
        else if (keysym.key == SDLK_LEFT)
        {
            left = false;
        }
        else if (keysym.key == SDLK_RIGHT)
        {
            right = false;
        }
        else if (keysym.key == SDLK_LCTRL)
        {
            leftCtrlDown = false;
        }
        else if (keysym.key == SDLK_RCTRL)
        {
            rightCtrlDown = false;
        }
        else if (keysym.key == SDLK_LSHIFT)
        {
            leftShiftDown = false;
        }
        else if (keysym.key == SDLK_RSHIFT)
        {
            rightShiftDown = false;
        }
    }

    void GameScene::onMouseDown(MouseButtonEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseDown(event);
            }
            return;
        }

        currentPanel->mouseDown(event);

        // Debug placing mode: clicks drop units on the map instead of
        // selecting and ordering, so a test scenario can be set up quickly.
        if (unitSpawnOnClick && !unitSpawnType.empty() && isValidUnitType(simulation, unitSpawnType))
        {
            if (event.button == MouseButtonEvent::MouseButton::Left)
            {
                if (auto terrainPos = getMouseTerrainCoordinate())
                {
                    placeDebugUnit(*terrainPos);
                }
                return;
            }
            if (event.button == MouseButtonEvent::MouseButton::Right)
            {
                // Right-click leaves placing mode, like cancelling any other cursor mode.
                unitSpawnOnClick = false;
                return;
            }
        }

        if (event.button == MouseButtonEvent::MouseButton::Left)
        {
            match(
                cursorMode.getValue(),
                [&](const AttackCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                        else
                        {
                            auto coord = getMouseTerrainCoordinate();
                            if (coord)
                            {
                                if (isShiftDown())
                                {
                                    localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*coord));
                                }
                                else
                                {
                                    localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*coord));
                                    cursorMode.next(NormalCursorMode());
                                }
                            }
                        }
                    }
                },
                [&](const DgunCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        // A D-gun order takes a unit or bare ground, the same
                        // pair an attack order takes: the disintegrator is
                        // happy to be fired at a patch of dirt.
                        std::optional<UnitOrder> order;
                        if (hoveredUnit)
                        {
                            order = DgunOrder(*hoveredUnit);
                        }
                        else if (auto coord = getMouseTerrainCoordinate())
                        {
                            order = DgunOrder(*coord);
                        }

                        if (!order)
                        {
                            continue;
                        }

                        if (isShiftDown())
                        {
                            localPlayerEnqueueUnitOrder(selectedUnit, *order);
                        }
                        else
                        {
                            localPlayerIssueUnitOrder(selectedUnit, *order);
                            cursorMode.next(NormalCursorMode());
                        }
                    }
                },
                [&](const MoveCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        // A move onto one of our own repair pads is a landing,
                        // not a move. The original's order dispatcher turns the
                        // same click into VTOL_LANDING (0x43FB1B), and the FAQ
                        // describes this exact gesture from the other side:
                        // "Select the plane, click on Move and then click on
                        // the repair pad."
                        if (hoveredUnit && unitShouldLandOnAirBase(simulation, selectedUnit, *hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, LandOnAirBaseOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, LandOnAirBaseOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                            continue;
                        }

                        auto coord = getMouseTerrainCoordinate();
                        if (coord)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const GuardCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isFriendly(*hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, GuardOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, GuardOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const CaptureCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isEnemy(*hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, CaptureOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, CaptureOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const LoadCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isFriendly(*hoveredUnit) && *hoveredUnit != selectedUnit)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, LoadOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, LoadOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const UnloadCursorMode&) {
                    auto coord = getMouseTerrainCoordinate();
                    if (coord)
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, UnloadOrder(*coord));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, UnloadOrder(*coord));
                            }
                        }
                        if (!isShiftDown())
                        {
                            cursorMode.next(NormalCursorMode());
                        }
                    }
                },
                [&](const PatrolCursorMode&) {
                    auto coord = getMouseTerrainCoordinate();
                    if (coord)
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, PatrolOrder(*coord));
                            }
                            else
                            {
                                // A fresh patrol loops between the clicked point
                                // and wherever the unit is standing now.
                                localPlayerIssueUnitOrder(selectedUnit, PatrolOrder(*coord));
                                localPlayerEnqueueUnitOrder(selectedUnit, PatrolOrder(getUnit(selectedUnit).position));
                            }
                        }
                        if (!isShiftDown())
                        {
                            cursorMode.next(NormalCursorMode());
                        }
                    }
                },
                [&](const RepairCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit && isFriendly(*hoveredUnit))
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const ReclaimCursorMode&) {
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (hoveredUnit)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredUnit));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredUnit));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                        else if (hoveredFeature)
                        {
                            if (isShiftDown())
                            {
                                localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                            }
                            else
                            {
                                localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                    }
                },
                [&](const BuildCursorMode& buildCursor) {
                    if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
                    {
                        if (hoverBuildInfo)
                        {
                            if (hoverBuildInfo->isValid)
                            {
                                auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(hoverBuildInfo->rect.x,
                                    hoverBuildInfo->rect.y);
                                auto x = topLeftWorld.x + ((SimScalar(hoverBuildInfo->rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss);
                                auto z = topLeftWorld.z + ((SimScalar(hoverBuildInfo->rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss);
                                auto y = simulation.terrain.getHeightAt(x, z);
                                SimVector buildPos(x, y, z);
                                if (isShiftDown())
                                {
                                    // Shift-clicking a building already in the plan takes it out again.
                                    if (auto planned = plannedBuildOrderAt(*selectedUnit, buildPos))
                                    {
                                        localPlayerCancelBuildOrder(*selectedUnit, *planned);
                                    }
                                    else
                                    {
                                        localPlayerEnqueueUnitOrder(*selectedUnit, BuildOrder(buildCursor.unitType, buildPos));
                                    }
                                }
                                else
                                {
                                    localPlayerIssueUnitOrder(*selectedUnit, BuildOrder(buildCursor.unitType, buildPos));
                                    cursorMode.next(NormalCursorMode());
                                }
                            }
                            else if (isShiftDown() && getMouseTerrainCoordinate())
                            {
                                // The spot is blocked by our own plan: shift-click removes that plan.
                                if (auto planned = plannedBuildOrderAt(*selectedUnit, *getMouseTerrainCoordinate()))
                                {
                                    localPlayerCancelBuildOrder(*selectedUnit, *planned);
                                }
                                else if (sounds.notOkToBuild)
                                {
                                    playUiSound(*sounds.notOkToBuild);
                                }
                            }
                            else
                            {
                                if (sounds.notOkToBuild)
                                {
                                    playUiSound(*sounds.notOkToBuild);
                                }
                            }
                        }
                    }
                },
                [&](const NormalCursorMode&) {
                    if (isCursorOverMinimap())
                    {
                        if (leftClickMode())
                        {
                            for (const auto& selectedUnit : selectedUnits)
                            {
                                if (hoveredUnit)
                                {
                                    if (isEnemy(*hoveredUnit))
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                        }
                                    }
                                    else
                                    {
                                        if (const auto& u = getUnit(*hoveredUnit); u.isBeingBuilt(simulation.unitDefinitions.at(u.unitType)))
                                        {
                                            if (isShiftDown())
                                            {
                                                localPlayerEnqueueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                            }
                                            else
                                            {
                                                localPlayerIssueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    auto coord = getMouseTerrainCoordinate();
                                    if (coord)
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                        }
                                    }
                                }
                            }
                        }
                        else
                        {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::DraggingMinimapState()});
                        }
                    }
                    else if (isCursorOverWorld())
                    {
                        Point p(event.x, event.y);
                        auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, p);
                        const auto cameraPosition = worldCameraState.getRoundedPosition();
                        Point originRelativePos(cameraPosition.x + worldViewportPos.x, cameraPosition.z + worldViewportPos.y);
                        cursorMode.next(NormalCursorMode{NormalCursorMode::SelectingState(sceneTime, originRelativePos)});
                    }
                });
        }
        else if (event.button == MouseButtonEvent::MouseButton::Right)
        {
            match(
                cursorMode.getValue(),
                [&](const AttackCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const DgunCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const MoveCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const GuardCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const ReclaimCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const RepairCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const PatrolCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const CaptureCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const LoadCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const UnloadCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const BuildCursorMode&) {
                    cursorMode.next(NormalCursorMode());
                },
                [&](const NormalCursorMode&) {
                    if (leftClickMode())
                    {
                        if (isCursorOverMinimap())
                        {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::DraggingMinimapState()});
                        }
                        else if (isCursorOverWorld())
                        {
                            clearUnitSelection();
                        }
                    }
                    else
                    {
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            if (hoveredUnit)
                            {
                                if (isEnemy(*hoveredUnit))
                                {
                                    if (isShiftDown())
                                    {
                                        localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                    }
                                    else
                                    {
                                        localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                    }
                                }
                                else
                                {
                                    if (const auto& u = getUnit(*hoveredUnit); u.isBeingBuilt(simulation.unitDefinitions.at(u.unitType)))
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                        }
                                    }
                                }
                            }
                            else if (hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                            {
                                if (isShiftDown())
                                {
                                    localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                }
                                else
                                {
                                    localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                }
                            }
                            else
                            {
                                auto coord = getMouseTerrainCoordinate();
                                if (coord)
                                {
                                    if (isShiftDown())
                                    {
                                        localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                                    }
                                    else
                                    {
                                        localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                    }
                                }
                            }
                        }
                    }
                });
        }
        else if (event.button == MouseButtonEvent::MouseButton::Middle)
        {
            cameraControlState = CameraControlStateMiddleMousePan{getMousePosition()};
        }
    }

    void GameScene::onMouseUp(MouseButtonEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseUp(event);
            }
            return;
        }

        currentPanel->mouseUp(event);

        if (event.button == MouseButtonEvent::MouseButton::Left)
        {
            match(
                cursorMode.getValue(),
                [&](const NormalCursorMode& normalCursor) {
                    match(
                        normalCursor.state,
                        [&](const NormalCursorMode::SelectingState& state) {
                            Point p(event.x, event.y);
                            auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, p);
                            const auto cameraPosition = worldCameraState.getRoundedPosition();
                            Point originRelativePos(cameraPosition.x + worldViewportPos.x, cameraPosition.z + worldViewportPos.y);

                            if (sceneTime - state.startTime < SceneTime(30) && state.startPosition.maxSingleDimensionDistance(originRelativePos) < 32)
                            {
                                if (hoveredUnit && getUnit(*hoveredUnit).isSelectableBy(simulation.unitDefinitions.at(getUnit(*hoveredUnit).unitType), localPlayerId))
                                {
                                    if (isShiftDown())
                                    {
                                        toggleUnitSelection(*hoveredUnit);
                                    }
                                    else
                                    {
                                        replaceUnitSelection(*hoveredUnit);
                                    }
                                }
                                else if (leftClickMode() && hoveredUnit)
                                {
                                    if (isEnemy(*hoveredUnit))
                                    {
                                        for (const auto& selectedUnit : selectedUnits)
                                        {
                                            if (isShiftDown())
                                            {
                                                localPlayerEnqueueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                            }
                                            else
                                            {
                                                localPlayerIssueUnitOrder(selectedUnit, AttackOrder(*hoveredUnit));
                                            }
                                        }
                                    }
                                    else
                                    {
                                        if (const auto& u = getUnit(*hoveredUnit); u.isBeingBuilt(simulation.unitDefinitions.at(u.unitType)))
                                        {
                                            for (const auto& selectedUnit : selectedUnits)
                                            {
                                                if (isShiftDown())
                                                {
                                                    localPlayerEnqueueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                                }
                                                else
                                                {
                                                    localPlayerIssueUnitOrder(selectedUnit, CompleteBuildOrder(*hoveredUnit));
                                                }
                                            }
                                        }
                                        else if (unitIsDamaged(simulation, *hoveredUnit))
                                        {
                                            for (const auto& selectedUnit : selectedUnits)
                                            {
                                                if (!unitIsBuilder(simulation, selectedUnit))
                                                {
                                                    continue;
                                                }
                                                if (isShiftDown())
                                                {
                                                    localPlayerEnqueueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                                                }
                                                else
                                                {
                                                    localPlayerIssueUnitOrder(selectedUnit, RepairOrder(*hoveredUnit));
                                                }
                                            }
                                        }
                                    }
                                }
                                else if (leftClickMode() && hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                                {
                                    for (const auto& selectedUnit : selectedUnits)
                                    {
                                        if (isShiftDown())
                                        {
                                            localPlayerEnqueueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                        }
                                        else
                                        {
                                            localPlayerIssueUnitOrder(selectedUnit, ReclaimOrder(*hoveredFeature));
                                        }
                                    }
                                }
                                else if (leftClickMode())
                                {
                                    auto coord = getMouseTerrainCoordinate();
                                    if (coord)
                                    {
                                        for (const auto& selectedUnit : selectedUnits)
                                        {
                                            if (isShiftDown())
                                            {
                                                localPlayerEnqueueUnitOrder(selectedUnit, MoveOrder(*coord));
                                            }
                                            else
                                            {
                                                localPlayerIssueUnitOrder(selectedUnit, MoveOrder(*coord));
                                            }
                                        }
                                    }
                                }
                                else
                                {
                                    if (!isShiftDown())
                                    {
                                        clearUnitSelection();
                                    }
                                }
                            }
                            else
                            {
                                selectUnitsInBandbox(DiscreteRect::fromPoints(state.startPosition, originRelativePos));
                            }

                            cursorMode.next(NormalCursorMode{NormalCursorMode::UpState()});
                        },
                        [&](const NormalCursorMode::DraggingMinimapState&) {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::UpState()});
                        },
                        [&](const NormalCursorMode::UpState&) {
                        });
                },
                [&](const auto&) {
                    // do nothing
                });
        }
        else if (event.button == MouseButtonEvent::MouseButton::Right)
        {
            match(
                cursorMode.getValue(),
                [&](const NormalCursorMode& m) {
                    match(
                        m.state,
                        [&](const NormalCursorMode::DraggingMinimapState&) {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::UpState()});
                        },
                        [](const auto&) {});
                },
                [](const auto&) {});
        }
        else if (event.button == MouseButtonEvent::MouseButton::Middle)
        {
            if (std::holds_alternative<CameraControlStateMiddleMousePan>(cameraControlState))
            {
                cameraControlState = CameraControlStateFree();
            }
        }
    }

    Rectangle2f computeCameraConstraint(const MapTerrain& terrain, float viewportWidth, float viewportHeight)
    {
        auto cameraHalfWidth = viewportWidth / 2.0f;
        auto cameraHalfHeight = viewportHeight / 2.0f;

        auto top = simScalarToFloat(terrain.topInWorldUnits()) + cameraHalfHeight;
        auto left = simScalarToFloat(terrain.leftInWorldUnits()) + cameraHalfWidth;
        auto bottom = simScalarToFloat(terrain.bottomCutoffInWorldUnits()) - cameraHalfHeight;
        auto right = simScalarToFloat(terrain.rightCutoffInWorldUnits()) - cameraHalfWidth;

        if (left > right)
        {
            auto middle = (left + right) / 2.0f;
            left = middle;
            right = middle;
        }

        if (top > bottom)
        {
            auto middle = (top + bottom) / 2.0f;
            top = middle;
            bottom = middle;
        }

        return Rectangle2f::fromTLBR(top, left, bottom, right);
    }

    void GameScene::onMouseMove(MouseMoveEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseMove(event);
            }
            return;
        }

        if (auto middleMousePanningState = std::get_if<CameraControlStateMiddleMousePan>(&cameraControlState); middleMousePanningState)
        {
            auto cameraConstraint = computeCameraConstraint(simulation.terrain, worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()));

            const auto& cameraPos = worldCameraState.position;

            auto currentCursorPosition = Point(event.x, event.y);
            auto delta = currentCursorPosition - middleMousePanningState->previousCursorPosition;

            auto newCameraPos = cameraConstraint.clamp(Vector2f(cameraPos.x - delta.x, cameraPos.z - delta.y));
            worldCameraState.position = Vector3f(newCameraPos.x, cameraPos.y, newCameraPos.y);

            middleMousePanningState->previousCursorPosition = currentCursorPosition;
        }
        currentPanel->mouseMove(event);
    }

    void GameScene::onMouseWheel(MouseWheelEvent event)
    {
        if (isGameMenuOpen())
        {
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseWheel(event);
            }
            return;
        }

        currentPanel->mouseWheel(event);
    }

    void GameScene::update(int millisecondsElapsed)
    {
        // The battle harness, if one was asked for: keep both sides at
        // strength and send every replacement at the enemy. Gated on the
        // harness being enabled rather than on the count, since a count of
        // zero is the slider being dragged to the bottom and still has work
        // to do -- clearing the field.
        if (!battleTestPlayers.empty())
        {
            RWE_RENDERPROF("u.battletest");
            runBattleTest();
        }

        for (auto& action : std::exchange(pendingMenuActions, {}))
        {
            action();
        }

        updateMusic();

        // Pause halts simulation tick dispatch by not advancing the
        // scaled-time accumulator. Speed scales the accumulator using
        // integer arithmetic to keep determinism friendly: at perMille
        // == 1000 we accumulate 1ms per real ms; at 100 we accumulate
        // 0.1ms per real ms; at 5000 we accumulate 5ms per real ms.
        // The sim tick threshold (SimMillisecondsPerTick) is unchanged.
        if (!paused)
        {
            millisecondsBuffer += (millisecondsElapsed * gameSpeed.perMille()) / 1000;
        }

        auto cameraConstraint = computeCameraConstraint(simulation.terrain, worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()));

        // update camera position from keyboard arrows
        {
            int directionX = (right ? 1 : 0) - (left ? 1 : 0);
            int directionZ = (down ? 1 : 0) - (up ? 1 : 0);

            if (directionX || directionZ)
            {
                nudgeCamera(millisecondsElapsed, cameraConstraint, directionX, directionZ);
            }
        }

        // update camera position from edge scroll
        {
            auto mousePosition = getMousePosition();
            auto directionX = mousePosition.x == sceneContext.viewport->left()
                ? -1
                : mousePosition.x == sceneContext.viewport->right() - 1
                ? 1
                : 0;
            auto directionZ = mousePosition.y == sceneContext.viewport->top()
                ? -1
                : mousePosition.y == sceneContext.viewport->bottom() - 1
                ? 1
                : 0;

            if (directionX || directionZ)
            {
                nudgeCamera(millisecondsElapsed, cameraConstraint, directionX, directionZ);
            }
        }

        // handle minimap dragging
        if (auto cursor = std::get_if<NormalCursorMode>(&cursorMode.getValue()); cursor != nullptr)
        {
            if (std::holds_alternative<NormalCursorMode::DraggingMinimapState>(cursor->state))
            {
                // ok, the cursor is dragging the minimap.
                // work out where the cursor is on the minimap,
                // convert that to the world, then set the camera's position to there
                // (clamped to map bounds)

                auto minimapToWorld = minimapToWorldMatrix(simulation.terrain, minimapRect);
                auto mousePos = getMousePosition();
                auto worldPos = minimapToWorld * Vector3f(static_cast<float>(mousePos.x) + 0.5f, static_cast<float>(mousePos.y) + 0.5, 0.0f);

                relocateCamera(cameraConstraint, worldPos.x, worldPos.z);
            }
        }

        // handle tracking
        {
            // TODO (kwh) - tracking of projectiles not yet implemented. E.g. while tracking Bertha or Nuke Silo,
            // screen should follow a projectile until it hits, then return to the tracking group

            if (std::holds_alternative<CameraControlStateTrackingUnit>(cameraControlState) && trackedUnitId)
            {
                // get tracked unit position, or stop tracking if it's gone
                auto unit = tryGetUnit(*trackedUnitId);
                if (unit && !unit->get().isDead())
                {
                    // Move camera... OTA behavior:
                    // For each x and z component (not euclidean distance), halve the distance from camera to unit each frame,
                    //  but limit to a max of 320 pixels per frame, at 30fps for normal speed (Scroll speed followed game speed, eg +10 scrolls faster).
                    // Presumably 320 to make scrolling look continuous on the lowest res setting of 640x480

                    // We will use millisecondsElapsed to interpolate for smoother scrolling at high fps in rwe,
                    // while maintaining similar scroll speed on the map; Speed is linear and clamped at 320 pixels/(1/30)s = 9600 pix/s,
                    const float maxScroll = 9.6f * millisecondsElapsed;
                    // To interpolate halving distance every 1/30s, we'll use the definition of geometric progression: a_n = a*r^(n); where:
                    //  a_n = distance (pixels) from the unit we should be after n 1/30s frames, a = current distance from the unit
                    //  r = common ratio i.e. 1/2, the ratio the distance should decrease every 1/30 seconds
                    //  n = # of OTA frames = seconds elapsed / (1/30 s per OTA frame) = (time_ms / 1000) * 30 = 3 * time_ms / 100

                    const auto& cameraPos = worldCameraState.position;
                    const auto unitPos = simVectorToFloat(unit->get().position);
                    const auto cameraPosDelta = unitPos - cameraPos;

                    float decayFactor = 1.0f - std::pow(.5f, .03f * millisecondsElapsed);

                    float newDelta_x = cameraPosDelta.x * decayFactor;
                    if (std::abs(newDelta_x) > maxScroll)
                    {
                        newDelta_x = newDelta_x < 0 ? -maxScroll : maxScroll;
                    }

                    float newDelta_z = cameraPosDelta.z * decayFactor;
                    if (std::abs(newDelta_z) > maxScroll)
                    {
                        newDelta_z = newDelta_z < 0 ? -maxScroll : maxScroll;
                    }

                    auto newPos = cameraConstraint.clamp(Vector2f(newDelta_x + cameraPos.x, newDelta_z + cameraPos.z));
                    worldCameraState.position = Vector3f(newPos.x, worldCameraState.position.y, newPos.y);
                }
            }
        }

        // reset cursor mode if shift is released and at least 1 order was queued since shift was held
        if (commandWasQueued && !isShiftDown())
        {
            cursorMode.next(NormalCursorMode());
            commandWasQueued = false;
        }

        hoveredUnit = getUnitUnderCursor();
        hoveredFeature = getFeatureUnderCursor();

        if (auto buildCursor = std::get_if<BuildCursorMode>(&cursorMode.getValue()); buildCursor != nullptr && isCursorOverWorld())
        {
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            auto intersect = simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));

            if (intersect)
            {
                const auto& unitType = buildCursor->unitType;
                const auto& pos = *intersect;
                const auto& unitDefinition = simulation.unitDefinitions.at(unitType);
                auto mc = simulation.getAdHocMovementClass(unitDefinition.movementCollisionInfo);
                auto footprintRect = simulation.computeFootprintRegion(pos, unitDefinition.movementCollisionInfo);
                auto isValid = simulation.canBeBuiltAt(mc, unitDefinition.yardMap, unitDefinition.yardMapContainsGeo, footprintRect.x, footprintRect.y);
                hoverBuildInfo = HoverBuildInfo{footprintRect, isValid};
            }
            else
            {
                hoverBuildInfo = std::nullopt;
            }
        }
        else
        {
            hoverBuildInfo = std::nullopt;
        }

        if (!isCursorOverMinimap() && !isCursorOverWorld())
        {
            // The cursor is outside the world, so over UI elements.
            sceneContext.cursor->useCursor(CursorType::Normal);
        }
        else
        {
            match(
                cursorMode.getValue(),
                [&](const AttackCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Attack);
                },
                [&](const DgunCursorMode&) {
                    // CURSORS.GAF has no D-gun cursor of its own.
                    sceneContext.cursor->useCursor(CursorType::Attack);
                },
                [&](const MoveCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Move);
                },
                [&](const GuardCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Guard);
                },
                [&](const ReclaimCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Reclaim);
                },
                [&](const RepairCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Repair);
                },
                [&](const PatrolCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Patrol);
                },
                [&](const CaptureCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Capture);
                },
                [&](const LoadCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Load);
                },
                [&](const UnloadCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Unload);
                },
                [&](const BuildCursorMode&) {
                    sceneContext.cursor->useCursor(CursorType::Normal);
                },
                [&](const NormalCursorMode&) {
                    if (leftClickMode())
                    {
                        if (hoveredUnit && unitIsSelectableBy(simulation, *hoveredUnit, localPlayerId))
                        {
                            sceneContext.cursor->useCursor(CursorType::Select);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanAttack(simulation, id); })
                            && hoveredUnit && isEnemy(*hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Attack);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); })
                            && hoveredUnit && isFriendly(*hoveredUnit) && (unitIsBeingBuilt(simulation, *hoveredUnit) || unitIsDamaged(simulation, *hoveredUnit)))
                        {
                            sceneContext.cursor->useCursor(CursorType::Repair);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); }) && hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                        {
                            sceneContext.cursor->useCursor(CursorType::Reclaim);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanMove(simulation, id); }))
                        {
                            sceneContext.cursor->useCursor(CursorType::Move);
                        }
                        else
                        {
                            sceneContext.cursor->useCursor(CursorType::Normal);
                        }
                    }
                    else
                    {
                        if (hoveredUnit && unitIsSelectableBy(simulation, *hoveredUnit, localPlayerId))
                        {
                            sceneContext.cursor->useCursor(CursorType::Select);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanAttack(simulation, id); })
                            && hoveredUnit && isEnemy(*hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Red);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); })
                            && hoveredUnit && isFriendly(*hoveredUnit) && unitIsBeingBuilt(simulation, *hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Green);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitCanGuard(simulation, id); })
                            && hoveredUnit && isFriendly(*hoveredUnit))
                        {
                            sceneContext.cursor->useCursor(CursorType::Green);
                        }
                        else if (std::any_of(selectedUnits.begin(), selectedUnits.end(), [&](const auto& id) { return unitIsBuilder(simulation, id); }) && hoveredFeature && featureCanBeReclaimed(simulation, *hoveredFeature))
                        {
                            sceneContext.cursor->useCursor(CursorType::Green);
                        }
                        else
                        {
                            sceneContext.cursor->useCursor(CursorType::Normal);
                        }
                    }
                });
        }

        auto maxRtt = std::clamp(gameNetworkService->getMaxAverageRttMillis(), 16.0f, 2000.0f);
        auto highCommandLatencyMillis = maxRtt + (maxRtt / 4.0f) + 200.0f;
        auto commandLatencyFrames = static_cast<unsigned int>(highCommandLatencyMillis / 16.0f) + 1;
        auto targetCommandBufferSize = commandLatencyFrames;

        auto bufferedCommandCount = playerCommandService->bufferedCommandCount(localPlayerId);

        LOG_DEBUG << "Buffer levels (real/target) " << bufferedCommandCount << "/" << targetCommandBufferSize;

        // If we have too many commands buffered,
        // defer submitting commands this frame
        // so that we drop back down to the threshold.
        if (bufferedCommandCount <= targetCommandBufferSize)
        {
            // Queue up commands collected from the local player
            playerCommandService->pushCommands(localPlayerId, localPlayerCommandBuffer);
            gameNetworkService->submitCommands(sceneTime, localPlayerCommandBuffer);
            localPlayerCommandBuffer.clear();
            ++bufferedCommandCount;
        }

        // fill up to the required threshold
        for (; bufferedCommandCount < targetCommandBufferSize; ++bufferedCommandCount)
        {
            playerCommandService->pushCommands(localPlayerId, std::vector<PlayerCommand>());
            gameNetworkService->submitCommands(sceneTime, std::vector<PlayerCommand>());
        }

        // Queue up commands from the computer players. The AI runs inside
        // the simulation (one tick ahead of this drain) and writes its
        // PlayerCommands into `simulation.aiPendingCommands`. We pull them
        // here and push them through the same PlayerCommandService channel
        // human input uses, so MP/replay/desync detection treats AI
        // identically to a remote human.
        //
        // The AI buffer is kept topped up to the same threshold as the
        // local human buffer. A single frame may dispatch several sim
        // ticks (catch-up after a slow frame, or any game speed above 1x),
        // and each tick pops one entry from every player's buffer. If the
        // AI only had one entry queued, the second tick in a frame would
        // find its buffer empty and be skipped ("Blocked waiting for
        // player commands").
        for (Index i = 0; i < getSize(simulation.players); ++i)
        {
            PlayerId id(i);
            const auto& player = simulation.players[i];
            if (player.type != GamePlayerType::Computer)
            {
                continue;
            }

            auto aiBufferedCount = playerCommandService->bufferedCommandCount(id);
            if (aiBufferedCount <= targetCommandBufferSize)
            {
                auto aiCommands = simulation.takeAiCommandsForPlayer(id);
                playerCommandService->pushCommands(id, aiCommands);
                ++aiBufferedCount;
            }

            for (; aiBufferedCount < targetCommandBufferSize; ++aiBufferedCount)
            {
                playerCommandService->pushCommands(id, std::vector<PlayerCommand>());
            }
        }

        // If we are waiting to swap in a new unit GUI panel, do that now
        if (nextPanel)
        {
            currentPanel = std::move(*nextPanel);
            nextPanel = std::nullopt;
            attachOrdersMenuEventHandlers();
        }

        auto averageSceneTime = gameNetworkService->estimateAvergeSceneTime(sceneTime);

        // allow skipping sim frames every so often to get back down to average.
        // We tolerate X frames of drift in either direction to cope with noisiness in the estimation.
        const SceneTime frameTolerance(3);
        const SceneTime frameCheckInterval(5);
        auto highSceneTime = averageSceneTime + frameTolerance;
        auto lowSceneTime = averageSceneTime <= frameTolerance ? SceneTime{0} : averageSceneTime - frameTolerance;
        // Cap the number of sim ticks we dispatch per frame to prevent
        // a runaway "spiral of death" if frame times spike at high speeds.
        const int maxTicksPerFrame = 10;
        int ticksThisFrame = 0;
        for (; millisecondsBuffer >= SimMillisecondsPerTick && ticksThisFrame < maxTicksPerFrame; millisecondsBuffer -= SimMillisecondsPerTick)
        {
            if (sceneTime % frameCheckInterval != SceneTime(0) || sceneTime <= highSceneTime)
            {
                tryTickGame();
                ++ticksThisFrame;

                // simulate an extra frame to catch up every so often
                if (sceneTime % frameCheckInterval == SceneTime(0) && sceneTime < lowSceneTime && ticksThisFrame < maxTicksPerFrame)
                {
                    tryTickGame();
                    ++ticksThisFrame;
                }
            }
        }
        // If we hit the cap, drain the buffer so we don't carry over
        // unbounded backlog into the next frame.
        if (ticksThisFrame >= maxTicksPerFrame)
        {
            millisecondsBuffer = 0;
        }

        // A launcher's magazine fills without anybody ordering anything, so its
        // readout cannot be refreshed off a command the way a build queue's is.
        refreshStockpileGuiTotal();

        // Nor can a factory's queue, once it starts working through it: the
        // count drops when a unit begins, and no command passes through the
        // UI to hang a refresh on. The original does not try -- 0x4199B0
        // rebuilds the caption of every gadget on the page on every refresh,
        // counting the outstanding orders on demand through 0x439D80 -- so
        // this does the same.
        refreshBuildGuiTotals();

        renderDebugWindow();
    }

    std::optional<UnitId> GameScene::spawnUnit(const std::string& unitType, PlayerId owner, const SimVector& position, std::optional<const std::reference_wrapper<SimAngle>> rotation)
    {
        return simulation.trySpawnUnit(unitType, owner, position, rotation);
    }

    unsigned int GameScene::debugSpawnHitPoints(const UnitDefinition& unitDefinition) const
    {
        auto percent = static_cast<unsigned int>(std::clamp(unitSpawnHealthPercent, 1, 100));
        auto points = (static_cast<std::uint64_t>(unitDefinition.maxHitPoints) * percent) / 100u;
        return static_cast<unsigned int>(std::max<std::uint64_t>(1u, points));
    }

    void GameScene::buildUnitTypeCategories()
    {
        if (!unitTypesByCategory.empty())
        {
            return;
        }

        // TEDClass is the game's own classification -- TANK, KBOT, VTOL, SHIP,
        // FORT, PLANT, ENERGY, METAL, CNSTR, WATER, SPECIAL, COMMANDER -- so
        // the grouping is the data's rather than one invented here. It is
        // written with stray whitespace in a few files, hence the trim.
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> byCategory;
        for (const auto& [unitType, definition] : simulation.unitDefinitions)
        {
            auto category = toUpper(definition.tedClass);
            auto first = category.find_first_not_of(" \t");
            auto last = category.find_last_not_of(" \t");
            category = first == std::string::npos ? std::string() : category.substr(first, last - first + 1);
            if (category.empty())
            {
                category = "UNCLASSIFIED";
            }

            auto name = definition.unitName.empty() ? unitType : definition.unitName;
            byCategory[category].emplace_back(unitType, name);
        }

        for (auto& [category, units] : byCategory)
        {
            std::sort(units.begin(), units.end(), [](const auto& a, const auto& b) {
                return a.second < b.second;
            });
            unitTypesByCategory.emplace_back(category, std::move(units));
        }
    }

    void GameScene::renderUnitSpawnerWindow()
    {
        if (!showUnitSpawnerWindow)
        {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(420.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Unit spawner", &showUnitSpawnerWindow))
        {
            ImGui::End();
            return;
        }

        buildUnitTypeCategories();

        // Owner, named with its side so it is obvious which team the unit
        // will fight for.
        std::string ownerLabel = "none";
        if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
        {
            const auto& player = simulation.players[unitSpawnPlayer];
            ownerLabel = std::to_string(unitSpawnPlayer) + ": " + player.side
                + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                + (PlayerId(unitSpawnPlayer) == localPlayerId ? " [you]" : "");
        }
        if (ImGui::BeginCombo("Owner", ownerLabel.c_str()))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                const auto& player = simulation.players[i];
                auto label = std::to_string(i) + ": " + player.side
                    + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                    + (PlayerId(i) == localPlayerId ? " [you]" : "");
                if (ImGui::Selectable(label.c_str(), unitSpawnPlayer == static_cast<int>(i)))
                {
                    unitSpawnPlayer = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filter", unitSpawnFilter, IM_ARRAYSIZE(unitSpawnFilter));
        auto filter = toUpper(std::string(unitSpawnFilter));

        ImGui::BeginChild("spawner unit list", ImVec2(0.0f, 300.0f), true);
        for (const auto& [category, units] : unitTypesByCategory)
        {
            // Which units survive the filter decides whether the category is
            // worth showing at all, so a search for "solar" does not leave
            // eleven empty headers to open.
            std::vector<const std::pair<std::string, std::string>*> matching;
            for (const auto& unit : units)
            {
                if (filter.empty()
                    || unit.first.find(filter) != std::string::npos
                    || toUpper(unit.second).find(filter) != std::string::npos)
                {
                    matching.push_back(&unit);
                }
            }
            if (matching.empty())
            {
                continue;
            }

            // Open by default while filtering: the point of typing is to see
            // what matched, not to then go opening headers.
            ImGui::SetNextItemOpen(!filter.empty(), ImGuiCond_Always);
            auto header = category + " (" + std::to_string(matching.size()) + ")";
            if (ImGui::TreeNode(header.c_str()))
            {
                for (const auto* unit : matching)
                {
                    // The name first, because that is what a person is looking
                    // for, and the code after it, because that is what the
                    // logs and the console use.
                    auto label = unit->second + "  [" + unit->first + "]";
                    if (ImGui::Selectable(label.c_str(), unit->first == unitSpawnType))
                    {
                        unitSpawnType = unit->first;
                    }
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        if (unitSpawnType.empty())
        {
            ImGui::TextUnformatted("Nothing selected");
        }
        else
        {
            const auto& definition = simulation.unitDefinitions.at(unitSpawnType);
            ImGui::Text("%s [%s]", definition.unitName.empty() ? unitSpawnType.c_str() : definition.unitName.c_str(), unitSpawnType.c_str());
            if (!definition.unitDescription.empty())
            {
                ImGui::TextWrapped("%s", definition.unitDescription.c_str());
            }
            ImGui::Text("%u hit points", definition.maxHitPoints);
        }

        ImGui::Separator();

        ImGui::SliderInt("Health %", &unitSpawnHealthPercent, 1, 100);
        if (!unitSpawnType.empty())
        {
            const auto& definition = simulation.unitDefinitions.at(unitSpawnType);
            ImGui::Text("spawns on %u of %u", debugSpawnHitPoints(definition), definition.maxHitPoints);
        }

        ImGui::Checkbox("Place on click (right-click to stop)", &unitSpawnOnClick);
        ImGui::Checkbox("Place finished (off: place a nanoframe)", &unitSpawnComplete);
        if (!unitSpawnComplete)
        {
            ImGui::TextUnformatted("(health applies to finished units only)");
        }

        if (ImGui::Button("Place one at the cursor"))
        {
            if (auto terrainPos = getMouseTerrainCoordinate())
            {
                placeDebugUnit(*terrainPos);
            }
        }

        ImGui::End();
    }

    void GameScene::placeDebugUnit(const SimVector& position)
    {
        if (unitSpawnType.empty() || !isValidUnitType(simulation, unitSpawnType))
        {
            return;
        }
        if (unitSpawnPlayer < 0 || unitSpawnPlayer >= getSize(simulation.players))
        {
            return;
        }

        // The list offers every unit type the data defines, and some of those
        // name a model or a script that did not load. Spawning one of those
        // would take the game down, so refuse it here: this is a tool for
        // poking at units, not a way to crash out of a game.
        const auto& unitDefinition = simulation.unitDefinitions.at(unitSpawnType);
        if (simulation.unitModelDefinitions.find(unitDefinition.objectName) == simulation.unitModelDefinitions.end())
        {
            LOG_WARN << "Cannot place " << unitSpawnType << ": its model " << unitDefinition.objectName << " is not loaded";
            return;
        }
        if (simulation.unitScriptDefinitions.find(unitSpawnType) == simulation.unitScriptDefinitions.end())
        {
            LOG_WARN << "Cannot place " << unitSpawnType << ": its script is not loaded";
            return;
        }

        auto owner = PlayerId(unitSpawnPlayer);
        if (unitSpawnComplete)
        {
            if (auto unit = spawnCompletedUnit(unitSpawnType, owner, position))
            {
                // A nanoframe's hit points track its build progress, so this
                // only means anything for a finished unit; the window says so.
                unit->get().hitPoints = debugSpawnHitPoints(unitDefinition);
            }
        }
        else
        {
            // Left as a nanoframe, so a builder can be told to finish it.
            spawnUnit(unitSpawnType, owner, position, std::nullopt);
        }
    }

    void GameScene::renderUnitPlacer()
    {
        if (!ImGui::CollapsingHeader("Place units"))
        {
            return;
        }
        ImGui::Indent();

        if (allUnitTypes.empty())
        {
            for (const auto& [unitType, _] : simulation.unitDefinitions)
            {
                allUnitTypes.push_back(unitType);
            }
            std::sort(allUnitTypes.begin(), allUnitTypes.end());
        }

        // Owner: every player in the game, named with its side so it is
        // obvious which team a unit will fight for.
        std::string ownerLabel = "none";
        if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
        {
            const auto& player = simulation.players[unitSpawnPlayer];
            ownerLabel = std::to_string(unitSpawnPlayer) + ": " + player.side
                + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                + (PlayerId(unitSpawnPlayer) == localPlayerId ? " [you]" : "");
        }
        if (ImGui::BeginCombo("Owner", ownerLabel.c_str()))
        {
            for (Index i = 0; i < getSize(simulation.players); ++i)
            {
                const auto& player = simulation.players[i];
                auto label = std::to_string(i) + ": " + player.side
                    + (player.type == GamePlayerType::Human ? " (human)" : " (computer)")
                    + (PlayerId(i) == localPlayerId ? " [you]" : "");
                if (ImGui::Selectable(label.c_str(), unitSpawnPlayer == static_cast<int>(i)))
                {
                    unitSpawnPlayer = static_cast<int>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputText("Filter", unitSpawnFilter, IM_ARRAYSIZE(unitSpawnFilter));
        auto filter = toUpper(std::string(unitSpawnFilter));

        ImGui::BeginChild("unit type list", ImVec2(0.0f, 160.0f), true);
        for (const auto& unitType : allUnitTypes)
        {
            if (!filter.empty() && unitType.find(filter) == std::string::npos)
            {
                continue;
            }
            if (ImGui::Selectable(unitType.c_str(), unitType == unitSpawnType))
            {
                unitSpawnType = unitType;
            }
        }
        ImGui::EndChild();

        ImGui::LabelText("Selected", "%s", unitSpawnType.empty() ? "none" : unitSpawnType.c_str());
        ImGui::Checkbox("Place on click (right-click to stop)", &unitSpawnOnClick);
        ImGui::Checkbox("Place finished (off: place a nanoframe)", &unitSpawnComplete);

        if (ImGui::Button("Place one at the cursor"))
        {
            if (auto terrainPos = getMouseTerrainCoordinate())
            {
                placeDebugUnit(*terrainPos);
            }
        }

        // The old typed entry, kept for when the name is already known.
        if (ImGui::InputText("Type a name and press enter", unitSpawnText, IM_ARRAYSIZE(unitSpawnText), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            auto text = toUpper(std::string(unitSpawnText));
            if (!text.empty() && isValidUnitType(simulation, text))
            {
                unitSpawnType = text;
                if (auto terrainPos = getMouseTerrainCoordinate())
                {
                    placeDebugUnit(*terrainPos);
                }
            }
            ImGui::SetKeyboardFocusHere(-1);
        }

        if (ImGui::Button("Kill every unit of the chosen owner"))
        {
            if (unitSpawnPlayer >= 0 && unitSpawnPlayer < getSize(simulation.players))
            {
                std::vector<UnitId> doomed;
                for (const auto& [unitId, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(PlayerId(unitSpawnPlayer)) && unit.isAlive())
                    {
                        doomed.push_back(unitId);
                    }
                }
                for (auto unitId : doomed)
                {
                    simulation.killUnit(unitId);
                }
            }
        }

        ImGui::Unindent();
    }

    std::optional<std::reference_wrapper<UnitState>> GameScene::spawnCompletedUnit(const std::string& unitType, PlayerId owner, const SimVector& position)
    {
        auto unitId = spawnUnit(unitType, owner, position, std::nullopt);
        if (unitId)
        {
            auto& unit = getUnit(*unitId);
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            // units start as unbuilt nanoframes,
            // we we need to convert it immediately into a completed unit.
            unit.finishBuilding(unitDefinition);

            return unit;
        }

        return std::nullopt;
    }

    void GameScene::setCameraPosition(const Vector3f& newPosition)
    {
        auto cameraConstraint = computeCameraConstraint(simulation.terrain, worldCameraState.scaleDimension(worldViewport.width()), worldCameraState.scaleDimension(worldViewport.height()));
        auto constrainedPosition = cameraConstraint.clamp(Vector2f(newPosition.x, newPosition.z));
        worldCameraState.position = Vector3f(constrainedPosition.x, newPosition.y, constrainedPosition.y);
    }

    const MapTerrain& GameScene::getTerrain() const
    {
        return simulation.terrain;
    }

    GameTime
    GameScene::getGameTime() const
    {
        return simulation.gameTime;
    }

    void GameScene::playUiSound(const AudioService::SoundHandle& handle)
    {
        sceneContext.audioService->playSoundIfFree(handle, UnitSelectChannel);
    }

    void GameScene::playNotificationSound(const PlayerId& playerId, const AudioService::SoundHandle& sound)
    {
        if (playerId == localPlayerId)
        {
            sceneContext.audioService->playSoundIfFree(sound, UnitSelectChannel);
        }
    }


    std::optional<std::string> getSoundName(const SoundClass& c, UnitSoundType sound)
    {
        switch (sound)
        {
            case UnitSoundType::Select1:
                return c.select1;
            case UnitSoundType::UnitComplete:
                return c.unitComplete;
            case UnitSoundType::Activate:
                return c.activate;
            case UnitSoundType::Deactivate:
                return c.deactivate;
            case UnitSoundType::Ok1:
                return c.ok1;
            case UnitSoundType::Arrived1:
                return c.arrived1;
            case UnitSoundType::Cant1:
                return c.cant1;
            case UnitSoundType::UnderAttack:
                return c.underAttack;
            case UnitSoundType::Build:
                return c.build;
            case UnitSoundType::Repair:
                return c.repair;
            case UnitSoundType::Working:
                return c.working;
            case UnitSoundType::Cloak:
                return c.cloak;
            case UnitSoundType::Uncloak:
                return c.uncloak;
            case UnitSoundType::Capture:
                return c.capture;
            case UnitSoundType::Count5:
                return c.count5;
            case UnitSoundType::Count4:
                return c.count4;
            case UnitSoundType::Count3:
                return c.count3;
            case UnitSoundType::Count2:
                return c.count2;
            case UnitSoundType::Count1:
                return c.count1;
            case UnitSoundType::Count0:
                return c.count0;
            case UnitSoundType::CancelDestruct:
                return c.cancelDestruct;
            default:
                throw std::logic_error("Invalid sound type");
        }
    }

    std::optional<AudioService::SoundHandle> getSound(const GameSimulation& sim, const GameMediaDatabase& meshDb, const std::string& unitType, UnitSoundType soundType)
    {
        const auto& unitDefinition = sim.unitDefinitions.at(unitType);
        const auto& soundClass = meshDb.getSoundClassOrDefault(unitDefinition.soundCategory);
        const auto& soundId = getSoundName(soundClass, soundType);
        if (soundId)
        {
            return meshDb.tryGetSoundHandle(*soundId);
        }
        return std::nullopt;
    }

    void GameScene::playUnitNotificationSound(const PlayerId& playerId, const std::string& unitType, UnitSoundType soundType)
    {
        // SOUNDS.GUI's Unit Sounds setting. Off silences the chatter
        // entirely; Medium keeps only what a player needs to hear -- the
        // warnings and the completions -- and drops the acknowledgements.
        // (Which sounds sit in "medium" is inference: the original's own
        // split has not been read out of the binary.)
        if (unitSpeechSetting == UnitSpeechLevel::Off)
        {
            return;
        }
        if (unitSpeechSetting == UnitSpeechLevel::Medium)
        {
            switch (soundType)
            {
                case UnitSoundType::Select1:
                case UnitSoundType::Ok1:
                case UnitSoundType::Arrived1:
                    return;
                default:
                    break;
            }
        }

        auto sound = getSound(simulation, gameMediaDatabase, unitType, soundType);
        if (sound)
        {
            playNotificationSound(playerId, *sound);
        }
    }

    namespace
    {
        /**
         * TA's ten player colours, read off the shipped palette by eye --
         * close enough for tinting a line of text.
         */
        Color playerColorToRgb(const PlayerColorIndex& index)
        {
            static const Color colors[] = {
                Color(60, 88, 244),   // blue
                Color(228, 32, 32),   // red
                Color(252, 252, 252), // white
                Color(24, 208, 24),   // green
                Color(44, 60, 148),   // navy
                Color(180, 72, 180),  // purple
                Color(252, 252, 0),   // yellow
                Color(96, 96, 96),    // black, lifted so it still reads
                Color(128, 192, 252), // sky
                Color(240, 160, 40),  // orange
            };
            return index.value < 10 ? colors[index.value] : Color(255, 255, 255);
        }
    }

    void GameScene::printConsole(const std::string& text, const Color& color)
    {
        // Five seconds a line, and never more than eight on screen.
        consoleMessages.push_back(ConsoleMessage{text, color, sceneTime + SceneTime(5u * 30u)});
        while (consoleMessages.size() > 8)
        {
            consoleMessages.pop_front();
        }
    }

    void GameScene::renderConsole()
    {
        while (!consoleMessages.empty() && consoleMessages.front().expires <= sceneTime)
        {
            consoleMessages.pop_front();
        }

        // Top-left of the world view, under the resource bar, newest line at
        // the bottom -- where the original prints its speech text, and in the
        // font it prints it in: COMIX, the taller of its two game fonts.
        float y = static_cast<float>(GuiSizeTop) + 16.0f;
        for (const auto& message : consoleMessages)
        {
            chromeUiRenderService.drawText(static_cast<float>(GuiSizeLeft) + 8.0f, y, message.text, *speechFont, message.color);
            y += 14.0f;
        }
    }

    void GameScene::updateSelfDestructNotifications()
    {
        // "Commander: five", printed and spoken a number a second. The words
        // zero..five sit beside the count0-count5 speech keys in the binary
        // with the format "%s: %s" under a "Speech Text" label, and SOUND.TDF
        // maps them backwards -- count5 plays the file COUNT1 -- because the
        // recordings are numbered by their position in the countdown, not by
        // the number they say.
        static const char* const countWords[] = {"zero", "one", "two", "three", "four", "five"};

        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unit.isOwnedBy(localPlayerId))
            {
                continue;
            }

            auto it = selfDestructAnnounced.find(unitId);
            if (!unit.isAlive() || !unit.selfDestructTime)
            {
                if (it != selfDestructAnnounced.end())
                {
                    selfDestructAnnounced.erase(it);
                    if (unit.isAlive())
                    {
                        // Toggled off, not gone off.
                        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                        printConsole(unitDefinition.unitName + ": Self destruct terminated");
                        playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::CancelDestruct);
                    }
                }
                continue;
            }

            auto ticksLeft = *unit.selfDestructTime > simulation.gameTime
                ? (*unit.selfDestructTime - simulation.gameTime).value
                : 0u;
            auto secondsLeft = (ticksLeft + SimTicksPerSecond - 1) / SimTicksPerSecond;
            if (it != selfDestructAnnounced.end() && it->second == secondsLeft)
            {
                continue;
            }
            selfDestructAnnounced[unitId] = secondsLeft;

            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            auto word = secondsLeft < 6 ? std::string(countWords[secondsLeft]) : std::to_string(secondsLeft);
            printConsole(unitDefinition.unitName + ": " + word);

            std::optional<UnitSoundType> countSound;
            switch (secondsLeft)
            {
                case 5: countSound = UnitSoundType::Count5; break;
                case 4: countSound = UnitSoundType::Count4; break;
                case 3: countSound = UnitSoundType::Count3; break;
                case 2: countSound = UnitSoundType::Count2; break;
                case 1: countSound = UnitSoundType::Count1; break;
                default: break;
            }
            if (countSound)
            {
                playUnitNotificationSound(unit.owner, unit.unitType, *countSound);
            }
        }

        // Ids are reused, so entries for units that no longer exist must go.
        for (auto it = selfDestructAnnounced.begin(); it != selfDestructAnnounced.end();)
        {
            if (!simulation.units.tryGet(it->first))
            {
                it = selfDestructAnnounced.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void GameScene::updateDefeatNotifications()
    {
        // "Arm forces have been obliterated", in the fallen side and colour.
        // The original keeps a family of these -- "forces have gone to a
        // better place", "vermin have been exterminated" -- but obliterated
        // is the one everybody remembers.
        for (unsigned int i = 0; i < simulation.players.size(); ++i)
        {
            const auto& player = simulation.players[i];
            if (player.status != GamePlayerStatus::Dead || defeatAnnounced.count(i) != 0)
            {
                continue;
            }
            defeatAnnounced.insert(i);

            auto side = player.side;
            std::transform(side.begin() + 1, side.end(), side.begin() + 1, [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            printConsole(side + " forces have been obliterated", playerColorToRgb(player.color));
        }
    }

    namespace
    {
        /**
         * The default track types, decoded from the exe: when the original
         * recognises the game disc it types MCI tracks 1-7 Battle and 8-16
         * Building (0x42F7xx area), and the GOG shim plays music/<n>.mp3 by
         * raw track number. Matching the GOG rips to the tagged soundtrack by
         * duration gives these names. A file the table does not know plays as
         * Building; the title theme -- which the game itself never plays, it
         * belongs to the intro -- is left out entirely.
         */
        bool isBattleTrackName(const std::string& lowerName)
        {
            static const char* const battleNames[] = {
                "brutal battle",
                "fire and ice",
                "attack",
                "warpath",
                "march unto death",
                "ambush in the passage",
            };
            for (const auto* name : battleNames)
            {
                if (lowerName.find(name) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
    }

    void GameScene::addBattlePoints(int points)
    {
        battlePointsRing[battleRingCursor] += points;
    }

    void GameScene::updateMusic()
    {
        if (!musicPlaylistBuilt)
        {
            musicPlaylistBuilt = true;
            for (const auto& path : sceneContext.audioService->getMusicPlaylist())
            {
                auto lower = path;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find("theme") != std::string::npos)
                {
                    continue;
                }
                (isBattleTrackName(lower) ? battleTracks : buildingTracks).push_back(path);
            }
            // A one-sided soundtrack plays whatever it has in both moods.
            if (battleTracks.empty())
            {
                battleTracks = buildingTracks;
            }
            if (buildingTracks.empty())
            {
                buildingTracks = battleTracks;
            }
        }
        if (buildingTracks.empty() || !sceneContext.audioService->isMusicEnabled())
        {
            return;
        }

        // The evaluator runs once a game second, like the original's.
        auto second = simulation.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond);
        if (second != lastMusicSecond)
        {
            lastMusicSecond = second;
            battleRingCursor = (battleRingCursor + 1) % battlePointsRing.size();
            battlePointsRing[battleRingCursor] = 0;

            int sum30 = 0;
            for (auto v : battlePointsRing)
            {
                sum30 += v;
            }
            int sum5 = 0;
            for (unsigned int i = 0; i < 5; ++i)
            {
                sum5 += battlePointsRing[(battleRingCursor + battlePointsRing.size() - i) % battlePointsRing.size()];
            }

            if (!musicFadeTarget && simulation.gameTime >= musicLockoutUntil)
            {
                if (musicSituation == MusicSituation::Building)
                {
                    // The unit-count gate is the original's: with thirty or
                    // fewer units the fight is not big enough for war drums.
                    unsigned int owned = 0;
                    for (const auto& [_, unit] : simulation.units)
                    {
                        if (unit.isAlive() && unit.isOwnedBy(localPlayerId))
                        {
                            ++owned;
                        }
                    }
                    if ((sum30 > 50 || sum5 > 30) && owned > 30)
                    {
                        musicFadeTarget = MusicSituation::Battle;
                    }
                }
                else
                {
                    auto inBattleFor = simulation.gameTime.value - battleEnteredTime.value;
                    if (sum30 < 10 && sum5 == 0 && inBattleFor >= 60u * SimTicksPerSecond)
                    {
                        musicFadeTarget = MusicSituation::Building;
                    }
                }
            }
        }

        // A switch fades the old track out over about 1.2 seconds, the
        // original's -vol/18 every other tick.
        if (musicFadeTarget)
        {
            musicFade -= 1.0f / 36.0f;
            if (musicFade <= 0.0f || !sceneContext.audioService->musicPlaying())
            {
                sceneContext.audioService->stopMusic();
                musicSituation = *musicFadeTarget;
                musicFadeTarget = std::nullopt;
                musicFade = 1.0f;
                sceneContext.audioService->setMusicFadeScale(1.0f);
                musicLockoutUntil = simulation.gameTime + GameTime(10u * SimTicksPerSecond);
                musicBag.clear();
                if (musicSituation == MusicSituation::Battle)
                {
                    battleEnteredTime = simulation.gameTime;
                }
                else
                {
                    // Coming down from battle gets four seconds of quiet.
                    musicHoldOffUntil = simulation.gameTime + GameTime(4u * SimTicksPerSecond);
                }
            }
            else
            {
                sceneContext.audioService->setMusicFadeScale(musicFade);
            }
            return;
        }

        if (sceneContext.audioService->musicPlaying() || simulation.gameTime < musicHoldOffUntil)
        {
            return;
        }

        // Draw the next track of the current mood from a bag, so everything
        // of that type plays before anything repeats.
        const auto& tracks = musicSituation == MusicSituation::Battle ? battleTracks : buildingTracks;
        if (musicBag.empty())
        {
            musicBag = tracks;
            for (auto i = musicBag.size(); i > 1; --i)
            {
                std::swap(musicBag[i - 1], musicBag[effectsRng() % i]);
            }
            if (musicBag.size() > 1 && musicBag.back() == lastMusicTrack)
            {
                std::swap(musicBag.back(), musicBag.front());
            }
        }
        if (musicBag.empty())
        {
            return;
        }

        auto next = musicBag.back();
        musicBag.pop_back();
        if (sceneContext.audioService->playMusic(next, false))
        {
            lastMusicTrack = next;
        }
        else
        {
            buildingTracks.erase(std::remove(buildingTracks.begin(), buildingTracks.end(), next), buildingTracks.end());
            battleTracks.erase(std::remove(battleTracks.begin(), battleTracks.end(), next), battleTracks.end());
        }
    }

    void GameScene::updateCloakNotifications()
    {
        // Cloaking and decloaking are the only thing about a cloak the original
        // tells anyone about. `0x48B173` raises notification 0xE the tick the
        // flag comes on and `0x48B1A5` raises 0xF when it goes off; the table
        // at `0x5086E8` pairs those with the sounds `cloak` and `uncloak` and
        // the captions "Cloaked" and "Visible". None of it is a COB event --
        // no shipped script of a cloakable unit has a function for either --
        // and none of it is drawn on the unit. The captions go to the console
        // in the corner, the same place the original prints them; the sounds
        // were parsed and loaded all along and simply never played.
        for (const auto& [unitId, unit] : simulation.units)
        {
            auto wasCloaked = cloakedUnits.find(unitId) != cloakedUnits.end();
            if (unit.cloaked == wasCloaked)
            {
                continue;
            }

            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (unit.cloaked)
            {
                cloakedUnits.insert(unitId);
                if (unit.isOwnedBy(localPlayerId))
                {
                    printConsole(unitDefinition.unitName + ": Cloaked");
                }
                playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::Cloak);
            }
            else
            {
                cloakedUnits.erase(unitId);
                if (unit.isOwnedBy(localPlayerId))
                {
                    printConsole(unitDefinition.unitName + ": Visible");
                }
                playUnitNotificationSound(unit.owner, unit.unitType, UnitSoundType::Uncloak);
            }
        }

        // A unit killed while cloaked leaves its id behind, and ids are reused,
        // so a later unit would start out believed to be cloaked already and
        // never announce itself.
        for (auto it = cloakedUnits.begin(); it != cloakedUnits.end();)
        {
            it = simulation.unitExists(*it) ? std::next(it) : cloakedUnits.erase(it);
        }
    }

    void GameScene::playSoundAt(const Vector3f& /*position*/, const AudioService::SoundHandle& sound)
    {
        // FIXME: should play on a position-aware channel
        auto channel = sceneContext.audioService->playSound(sound);
        if (channel < 0)
        {
            return;
        }
        int volume;
        {
            std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
            playingUnitChannels.insert(channel);
            volume = computeSoundVolume(playingUnitChannels.size());
        }
        sceneContext.audioService->setVolume(channel, volume);
    }

    void GameScene::playWeaponStartSound(const Vector3f& position, const std::string& weaponType)
    {

        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        if (weaponMediaInfo.soundStart)
        {
            auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundStart);
            if (sound)
            {
                playSoundAt(position, *sound);
            }
        }
    }

    void GameScene::playWeaponImpactSound(const Vector3f& position, const std::string& weaponType, ImpactType impactType)
    {
        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        switch (impactType)
        {
            case ImpactType::Normal:
            {
                if (weaponMediaInfo.soundHit)
                {
                    auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundHit);
                    if (sound)
                    {
                        playSoundAt(position, *sound);
                    }
                }
                break;
            }
            case ImpactType::Water:
            {
                if (weaponMediaInfo.soundWater)
                {
                    auto sound = gameMediaDatabase.tryGetSoundHandle(*weaponMediaInfo.soundWater);
                    if (sound)
                    {
                        playSoundAt(position, *sound);
                    }
                }
                break;
            }
        }
    }

    void GameScene::spawnWeaponImpactExplosion(const Vector3f& position, const std::string& weaponType, ImpactType impactType, bool positionVisible)
    {
        const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(weaponType);
        auto effects = computeWeaponImpactEffects(weaponMediaInfo, impactType, positionVisible);

        if (effects.explosion)
        {
            spawnExplosion(position, *effects.explosion);
        }
        if (effects.smoke)
        {
            createLightSmoke(position);
        }
        if (effects.flash)
        {
            spawnFlash(position);
        }
    }

    void GameScene::onChannelFinished(int channel)
    {
        std::scoped_lock<std::mutex> lock(playingUnitChannelsLock);
        playingUnitChannels.erase(channel);
    }

    Matrix4f GameScene::worldToMinimapMatrix(const MapTerrain& terrain, const Rectangle2f& minimapRect)
    {
        auto view = Matrix4f::rotationToAxes(
            Vector3f(1.0f, 0.0f, 0.0f),
            Vector3f(0.0f, 0.0f, 1.0f),
            Vector3f(0.0f, -1.0f, 0.0f));
        auto cabinet = Matrix4f::cabinetProjection(0.0f, 0.5f);
        auto orthographic = Matrix4f::orthographicProjection(
            simScalarToFloat(terrain.leftInWorldUnits()),
            simScalarToFloat(terrain.rightCutoffInWorldUnits()),
            simScalarToFloat(terrain.bottomCutoffInWorldUnits()),
            simScalarToFloat(terrain.topInWorldUnits()),
            -1000.0f,
            1000.0f);
        auto worldProjection = orthographic * cabinet;
        auto minimapInverseProjection = Matrix4f::inverseOrthographicProjection(
            minimapRect.left(),
            minimapRect.right(),
            minimapRect.bottom(),
            minimapRect.top(),
            -1.0f,
            1.0f);
        return minimapInverseProjection * worldProjection * view;
    }

    Matrix4f GameScene::minimapToWorldMatrix(const MapTerrain& terrain, const Rectangle2f& minimapRect)
    {
        auto view = Matrix4f::rotationToAxes(
            Vector3f(1.0f, 0.0f, 0.0f),
            Vector3f(0.0f, 0.0f, 1.0f),
            Vector3f(0.0f, -1.0f, 0.0f));
        auto inverseView = view.transposed();
        auto inverseCabinet = Matrix4f::cabinetProjection(0.0f, -0.5f);
        auto inverseOrthographic = Matrix4f::inverseOrthographicProjection(
            simScalarToFloat(terrain.leftInWorldUnits()),
            simScalarToFloat(terrain.rightCutoffInWorldUnits()),
            simScalarToFloat(terrain.bottomCutoffInWorldUnits()),
            simScalarToFloat(terrain.topInWorldUnits()),
            -1000.0f,
            1000.0f);
        auto worldInverseProjection = inverseCabinet * inverseOrthographic;
        auto minimapProjection = Matrix4f::orthographicProjection(
            minimapRect.left(),
            minimapRect.right(),
            minimapRect.bottom(),
            minimapRect.top(),
            -1.0f,
            1.0f);
        return inverseView * worldInverseProjection * minimapProjection;
    }

    void GameScene::tryTickGame()
    {
        if (!playerCommandService->checkHashes())
        {
            std::ofstream dumpFile;
            dumpFile.open("rwe-dump-" + std::to_string(std::rand()) + ".json");
            dumpFile << dumpJson(simulation);
            dumpFile.close();
            throw std::runtime_error("Desync detected");
        }

        auto playerCommands = playerCommandService->tryPopCommands();
        if (!playerCommands)
        {
            LOG_ERROR << "Blocked waiting for player commands";
            return;
        }

        sceneTime += SceneTime(1);

        processActions();

        processPlayerCommands(*playerCommands);

        {
            RWE_RENDERPROF("u.simtick");
            simulation.tick();
        }

        GameHash gameHash{0};
        {
            RWE_RENDERPROF("u.hash");
            gameHash = simulation.computeHash();
        }
        playerCommandService->pushHash(localPlayerId, gameHash);
        gameNetworkService->submitGameHash(gameHash);

        if (stateLogStream)
        {
            *stateLogStream << dumpJson(simulation) << std::endl;
        }

        {
            RWE_RENDERPROF("u.events");
            processSimEvents();
        }

        updateCloakNotifications();

        updateSelfDestructNotifications();

        updateDefeatNotifications();

        updateProjectiles();

        updateFlashes();

        updateScreenShake();

        {
            RWE_RENDERPROF("u.particles");
            updateParticles(gameMediaDatabase, simulation.terrain, simulation.gameTime, particles);
        }

        {
            RWE_RENDERPROF("u.spawnnano");
            spawnNanoParticles();
        }

        spawnGeoVentSteam();

        updateDebris();

        updateBuildBoxAppearances();

        // Testing aid: RWE_DEBUG_SPAWN=<unitType>*<count>@<player>:<seconds>
        // drops finished units of that type, owned by that player, in a ring
        // around the local player's first unit at that game time (for example
        // CORAK*6@1:10 to have six AKs attack the commander at ten seconds).
        if (const char* debugSpawn = std::getenv("RWE_DEBUG_SPAWN"))
        {
            std::string spec(debugSpawn);
            auto star = spec.find('*');
            auto at = spec.find('@');
            auto colon = spec.find(':');
            if (star != std::string::npos && at != std::string::npos && colon != std::string::npos)
            {
                auto unitType = spec.substr(0, star);
                auto count = std::atoi(spec.substr(star + 1, at - star - 1).c_str());
                auto player = std::atoi(spec.substr(at + 1, colon - at - 1).c_str());
                auto rest = spec.substr(colon + 1);
                auto nearSep = rest.find(':');
                auto seconds = static_cast<unsigned int>(std::atoi(rest.substr(0, nearSep).c_str()));
                // An optional trailing :<player> puts the ring around that player's first unit instead.
                auto nearPlayer = nearSep == std::string::npos ? PlayerId(localPlayerId) : PlayerId(static_cast<unsigned int>(std::atoi(rest.substr(nearSep + 1).c_str())));
                if (seconds > 0 && simulation.gameTime.value == seconds * static_cast<unsigned int>(SimTicksPerSecond) && isValidUnitType(simulation, unitType) && player >= 0 && player < getSize(simulation.players))
                {
                    std::optional<SimVector> centre;
                    for (const auto& [unitId, unit] : simulation.units)
                    {
                        if (unit.isOwnedBy(nearPlayer) && unit.isAlive())
                        {
                            centre = unit.position;
                            break;
                        }
                    }
                    for (int i = 0; centre && i < count; ++i)
                    {
                        auto angle = (2.0f * Pif * static_cast<float>(i)) / static_cast<float>(std::max(1, count));
                        SimVector position(centre->x + SimScalar(std::cos(angle) * 96.0f), centre->y, centre->z + SimScalar(std::sin(angle) * 96.0f));
                        position.y = simulation.terrain.getHeightAt(position.x, position.z);
                        LOG_INFO << "Debug: spawning " << unitType << " for player " << player << " at " << simScalarToFloat(position.x) << "," << simScalarToFloat(position.z);
                        spawnCompletedUnit(unitType, PlayerId(static_cast<unsigned int>(player)), position);
                    }
                }
            }
        }

        // Testing aid: RWE_DEBUG_SELF_DESTRUCT=<seconds> self-destructs a
        // player's first unit (the commander) at that game time, so a crash on
        // commander death can be reproduced under a debugger. The player is
        // the local one unless RWE_DEBUG_SELF_DESTRUCT_PLAYER gives an index.
        if (const char* debugSelfDestruct = std::getenv("RWE_DEBUG_SELF_DESTRUCT"))
        {
            auto seconds = static_cast<unsigned int>(std::atoi(debugSelfDestruct));
            if (seconds > 0 && simulation.gameTime.value == seconds * static_cast<unsigned int>(SimTicksPerSecond))
            {
                auto player = localPlayerId;
                if (const char* debugPlayer = std::getenv("RWE_DEBUG_SELF_DESTRUCT_PLAYER"))
                {
                    player = PlayerId(static_cast<unsigned int>(std::atoi(debugPlayer)));
                }
                for (const auto& [unitId, unit] : simulation.units)
                {
                    if (unit.isOwnedBy(player) && unit.isAlive())
                    {
                        LOG_INFO << "Debug: self-destructing unit " << unitId.value << " of player " << player.value;
                        if (player == localPlayerId)
                        {
                            localPlayerSelfDestructUnit(unitId);
                        }
                        else
                        {
                            simulation.toggleSelfDestruct(unitId);
                        }
                        break;
                    }
                }
            }
        }

        // A game needs an opponent before it can be decided; a lone player
        // is just exploring the map.
        if (!gameOver && simulation.players.size() >= 2)
        {
            auto winStatus = simulation.computeWinStatus();
            match(
                winStatus,
                [&](const WinStatusWon& w) {
                    gameOver = winStatus;
                    gameOverTime = simulation.gameTime;
                    LOG_INFO << "Game over: player " << w.winner.value << " won at tick " << simulation.gameTime.value;
                },
                [&](const WinStatusDraw&) {
                    gameOver = winStatus;
                    gameOverTime = simulation.gameTime;
                    LOG_INFO << "Game over: draw at tick " << simulation.gameTime.value;
                },
                [&](const WinStatusUndecided&) {
                    // do nothing, game still in progress
                });
        }
    }

    std::optional<UnitId> GameScene::getUnitUnderCursor() const
    {
        if (isCursorOverMinimap())
        {
            auto mousePos = getMousePosition();

            auto worldToMinimap = worldToMinimapMatrix(simulation.terrain, minimapRect);

            for (const auto& [unitId, unit] : simulation.units)
            {
                // Only what the minimap actually shows can be picked: your own
                // units and enemies you can see or have on radar. Asking the
                // same predicate the dots are drawn with, rather than the
                // simulation's own detection test, is what makes that true.
                if (!unitIsDetectableByLocalPlayer(unitId, unit))
                {
                    continue;
                }

                // convert to minimap rect
                auto minimapPos = worldToMinimap * simVectorToFloat(unit.position);
                minimapPos.x = std::floor(minimapPos.x);
                minimapPos.y = std::floor(minimapPos.y);
                auto ownerId = unit.owner;
                auto colorIndex = getPlayer(ownerId).color;
                const auto& sprite = *minimapDots->sprites[colorIndex.value];
                auto bounds = sprite.bounds;

                // test cursor against the rect
                Vector2f mousePosFloat(static_cast<float>(mousePos.x) + 0.5f, static_cast<float>(mousePos.y) + 0.5f);
                if (bounds.contains(mousePosFloat - minimapPos.xy()))
                {
                    return unitId;
                }
            }

            return std::nullopt;
        }

        if (isCursorOverWorld())
        {
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            return getFirstCollidingUnit(ray);
        }

        return std::nullopt;
    }

    std::optional<FeatureId> GameScene::getFeatureUnderCursor() const
    {
        if (!isCursorOverWorld())
        {
            return std::nullopt;
        }

        auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
        return getFirstCollidingFeature(ray);
    }

    Vector2f GameScene::screenToWorldClipSpace(Point p) const
    {
        return worldViewport.toClipSpace(sceneContext.viewport->toOtherViewport(worldViewport, p));
    }

    bool GameScene::isCursorOverMinimap() const
    {
        auto mousePos = getMousePosition();
        return minimapRect.contains(mousePos.x, mousePos.y);
    }

    bool GameScene::isCursorOverWorld() const
    {
        return worldViewport.contains(getMousePosition());
    }

    Point GameScene::getMousePosition() const
    {
        float fx;
        float fy;
        sceneContext.sdl->getMouseState(&fx, &fy);
        return Point(static_cast<int>(fx), static_cast<int>(fy));
    }

    std::optional<UnitId> GameScene::getFirstCollidingUnit(const Ray3f& ray) const
    {
        auto winnerIsMobile = false;
        auto bestDistance = std::numeric_limits<float>::infinity();
        std::optional<UnitId> it;

        for (const auto& entry : simulation.units)
        {
            if (!unitIsVisibleToLocalPlayer(entry.first, entry.second))
            {
                // What cannot be seen cannot be clicked.
                continue;
            }
            if (entry.second.carriedBy)
            {
                // Cargo has no hitbox: clicks go to the transport carrying it.
                continue;
            }
            const auto& unitDefinition = simulation.unitDefinitions.at(entry.second.unitType);
            auto selectionMesh = gameMediaDatabase.getSelectionCollisionMesh(unitDefinition.objectName);
            auto distance = selectionIntersect(entry.second, *selectionMesh.value(), ray);
            auto isMobile = unitDefinition.isMobile;
            if (distance && ((!winnerIsMobile && isMobile) || distance < bestDistance))
            {
                winnerIsMobile = isMobile;
                bestDistance = *distance;
                it = entry.first;
            }
        }

        return it;
    }

    std::optional<FeatureId> GameScene::getFirstCollidingFeature(const Ray3f& ray) const
    {
        auto intersect = simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
        if (!intersect)
        {
            return std::nullopt;
        }

        auto heightmapPosition = simulation.terrain.worldToHeightmapCoordinate(*intersect);

        auto cellContents = simulation.occupiedGrid.tryGet(heightmapPosition);
        if (!cellContents)
        {
            return std::nullopt;
        }

        return cellContents->get().featureId;
    }

    std::optional<float> GameScene::selectionIntersect(const UnitState& unit, const CollisionMesh& mesh, const Ray3f& ray) const
    {
        auto inverseTransform = toFloatMatrix(unit.getInverseTransform());
        auto line = ray.toLine();
        Line3f modelSpaceLine(inverseTransform * line.start, inverseTransform * line.end);
        auto v = mesh.intersectLine(modelSpaceLine);
        if (!v)
        {
            return std::nullopt;
        }

        return ray.origin.distance(*v);
    }

    std::optional<SimVector> GameScene::getMouseTerrainCoordinate() const
    {
        if (isCursorOverMinimap())
        {
            auto transform = minimapToWorldMatrix(simulation.terrain, minimapRect);
            auto mousePos = getMousePosition();
            auto mouseX = static_cast<float>(mousePos.x) + 0.5f;
            auto mouseY = static_cast<float>(mousePos.y) + 0.5f;

            auto startPoint = transform * Vector3f(mouseX, mouseY, -1.0f);
            auto endPoint = transform * Vector3f(mouseX, mouseY, 1.0f);
            auto direction = endPoint - startPoint;
            Ray3f ray(startPoint, direction);
            return simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
        }

        if (isCursorOverWorld())
        {
            auto ray = screenToWorldRayUtil(computeInverseViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height()), screenToWorldClipSpace(getMousePosition()));
            return simulation.intersectLineWithTerrain(floatToSimLine(ray.toLine()));
        }

        return std::nullopt;
    }

    void GameScene::localPlayerIssueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto kind = PlayerUnitCommand::IssueOrder::IssueKind::Immediate;
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(order, kind)));

        if (std::holds_alternative<BuildOrder>(order))
        {
            if (sounds.okToBuild)
            {
                playUiSound(*sounds.okToBuild);
            }
        }
        else
        {
            const auto& unit = getUnit(unitId);
            auto handle = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Ok1);
            if (handle)
            {
                playUiSound(*handle);
            }
        }
    }

    void GameScene::localPlayerEnqueueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto kind = PlayerUnitCommand::IssueOrder::IssueKind::Queued;
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::IssueOrder(order, kind)));

        if (std::holds_alternative<BuildOrder>(order))
        {
            if (sounds.okToBuild)
            {
                playUiSound(*sounds.okToBuild);
            }
        }

        commandWasQueued = true;
    }

    void GameScene::localPlayerStopUnit(UnitId unitId)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::Stop()));

        const auto& unit = getUnit(unitId);
        auto handle = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Ok1);
        if (handle)
        {
            playUiSound(*handle);
        }
    }

    void GameScene::localPlayerSelfDestructUnit(UnitId unitId)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SelfDestruct()));
    }

    void GameScene::localPlayerSetFireOrders(UnitId unitId, UnitFireOrders orders)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetFireOrders{orders}));
    }

    void GameScene::localPlayerSetMovementOrders(UnitId unitId, UnitMovementOrders orders)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetMovementOrders{orders}));
    }

    bool GameScene::selectAllWhere(const std::function<bool(const UnitState&, const UnitDefinition&)>& predicate)
    {
        std::vector<UnitId> matches;
        for (const auto& [unitId, unit] : simulation.units)
        {
            if (!unit.isAlive() || !unit.isOwnedBy(localPlayerId))
            {
                continue;
            }
            if (predicate(unit, simulation.unitDefinitions.at(unit.unitType)))
            {
                matches.push_back(unitId);
            }
        }
        if (matches.empty())
        {
            return false;
        }
        clearUnitSelection();
        for (auto unitId : matches)
        {
            selectAdditionalUnit(unitId);
        }
        return true;
    }

    void GameScene::selectAllByCategoryToken(const std::string& token)
    {
        selectAllWhere([&token](const UnitState&, const UnitDefinition& d) {
            return categoryListContains(d.category, token);
        });
    }

    void GameScene::localPlayerSetOnOff(UnitId unitId, bool on)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetOnOff{on}));
    }

    void GameScene::localPlayerSetCloak(UnitId unitId, bool cloaked)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::SetCloak{cloaked}));
    }

    void GameScene::localPlayerModifyBuildQueue(UnitId unitId, const std::string& unitType, int count)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyBuildQueue{count, unitType}));

        updateUnconfirmedBuildQueueDelta(unitId, unitType, count);
        refreshBuildGuiTotal(unitId, unitType);
    }

    void GameScene::localPlayerModifyStockpile(UnitId unitId, int count)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::ModifyStockpile{count}));

        unconfirmedStockpileDelta[unitId] += count;
        refreshStockpileGuiTotal();
    }

    void GameScene::issueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            // Whatever it was doing (building, reclaiming) stops now, so the
            // arm is stowed and the nano spray ends; a later order to the same
            // target starts cleanly with StartBuilding.
            UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            unit->get().clearOrders();
            unit->get().addOrder(order);
        }
    }

    void GameScene::enqueueUnitOrder(UnitId unitId, const UnitOrder& order)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            // An idle unit has nothing to queue behind, so this order starts
            // straight away — which means an aircraft part-way through setting
            // down has to break off and get back in the air for it.
            if (unit->get().orders.empty())
            {
                UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            }
            unit->get().addOrder(order);
        }
    }

    void GameScene::stopUnit(UnitId unitId)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            UnitBehaviorService(&simulation).interruptCurrentTask(unitId);
            unit->get().clearOrders();
        }
    }

    void GameScene::cancelBuildOrderAt(UnitId unitId, const SimVector& position)
    {
        auto unit = tryGetUnit(unitId);
        if (!unit)
        {
            return;
        }
        auto cell = simulation.terrain.worldToHeightmapCoordinate(position);
        auto& orders = unit->get().orders;
        for (auto it = orders.begin(); it != orders.end(); ++it)
        {
            auto buildOrder = std::get_if<BuildOrder>(&*it);
            if (!buildOrder)
            {
                continue;
            }
            const auto& definition = simulation.unitDefinitions.at(buildOrder->unitType);
            auto rect = simulation.computeFootprintRegion(buildOrder->position, definition.movementCollisionInfo);
            if (cell.x >= rect.x && cell.x < rect.x + static_cast<int>(rect.width) && cell.y >= rect.y && cell.y < rect.y + static_cast<int>(rect.height))
            {
                // Only the plan is dropped; a building already started stays.
                if (it == orders.begin() && std::holds_alternative<UnitBehaviorStateBuilding>(unit->get().behaviourState))
                {
                    return;
                }
                orders.erase(it);
                return;
            }
        }
    }

    void GameScene::setFireOrders(UnitId unitId, UnitFireOrders orders)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            unit->get().setFireOrders(orders);

            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && *selectedUnit == unitId)
            {
                fireOrders.next(orders);
            }
        }
    }

    void GameScene::startTrack()
    {
        // sort selection by unit id so repeated 'T' keydown cycles through all units in a group consistently
        std::vector<UnitId> unitIds;
        for (const auto& u : selectedUnits)
        {
            unitIds.push_back(u);
        }
        std::sort(unitIds.begin(), unitIds.end());

        startTrackInternal(unitIds);
    }

    void GameScene::startTrackInternal(const std::vector<UnitId>& unitIds)
    {
        // Only allow tracking in free camera mode or if we are already tracking.
        auto canStartTracking = match(
            cameraControlState,
            [&](const CameraControlStateFree&) {
                return true;
            },
            [&](const CameraControlStateTrackingUnit&) {
                return true;
            },
            [&](const CameraControlStateMiddleMousePan&) {
                return false;
            });

        if (!canStartTracking)
        {
            return;
        }

        // If 'T' is pressed and no units are selected, stop tracking.
        if (unitIds.empty())
        {
            cameraControlState = CameraControlStateFree();
            return;
        }

        // If already tracking, check if currently tracked unit is in this selection. If it is, select the next id in the group.
        if (trackedUnitId)
        {
            auto it = std::find(unitIds.begin(), unitIds.end(), trackedUnitId);
            if (it != unitIds.end() && ++it != unitIds.end())
            {
                trackedUnitId = *it;
            }
            else
            {
                trackedUnitId = unitIds[0];
            }
        }
        else
        {
            trackedUnitId = unitIds[0];
        }

        cameraControlState = CameraControlStateTrackingUnit();
    }

    bool GameScene::isCtrlDown() const
    {
        return leftCtrlDown || rightCtrlDown;
    }

    bool GameScene::isShiftDown() const
    {
        return leftShiftDown || rightShiftDown;
    }

    namespace
    {
        /** Fills the GAMES listbox and mirrors clicks into the name box. */
        void wireSaveList(UiPanel& panel)
        {
            // The metadata gadgets default to their caption text, doubling
            // the captions the art already paints; they stay empty until a
            // selected save can fill them in.
            for (const auto* name : {"GAMETYPE", "SIDE", "MISSION", "DIFF", "TIME"})
            {
                if (auto label = panel.find<UiLabel>(name))
                {
                    label->get().setText(std::string());
                }
            }

            auto games = panel.find<UiListBox>("GAMES");
            if (!games)
            {
                return;
            }
            for (const auto& name : listSaveGames())
            {
                games->get().appendItem(name);
            }
            auto sub = games->get().selectedIndex().subscribe([&panel](const std::optional<unsigned int>& index) {
                if (!index)
                {
                    return;
                }
                auto games = panel.find<UiListBox>("GAMES");
                auto box = panel.find<UiTextBox>("GAMENAME");
                if (games && box && *index < games->get().getItems().size())
                {
                    box->get().setText(games->get().getItems()[*index]);
                }
            });
            games->get().addSubscription(std::move(sub));
        }
    }

    void GameScene::openSaveDialog()
    {
        // LOADGAME.GUI is the save/load dialog both ways in the original --
        // list, name field, metadata labels, radar frame -- and only the
        // painted background differs: DSavegame2 titles it SAVE GAME.
        // (SAVEGAME.GUI is a smaller, matching nothing that ships; unused.)
        auto guiRaw = sceneContext.vfs->readFile("guis/LOADGAME.GUI");
        auto entries = guiRaw ? parseGuiFromBytes(*guiRaw) : std::nullopt;
        auto panel = entries
            ? uiFactory.panelFromGuiFile("SAVEGAME", "DSavegame2", *entries)
            : uiFactory.panelFromGuiFile("SAVEGAME");
        wireSaveList(*panel);
        if (auto box = panel->find<UiTextBox>("GAMENAME"))
        {
            box->get().setText("savegame");
        }

        // LOADGAME.GUI ships an empty defaultfocus, so the original opens
        // this dialog with no focused gadget and therefore no caret. RWE
        // starts the name field focused instead: its keyDown is broadcast to
        // every child rather than routed to the focused one, so typing works
        // either way, and a field you can type into ought to look like one.
        panel->setFocusByName("GAMENAME");

        setGameMenuPanel(std::move(panel));
    }

    void GameScene::openLoadDialog()
    {
        auto panel = uiFactory.panelFromGuiFile("LOADGAME");
        wireSaveList(*panel);
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::saveCurrentGame(const std::string& name)
    {
        SaveFile save(gameParameters);
        save.cameraPosition = worldCameraState.position;
        save.simulation = saveSimulationToJson(simulation);
        writeSaveFile(savePathForName(name), save);
        printConsole("Game saved: " + name);
    }

    void GameScene::loadSavedGame(const std::string& name)
    {
        auto path = savePathForName(name);
        auto save = readSaveFile(path);
        if (!save)
        {
            printConsole("Could not read save: " + name);
            return;
        }

        // Rerun the whole loading pipeline for the saved game's map and
        // players; the loading scene applies the saved state instead of
        // spawning the starting commanders.
        auto parameters = save->parameters;
        parameters.loadFromSaveFile = path.string();
        sceneContext.audioService->stopMusic();
        auto scene = std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            parameters);
        sceneContext.sceneManager->setNextScene(scene);
    }

    void GameScene::applyLoadedGame(const SaveFile& save)
    {
        // The loader adds the players itself from the save -- economy state
        // and all -- so the loading pipeline's freshly added ones step
        // aside. Same parameters, same slots, same ids.
        simulation.clearPlayers();
        loadSimulationFromJson(save.simulation, simulation);
        setCameraPosition(save.cameraPosition);
    }

    void GameScene::setMenuPause(bool wantPaused)
    {
        // The original pauses when the game menu opens in single player and
        // never in multiplayer; RWE routes it through the same command path
        // as the Pause key so peers stay in step either way.
        if (wantPaused && !paused)
        {
            paused = true;
            menuPausedGame = true;
            localPlayerCommandBuffer.push_back(PlayerPauseGameCommand{});
        }
        else if (!wantPaused && menuPausedGame)
        {
            menuPausedGame = false;
            if (paused)
            {
                paused = false;
                localPlayerCommandBuffer.push_back(PlayerUnpauseGameCommand{});
            }
        }
    }

    void GameScene::setGameMenuPanel(std::unique_ptr<UiPanel>&& panel)
    {
        panel->groupMessages().subscribe([this](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                gameMenuMessage(msg.topic, msg.controlName);
            }
        });
        gameMenuPanels.clear();
        gameMenuPanels.push_back(std::move(panel));
    }

    void GameScene::toggleGameMenu()
    {
        if (isGameMenuOpen())
        {
            closeGameMenu();
        }
        else
        {
            openGameMenuRoot();
        }
    }

    void GameScene::openGameMenuRoot()
    {
        // The original's GAME OPTIONS panel, drawn over the left unit panel.
        // Tab opens it in single player (the sliding TABMENU bar it shares a
        // key with is multiplayer-only), F2 opens it anywhere.
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        auto panel = uiFactory.panelFromGuiFile(sidePrefix + "OPT");

        // The briefing and help do not exist in RWE yet; the original greys
        // what does not apply rather than hiding it.
        for (const auto* name : {"MISSION", "HELP"})
        {
            if (auto button = panel->find<UiStagedButton>(name))
            {
                button->get().setEnabled(false);
            }
        }

        setGameMenuPanel(std::move(panel));
        inGameOptionsPage.clear();
        setMenuPause(true);
    }

    void GameScene::openGameExitMenu()
    {
        auto panel = uiFactory.panelFromGuiFile("EXITMENU");
        if (auto button = panel->find<UiStagedButton>("RESTART"))
        {
            button->get().setEnabled(false);
        }
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::addGameMenuPanel(std::unique_ptr<UiPanel>&& panel)
    {
        panel->groupMessages().subscribe([this](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                gameMenuMessage(msg.topic, msg.controlName);
            }
        });
        gameMenuPanels.push_back(std::move(panel));
    }

    void GameScene::openInGameOptions(const std::string& page)
    {
        // The original's options screen is two panels side by side, and the
        // gui files say so: PREFS.GUI is the sidebar at (0,126) 128 wide,
        // and each RT page is 150 wide at (128,128) -- it folds out to the
        // right of the sidebar, over the game view. Merging them into one
        // panel put the page's gadgets at sidebar-relative coordinates,
        // which is why the sub-options landed on top of the tab buttons.
        // Each page carries its own background as a picture-box gadget.
        gameMenuPanels.clear();
        inGameOptionsPage = page;

        addGameMenuPanel(uiFactory.panelFromGuiFile("PREFS"));
        if (!page.empty())
        {
            addGameMenuPanel(uiFactory.panelFromGuiFile(page));
        }

        wireInGameOptionControls();
    }

    void GameScene::wireInGameOptionControls()
    {
        auto state = currentInGameOptions();

        if (auto bar = findInGameMenu<UiScrollBar>("FXVOL"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent(static_cast<float>(state.soundVolume) / 100.0f);
            auto sub = bar->scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setSoundVolume(v);
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto bar = findInGameMenu<UiScrollBar>("MUSICVOL"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent(static_cast<float>(state.musicVolume) / 100.0f);
            auto sub = bar->scrollChanged().subscribe([a = sceneContext.audioService](float v) {
                a->setMusicVolume(v);
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("NOTRAK"))
        {
            toggle->setStage(state.musicEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("MODE"))
        {
            toggle->setStage(static_cast<unsigned int>(state.soundMode));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("SPEECH"))
        {
            toggle->setStage(static_cast<unsigned int>(state.unitSpeech));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("BSHADOWS"))
        {
            toggle->setStage(state.shadows ? 1 : 0);
        }

        if (auto bar = findInGameMenu<UiScrollBar>("GAMMA"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent((static_cast<float>(state.gamma) - 50.0f) / 83.0f);
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                // The original's twenty steps of 0.5 + v/24: 0.5x to 1.333x.
                gammaSetting = 50u + static_cast<unsigned int>(v * 83.0f);
                applyGamma();
            });
            bar->addSubscription(std::move(sub));
        }

        // Screen scroll: how fast the view moves when the cursor is held at
        // the edge (and on the arrow keys), 25 to 200 percent.
        if (auto bar = findInGameMenu<UiScrollBar>("SCREEN"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent((static_cast<float>(state.scrollSpeed) - 25.0f) / 175.0f);
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                scrollSpeedSetting = 25u + static_cast<unsigned int>(v * 175.0f);
            });
            bar->addSubscription(std::move(sub));
        }

        // Game speed across the whole -10..+10 range, through the same
        // lockstep command the +/- keys use.
        if (auto bar = findInGameMenu<UiScrollBar>("GAME"))
        {
            bar->setScrollBarPercent(0.2f);
            bar->setScrollPercent(static_cast<float>(gameSpeed.index()) / static_cast<float>(GameSpeed::MaxIndex));
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                auto index = static_cast<int>((v * static_cast<float>(GameSpeed::MaxIndex)) + 0.5f);
                if (index != gameSpeed.index())
                {
                    localPlayerCommandBuffer.push_back(PlayerSetGameSpeedCommand{GameSpeed(index).index()});
                }
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto bar = findInGameMenu<UiScrollBar>("VIDSLDR"))
        {
            bar->setScrollBarPercent(0.34f);
            auto modeToPercent = pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f);
            bar->setScrollPercent(modeToPercent);
            auto sub = bar->scrollChanged().subscribe([this](float v) {
                pendingWindowMode = v < 0.33f ? "windowed" : (v < 0.67f ? "borderless" : "fullscreen");
                if (auto label = findInGameMenu<UiLabel>("VIDVAL"))
                {
                    label->setText(windowModeDisplayName(pendingWindowMode));
                }
            });
            bar->addSubscription(std::move(sub));
        }

        if (auto label = findInGameMenu<UiLabel>("VIDVAL"))
        {
            label->setText(windowModeDisplayName(pendingWindowMode));
        }

        // Still no machinery behind these; the original greys what does not
        // apply rather than letting it lie.
        if (auto toggle = findInGameMenu<UiStagedButton>("SHADING"))
        {
            toggle->setStage(shadingEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("ANTI"))
        {
            toggle->setStage(antiAliasEnabled ? 1 : 0);
        }

        for (const auto* name : {"LEFTCLICK", "UNITCHAT", "TXTSCROL", "MAXLINES"})
        {
            if (auto button = findInGameMenu<UiStagedButton>(name))
            {
                button->setEnabled(false);
            }
        }
    }

    void GameScene::closeGameMenu()
    {
        gameMenuPanels.clear();
        inGameOptionsPage.clear();
        setMenuPause(false);
    }

    GameOptions GameScene::currentInGameOptions() const
    {
        return GameOptions{
            static_cast<unsigned int>(sceneContext.audioService->getSoundVolume() * 100.0f),
            static_cast<unsigned int>(sceneContext.audioService->getMusicVolume() * 100.0f),
            sceneContext.audioService->isMusicEnabled(),
            pendingWindowMode,
            shadowsEnabled,
            scrollSpeedSetting,
            soundModeSetting,
            unitSpeechSetting,
            gammaSetting,
            shadingEnabled,
            antiAliasEnabled};
    }

    void GameScene::applyInGameOptions(const GameOptions& state)
    {
        auto* audio = sceneContext.audioService;
        audio->setSoundVolume(static_cast<float>(state.soundVolume) / 100.0f);
        audio->setMusicVolume(static_cast<float>(state.musicVolume) / 100.0f);
        audio->setMusicEnabled(state.musicEnabled);
        pendingWindowMode = state.windowMode;
        shadowsEnabled = state.shadows;
        scrollSpeedSetting = state.scrollSpeed;
        soundModeSetting = state.soundMode;
        unitSpeechSetting = state.unitSpeech;
        audio->setSoundEnabled(state.soundMode != SoundMode::Off);
        gammaSetting = state.gamma;
        applyGamma();
        shadingEnabled = state.shading;
        if (antiAliasEnabled != state.antiAlias)
        {
            antiAliasEnabled = state.antiAlias;
            recreateWorldRenderTextures();
        }
    }

    void GameScene::applyGamma()
    {
        // Nothing to push: the world's post-process blit reads gammaSetting
        // every frame, so moving the slider is visible at once. The hook is
        // kept so the callers read as intent rather than as an assignment.
    }

    void GameScene::saveInGameOptions()
    {
        auto localDataPath = getLocalDataPath();
        if (!localDataPath)
        {
            return;
        }
        writeGameOptions(*localDataPath / "rwe.cfg", currentInGameOptions());
    }

    void GameScene::exitToMainMenu()
    {
        sceneContext.audioService->stopMusic();
        auto menu = std::make_shared<MainMenuScene>(
            sceneContext,
            audioLookup,
            sceneContext.viewport->width(),
            sceneContext.viewport->height());
        sceneContext.sceneManager->setNextScene(menu);
    }

    void GameScene::gameMenuMessage(const std::string& topic, const std::string& control)
    {
        // Defer: this is called from inside the panel's own event dispatch,
        // and most handlers replace the panel, which would destroy the object
        // whose callback we are standing in.
        pendingMenuActions.push_back([this, topic, control]() { gameMenuMessageNow(topic, control); });
    }

    void GameScene::gameMenuMessageNow(const std::string& topic, const std::string& control)
    {
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        if (topic == sidePrefix + "OPT")
        {
            if (control == "OK")
            {
                closeGameMenu();
            }
            else if (control == "SAVEGAME")
            {
                openSaveDialog();
            }
            else if (control == "LOADGAME")
            {
                openLoadDialog();
            }
            else if (control == "PREFS")
            {
                if (pendingWindowMode.empty())
                {
                    pendingWindowMode = sceneContext.globalConfig->windowMode;
                }
                gameOptionsUndo = currentInGameOptions();
                openInGameOptions(std::string());
            }
            else if (control == "EXIT")
            {
                openGameExitMenu();
            }
        }
        else if (topic == "SAVEGAME")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
            }
            else if (control == "DELETE")
            {
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        std::error_code ec;
                        std::filesystem::remove(savePathForName(box->get().getText()), ec);
                    }
                }
                openSaveDialog();
            }
            // The gadget is named LOAD in the shared dialog gui; its label
            // is what says OK.
            else if (control == "SAVE" || control == "LOAD")
            {
                std::string name = "savegame";
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                saveCurrentGame(name);
                openGameMenuRoot();
            }
        }
        else if (topic == "LOADGAME")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
            }
            else if (control == "DELETE")
            {
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        std::error_code ec;
                        std::filesystem::remove(savePathForName(box->get().getText()), ec);
                    }
                }
                openLoadDialog();
            }
            else if (control == "LOAD")
            {
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        loadSavedGame(box->get().getText());
                    }
                }
            }
        }
        else if (topic == "EXITMENU")
        {
            if (control == "CANCEL")
            {
                openGameMenuRoot();
            }
            else if (control == "EXITGAME")
            {
                sceneContext.sceneManager->requestExit();
            }
            else if (control == "MAINMENU")
            {
                exitToMainMenu();
            }
        }
        else if (topic == "PREFS" || topic == "SOUNDSRT" || topic == "MUSICRT" || topic == "VISUALRT" || topic == "SPEEDSRT")
        {
            // The fold-out is two panels, and each emits under its own name:
            // the sidebar's tabs come in as PREFS, but every control on a
            // page arrives under the page's topic.
            if (control == "SOUND")
            {
                openInGameOptions("SOUNDSRT");
            }
            else if (control == "MUSIC")
            {
                openInGameOptions("MUSICRT");
            }
            else if (control == "VISUALS")
            {
                openInGameOptions("VISUALRT");
            }
            else if (control == "SPEEDS")
            {
                openInGameOptions("SPEEDSRT");
            }
            else if (control == "PREV")
            {
                // The button says OK: keep the settings, apply the ones
                // that are not already live, and go back.
                saveInGameOptions();
                sceneContext.sceneManager->setWindowMode(pendingWindowMode);
                openGameMenuRoot();
            }
            else if (control == "CANCEL")
            {
                applyInGameOptions(gameOptionsUndo);
                openGameMenuRoot();
            }
            else if (control == "RESTORE")
            {
                applyInGameOptions(GameOptions{});
                openInGameOptions(inGameOptionsPage);
            }
            else if (control == "UNDO")
            {
                applyInGameOptions(gameOptionsUndo);
                openInGameOptions(inGameOptionsPage);
            }
            else if (control == "NOTRAK")
            {
                auto* audio = sceneContext.audioService;
                audio->setMusicEnabled(!audio->isMusicEnabled());
            }
            else if (control == "BSHADOWS")
            {
                shadowsEnabled = !shadowsEnabled;
            }
            else if (control == "SHADING")
            {
                shadingEnabled = !shadingEnabled;
            }
            else if (control == "ANTI")
            {
                antiAliasEnabled = !antiAliasEnabled;
                recreateWorldRenderTextures();
            }
            else if (control == "MODE")
            {
                // Off | Mono | 3D, cycled by the button itself.
                soundModeSetting = nextStage(soundModeSetting);
                sceneContext.audioService->setSoundEnabled(soundModeSetting != SoundMode::Off);
            }
            else if (control == "SPEECH")
            {
                // Off | Medium | Full: how much of the unit chatter plays.
                unitSpeechSetting = nextStage(unitSpeechSetting);
            }
            else if (control == "CDPLAY")
            {
                sceneContext.audioService->setMusicEnabled(true);
            }
            else if (control == "CDSTOP")
            {
                sceneContext.audioService->stopMusic();
            }
            else if (control == "CDNEXT" || control == "CDPREV")
            {
                // The in-game rotation picks its own next track; stopping the
                // current one is what asks it for another.
                sceneContext.audioService->stopMusic();
            }
            else if (control == "TEST")
            {
                if (auto sound = sceneContext.audioService->loadSound("BUTTON10"))
                {
                    sceneContext.audioService->playSound(*sound);
                }
            }
        }

        // Whatever just happened, the widgets show the state as it now is.
        refreshInGameOptionControls();
    }

    void GameScene::refreshInGameOptionControls()
    {
        auto* audio = sceneContext.audioService;
        if (auto toggle = findInGameMenu<UiStagedButton>("NOTRAK"))
        {
            toggle->setStage(audio->isMusicEnabled() ? 1 : 0);
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("MODE"))
        {
            toggle->setStage(static_cast<unsigned int>(soundModeSetting));
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("SPEECH"))
        {
            toggle->setStage(static_cast<unsigned int>(unitSpeechSetting));
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("BSHADOWS"))
        {
            toggle->setStage(shadowsEnabled ? 1 : 0);
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("SHADING"))
        {
            toggle->setStage(shadingEnabled ? 1 : 0);
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("ANTI"))
        {
            toggle->setStage(antiAliasEnabled ? 1 : 0);
        }
        if (auto bar = findInGameMenu<UiScrollBar>("FXVOL"))
        {
            bar->setScrollPercent(audio->getSoundVolume());
        }
        if (auto bar = findInGameMenu<UiScrollBar>("MUSICVOL"))
        {
            bar->setScrollPercent(audio->getMusicVolume());
        }
        if (auto bar = findInGameMenu<UiScrollBar>("GAMMA"))
        {
            bar->setScrollPercent((static_cast<float>(gammaSetting) - 50.0f) / 83.0f);
        }
        if (auto bar = findInGameMenu<UiScrollBar>("SCREEN"))
        {
            bar->setScrollPercent((static_cast<float>(scrollSpeedSetting) - 25.0f) / 175.0f);
        }
        if (auto bar = findInGameMenu<UiScrollBar>("GAME"))
        {
            bar->setScrollPercent(static_cast<float>(gameSpeed.index()) / static_cast<float>(GameSpeed::MaxIndex));
        }
        if (auto bar = findInGameMenu<UiScrollBar>("VIDSLDR"))
        {
            bar->setScrollPercent(pendingWindowMode == "fullscreen" ? 1.0f : (pendingWindowMode == "borderless" ? 0.5f : 0.0f));
        }
        if (auto label = findInGameMenu<UiLabel>("VIDVAL"))
        {
            label->setText(windowModeDisplayName(pendingWindowMode));
        }
    }

    void GameScene::handleEscapeDown()
    {
        // Escape first closes anything drawn over the game.
        if (showDebugWindow)
        {
            showDebugWindow = false;
            return;
        }
        if (helpVisible)
        {
            helpVisible = false;
            return;
        }

        if (gameOver)
        {
            returnToMainMenu();
            return;
        }

        match(
            cursorMode.getValue(),
            [this](const NormalCursorMode&) {
                clearUnitSelection();
            },
            [this](const auto&) {
                cursorMode.next(NormalCursorMode());
            });
    }

    void GameScene::returnToMainMenu()
    {
        LOG_INFO << "Returning to the main menu";
        auto scene = std::make_unique<MainMenuScene>(
            sceneContext,
            audioLookup,
            static_cast<float>(sceneContext.viewport->width()),
            static_cast<float>(sceneContext.viewport->height()));
        sceneContext.sceneManager->setNextScene(std::shared_ptr<Scene>(std::move(scene)));
    }

    const PlayerVisibility& GameScene::localPlayerVisibility() const
    {
        const auto& vis = simulation.playerVisibility.at(localPlayerId.value);
        if (fogOfWarEnabled)
        {
            return vis;
        }

        // Fog off is a fully lit map rather than a second way of drawing one:
        // the same grids, with every cell already seen and remembered. The
        // simulation's own copy is left alone, since what this client chooses
        // to look at must not reach the simulation.
        auto width = vis.explored.getWidth();
        auto height = vis.explored.getHeight();
        if (!revealedVisibility || revealedVisibility->explored.getWidth() != width || revealedVisibility->explored.getHeight() != height)
        {
            PlayerVisibility revealed(width, height);
            auto& explored = revealed.explored.getVector();
            std::fill(explored.begin(), explored.end(), static_cast<unsigned char>(1));
            auto& visible = revealed.visible.getVector();
            std::fill(visible.begin(), visible.end(), static_cast<unsigned char>(1));
            revealedVisibility = std::move(revealed);
        }

        return *revealedVisibility;
    }

    bool GameScene::unitIsVisibleToLocalPlayer(UnitId unitId, const UnitState& unit) const
    {
        // Stowed inside a ship's hold (attached to no piece): out of sight until unloaded.
        if (unit.carriedBy && unit.carriedPiece.empty())
        {
            if (auto transport = tryGetUnit(*unit.carriedBy); transport && simulation.unitDefinitions.at(transport->get().unitType).floater)
            {
                return false;
            }
        }

        // The same questions the original's draw predicate asks, in the same
        // order: whose it is, whether it is cloaked, whether anything of it
        // breaks the surface, and only then whether the ground under it is
        // lit. It has to agree with the simulation's canSeeUnit or a unit
        // would be drawn to an enemy who cannot target it.
        //
        // The waterline question is the sonar one. 0x465AC0 refuses a unit
        // whose model is entirely below sea level unless the viewer holds it
        // on sonar, so a submarine is drawn to a destroyer and not to a
        // Peewee standing on the beach beside it.
        // Skipped with the fog off, which is a fully lit map and not a second
        // way of drawing one: nothing may stay hidden under it.
        if (fogOfWarEnabled && !unit.isOwnedBy(localPlayerId))
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
            if (unit.position.y + simulation.modelHeightOf(unitDefinition) < simulation.terrain.getSeaLevel())
            {
                const auto& heard = simulation.playerVisibility.at(localPlayerId.value).sonarContacts;
                if (heard.find(unitId) == heard.end())
                {
                    return false;
                }
            }
        }

        auto style = computeUnitDrawStyle(unit.isOwnedBy(localPlayerId), unit.cloaked, positionIsVisibleToLocalPlayer(unit.position));
        return style != UnitDrawStyle::Hidden;
    }

    bool GameScene::unitIsDetectableByLocalPlayer(UnitId unitId, const UnitState& unit) const
    {
        if (unitIsVisibleToLocalPlayer(unitId, unit))
        {
            return true;
        }

        // 0x466E6A: the minimap draws on either raw contact bit. The contact
        // sets are used rather than a plain range test against the dishes so
        // that the dot obeys the same stealth, jamming and waterline rules the
        // detection pass applied -- a submarine gets a dot from sonar and not
        // from a radar dish that cannot hear it.
        const auto& visibility = simulation.playerVisibility.at(localPlayerId.value);
        return visibility.radarContacts.find(unitId) != visibility.radarContacts.end()
            || visibility.sonarContacts.find(unitId) != visibility.sonarContacts.end();
    }

    bool GameScene::positionIsExploredByLocalPlayer(const SimVector& position) const
    {
        return localPlayerVisibility().isExplored(simulation.visionCellAt(position));
    }

    bool GameScene::positionIsVisibleToLocalPlayer(const SimVector& position) const
    {
        return effectIsVisibleToPlayer(simulation, localPlayerVisibility(), position);
    }

    std::optional<SimVector> GameScene::plannedBuildOrderAt(UnitId unitId, const SimVector& position) const
    {
        auto unit = tryGetUnit(unitId);
        if (!unit)
        {
            return std::nullopt;
        }
        auto cell = simulation.terrain.worldToHeightmapCoordinate(position);
        for (const auto& order : unit->get().orders)
        {
            auto buildOrder = std::get_if<BuildOrder>(&order);
            if (!buildOrder)
            {
                continue;
            }
            const auto& definition = simulation.unitDefinitions.at(buildOrder->unitType);
            auto rect = simulation.computeFootprintRegion(buildOrder->position, definition.movementCollisionInfo);
            if (cell.x >= rect.x && cell.x < rect.x + static_cast<int>(rect.width) && cell.y >= rect.y && cell.y < rect.y + static_cast<int>(rect.height))
            {
                return buildOrder->position;
            }
        }
        return std::nullopt;
    }

    void GameScene::localPlayerCancelBuildOrder(UnitId unitId, const SimVector& position)
    {
        localPlayerCommandBuffer.push_back(PlayerUnitCommand(unitId, PlayerUnitCommand::CancelBuildOrder{position}));
    }

    std::unique_ptr<UiPanel> GameScene::createOrdersPanel()
    {
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
        auto panel = uiFactory.panelFromGuiFile(sidePrefix + "GEN");
        applyOrderButtonGating(*panel);
        return panel;
    }

    void GameScene::applyOrderButtonGating(UiPanel& panel)
    {
        const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;

        // TA shows only the orders the selection can carry out, and it looks at
        // the whole selection rather than at one unit: the accumulator loop at
        // 0x41B49F-0x41B524 ORs each capability bit together, so a button is
        // offered when *any* selected unit names it. Picking up a transport
        // along with a squad of Peewees therefore gets you LOAD, and picking up
        // a solar collector with them does not take MOVE away.
        //
        // This runs over every panel that carries the order strip -- the
        // orders page AND the build pages, which include the full strip in
        // their own gui files. Ungated build pages were how a factory came to
        // offer the commander's D-gun (the BLAST gadget).
        std::vector<OrderButtonUnit> selection;
        for (const auto& selectedUnitId : selectedUnits)
        {
            auto selectedUnit = tryGetUnit(selectedUnitId);
            if (!selectedUnit)
            {
                continue;
            }

            bool hasCommandFireWeapon = false;
            for (const auto& weapon : selectedUnit->get().weapons)
            {
                if (weapon && simulation.weaponDefinitions.at(weapon->weaponType).commandFire)
                {
                    hasCommandFireWeapon = true;
                }
            }

            selection.push_back(OrderButtonUnit{&simulation.unitDefinitions.at(selectedUnit->get().unitType), hasCommandFireWeapon});
        }

        if (selection.empty())
        {
            return;
        }

        // The original greys these out rather than taking them away, except
        // LOAD and BLAST which share a slot and so have to be hidden
        // (0x41A412 and 0x41A471 call the "make inactive" helper, everything
        // else calls the "grey" one). The greyed frame is in every button's
        // own GAF, one past the pressed frame.
        std::vector<std::string> doomed;
        for (const auto& child : panel.getChildren())
        {
            const auto& name = child->getName();
            if (!startsWith(name, sidePrefix))
            {
                continue;
            }

            auto button = orderButtonFromName(name.substr(sidePrefix.size()));
            if (button && !selectionOffersOrderButton(selection, *button))
            {
                if (*button == OrderButton::Load || *button == OrderButton::Blast)
                {
                    doomed.push_back(name);
                }
                else if (auto stagedButton = dynamic_cast<UiStagedButton*>(child.get()); stagedButton != nullptr)
                {
                    stagedButton->setEnabled(false);
                }
            }
        }

        for (const auto& name : doomed)
        {
            panel.removeChildrenNamed(name);
        }
    }

    void GameScene::spawnDebris(const PieceExplodedEvent& e)
    {
        // Own units first, then the ground, as the death explosion beside it
        // does: the exploding unit has already been taken off the vision grid
        // when this is read, so a position test alone would swallow the last
        // of your own building in the dark.
        if (e.owner != localPlayerId && !positionIsVisibleToLocalPlayer(e.position))
        {
            return;
        }

        const auto& unitDefinition = simulation.unitDefinitions.at(e.unitType);
        auto position = simVectorToFloat(e.position);

        // TA's explode flags.
        const unsigned int shatter = 1u;
        const unsigned int bitmapOnly = 32u;

        // BITMAP1..5 (bits 6-10) choose an explosion sprite to show at the piece.
        static const char* const bitmapAnims[] = {"Explode2", "Explode3", "Explode4", "Explode5", "Explosion"};
        for (unsigned int i = 0; i < 5; ++i)
        {
            if ((e.flags & (64u << i)) && gameMediaDatabase.getSpriteSeries("FX", bitmapAnims[i]))
            {
                spawnExplosion(position, AnimLocation{"FX", bitmapAnims[i]});
                break;
            }
        }
        // Only buildings break into flying pieces; mobile units just get the
        // explosion sprites. A piece the model does not have cannot fly either.
        if ((e.flags & bitmapOnly) || unitDefinition.isMobile || e.pieceName.empty())
        {
            return;
        }

        std::uniform_real_distribution<float> sideways(-2.5f, 2.5f);
        std::uniform_real_distribution<float> upwards(3.0f, 7.0f);
        std::uniform_real_distribution<float> spin(-0.3f, 0.3f);
        std::uniform_int_distribution<unsigned int> lifetime(60u, 120u);

        auto makeDebris = [&](bool shard) {
            Debris d;
            d.objectName = unitDefinition.objectName;
            d.pieceName = e.pieceName;
            d.color = getPlayer(e.owner).color;
            d.position = position;
            d.velocity = Vector3f(sideways(effectsRng), upwards(effectsRng), sideways(effectsRng));
            d.rotation = Vector3f(0.0f, toRadians(e.rotation).value, 0.0f);
            d.angularVelocity = Vector3f(spin(effectsRng), spin(effectsRng), spin(effectsRng));
            d.endTime = simulation.gameTime + GameTime(lifetime(effectsRng));
            d.nextTrail = simulation.gameTime;
            d.flags = e.flags;
            d.shard = shard;
            debris.push_back(d);
        };

        if (e.flags & shatter)
        {
            // The piece breaks up: a handful of fragments instead of the mesh.
            for (int i = 0; i < 6; ++i)
            {
                makeDebris(true);
            }
        }
        else
        {
            makeDebris(false);
        }
    }

    void GameScene::updateDebris()
    {
        const unsigned int explodeOnHit = 2u;
        const unsigned int smoke = 8u;
        const unsigned int fire = 16u;
        const float gravity = 0.3f;

        auto corner = simVectorToFloat(simulation.terrain.heightmapIndexToWorldCorner(0, 0));
        auto tile = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
        auto mapWidth = static_cast<float>(simulation.terrain.getHeightMap().getWidth()) * tile;
        auto mapHeight = static_cast<float>(simulation.terrain.getHeightMap().getHeight()) * tile;

        auto end = debris.end();
        for (auto it = debris.begin(); it != end;)
        {
            auto& d = *it;
            d.velocity.y -= gravity;
            d.position += d.velocity;
            d.rotation += d.angularVelocity;

            bool onMap = d.position.x > corner.x + tile && d.position.x < corner.x + mapWidth - tile
                && d.position.z > corner.z + tile && d.position.z < corner.z + mapHeight - tile;

            if (onMap && (d.flags & (smoke | fire)) && simulation.gameTime >= d.nextTrail)
            {
                d.nextTrail = simulation.gameTime + GameTime(3);
                if ((d.flags & fire) && gameMediaDatabase.getSpriteSeries("FX", "fire1"))
                {
                    spawnExplosion(d.position, AnimLocation{"FX", "fire1"});
                }
                else
                {
                    spawnSmoke(d.position, "FX", "smoke 1", ParticleFinishTimeEndOfFrames(), GameTime(2));
                }
            }

            auto ground = onMap ? simScalarToFloat(simulation.terrain.getHeightAt(SimScalar(d.position.x), SimScalar(d.position.z))) : d.position.y;
            bool landed = onMap && d.position.y <= ground;
            if (!onMap || landed || simulation.gameTime >= d.endTime)
            {
                if (landed && (d.flags & explodeOnHit) && gameMediaDatabase.getSpriteSeries("FX", "Explode2"))
                {
                    spawnExplosion(Vector3f(d.position.x, ground, d.position.z), AnimLocation{"FX", "Explode2"});
                }
                *it = std::move(*--end);
                continue;
            }
            ++it;
        }
        debris.erase(end, debris.end());
    }

    void GameScene::updateFogSprite()
    {
        if (!fogTiles)
        {
            // TA's own fog artwork. Without it we fall back to square-edged
            // shapes: uglier, but the fog still reads correctly.
            fogTiles = loadFogTileSet(*sceneContext.vfs, "anims/fog.gaf").value_or(makeSquareFogTileSet());
        }

        const auto& vis = localPlayerVisibility();

        auto cellsWide = vis.explored.getWidth();
        auto cellsHigh = vis.explored.getHeight();
        // The vision grid starts at the map's top-left corner and covers whole
        // cells, which may extend slightly past the map's edge.
        auto cellWorldUnits = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits) * static_cast<float>(PlayerVisibility::VisionCellSizeInTiles);
        auto corner = simVectorToFloat(simulation.terrain.heightmapIndexToWorldCorner(0, 0));

        // The cells the camera can see. The fog grid is indexed in projected
        // space, and the terrain sheet is drawn flat, so the sheet's own x and
        // z are already that space and no skew is needed here. A whole terrain
        // tile of slack covers the tiles that hang over the camera's edge.
        auto camera = worldCameraState.getRoundedPosition();
        auto halfWidth = worldCameraState.scaleDimension(static_cast<float>(worldViewport.width())) / 2.0f;
        auto halfHeight = worldCameraState.scaleDimension(static_cast<float>(worldViewport.height())) / 2.0f;
        auto toCellX = [&](float worldX) {
            return std::clamp(static_cast<int>(std::floor((worldX - corner.x) / cellWorldUnits)), 0, cellsWide - 1);
        };
        auto toCellY = [&](float worldZ) {
            return std::clamp(static_cast<int>(std::floor((worldZ - corner.z) / cellWorldUnits)), 0, cellsHigh - 1);
        };
        auto viewX0 = toCellX(camera.x - halfWidth - cellWorldUnits);
        auto viewY0 = toCellY(camera.z - halfHeight - cellWorldUnits);
        auto viewX1 = toCellX(camera.x + halfWidth + cellWorldUnits);
        auto viewY1 = toCellY(camera.z + halfHeight + cellWorldUnits);
        GridRegion cellsInView(viewX0, viewY0, (viewX1 - viewX0) + 1, (viewY1 - viewY0) + 1);

        // The grids only change on sim ticks, and a couple of ticks of lag in
        // the fog itself is invisible. The window is another matter: once the
        // camera leaves it the edge of its texture would show, so a scroll off
        // the end is never put off.
        if (fogSprite && (simulation.gameTime.value - fogSpriteTime.value) < 2 && fogRasterizer.covers(cellsInView))
        {
            return;
        }
        fogSpriteTime = simulation.gameTime;

        // Rebuilding is the expensive part. The rasteriser keeps a window a
        // little larger than the view, tracks the corner codes it last drew,
        // and reports only the patch of texture that moved.
        auto update = fogRasterizer.update(*fogTiles, vis.visible, vis.explored, cellsInView);
        if (update)
        {
            auto overlayWidth = static_cast<unsigned int>(fogRasterizer.getWidth());
            auto overlayHeight = static_cast<unsigned int>(fogRasterizer.getHeight());
            if (update->windowChanged || !fogOverlayTexture.isValid() || fogOverlayWidth != overlayWidth || fogOverlayHeight != overlayHeight)
            {
                fogOverlayTexture = SharedTextureHandle(sceneContext.graphics->createSingleChannelTexture(overlayWidth, overlayHeight, fogRasterizer.getData()));
                fogOverlayWidth = overlayWidth;
                fogOverlayHeight = overlayHeight;
                fogOverlayBounds = Rectangle2f::fromTopLeft(
                    corner.x + static_cast<float>(fogRasterizer.getOffsetX()),
                    corner.z + static_cast<float>(fogRasterizer.getOffsetY()),
                    static_cast<float>(overlayWidth),
                    static_cast<float>(overlayHeight));
            }
            else
            {
                sceneContext.graphics->updateSingleChannelTexture(
                    fogOverlayTexture.get(),
                    overlayWidth,
                    static_cast<unsigned int>(update->dirty.x),
                    static_cast<unsigned int>(update->dirty.y),
                    static_cast<unsigned int>(update->dirty.width),
                    static_cast<unsigned int>(update->dirty.height),
                    fogRasterizer.getData());
            }
        }

        // The minimap keeps its own one-texel-per-cell copy of the whole map.
        // It is only a hundred-odd pixels across, so the authored tiles would
        // be thrown away by the downscale anyway, and this way it does not have
        // to care where the world's window happens to be.
        if (fogSprite && vis.visible.getVector() == fogVisibleSnapshot && vis.explored.getVector() == fogExploredSnapshot)
        {
            return;
        }
        fogVisibleSnapshot = vis.visible.getVector();
        fogExploredSnapshot = vis.explored.getVector();

        minimapFogPixels.resize(static_cast<size_t>(cellsWide) * static_cast<size_t>(cellsHigh));
        auto* out = minimapFogPixels.data();
        for (int y = 0; y < cellsHigh; ++y)
        {
            for (int x = 0; x < cellsWide; ++x)
            {
                if (vis.visible.get(x, y) != 0)
                {
                    *out++ = Color(0, 0, 0, 0);
                }
                else if (vis.explored.get(x, y))
                {
                    *out++ = Color(0, 0, 0, 120);
                }
                else
                {
                    *out++ = Color(0, 0, 0, 255);
                }
            }
        }

        if (!minimapFogTexture.isValid() || minimapFogWidth != cellsWide || minimapFogHeight != cellsHigh)
        {
            minimapFogTexture = SharedTextureHandle(sceneContext.graphics->createTexture(cellsWide, cellsHigh, minimapFogPixels.data()));
            minimapFogWidth = cellsWide;
            minimapFogHeight = cellsHigh;

            auto bounds = Rectangle2f::fromTopLeft(corner.x, corner.z, cellsWide * cellWorldUnits, cellsHigh * cellWorldUnits);
            auto region = Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f);
            fogSprite = sceneContext.graphics->createSprite(bounds, region, minimapFogTexture);
        }
        else
        {
            sceneContext.graphics->updateTexture(
                minimapFogTexture.get(),
                static_cast<unsigned int>(cellsWide),
                static_cast<unsigned int>(cellsHigh),
                minimapFogPixels.data());
        }
    }

    void GameScene::renderHelpOverlay()
    {
        if (!helpVisible)
        {
            return;
        }

        // Two columns of "key   what it does".
        static const std::vector<std::pair<std::string, std::string>> leftColumn{
            {"F1", "Show or hide this help"},
            {"Left click", "Select unit, or drag a box"},
            {"Right click", "Move / attack / assist (right-click mode)"},
            {"Shift + order", "Queue the order"},
            {"Esc", "Cancel cursor mode / deselect"},
            {"A", "Attack"},
            {"M", "Move"},
            {"G", "Guard"},
            {"D", "D-gun (hold to pick a target)"},
            {"P", "Patrol"},
            {"R", "Repair"},
            {"E", "Reclaim"},
            {"C", "Capture"},
            {"S", "Stop"},
            {"T", "Track selected unit"},
            {"Ctrl+D", "Self-destruct (again to cancel)"},
            {"Ctrl+1..9 / 1..9", "Assign / select group"},
            {"+ / -", "Game speed"},
            {"Pause", "Pause"},
            {"Arrows", "Scroll the map"},
            {"`", "Health bars"},
            {"F10 / F11", "Debug menus"},
        };
        static const std::vector<std::pair<std::string, std::string>> rightColumn{
            {"Ctrl+A", "Select all units"},
            {"Ctrl+B", "Next idle builder"},
            {"Ctrl+C", "Select commander"},
            {"Ctrl+F", "Fight (attack-move)"},
            {"Ctrl+G", "Armed ground units"},
            {"Ctrl+H", "Armed hovercraft"},
            {"Ctrl+J", "Metal makers"},
            {"Ctrl+K", "Armed kbots"},
            {"Ctrl+L", "Long range artillery"},
            {"Ctrl+M", "Mines"},
            {"Ctrl+N", "Armed naval units"},
            {"Ctrl+O", "Fighters"},
            {"Ctrl+P", "Armed aircraft"},
            {"Ctrl+Q", "Bombers"},
            {"Ctrl+R", "Radar / sonar / jammers"},
            {"Ctrl+S", "Armed units on screen"},
            {"Ctrl+T", "Transports"},
            {"Ctrl+U", "Armed underwater units"},
            {"Ctrl+V", "Armed vehicles"},
            {"Ctrl+W", "Guard mode"},
            {"Ctrl+X", "Defensive buildings"},
            {"Ctrl+Y", "Torpedo bombers"},
            {"Ctrl+Z", "All units of the selected types"},
        };

        const float lineHeight = 14.0f;
        const float columnWidth = 300.0f;
        const float keyWidth = 110.0f;
        auto rows = std::max(leftColumn.size(), rightColumn.size());
        const float boxWidth = (columnWidth * 2.0f) + 24.0f;
        const float boxHeight = (rows + 3) * lineHeight;
        auto centerX = worldViewport.left() + (worldViewport.width() / 2.0f);
        auto centerY = worldViewport.top() + (worldViewport.height() / 2.0f);
        auto boxX = centerX - (boxWidth / 2.0f);
        auto boxY = centerY - (boxHeight / 2.0f);

        chromeUiRenderService.fillColor(boxX, boxY, boxWidth, boxHeight, Color(0, 0, 0, 215));
        chromeUiRenderService.drawBoxOutline(boxX, boxY, boxWidth, boxHeight, Color(180, 180, 180), 1.0f);
        chromeUiRenderService.drawTextCenteredX(centerX, boxY + (lineHeight * 0.5f), "HOTKEYS", *guiFont);

        auto drawColumn = [&](const std::vector<std::pair<std::string, std::string>>& column, float x) {
            auto y = boxY + (lineHeight * 2.0f);
            for (const auto& [key, action] : column)
            {
                chromeUiRenderService.drawText(x, y, key, *guiFont, Color(83, 223, 79));
                chromeUiRenderService.drawText(x + keyWidth, y, action, *guiFont);
                y += lineHeight;
            }
        };
        drawColumn(leftColumn, boxX + 12.0f);
        drawColumn(rightColumn, boxX + 12.0f + columnWidth);
    }

    void GameScene::renderGameOverOverlay()
    {
        if (!gameOver)
        {
            return;
        }

        const auto& localPlayer = getPlayer(localPlayerId);
        auto title = match(
            *gameOver,
            [&](const WinStatusWon& w) { return w.winner == localPlayerId ? std::string("VICTORY") : std::string("DEFEAT"); },
            [&](const WinStatusDraw&) { return std::string("DRAW"); },
            [&](const WinStatusUndecided&) { return std::string(); });

        auto totalSeconds = gameOverTime.value / static_cast<unsigned int>(SimTicksPerSecond);
        auto minutes = totalSeconds / 60;
        auto seconds = totalSeconds % 60;
        std::string timeText = "Game time " + std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds);

        std::vector<std::string> lines{
            title,
            timeText,
            "Units destroyed: " + std::to_string(localPlayer.unitsKilled),
            "Units lost: " + std::to_string(localPlayer.unitsLost),
            "",
            "Press ESC to return to the main menu",
        };

        const float lineHeight = 16.0f;
        const float boxWidth = 300.0f;
        const float boxHeight = (lines.size() + 2) * lineHeight;
        auto centerX = worldViewport.left() + (worldViewport.width() / 2.0f);
        auto centerY = worldViewport.top() + (worldViewport.height() / 2.0f);
        auto boxX = centerX - (boxWidth / 2.0f);
        auto boxY = centerY - (boxHeight / 2.0f);

        chromeUiRenderService.fillColor(boxX, boxY, boxWidth, boxHeight, Color(0, 0, 0, 210));
        chromeUiRenderService.drawBoxOutline(boxX, boxY, boxWidth, boxHeight, title == "VICTORY" ? Color(83, 223, 79) : Color(255, 71, 0), 2.0f);

        auto y = boxY + (lineHeight * 1.5f);
        for (const auto& line : lines)
        {
            chromeUiRenderService.drawTextCentered(centerX, y, line, *guiFont);
            y += lineHeight;
        }
    }

    UnitState& GameScene::getUnit(UnitId id)
    {
        return simulation.getUnitState(id);
    }

    const UnitState& GameScene::getUnit(UnitId id) const
    {
        return simulation.getUnitState(id);
    }

    std::optional<std::reference_wrapper<UnitState>> GameScene::tryGetUnit(UnitId id)
    {
        return simulation.tryGetUnitState(id);
    }

    std::optional<std::reference_wrapper<const UnitState>> GameScene::tryGetUnit(UnitId id) const
    {
        return simulation.tryGetUnitState(id);
    }

    GamePlayerInfo& GameScene::getPlayer(PlayerId player)
    {
        return simulation.getPlayer(player);
    }

    const GamePlayerInfo& GameScene::getPlayer(PlayerId player) const
    {
        return simulation.getPlayer(player);
    }

    bool GameScene::isEnemy(UnitId id) const
    {
        // TODO: consider allies/teams here
        return !getUnit(id).isOwnedBy(localPlayerId);
    }

    bool GameScene::isFriendly(UnitId id) const
    {
        return !isEnemy(id);
    }

    void GameScene::updateProjectiles()
    {
        for (auto& [projectileId, projectile] : simulation.projectiles)
        {
            const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(projectile.weaponType);

            // emit smoke trail
            if (weaponMediaInfo.smokeTrail)
            {
                auto gameTime = getGameTime();
                if (gameTime > projectile.lastSmoke + *weaponMediaInfo.smokeTrail)
                {
                    // The beat is kept whether or not the puff is drawn, so
                    // that lastSmoke -- which is simulation state, saved with
                    // the projectile -- says the same thing on every client
                    // however much of the map each of them can see.
                    if (positionIsVisibleToLocalPlayer(projectile.position))
                    {
                        createLightSmoke(simVectorToFloat(projectile.position));
                    }
                    projectile.lastSmoke = gameTime;
                }
            }
        }
    }

    void GameScene::processSimEvents()
    {
        for (const auto& event : simulation.events)
        {
            match(
                event,
                [&](const FeatureReclaimedEvent& e) {
                    // The feature's reclaim sequence (TA's golden swirl) plays once where it stood.
                    if (!positionIsVisibleToLocalPlayer(e.position))
                    {
                        return;
                    }
                    const auto& featureMediaInfo = gameMediaDatabase.getFeature(e.featureType);
                    if (featureMediaInfo.fileName.empty() || featureMediaInfo.seqNameReclamate.empty())
                    {
                        return;
                    }
                    if (!gameMediaDatabase.getSpriteSeries(featureMediaInfo.fileName, featureMediaInfo.seqNameReclamate))
                    {
                        return;
                    }
                    spawnExplosion(simVectorToFloat(e.position), AnimLocation{featureMediaInfo.fileName, featureMediaInfo.seqNameReclamate});
                },
                [&](const PieceExplodedEvent& e) {
                    spawnDebris(e);
                },
                [&](const FireWeaponEvent& e) {
                    const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(e.weaponType);

                    if (e.shotNumber == 0 || weaponMediaInfo.soundTrigger)
                    {
                        playWeaponStartSound(simVectorToFloat(e.firePoint), e.weaponType);
                    }

                    if (e.shotNumber == 0 && weaponMediaInfo.startSmoke && positionIsVisibleToLocalPlayer(e.firePoint))
                    {
                        createWeaponSmoke(simVectorToFloat(e.firePoint));
                    }
                },
                [&](const UnitArrivedEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Arrived1);
                    }
                },
                [&](const UnitActivatedEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Activate);

                        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && *selectedUnit == e.unitId)
                        {
                            onOff.next(true);
                        }
                    }
                },
                [&](const UnitDeactivatedEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Deactivate);

                        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit && *selectedUnit == e.unitId)
                        {
                            onOff.next(false);
                        }
                    }
                },
                [&](const UnitCompleteEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::UnitComplete);
                    }
                },
                [&](const EmitParticleFromPieceEvent& e) {
                    if (!simulation.unitExists(e.unitId))
                    {
                        return;
                    }

                    switch (e.sfxType)
                    {
                        case EmitParticleFromPieceEvent::SfxType::LightSmoke:
                            emitLightSmokeFromPiece(e.unitId, e.pieceName);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::BlackSmoke:
                            emitBlackSmokeFromPiece(e.unitId, e.pieceName);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Wake1:
                            emitWakeFromPiece(e.unitId, e.pieceName, false, 16);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Wake2:
                            emitWakeFromPiece(e.unitId, e.pieceName, false, 8);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::ReverseWake1:
                            emitWakeFromPiece(e.unitId, e.pieceName, true, 16);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::ReverseWake2:
                            emitWakeFromPiece(e.unitId, e.pieceName, true, 8);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Vtol:
                            emitVtolFromPiece(e.unitId, e.pieceName, 6);
                            break;
                        case EmitParticleFromPieceEvent::SfxType::Thrust:
                            emitVtolFromPiece(e.unitId, e.pieceName, 7);
                            break;
                        default:
                            throw std::logic_error("unknown particle type");
                    }
                },
                [&](const UnitSpawnedEvent& e) {
                    // initialise local-player-specific UI data
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        const auto& unitDefinition = simulation.unitDefinitions.at(unit->get().unitType);
                        unitGuiInfos.insert_or_assign(e.unitId, UnitGuiInfo{unitDefinition.builder ? UnitGuiInfo::Section::Build : UnitGuiInfo::Section::Orders, 0});
                    }
                },

                [&](const UnitDamagedEvent& e) {
                    // One point per weapon hit involving the local player,
                    // either side of it -- the original's scoring.
                    if (e.victimOwner == localPlayerId || (e.attackerOwner && *e.attackerOwner == localPlayerId))
                    {
                        addBattlePoints(1);
                    }
                    if (e.victimOwner == localPlayerId)
                    {
                        if (auto victim = tryGetUnit(e.unitId))
                        {
                            lastAttackPosition = victim->get().position;
                        }
                    }
                },
                [&](const UnitDiedEvent& e) {
                    const auto& unitDefinition = simulation.unitDefinitions.at(e.unitType);

                    // Five points per unit the local player kills.
                    if (e.killerOwner && *e.killerOwner == localPlayerId)
                    {
                        addBattlePoints(5);
                    }


                    // A death is asked the same question the original's draw
                    // predicate asks of a live unit, in the same order: one of
                    // your own passes before line of sight is consulted
                    // (0x465AD7, S:17). It has to be asked of the owner rather
                    // than of the ground, because the dead unit's own sight
                    // has already been taken off the grid by the time this is
                    // read -- deleteDeadUnits runs before updateVisibility --
                    // so a lone scout dying in the dark would otherwise go
                    // without so much as a flash.
                    auto deathIsVisible = (e.owner && *e.owner == localPlayerId) || positionIsVisibleToLocalPlayer(e.position);

                    const auto& selfDestructExplosion = unitDefinition.selfDestructAs.empty() ? unitDefinition.explodeAs : unitDefinition.selfDestructAs;
                    switch (e.deathType)
                    {
                        case UnitDiedEvent::DeathType::NormalExploded:
                            if (!unitDefinition.explodeAs.empty())
                            {
                                doProjectileImpact(e.position, unitDefinition.explodeAs, ImpactType::Normal, deathIsVisible);
                                addScreenShakeFromWeapon(gameMediaDatabase.getWeapon(unitDefinition.explodeAs));
                            }
                            break;
                        case UnitDiedEvent::DeathType::WaterExploded:
                            if (!unitDefinition.explodeAs.empty())
                            {
                                doProjectileImpact(e.position, unitDefinition.explodeAs, ImpactType::Water, deathIsVisible);
                                addScreenShakeFromWeapon(gameMediaDatabase.getWeapon(unitDefinition.explodeAs));
                            }
                            break;
                        case UnitDiedEvent::DeathType::SelfDestructed:
                            // The last word of the countdown belongs to the
                            // detonation itself: count0 is mapped to COUNT6,
                            // the recording the shipped data saves for the end.
                            if (auto deadUnit = tryGetUnit(e.unitId); deadUnit && deadUnit->get().isOwnedBy(localPlayerId))
                            {
                                printConsole(unitDefinition.unitName + ": zero");
                                playUnitNotificationSound(localPlayerId, e.unitType, UnitSoundType::Count0);
                            }
                            if (!selfDestructExplosion.empty())
                            {
                                doProjectileImpact(e.position, selfDestructExplosion, ImpactType::Normal, deathIsVisible);
                                addScreenShakeFromWeapon(gameMediaDatabase.getWeapon(selfDestructExplosion));
                            }
                            break;
                        case UnitDiedEvent::DeathType::Deleted:
                            // do nothing
                            break;
                    }

                    deselectUnit(e.unitId);

                    if (hoveredUnit && *hoveredUnit == e.unitId)
                    {
                        hoveredUnit = std::nullopt;
                    }

                    unitGuiInfos.erase(e.unitId);
                },
                [&](const UnitStartedBuildingEvent& e) {
                    auto unit = tryGetUnit(e.unitId);
                    if (unit)
                    {
                        playUnitNotificationSound(unit->get().owner, unit->get().unitType, UnitSoundType::Build);
                    }
                },
                [&](const UnitCapturedEvent& e) {
                    // A unit we lost must not linger in our selection.
                    if (e.previousOwner == localPlayerId)
                    {
                        deselectUnit(e.unitId);
                    }
                },
                [&](const ProjectileDetonatedEvent& e) {
                    // A `noexplode` round that went off and kept flying. It
                    // gets the impact art and the shake of an ordinary hit --
                    // the disintegrator's explode5, once per tick along its
                    // trail -- but the projectile itself is still in the air,
                    // so nothing here may treat it as finished.
                    const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(e.weaponType);
                    doProjectileImpact(e.position, e.weaponType, e.inWater ? ImpactType::Water : ImpactType::Normal, positionIsVisibleToLocalPlayer(e.position));
                    addScreenShakeFromWeapon(weaponMediaInfo);
                },
                [&](const ProjectileDiedEvent& e) {
                    const auto& weaponMediaInfo = gameMediaDatabase.getWeapon(e.weaponType);
                    if (weaponMediaInfo.endSmoke && positionIsVisibleToLocalPlayer(e.position))
                    {
                        createLightSmoke(simVectorToFloat(e.position));
                    }

                    switch (e.deathType)
                    {
                        case ProjectileDiedEvent::DeathType::NormalImpact:
                            doProjectileImpact(e.position, e.weaponType, ImpactType::Normal, positionIsVisibleToLocalPlayer(e.position));
                            addScreenShakeFromWeapon(weaponMediaInfo);
                            break;
                        case ProjectileDiedEvent::DeathType::WaterImpact:
                            doProjectileImpact(e.position, e.weaponType, ImpactType::Water, positionIsVisibleToLocalPlayer(e.position));
                            addScreenShakeFromWeapon(weaponMediaInfo);
                            break;
                        case ProjectileDiedEvent::DeathType::OutOfBounds:
                        case ProjectileDiedEvent::DeathType::EndOfLife:
                            // do nothing
                            break;
                    }
                });
        }

        simulation.events.clear();
    }

    void GameScene::addScreenShakeFromWeapon(const WeaponMediaInfo& weaponMediaInfo)
    {
        if (weaponMediaInfo.shakeMagnitude == 0 || weaponMediaInfo.shakeDuration == 0)
        {
            return;
        }

        // No falloff with distance, and none in the original either: 0x499FAB
        // hands the weapon's two numbers straight to the shake without ever
        // looking at where the explosion was or where the camera is. A blast
        // in the far corner of the map shakes the screen exactly as hard as
        // one under the cursor.
        //
        // In practice this fires on deaths rather than on shots: every weapon
        // in the shipped data that sets the keys is an explodeAs or
        // selfDestructAs -- LARGE_BUILDING, BIG_UNIT, COMMANDER_BLAST,
        // ATOMIC_BLAST and friends -- and none of them is ever fired at
        // anything. The projectile path is wired up anyway because the
        // original's single call site is in the detonation routine and applies
        // to both.
        accumulateScreenShake(
            screenShake,
            static_cast<int>(weaponMediaInfo.shakeMagnitude),
            static_cast<int>(weaponMediaInfo.shakeDuration));
    }

    void GameScene::updateScreenShake()
    {
        // Take off whatever the last frame put on, so the jitter is a wobble
        // about where the player actually left the camera rather than a random
        // walk away from it. See the note on appliedShakeOffset.
        worldCameraState.position -= appliedShakeOffset;
        appliedShakeOffset = Vector3f(0.0f, 0.0f, 0.0f);

        auto [ampX, ampY] = screenShakeAmplitudes(screenShake);
        if (ampX > 0 || ampY > 0)
        {
            // Uniform on [-amp/2, amp/2), which is what 0x41C737-0x41C755
            // builds out of a rand() and a divide by 0x8000.
            std::uniform_int_distribution<int> distX(0, ampX > 0 ? ampX - 1 : 0);
            std::uniform_int_distribution<int> distY(0, ampY > 0 ? ampY - 1 : 0);
            appliedShakeOffset = Vector3f(
                static_cast<float>(distX(effectsRng) - ampX / 2),
                0.0f,
                static_cast<float>(distY(effectsRng) - ampY / 2));
            worldCameraState.position += appliedShakeOffset;
        }

        advanceScreenShake(screenShake);
    }

    void GameScene::updateFlashes()
    {
        flashes.erase(
            std::remove_if(
                flashes.begin(),
                flashes.end(),
                [&](const auto& flash) { return flash.isFinished(simulation.gameTime); }),
            flashes.end());
    }

    void GameScene::doProjectileImpact(const SimVector& position, const std::string& weaponType, ImpactType impactType, bool visible)
    {
        // The sound is not gated. It is played on a channel with no position
        // in it at all (see playSoundAt), so it is already the whole map's
        // noise rather than something the fog could hide; what a blast must
        // not do is *light* ground the player has not seen.
        playWeaponImpactSound(simVectorToFloat(position), weaponType, impactType);
        spawnWeaponImpactExplosion(simVectorToFloat(position), weaponType, impactType, visible);
    }

    void GameScene::createLightSmoke(const Vector3f& position)
    {
        spawnSmoke(position, "FX", "smoke 1", ParticleFinishTimeEndOfFrames(), GameTime(2));
    }

    void GameScene::createWeaponSmoke(const Vector3f& position)
    {
        auto anim = sceneContext.textureService->getGafEntry("anims/FX.GAF", "smoke 1");
        spawnSmoke(position, "FX", "smoke 1", ParticleFinishTimeFixedTime{simulation.gameTime + GameTime(30)}, GameTime(15));
    }

    void GameScene::emitLightSmokeFromPiece(UnitId unitId, const std::string& pieceName)
    {
        // Same gate as the wakes and the exhaust below: the original refuses
        // every emit-sfx for a unit the local player cannot see (0x480EEA),
        // and the damage smoke of types 257 and 258 goes through that same
        // dispatch. A burning enemy behind the fog gave itself away.
        if (!positionIsVisibleToLocalPlayer(getUnit(unitId).position))
        {
            return;
        }
        auto position = simulation.getUnitPiecePosition(unitId, pieceName);
        spawnSmokePuff(simVectorToFloat(position), "smoke 1", 0.5f);
    }

    void GameScene::emitBlackSmokeFromPiece(UnitId unitId, const std::string& pieceName)
    {
        if (!positionIsVisibleToLocalPlayer(getUnit(unitId).position))
        {
            return;
        }
        auto position = simulation.getUnitPiecePosition(unitId, pieceName);
        spawnSmokePuff(simVectorToFloat(position), "smoke 2", 0.5f);
    }

    float randomFloat(float low, float high)
    {
        return low + ((static_cast<float>(rand()) / static_cast<float>(RAND_MAX)) * (high - low));
    }

    void GameScene::emitWakeFromPiece(UnitId unitId, const std::string& pieceName, bool reverse, unsigned int rampPeriod)
    {
        const auto& unit = getUnit(unitId);
        if (!positionIsVisibleToLocalPlayer(unit.position))
        {
            // The original refuses every emit-sfx for a unit the local player
            // cannot see, before it works anything else out (0x480EEA).
            return;
        }

        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
        auto pieceTransform = toFloatMatrix(simulation.getUnitPieceTransform(unitId, pieceName));
        const auto& pieceMesh = gameMediaDatabase.getUnitPieceMesh(unitDefinition.objectName, pieceName).value().get();

        // All four wake types are one routine. The only thing that separates
        // Wake from ReverseWake is which of the emitting piece's two vertices
        // the foam starts at, which turns the drift round; the only thing that
        // separates 1 from 2 is the ramp period, and with it the life.
        auto firstVertex = pieceTransform * pieceMesh.firstVertexPosition;
        auto secondVertex = pieceTransform * pieceMesh.secondVertexPosition;
        auto emission = computeWakeEmission(firstVertex, secondVertex, reverse, rampPeriod);
        const auto& spawnPosition = emission.spawnPosition;
        const auto& velocity = emission.velocity;
        auto duration = emission.duration;

        // A whole number of world units on each of the three axes, y included
        // -- the original adds its roll to the high word of each coordinate,
        // so the scatter is never fractional and is not confined to the
        // horizontal.
        std::uniform_int_distribution<int> jitter(-3, 3);
        auto scattered = [&]() {
            return Vector3f(
                spawnPosition.x + static_cast<float>(jitter(effectsRng)),
                spawnPosition.y + static_cast<float>(jitter(effectsRng)),
                spawnPosition.z + static_cast<float>(jitter(effectsRng)));
        };

        // Two dots per call: one now and one on the following tick. The
        // emitter is due again the tick after it is created and then never
        // again, so the repetition rate is entirely up to the ship's script.
        //
        // Particles drift whether or not they have started, so the second one
        // is seeded a tick's travel upstream to land on the emitter's anchor
        // at the moment it appears, which is where the original puts it.
        spawnWake(scattered(), velocity, duration, rampPeriod, simulation.gameTime);
        spawnWake(scattered() - velocity, velocity, duration, rampPeriod, simulation.gameTime + GameTime(1));
    }

    void GameScene::modifyBuildQueue(UnitId unitId, const std::string& unitType, int count)
    {
        auto unit = tryGetUnit(unitId);
        if (unit)
        {
            unit->get().modifyBuildQueue(unitType, count);

            updateUnconfirmedBuildQueueDelta(unitId, unitType, -count);
            refreshBuildGuiTotal(unitId, unitType);
        }
    }

    void GameScene::modifyStockpileQueue(UnitId unitId, int count)
    {
        simulation.modifyStockpileQueue(unitId, count);

        auto it = unconfirmedStockpileDelta.find(unitId);
        if (it != unconfirmedStockpileDelta.end())
        {
            it->second -= count;
            if (it->second == 0)
            {
                unconfirmedStockpileDelta.erase(it);
            }
        }
    }

    struct CorpseSpawnInfo
    {
        std::string featureName;
        SimVector position;
        SimAngle rotation;
    };

    void GameScene::processActions()
    {
        for (auto& a : actions)
        {
            if (!a)
            {
                continue;
            }

            if (sceneTime < a->triggerTime)
            {
                continue;
            }

            a->callback();
            a = std::nullopt;
        }
    }

    void GameScene::processPlayerCommands(const std::vector<std::pair<PlayerId, std::vector<PlayerCommand>>>& commands)
    {
        for (const auto& [issuingPlayer, playerCommands] : commands)
        {
            for (const auto& command : playerCommands)
            {
                processPlayerCommand(issuingPlayer, command);
            }
        }
    }

    void GameScene::attachOrdersMenuEventHandlers()
    {
        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "ATTACK"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<AttackCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "MOVE"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<MoveCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "DEFEND"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<GuardCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "REPAIR"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<RepairCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "PATROL"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<PatrolCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "CAPTURE"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<CaptureCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "LOAD"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<LoadCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "UNLOAD"))
        {
            p->get().addSubscription(cursorMode.subscribe([&p = p->get()](const auto& v) { p.setToggledOn(std::holds_alternative<UnloadCursorMode>(v)); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "FIREORD"))
        {
            p->get().addSubscription(fireOrders.subscribe([&p = p->get()](const auto& v) {
                switch (v)
                {
                    case UnitFireOrders::HoldFire:
                        p.setStage(0);
                        break;
                    case UnitFireOrders::ReturnFire:
                        p.setStage(1);
                        break;
                    case UnitFireOrders::FireAtWill:
                        p.setStage(2);
                        break;
                    default:
                        throw std::logic_error("Invalid FireOrders value");
                } }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "ONOFF"))
        {
            p->get().addSubscription(onOff.subscribe([&p = p->get()](const auto& v) { p.setStage(v ? 1 : 0); }));
        }

        if (auto p = findWithSidePrefix<UiStagedButton>(*currentPanel, "CLOAK"))
        {
            p->get().addSubscription(cloak.subscribe([&p = p->get()](const auto& v) { p.setStage(v ? 1 : 0); }));
        }

        currentPanel->groupMessages().subscribe([this](const auto& msg) {
            if (auto activateMessage = std::get_if<ActivateMessage>(&msg.message); activateMessage != nullptr)
            {
                onMessage(msg.controlName, activateMessage->type);
            } });
    }

    UnitFireOrders nextFireOrders(UnitFireOrders orders)
    {
        switch (orders)
        {
            case UnitFireOrders::HoldFire:
                return UnitFireOrders::ReturnFire;
            case UnitFireOrders::ReturnFire:
                return UnitFireOrders::FireAtWill;
            case UnitFireOrders::FireAtWill:
                return UnitFireOrders::HoldFire;
            default:
                throw std::logic_error("Invalid UnitFireOrders value");
        }
    }

    void GameScene::onMessage(const std::string& message, ActivateMessage::Type type)
    {
        if (matchesWithSidePrefix("ATTACK", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<AttackCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(AttackCursorMode());
            }
        }
        else if (matchesWithSidePrefix("BLAST", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<DgunCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(DgunCursorMode());
            }
        }
        else if (matchesWithSidePrefix("MOVE", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<MoveCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(MoveCursorMode());
            }
        }
        else if (matchesWithSidePrefix("DEFEND", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<GuardCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(GuardCursorMode());
            }
        }
        else if (matchesWithSidePrefix("STOP", message))
        {
            if (sounds.immediateOrders)
            {
                sceneContext.audioService->playSound(*sounds.immediateOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                cursorMode.next(NormalCursorMode());
                localPlayerStopUnit(selectedUnit);
            }
        }
        else if (matchesWithSidePrefix("RECLAIM", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<ReclaimCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(ReclaimCursorMode());
            }
        }
        else if (matchesWithSidePrefix("REPAIR", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<RepairCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(RepairCursorMode());
            }
        }
        else if (matchesWithSidePrefix("PATROL", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<PatrolCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(PatrolCursorMode());
            }
        }
        else if (matchesWithSidePrefix("CAPTURE", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<CaptureCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(CaptureCursorMode());
            }
        }
        else if (matchesWithSidePrefix("LOAD", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<LoadCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(LoadCursorMode());
            }
        }
        else if (matchesWithSidePrefix("UNLOAD", message))
        {
            if (sounds.specialOrders)
            {
                sceneContext.audioService->playSound(*sounds.specialOrders);
            }

            if (std::holds_alternative<UnloadCursorMode>(cursorMode.getValue()))
            {
                cursorMode.next(NormalCursorMode());
            }
            else
            {
                cursorMode.next(UnloadCursorMode());
            }
        }
        else if (matchesWithSidePrefix("FIREORD", message))
        {
            if (sounds.setFireOrders)
            {
                sceneContext.audioService->playSound(*sounds.setFireOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                // FIXME: should set all to a consistent single fire order rather than advancing all
                auto& u = getUnit(selectedUnit);

                // The original gathers this button out of FireStandOrders and
                // skips any unit in the selection that does not name it, so a
                // transport picked up along with an escort keeps its own order
                // instead of being dragged round the cycle with everything else.
                if (!simulation.unitDefinitions.at(u.unitType).fireStandOrders)
                {
                    continue;
                }

                auto newFireOrders = nextFireOrders(u.fireOrders);
                localPlayerSetFireOrders(selectedUnit, newFireOrders);
            }
        }
        else if (matchesWithSidePrefix("MOVEORD", message))
        {
            if (sounds.setMoveOrders)
            {
                sceneContext.audioService->playSound(*sounds.setMoveOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                auto& u = getUnit(selectedUnit);

                // Gathered from MobileStandOrders the way FIREORD comes from
                // FireStandOrders: a unit that does not name it keeps its own
                // order instead of being dragged round the cycle.
                if (!simulation.unitDefinitions.at(u.unitType).mobileStandOrders)
                {
                    continue;
                }

                auto next = u.moveOrders == UnitMovementOrders::HoldPosition
                    ? UnitMovementOrders::Maneuver
                    : (u.moveOrders == UnitMovementOrders::Maneuver ? UnitMovementOrders::Roam : UnitMovementOrders::HoldPosition);
                localPlayerSetMovementOrders(selectedUnit, next);
            }
        }
        else if (matchesWithSidePrefix("ONOFF", message))
        {
            if (sounds.immediateOrders)
            {
                sceneContext.audioService->playSound(*sounds.immediateOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                auto& u = getUnit(selectedUnit);
                auto newOnOff = !u.activated;
                localPlayerSetOnOff(selectedUnit, newOnOff);
            }
        }
        else if (matchesWithSidePrefix("CLOAK", message))
        {
            if (sounds.immediateOrders)
            {
                sceneContext.audioService->playSound(*sounds.immediateOrders);
            }

            for (const auto& selectedUnit : selectedUnits)
            {
                auto& u = getUnit(selectedUnit);

                // The original offers the button only where CloakCost is set,
                // so a mixed selection leaves everything else alone.
                if (!simulation.unitDefinitions.at(u.unitType).cloakable)
                {
                    continue;
                }

                auto newCloak = !u.cloakRequested;
                localPlayerSetCloak(selectedUnit, newCloak);
                if (auto singleUnit = getSingleSelectedUnit(); singleUnit && *singleUnit == selectedUnit)
                {
                    cloak.next(newCloak);
                }
            }
        }
        else if (matchesWithSidePrefix("NEXT", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.nextBuildMenu)
                {
                    sceneContext.audioService->playSound(*sounds.nextBuildMenu);
                }

                const auto& unit = getUnit(*selectedUnit);
                auto& guiInfo = getGuiInfo(*selectedUnit);
                auto pages = getBuildPageCount(builderGuisDatabase, unit.unitType);
                if (pages == 0)
                {
                    // No build pages for this unit at all; nothing to leaf through.
                    return;
                }
                guiInfo.currentBuildPage = (guiInfo.currentBuildPage + 1) % pages;

                auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
                if (buildPanelDefinition)
                {
                    setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                }
            }
        }
        else if (matchesWithSidePrefix("PREV", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.nextBuildMenu)
                {
                    sceneContext.audioService->playSound(*sounds.nextBuildMenu);
                }

                const auto& unit = getUnit(*selectedUnit);
                auto& guiInfo = getGuiInfo(*selectedUnit);
                auto pages = getBuildPageCount(builderGuisDatabase, unit.unitType);
                if (pages == 0)
                {
                    return;
                }
                guiInfo.currentBuildPage = guiInfo.currentBuildPage == 0 ? pages - 1 : guiInfo.currentBuildPage - 1;

                auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
                if (buildPanelDefinition)
                {
                    setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                }
            }
        }
        else if (matchesWithSidePrefix("BUILD", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.buildButton)
                {
                    sceneContext.audioService->playSound(*sounds.buildButton);
                }

                const auto& unit = getUnit(*selectedUnit);
                auto& guiInfo = getGuiInfo(*selectedUnit);
                guiInfo.section = UnitGuiInfo::Section::Build;

                auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
                if (buildPanelDefinition)
                {
                    setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
                }
            }
        }
        else if (matchesWithSidePrefix("ORDERS", message))
        {
            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                if (sounds.ordersButton)
                {
                    sceneContext.audioService->playSound(*sounds.ordersButton);
                }

                auto& guiInfo = getGuiInfo(*selectedUnit);
                guiInfo.section = UnitGuiInfo::Section::Orders;

                setNextPanel(createOrdersPanel());
            }
        }
        else if (isStockpileButtonName(message))
        {
            // A launcher's build page has one live button and it orders a round
            // rather than a unit, so the original tests for it before it tries
            // the name as a unit type (0x419B3C, ahead of the lookup at
            // 0x419B61) and turns it into the same "BUILDWEAPON" command
            // whichever of the two names it matched.
            if (sounds.addBuild)
            {
                sceneContext.audioService->playSound(*sounds.addBuild);
            }

            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                int count = (isShiftDown() ? 5 : 1) * (type == ActivateMessage::Type::Primary ? 1 : -1);
                localPlayerModifyStockpile(*selectedUnit, count);
            }
        }
        else if (isValidUnitType(simulation, message))
        {
            if (sounds.addBuild)
            {
                sceneContext.audioService->playSound(*sounds.addBuild);
            }

            if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit)
            {
                const auto& unit = getUnit(*selectedUnit);
                const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
                if (unitDefinition.isMobile)
                {
                    cursorMode.next(BuildCursorMode{message});
                }
                else
                {
                    int count = (isShiftDown() ? 5 : 1) * (type == ActivateMessage::Type::Primary ? 1 : -1);
                    localPlayerModifyBuildQueue(*selectedUnit, message, count);
                }
            }
        }
    }

    bool GameScene::matchesWithSidePrefix(const std::string& suffix, const std::string& value) const
    {
        for (const auto& [_, side] : *sceneContext.sideData)
        {
            if (side.namePrefix + suffix == value)
            {
                return true;
            }
        }

        return false;
    }

    std::optional<UnitId> GameScene::getSingleSelectedUnit() const
    {
        return selectedUnits.size() == 1
            ? std::make_optional(*selectedUnits.begin())
            : std::nullopt;
    }

    void GameScene::selectUnitsInBandbox(const DiscreteRect& box)
    {
        const auto cameraPos = worldCameraState.getRoundedPosition();
        auto cameraBox = box.translate(-cameraPos.x, -cameraPos.z);
        const auto& matrix = computeViewProjectionMatrix(worldCameraState, worldViewport.width(), worldViewport.height());
        std::unordered_set<UnitId> units;

        for (const auto& e : simulation.units)
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(e.second.unitType);
            if (!e.second.isSelectableBy(unitDefinition, localPlayerId))
            {
                continue;
            }

            const auto& worldPos = e.second.position;
            auto clipPos = matrix * simVectorToFloat(worldPos);
            Point viewportPos = worldViewport.toViewportSpace(clipPos.x, clipPos.y);
            if (!cameraBox.contains(viewportPos))
            {
                continue;
            }

            units.insert(e.first);
        }

        if (isShiftDown())
        {
            toggleUnitSelection(units);
        }
        else
        {
            replaceUnitSelection(units);
        }
    }

    void GameScene::selectAllOnScreen()
    {
        // Compute the camera's visible world rectangle directly. Matrix-based
        // projection here doesn't perform the perspective divide (see
        // Matrix4x.h:506), so we'd otherwise have no reliable on-screen test.
        const auto cameraPos = worldCameraState.getRoundedPosition();
        const float halfWidth = worldCameraState.scaleDimension(worldViewport.width()) / 2.0f;
        const float halfHeight = worldCameraState.scaleDimension(worldViewport.height()) / 2.0f;
        const float minX = cameraPos.x - halfWidth;
        const float maxX = cameraPos.x + halfWidth;
        const float minZ = cameraPos.z - halfHeight;
        const float maxZ = cameraPos.z + halfHeight;

        std::unordered_set<UnitId> units;
        for (const auto& e : simulation.units)
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(e.second.unitType);
            if (!e.second.isSelectableBy(unitDefinition, localPlayerId))
            {
                continue;
            }

            const auto worldPos = simVectorToFloat(e.second.position);
            if (worldPos.x < minX || worldPos.x > maxX
                || worldPos.z < minZ || worldPos.z > maxZ)
            {
                continue;
            }

            units.insert(e.first);
        }

        replaceUnitSelection(units);
    }

    void GameScene::toggleUnitSelection(const rwe::UnitId& unitId)
    {
        auto it = selectedUnits.find(unitId);
        if (it != selectedUnits.end())
        {
            deselectUnit(unitId);
            return;
        }

        selectAdditionalUnit(unitId);
    }

    void GameScene::toggleUnitSelection(const std::unordered_set<UnitId>& units)
    {
        std::unordered_set<UnitId> newSelection(selectedUnits);
        for (const auto& unitId : units)
        {
            auto [it, inserted] = newSelection.insert(unitId);
            if (!inserted)
            {
                newSelection.erase(it);
            }
        }

        replaceUnitSelection(newSelection);
    }

    void GameScene::selectAdditionalUnit(const rwe::UnitId& unitId)
    {
        selectedUnits.insert(unitId);

        const auto& unit = getUnit(unitId);
        auto selectionSound = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Select1);
        if (selectionSound)
        {
            playUiSound(*selectionSound);
        }

        onSelectedUnitsChanged();
    }

    void GameScene::replaceUnitSelection(const UnitId& unitId)
    {
        selectedUnits.clear();
        selectAdditionalUnit(unitId);
    }

    void GameScene::deselectUnit(const UnitId& unitId)
    {
        selectedUnits.erase(unitId);
        onSelectedUnitsChanged();
    }

    void GameScene::clearUnitSelection()
    {
        selectedUnits.clear();
        onSelectedUnitsChanged();
    }

    void GameScene::replaceUnitSelection(const std::unordered_set<UnitId>& units)
    {
        selectedUnits = units;

        if (selectedUnits.size() == 1)
        {
            const auto& unit = getUnit(*units.begin());
            auto selectionSound = getSound(simulation, gameMediaDatabase, unit.unitType, UnitSoundType::Select1);
            if (selectionSound)
            {
                playUiSound(*selectionSound);
            }
        }
        else if (selectedUnits.size() > 0)
        {
            if (sounds.selectMultipleUnits)
            {
                playUiSound(*sounds.selectMultipleUnits);
            }
        }

        onSelectedUnitsChanged();
    }

    void GameScene::onSelectedUnitsChanged()
    {
        if (selectedUnits.empty())
        {
            const auto& sidePrefix = sceneContext.sideData->at(getPlayer(localPlayerId).side).namePrefix;
            setNextPanel(uiFactory.panelFromGuiFile(sidePrefix + "MAIN2"));
        }
        else if (auto unitId = getSingleSelectedUnit(); unitId)
        {
            // Use tryGetUnit: when several units in the selection self-destruct
            // in the same tick, the first UnitDiedEvent triggers this callback
            // while the remaining selected units have already been removed from
            // the simulation but not yet from selectedUnits.
            auto unitRef = tryGetUnit(*unitId);
            if (!unitRef)
            {
                return;
            }
            const auto& unit = unitRef->get();
            fireOrders.next(unit.fireOrders);
            onOff.next(unit.activated);
            cloak.next(unit.cloakRequested);

            const auto& guiInfo = getGuiInfo(*unitId);
            auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage);
            if (guiInfo.section == UnitGuiInfo::Section::Build && buildPanelDefinition)
            {
                setNextPanel(createBuildPanel(unit.unitType + std::to_string(guiInfo.currentBuildPage + 1), *buildPanelDefinition, unit.getBuildQueueTotals()));
            }
            else
            {
                setNextPanel(createOrdersPanel());
            }
        }
        else
        {
            setNextPanel(createOrdersPanel());
        }
    }

    UnitGuiInfo& GameScene::getGuiInfo(const UnitId& unitId)
    {
        auto it = unitGuiInfos.find(unitId);
        if (it != unitGuiInfos.end())
        {
            return it->second;
        }

        // No panel state yet. This used to throw, which took the game down
        // whenever a unit was selected before its spawn event had been
        // processed — placing one from the debug window and clicking it
        // straight away, for instance. Set it up on the spot instead: a
        // builder opens on its build page, anything else on its orders.
        auto section = UnitGuiInfo::Section::Orders;
        if (auto unit = tryGetUnit(unitId))
        {
            const auto& unitDefinition = simulation.unitDefinitions.at(unit->get().unitType);
            if (unitDefinition.builder)
            {
                section = UnitGuiInfo::Section::Build;
            }
        }
        return unitGuiInfos.insert_or_assign(unitId, UnitGuiInfo{section, 0}).first->second;
    }

    void GameScene::setNextPanel(std::unique_ptr<UiPanel>&& panel)
    {
        nextPanel = std::move(panel);
    }

    void GameScene::refreshBuildGuiTotal(UnitId unitId, const std::string& unitType)
    {
        if (auto selectedUnit = getSingleSelectedUnit(); selectedUnit == unitId)
        {
            const auto& unit = getUnit(*selectedUnit);
            auto total = unit.getBuildQueueTotal(unitType) + getUnconfirmedBuildQueueCount(unitId, unitType);
            auto button = currentPanel->find<UiStagedButton>(unitType);
            if (button)
            {
                button->get().setLabel(total > 0 ? "+" + std::to_string(total) : "");
            }
        }
    }

    void GameScene::updateUnconfirmedBuildQueueDelta(UnitId unitId, const std::string& unitType, int count)
    {
        auto it = unconfirmedBuildQueueDelta.find(unitId);
        if (it == unconfirmedBuildQueueDelta.end())
        {
            unconfirmedBuildQueueDelta.emplace(unitId, std::unordered_map<std::string, int>{{unitType, count}});
        }
        else
        {
            auto it2 = it->second.find(unitType);
            if (it2 == it->second.end())
            {
                it->second.emplace(unitType, count);
            }
            else
            {
                int newTotal = it2->second + count;
                if (newTotal != 0)
                {
                    it2->second = newTotal;
                }
                else
                {
                    it->second.erase(it2);
                }
            }
        }
    }

    int GameScene::getUnconfirmedBuildQueueCount(UnitId unitId, const std::string& unitType) const
    {
        auto it = unconfirmedBuildQueueDelta.find(unitId);
        if (it == unconfirmedBuildQueueDelta.end())
        {
            return 0;
        }

        auto it2 = it->second.find(unitType);
        if (it2 == it->second.end())
        {
            return 0;
        }

        return it2->second;
    }

    void GameScene::refreshBuildGuiTotals()
    {
        auto selectedUnit = getSingleSelectedUnit();
        if (!selectedUnit)
        {
            return;
        }

        auto unit = tryGetUnit(*selectedUnit);
        if (!unit)
        {
            return;
        }

        for (const auto& child : currentPanel->getChildren())
        {
            const auto& name = child->getName();
            if (!isValidUnitType(simulation, name))
            {
                continue;
            }

            auto button = dynamic_cast<UiStagedButton*>(child.get());
            if (button == nullptr)
            {
                continue;
            }

            // "+%d" at 0x419A1E, and the caption is cleared outright at zero
            // (0x419A26 writes a nul into the buffer) rather than reading
            // "+0".
            auto total = unit->get().getBuildQueueTotal(name) + getUnconfirmedBuildQueueCount(*selectedUnit, name);
            button->setLabel(total > 0 ? "+" + std::to_string(total) : "");
        }
    }

    void GameScene::refreshStockpileGuiTotal()
    {
        auto selectedUnit = getSingleSelectedUnit();
        if (!selectedUnit)
        {
            return;
        }

        auto weapon = simulation.tryGetStockpileWeapon(*selectedUnit);
        if (!weapon)
        {
            return;
        }

        auto it = unconfirmedStockpileDelta.find(*selectedUnit);
        auto queued = weapon->get().queuedRounds + (it == unconfirmedStockpileDelta.end() ? 0 : it->second);
        auto label = stockpileButtonLabel(weapon->get().stockedRounds, std::max(0, queued));

        // The gadget's name is whatever the launcher's own GUI file called it,
        // so it has to be found the way the original's readout loop finds it:
        // by walking the panel rather than by looking a name up.
        for (const auto& child : currentPanel->getChildren())
        {
            if (isStockpileButtonName(child->getName()))
            {
                if (auto button = dynamic_cast<UiStagedButton*>(child.get()); button != nullptr)
                {
                    button->setLabel(label);
                }
            }
        }
    }

    std::unique_ptr<UiPanel> GameScene::createBuildPanel(const std::string& guiName, const std::vector<GuiEntry>& buildPanelDefinition, const std::unordered_map<std::string, int>& totals)
    {
        auto panel = uiFactory.panelFromGuiFile(guiName, buildPanelDefinition);
        applyOrderButtonGating(*panel);
        for (const auto& e : totals)
        {
            auto button = panel->find<UiStagedButton>(e.first);
            if (button)
            {
                button->get().setLabel("+" + std::to_string(e.second));
            }
        }

        return panel;
    }

    void GameScene::processPlayerCommand(PlayerId issuingPlayer, const PlayerCommand& playerCommand)
    {
        match(
            playerCommand,
            [&](const PlayerUnitCommand& c) {
                processUnitCommand(c);
            },
            [&](const PlayerPauseGameCommand&) {
                // Pause is open to any player. The local player toggles
                // `paused` immediately in the key handler so the tick loop
                // can resume to process the unpause; ignoring the round-tripped
                // command here prevents a stale pause from re-applying after
                // the user has already unpaused.
                if (issuingPlayer != localPlayerId)
                {
                    paused = true;
                }
            },
            [&](const PlayerUnpauseGameCommand&) {
                if (issuingPlayer != localPlayerId)
                {
                    paused = false;
                }
            },
            [&](const PlayerSetGameSpeedCommand& c) {
                // Host-authoritative: only honor speed changes from player 0.
                // Non-host requests are silently dropped.
                if (issuingPlayer == PlayerId(0))
                {
                    gameSpeed = GameSpeed(c.speedIndex);
                }
            });
    }

    void GameScene::processUnitCommand(const PlayerUnitCommand& unitCommand)
    {
        match(
            unitCommand.command,
            [&](const PlayerUnitCommand::IssueOrder& c) {
                switch (c.issueKind)
                {
                    case PlayerUnitCommand::IssueOrder::IssueKind::Immediate:
                        issueUnitOrder(unitCommand.unit, c.order);
                        break;
                    case PlayerUnitCommand::IssueOrder::IssueKind::Queued:
                        enqueueUnitOrder(unitCommand.unit, c.order);
                        break;
                }
            },
            [&](const PlayerUnitCommand::ModifyBuildQueue& c) {
                modifyBuildQueue(unitCommand.unit, c.unitType, c.count);
            },
            [&](const PlayerUnitCommand::ModifyStockpile& c) {
                modifyStockpileQueue(unitCommand.unit, c.count);
            },
            [&](const PlayerUnitCommand::Stop&) {
                stopUnit(unitCommand.unit);
            },
            [&](const PlayerUnitCommand::SetFireOrders& c) {
                setFireOrders(unitCommand.unit, c.orders);
            },
            [&](const PlayerUnitCommand::SetMovementOrders& c) {
                if (auto unit = tryGetUnit(unitCommand.unit))
                {
                    unit->get().moveOrders = c.orders;
                }
            },
            [&](const PlayerUnitCommand::SetOnOff& c) {
                if (c.on)
                {
                    simulation.activateUnit(unitCommand.unit);
                }
                else
                {
                    simulation.deactivateUnit(unitCommand.unit);
                }
            },
            [&](const PlayerUnitCommand::SetCloak& c) {
                // This only records what the unit is asking for. Whether it
                // actually cloaks is settled a second at a time by the energy
                // and by how close the nearest enemy is standing.
                if (auto unit = tryGetUnit(unitCommand.unit); unit)
                {
                    unit->get().cloakRequested = c.cloaked;
                }
            },
            [&](const PlayerUnitCommand::CancelBuildOrder& c) {
                cancelBuildOrderAt(unitCommand.unit, c.position);
            },
            [&](const PlayerUnitCommand::SelfDestruct&) {
                // Starts the countdown, or cancels it if pressed again.
                simulation.toggleSelfDestruct(unitCommand.unit);
            });
    }

    bool GameScene::leftClickMode() const
    {
        return sceneContext.globalConfig->leftClickInterfaceMode;
    }

    void GameScene::spawnExplosion(const Vector3f& position, const AnimLocation& anim)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = Vector3f(0.0f, 0.0f, 0.0f);
        particle.renderType = ParticleRenderTypeSprite{
            anim.gafName,
            anim.animName,
            ParticleFinishTimeEndOfFrames(),
            GameTime(2),
            false,
        };
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::spawnFlash(const Vector3f& position)
    {
        FlashEffect flash;
        flash.position = position;
        flash.startTime = simulation.gameTime;
        flash.duration = GameTime(15);
        flash.maxRadius = 30.0f;
        flash.color = Vector3f(1.0f, 1.0f, 1.0f);
        flash.maxIntensity = 1.0f;
        flashes.push_back(flash);
    }

    void GameScene::spawnSmoke(const Vector3f& position, const std::string& gaf, const std::string& anim, ParticleFinishTime duration, GameTime frameDuration)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = Vector3f(0.0f, 0.5f, 0.0f);
        particle.renderType = ParticleRenderTypeSprite{
            gaf,
            anim,
            duration,
            frameDuration,
            true,
        };
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::spawnSmokePuff(const Vector3f& position, const std::string& anim, float riseRate)
    {
        auto numberOfFrames = static_cast<int>(gameMediaDatabase.getSpriteSeries("FX", anim).value()->sprites.size());

        Particle particle;
        particle.position = position;

        // The original lifts a puff by some multiple of the map's gravity
        // every tick -- four for a damaged unit, sixteen for a thermal vent.
        // On the 112 that nearly every shipped map uses four works out at
        // 0.498 world units, so the callers here pass half a unit and two,
        // which is the right answer for all but a handful of maps and saves
        // threading the map's gravity through to reach.
        particle.velocity = Vector3f(0.0f, riseRate, 0.0f);

        particle.renderType = ParticleRenderTypeSprite{
            "FX",
            anim,
            ParticleFinishTimeEndOfFrames(),
            GameTime(2),
            true,
            false,
            makeSmokePuffFrameSchedule(numberOfFrames, [](int n) { return std::rand() % n; }),
        };
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::spawnGeoVentSteam()
    {
        // Every vent puffs on the same ticks because the original makes all
        // their emitters on the same tick, at map load, and each then counts
        // its own five ticks from there.
        if (simulation.gameTime.value % geoVentSteamIntervalTicks != 0)
        {
            return;
        }

        for (const auto& point : findGeoVentSteamPoints(simulation))
        {
            // A vent under the fog of war keeps its steam to itself.
            if (!positionIsVisibleToLocalPlayer(SimVector(SimScalar(point.x), SimScalar(point.y), SimScalar(point.z))))
            {
                continue;
            }
            spawnSmokePuff(point, "smoke 1", geoVentSteamRiseRate);
        }
    }

    void GameScene::spawnWake(const Vector3f& position, const Vector3f& velocity, GameTime duration, unsigned int rampPeriod, GameTime startTime)
    {
        Particle particle;
        particle.position = position;
        particle.velocity = velocity;
        particle.renderType = ParticleRenderTypeWake{startTime + duration, rampPeriod};
        particle.startTime = startTime;

        particles.push_back(particle);
    }

    void GameScene::spawnNanoParticles()
    {
        for (const auto& [unitId, unit] : simulation.units)
        {
            auto nanolatheTarget = unit.getActiveNanolatheTarget();
            if (!nanolatheTarget || !unitIsVisibleToLocalPlayer(unitId, unit))
            {
                continue;
            }

            // Where the spray lands. The original samples a uniform point in
            // the target's bounding box, shrunk first to the middle three
            // elevenths of each axis, so the stream fans across the middle of
            // what is being worked on rather than converging on a point.
            //
            // The one place we depart from it is the height: the original
            // samples inside the model too, which it can afford because its
            // spray is composited over the world in a late layer. Ours is
            // depth tested so that a construction aircraft can cover its own
            // beam, so it lands on the roof instead and the structure cannot
            // swallow the end of the stream.
            std::optional<Vector3f> targetCentre;
            // The width and depth of the target we scatter the landing point over.
            // Nothing in y: the height is already in targetCentre, because we
            // land on the roof rather than inside the model.
            Vector3f spread(0.0f, 0.0f, 0.0f);
            bool reclaimingFeature = false;
            match(
                std::get<0>(*nanolatheTarget),
                [&](const UnitId& targetUnitId) {
                    auto targetUnit = tryGetUnit(targetUnitId);
                    if (!targetUnit)
                    {
                        return;
                    }
                    const auto& targetDefinition = simulation.unitDefinitions.at(targetUnit->get().unitType);
                    const auto& targetModel = simulation.unitModelDefinitions.at(targetDefinition.objectName);
                    auto height = simScalarToFloat(targetModel.height);
                    targetCentre = simVectorToFloat(targetUnit->get().position) + Vector3f(0.0f, height, 0.0f);
                    auto [footprintX, footprintZ] = simulation.getFootprintXZ(targetDefinition.movementCollisionInfo);
                    auto tile = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
                    spread = Vector3f(static_cast<float>(footprintX) * tile, 0.0f, static_cast<float>(footprintZ) * tile);
                },
                [&](const FeatureId& targetFeatureId) {
                    auto targetFeature = simulation.tryGetFeature(targetFeatureId);
                    if (!targetFeature)
                    {
                        return;
                    }
                    const auto& featureDefinition = simulation.getFeatureDefinition(targetFeature->get().featureName);
                    auto height = simScalarToFloat(featureDefinition.height);
                    targetCentre = simVectorToFloat(targetFeature->get().position) + Vector3f(0.0f, height, 0.0f);
                    auto tile = simScalarToFloat(MapTerrain::HeightTileWidthInWorldUnits);
                    spread = Vector3f(static_cast<float>(featureDefinition.footprintX) * tile, 0.0f, static_cast<float>(featureDefinition.footprintZ) * tile);
                    // The original runs two emitters when reclaiming a feature.
                    reclaimingFeature = true;
                });
            if (!targetCentre)
            {
                continue;
            }

            auto nozzle = simVectorToFloat(std::get<1>(*nanolatheTarget));
            std::uniform_real_distribution<float> centralThird(-3.0f / 11.0f, 3.0f / 11.0f);

            // The original emits a burst of five every tick, and each emitter
            // fires again on the following tick, so ten particles a tick are in
            // flight. Reclaiming a feature runs two emitters, which is what
            // makes a reclaim stream look twice as thick as a build stream.
            const int burstSize = 5;
            const int burstsPerTick = 2;
            auto emitters = reclaimingFeature ? 2 : 1;
            for (int emitter = 0; emitter < emitters; ++emitter)
            {
                for (int burst = 0; burst < burstsPerTick; ++burst)
                {
                    for (int i = 0; i < burstSize; ++i)
                    {
                        // The nozzle end has no scatter at all; the far end is
                        // a uniform point in the middle three elevenths of the
                        // target's bounding box, which is the window the
                        // original shrinks the box to before sampling it.
                        auto landing = *targetCentre + Vector3f(centralThird(effectsRng) * spread.x, 0.0f, centralThird(effectsRng) * spread.z);

                        // Each particle starts one place further along the
                        // seven-colour cycle than the last.
                        auto colorPhase = static_cast<unsigned char>(i % 7);

                        switch (std::get<2>(*nanolatheTarget))
                        {
                            case UnitState::NanolatheDirection::Forward:
                                spawnNanoParticle(nozzle, landing, colorPhase);
                                break;
                            case UnitState::NanolatheDirection::Reverse:
                                spawnNanoParticle(landing, nozzle, colorPhase);
                                break;
                            default:
                                throw std::logic_error("unhandled nanolathe direction");
                        }
                    }
                }
            }
        }
    }

    void GameScene::spawnNanoParticle(const Vector3f& from, const Vector3f& to, unsigned char colorPhase)
    {
        // Four world units a tick, as in the original: it divides the distance
        // by four to get the step count, then walks the particle along one step
        // per tick. A target closer than one step gets no particle at all.
        const float speed = 4.0f;
        auto delta = to - from;
        auto ticks = static_cast<int>(delta.length() / speed);
        if (ticks < 1)
        {
            return;
        }

        Particle particle;
        particle.position = from;
        particle.velocity = delta / static_cast<float>(ticks);
        particle.renderType = ParticleRenderTypeNano{
            simulation.gameTime + GameTime(ticks),
            colorPhase,
            // The original fills a two pixel square, which at one world unit
            // per pixel is a half-size of one.
            1.0f,
            // No nudge towards the camera: the spray leaves a nozzle
            // underneath a construction aircraft, so the aircraft has to be
            // able to cover it. It clears the structure by landing on top of
            // it instead — see where the target point is chosen above.
            0.0f};
        particle.startTime = simulation.gameTime;

        particles.push_back(particle);
    }

    void GameScene::emitVtolFromPiece(UnitId unitId, const std::string& pieceName, unsigned int divisor)
    {
        // `Thrust` and `Vtol` are the same emitter with one number changed:
        // the original passes 6 for one and 7 for the other, and that number
        // is both the divisor for the drift and the emitter's own lifetime,
        // so a thrust plume is one puff longer and each puff moves a little
        // more slowly. Nothing else about them differs.
        const auto& unit = getUnit(unitId);
        if (!positionIsVisibleToLocalPlayer(unit.position))
        {
            return;
        }
        const auto& unitDefinition = simulation.unitDefinitions.at(unit.unitType);
        auto pieceTransform = toFloatMatrix(simulation.getUnitPieceTransform(unitId, pieceName));
        const auto& pieceMesh = gameMediaDatabase.getUnitPieceMesh(unitDefinition.objectName, pieceName).value().get();

        // A thruster piece is a bare two-vertex segment running from hull
        // level down to a few units below it, and the exhaust comes out of
        // the bottom. TA's own models wind these inconsistently — on the
        // Atlas, two jets run bottom to top and the third runs top to bottom
        // — so taking the difference would have one rotor spraying upwards.
        // Take the lower end and blow downwards instead.
        auto a = pieceTransform * pieceMesh.firstVertexPosition;
        auto b = pieceTransform * pieceMesh.secondVertexPosition;
        const auto& lowerEnd = a.y <= b.y ? a : b;
        const auto& upperEnd = a.y <= b.y ? b : a;

        // TA draws this effect with a sprite, not with coloured dots: the
        // engine's handler for emit-sfx type 0 hands the piece's two vertices
        // to a particle that plays the `flamestream` sequence out of
        // anims/FX.GAF, which starts as a two pixel yellow speck and swells,
        // frame by frame, into a ragged yellow flame about nine pixels
        // across. The engine divides the piece vector by six for the drift
        // per tick and gives the particle six ticks to live, so one puff
        // crosses the length of the thruster while it grows.
        auto velocity = (lowerEnd - upperEnd) / static_cast<float>(divisor);

        // TA drops one of these every tick and lets a whole run of them die
        // together, so the plume is a graded line with the biggest, oldest
        // flame furthest from the nozzle. The script only calls us every
        // other tick, so lay several at once and backdate the trailing ones:
        // each starts a frame further into the animation and a step further
        // down, which is exactly where the engine's own would have got to.
        const int particlesPerEmit = 4;
        const unsigned int lifeInTicks = divisor + 1;
        std::uniform_real_distribution<float> scatter(-0.75f, 0.75f);
        for (int i = 0; i < particlesPerEmit; ++i)
        {
            auto age = std::min(static_cast<unsigned int>(i), simulation.gameTime.value);

            Particle particle;
            particle.position = lowerEnd + (velocity * static_cast<float>(i)) + Vector3f(scatter(effectsRng), 0.0f, scatter(effectsRng));
            // The puffs keep no share of the aircraft's speed: they hang
            // where they were dropped, so the aircraft draws a trail out
            // behind itself as it flies on.
            particle.velocity = velocity;
            particle.startTime = simulation.gameTime - GameTime(age);
            particle.renderType = ParticleRenderTypeSprite{
                "FX",
                "flamestream",
                ParticleFinishTimeFixedTime{simulation.gameTime + GameTime(lifeInTicks - age)},
                // One animation frame per tick, so the flame grows as fast as
                // it falls, the way the original's does.
                GameTime(1),
                false,
                // Drawn among the world's geometry: the exhaust leaves from
                // under the hull, so the hull must cover it.
                true,
            };
            particles.push_back(particle);
        }
    }

    void GameScene::recreateWorldRenderTextures()
    {
        // Anti-aliasing: the original renders each unit into a double-size
        // bitmap and box-filters it down through the alpha table. Here the
        // whole world buffer is doubled and the blit's linear filter does the
        // averaging, which costs one draw and catches every edge rather than
        // only unit silhouettes.
        worldRenderTextureScale = antiAliasEnabled ? 2u : 1u;
        auto width = worldViewport.width() * worldRenderTextureScale;
        auto height = worldViewport.height() * worldRenderTextureScale;
        worldFrameBuffer = sceneContext.graphics->createFrameBuffer(width, height);
        dodgeMask = sceneContext.graphics->createEmptyTexture(width, height);
        worldRenderTextureSize = {worldViewport.width(), worldViewport.height()};
    }

    void GameScene::nudgeCamera(int millisecondsElapsed, const Rectangle2f& cameraConstraint, int directionX, int directionZ)
    {
        assert(directionX == 1 || directionX == 0 || directionX == -1);
        assert(directionZ == 1 || directionZ == 0 || directionZ == -1);

        // The player can only nudge the camera in free mode.
        // If the camera is in a different mode, try and transition out of it.
        cameraControlState = match(
            cameraControlState,
            [&](const CameraControlStateTrackingUnit&) -> CameraControlState {
                return CameraControlStateFree();
            },
            [&](const CameraControlStateFree& s) -> CameraControlState {
                return s;
            },
            [&](const CameraControlStateMiddleMousePan& s) -> CameraControlState {
                // Middle mouse pan takes precedence over nudging the camera.
                return s;
            });

        // Only nudge the camera if it is now in free mode.
        match(
            cameraControlState,
            [&](const CameraControlStateFree&) {
                const float speed = CameraPanSpeed * (static_cast<float>(scrollSpeedSetting) / 100.0f) * millisecondsElapsed / 1000.0f;

                auto dx = directionX * speed;
                auto dz = directionZ * speed;
                const auto& cameraPos = worldCameraState.position;
                auto newPos = cameraConstraint.clamp(Vector2f(cameraPos.x + dx, cameraPos.z + dz));

                worldCameraState.position = Vector3f(newPos.x, cameraPos.y, newPos.y);
            },
            [&](const CameraControlStateTrackingUnit&) {
                // do nothing
            },
            [&](const CameraControlStateMiddleMousePan&) {
                // do nothing
            });
    }

    void GameScene::relocateCamera(const Rectangle2f& cameraConstraint, float x, float z)
    {
        // The player can only relocate the camera in free mode.
        // If the camera is in a different mode, try and transition out of it.
        cameraControlState = match(
            cameraControlState,
            [&](const CameraControlStateTrackingUnit&) -> CameraControlState {
                return CameraControlStateFree();
            },
            [&](const CameraControlStateFree& s) -> CameraControlState {
                return s;
            },
            [&](const CameraControlStateMiddleMousePan& s) -> CameraControlState {
                // Middle mouse pan takes precedence over relocating the camera.
                return s;
            });

        auto newCameraPos = cameraConstraint.clamp(Vector2f(x, z));
        worldCameraState.position = Vector3f(newCameraPos.x, worldCameraState.position.y, newCameraPos.y);
    }

    std::optional<std::string> GameScene::getUnitBuildButtonUnderCursor() const
    {
        auto cursorPosition = getMousePosition();
        auto control = currentPanel->findAtPosition<UiStagedButton>(cursorPosition.x, cursorPosition.y);
        if (!control)
        {
            return std::nullopt;
        }

        const auto& name = control->get().getName();
        if (!isValidUnitType(simulation, name))
        {
            return std::nullopt;
        }

        return name;
    }
}
