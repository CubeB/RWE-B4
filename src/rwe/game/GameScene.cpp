#include "GameScene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
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
#include <rwe/util/CrashHandler.h>
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
          shadingMode(static_cast<ShadingMode>(sceneContext.globalConfig->shadingMode)),
          antiAliasEnabled(sceneContext.globalConfig->antiAlias),
          shadowsEnabled(sceneContext.globalConfig->shadows),
          scrollSpeedSetting(sceneContext.globalConfig->scrollSpeed),
          gameParameters(gameParameters),
          audioLookup(audioLookup),
          stateLogStream(std::move(stateLogStream))
    {
        if (this->gameParameters.aiArenaSeconds)
        {
            // One row every ten seconds of game time: the interesting thing
            // is the shape of the curve, and a row is cheap.
            const unsigned int sampleIntervalTicks = 10u * static_cast<unsigned int>(SimTicksPerSecond);
            arenaReport.emplace(sampleIntervalTicks);
            arenaEndTick = *this->gameParameters.aiArenaSeconds * static_cast<unsigned int>(SimTicksPerSecond);
            LOG_INFO << "AI arena: running for " << *this->gameParameters.aiArenaSeconds
                     << " seconds of game time (" << *arenaEndTick << " ticks)";
        }
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
        setCrashScene("GameScene");
        setCrashMap(gameParameters.mapName.c_str());
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
        if (replaySeekTarget)
        {
            // Seeking: fill the accumulator past anything the cap will
            // dispatch, so the block runs at whatever rate the machine
            // manages rather than at the rate the clock ticks.
            millisecondsBuffer = static_cast<unsigned int>(SimMillisecondsPerTick) * 2001u;
        }
        else if (replayPlayback)
        {
            // Speed is a whole multiple of real time and multiplies the
            // elapsed time rather than the game speed, so that one second of
            // watching is one second of the recorded game at 1x whatever the
            // frame rate is doing.
            if (replayPlaying)
            {
                millisecondsBuffer += millisecondsElapsed * static_cast<unsigned int>(std::max(replaySpeed, 1));
            }
        }
        else if (!paused)
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

        // Watching a recording: every command for every player comes out of
        // the file, one set per player per tick, pushed in tryTickGame where
        // the tick number is known. Nothing below here may push as well.
        if (!replayPlayback)
        {

        // In an AI arena there is no human at all and the local player is
        // itself a computer, so its commands arrive through the drain below
        // like everybody else's. Pushing here as well would put two entries a
        // tick into one player's queue and take it out of step with the rest.
        if (simulation.getPlayer(localPlayerId).type == GamePlayerType::Human)
        {
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
        }

        // If we are waiting to swap in a new unit GUI panel, do that now
        if (nextPanel)
        {
            currentPanel = std::move(*nextPanel);
            nextPanel = std::nullopt;
            attachOrdersMenuEventHandlers();
        }

        // The drift gate below asks the network thread what time everyone
        // else is at, and skips ticks to stay level with them. There is
        // nobody else in a replay, and letting it skip would end the playback
        // at a different game time than the recording did.
        auto averageSceneTime = replayPlayback ? sceneTime : gameNetworkService->estimateAvergeSceneTime(sceneTime);

        // allow skipping sim frames every so often to get back down to average.
        // We tolerate X frames of drift in either direction to cope with noisiness in the estimation.
        const SceneTime frameTolerance(3);
        const SceneTime frameCheckInterval(5);
        auto highSceneTime = averageSceneTime + frameTolerance;
        auto lowSceneTime = averageSceneTime <= frameTolerance ? SceneTime{0} : averageSceneTime - frameTolerance;
        // Cap the number of sim ticks we dispatch per frame to prevent
        // a runaway "spiral of death" if frame times spike at high speeds.
        //
        // Watching a recording raises it instead of raising the game speed.
        // Speed scales the accumulator, and whatever the cap then refuses to
        // dispatch is thrown away below -- so a fast-forward driven that way
        // silently drops ticks and finishes the replay early, which is the
        // one thing a replay must not do. Seeking runs flat out in blocks,
        // large enough to cross ten minutes in a couple of seconds and small
        // enough that the window still answers between them.
        const int maxTicksPerFrame = replaySeekTarget
            ? 2000
            : (replayPlayback ? 10 * std::max(replaySpeed, 1) : 10);
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

        if (replaySeekTarget && sceneTime.value >= *replaySeekTarget)
        {
            LOG_INFO << "Replay: seek reached tick " << sceneTime.value;
            replaySeekTarget.reset();
            millisecondsBuffer = 0;
        }

        // The recording has run out. Pause rather than carry on ticking a
        // game with no more commands coming, which looks like the viewer has
        // frozen when in fact it has finished.
        if (replayPlayback && !replaySeekTarget && !replayReachedEnd
            && sceneTime.value >= replayPlayback->lastTick)
        {
            replayReachedEnd = true;
            replayPlaying = false;
            millisecondsBuffer = 0;
            LOG_INFO << "Replay: reached the end at tick " << sceneTime.value;
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

        renderReplayWindow();
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

            // Force every header open while filtering -- the point of typing
            // is to see what matched, not to then go opening headers -- but
            // only while filtering. This used to pass !filter.empty() with
            // ImGuiCond_Always unconditionally, which re-asserted "closed" on
            // every frame once the box was empty: a click opened the node and
            // the next frame shut it again.
            if (!filter.empty())
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            // The ID comes from the bare category and the count is only in the
            // format string, so a header keeps its open state when the filter
            // changes how many units it matches. Building the label into the
            // ID, as this did, made every distinct count a different node.
            if (ImGui::TreeNode(category.c_str(), "%s (%d)", category.c_str(), static_cast<int>(matching.size())))
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

}
