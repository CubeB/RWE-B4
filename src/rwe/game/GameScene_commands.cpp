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

// Cursor picking and the local player's commands, split out of GameScene.cpp
// for the reason set out at the head of GameScene_render.cpp: a COFF object
// can address 32767 sections, and at -O0 this scene's share of the standard
// library's variant, string and vector machinery ran well past it.

namespace rwe
{
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
        /** H:MM:SS, the way DIFF's neighbours read -- no leading zero on the hour, one always on the rest. */
        std::string formatSaveGameTime(unsigned int totalSeconds)
        {
            auto hours = totalSeconds / 3600u;
            auto minutes = (totalSeconds % 3600u) / 60u;
            auto seconds = totalSeconds % 60u;
            auto pad2 = [](unsigned int v) {
                auto s = std::to_string(v);
                return s.size() < 2 ? std::string("0") + s : s;
            };
            return std::to_string(hours) + ":" + pad2(minutes) + ":" + pad2(seconds);
        }

        void setSaveListLabel(UiPanel& panel, const char* gadget, const std::string& text)
        {
            if (auto label = panel.find<UiLabel>(gadget))
            {
                label->get().setText(text);
            }
        }

        /** Fills the GAMES listbox and mirrors clicks into the name box and the metadata labels. */
        void wireSaveList(UiPanel& panel)
        {
            // The metadata gadgets default to their caption text, doubling
            // the captions the art already paints; they stay empty until a
            // selected save can fill them in.
            for (const auto* name : {"GAMETYPE", "SIDE", "MISSION", "DIFF", "TIME"})
            {
                setSaveListLabel(panel, name, std::string());
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
                if (!games || !box || *index >= games->get().getItems().size())
                {
                    return;
                }
                auto name = games->get().getItems()[*index];
                box->get().setText(name);

                // One file read per click, not the whole list -- this only
                // has to answer for the entry that was just clicked, and
                // reading every save's header to populate a listbox would
                // mean opening every file on disk on every dialog open.
                auto save = readSaveFile(savePathForName(name));
                if (!save)
                {
                    for (const auto* gadget : {"GAMETYPE", "SIDE", "MISSION", "DIFF", "TIME"})
                    {
                        setSaveListLabel(panel, gadget, std::string());
                    }
                    return;
                }

                setSaveListLabel(panel, "MISSION", save->parameters.mapName);

                auto isNetwork = std::any_of(save->parameters.players.begin(), save->parameters.players.end(), [](const auto& p) {
                    return p && std::holds_alternative<PlayerControllerTypeNetwork>(p->controller);
                });
                setSaveListLabel(panel, "GAMETYPE", isNetwork ? "Network" : "Skirmish");

                // No slot in GameParameters says which player is local, so
                // (per LoadingScene's own reading of a fresh game) the first
                // human controller stands in for it.
                std::string side;
                for (const auto& p : save->parameters.players)
                {
                    if (p && std::visit(IsHumanVisitor(), p->controller))
                    {
                        side = p->side;
                        break;
                    }
                }
                setSaveListLabel(panel, "SIDE", side);

                setSaveListLabel(panel, "DIFF", aiDifficultyDisplayName(save->parameters.aiDifficulty));

                setSaveListLabel(panel, "TIME", save->gameTimeSeconds ? formatSaveGameTime(*save->gameTimeSeconds) : std::string());
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
        save.gameTimeSeconds = simulation.gameTime.value / static_cast<unsigned int>(SimTicksPerSecond);
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
            // TOTALA-EXE.md S:63: campaign/skirmish gets this enabled with
            // the fixed text "Restart"; only multiplayer keeps it blank and
            // dead, and RWE has no multiplayer game in progress to gate it
            // on here yet.
            button->get().setEnabled(true);
            button->get().setLabel("Restart");
        }
        setGameMenuPanel(std::move(panel));
    }

    void GameScene::openRestartMenu()
    {
        // RESTART.GUI, ui_probe'd against the shipped data: its buttons are
        // named CANCEL and RESTART, same as the panel's own topic.
        setGameMenuPanel(uiFactory.panelFromGuiFile("RESTART"));
    }

    void GameScene::restartGame()
    {
        // Same pipeline loadSavedGame hands a save to, minus the save: a
        // fresh LoadingScene over the game's own parameters spawns the
        // starting commanders exactly as the first load did.
        sceneContext.audioService->stopMusic();
        auto scene = std::make_shared<LoadingScene>(
            sceneContext,
            audioLookup,
            AudioService::LoopToken(),
            gameParameters);
        sceneContext.sceneManager->setNextScene(scene);
    }

    void GameScene::openConfirmDialog(const std::string& title, std::function<void()> onYes, std::function<void()> onNo)
    {
        // YESORNO.GUI is the original's one confirmation dialog, reused for
        // both exit prompts and (below) the save-list overwrite/delete
        // prompts: TOTALA-EXE.md S:63 has it asking under CHOICE1/CHOICE2,
        // and nothing else about it changes between call sites.
        auto panel = uiFactory.panelFromGuiFile("YESORNO");

        // S:63 says the question goes into a TITLE gadget. The shipped file
        // has no such gadget: it is three entries, the panel and the two
        // buttons, and the original writes the question into the panel's own
        // text at 0x4605c0. A UiPanel has no text, so the question gets a
        // label of its own, centred across the panel above the buttons --
        // which sit at y=55, 20 tall, in a box 400x100. Without this the
        // dialog asked nothing at all and offered Yes and No to a blank
        // plate.
        if (auto label = panel->find<UiLabel>("TITLE"))
        {
            label->get().setText(title);
        }
        else
        {
            panel->appendChild(uiFactory.createLabel(0, 20, panel->getWidth(), 20, title, UiLabel::Alignment::Center));
        }
        pendingConfirmAction = std::move(onYes);
        pendingConfirmCancel = std::move(onNo);
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

    namespace
    {
        /**
         * How much of the PALETTE.SHD lookup each kind of model gets.
         *
         * The original has no such split. 0x459C70 shades every unit and
         * every feature through one path and never reads a "is a building"
         * flag (TOTALA-EXE-SHADING.md S:11, NOT FOUND), so this is a
         * deliberate divergence and is recorded as one in TOTALA-EXE.md S:88.
         *
         * The reason to have it is that the two read differently on screen
         * even though the arithmetic is identical. A building is a large,
         * still, mostly flat-sided model: its faces hold one shade level
         * across a big area, the wrap puts a whole wall near row 0, and the
         * banding reads as deliberate. A unit is small, in motion, and
         * shaded in world space rather than object space -- so the bands
         * slide across it as it turns, which reads as flicker rather than
         * form. Pulling the unit strength down keeps the shape information
         * and drops most of that movement.
         *
         * Both strengths default to 1.0, the faithful setting: row 0 is
         * genuinely black, as the original's is, on units and buildings
         * alike. The two values are rwe.cfg keys (shading-strength-units,
         * shading-strength-buildings) for anyone who wants it softer, and
         * the VISUALS switch still turns either category off outright.
         */
    }

    float GameScene::shadeStrengthFor(bool isBuilding) const
    {
        const auto& config = *sceneContext.globalConfig;
        if (isBuilding)
        {
            return shadingModeCoversBuildings(shadingMode)
                ? static_cast<float>(config.shadingStrengthBuildings) / 100.0f
                : 0.0f;
        }
        return shadingModeCoversUnits(shadingMode)
            ? static_cast<float>(config.shadingStrengthUnits) / 100.0f
            : 0.0f;
    }

    void GameScene::widenShadingButton()
    {
        // VISUALRT.GUI declares SHADING with two stages, Off|On, because the
        // original has two whole rasterizer chains and one bit to choose
        // between them. Splitting it by category needs four stages, and the
        // GUI files are read-only game data with no override directory, so
        // the gadget is rebuilt here at exactly the geometry the data gave
        // it. openInGameOptions clears and rebuilds every panel each time it
        // runs, so doing this unconditionally from wireInGameOptionControls
        // is idempotent.
        for (auto& panel : gameMenuPanels)
        {
            uiFactory.replaceStagedButton(*panel, "VISUALRT", "SHADING", "SHADINGMODE", shadingModeLabels(), static_cast<unsigned int>(shadingMode));
            addBuildingHaloButton(*panel);
        }
    }

    void GameScene::addBuildingHaloButton(UiPanel& panel)
    {
        // VISUALRT has no gadget for the purple building fringe, and the GUI
        // files are read-only game data with no override directory, so the
        // button is built here -- the same reasoning that rebuilds SHADING
        // above, one step further because this one does not exist at all.
        //
        // Where it goes is derived, not guessed: one row below the shadows
        // toggle, with the row step taken from the gap between that and the
        // anti-alias toggle above it. On this panel ui_probe measures SHADING
        // at y=63, ANTI at 108 and BSHADOWS at 152, and the next gadget down
        // is RESTORE at 269 -- so a 44-pixel step lands at 196 with room to
        // spare. It borrows BSHADOWS's artwork so it looks like the toggles
        // either side of it rather than like an addition.
        uiFactory.addStagedButtonBelow(panel, "VISUALRT", "BSHADOWS", "HALO", "BSHADOWS", "ANTI", {"Fringe Off", "Fringe On"}, buildingHaloEnabled ? 1 : 0);
    }

    void GameScene::wireInGameOptionControls()
    {
        widenShadingButton();

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
            toggle->setStage(static_cast<unsigned int>(shadingMode));
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("ANTI"))
        {
            toggle->setStage(antiAliasEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("HALO"))
        {
            toggle->setStage(buildingHaloEnabled ? 1 : 0);
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
            shadingMode,
            antiAliasEnabled,
            buildingHaloEnabled};
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
        shadingMode = state.shading;
        buildingHaloEnabled = state.buildingHalo;
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
                std::string name;
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                if (!name.empty())
                {
                    openConfirmDialog(
                        "Delete the saved game?",
                        [this, name]() {
                            std::error_code ec;
                            std::filesystem::remove(savePathForName(name), ec);
                            openSaveDialog();
                        },
                        [this]() { openSaveDialog(); });
                }
                else
                {
                    openSaveDialog();
                }
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
                if (std::filesystem::exists(savePathForName(name)))
                {
                    openConfirmDialog(
                        "Overwrite the saved game?",
                        [this, name]() {
                            saveCurrentGame(name);
                            openGameMenuRoot();
                        },
                        [this]() { openSaveDialog(); });
                }
                else
                {
                    saveCurrentGame(name);
                    openGameMenuRoot();
                }
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
                std::string name;
                if (!gameMenuPanels.empty())
                {
                    if (auto box = gameMenuPanels.front()->find<UiTextBox>("GAMENAME"); box && !box->get().getText().empty())
                    {
                        name = box->get().getText();
                    }
                }
                if (!name.empty())
                {
                    openConfirmDialog(
                        "Delete the saved game?",
                        [this, name]() {
                            std::error_code ec;
                            std::filesystem::remove(savePathForName(name), ec);
                            openLoadDialog();
                        },
                        [this]() { openLoadDialog(); });
                }
                else
                {
                    openLoadDialog();
                }
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
            else if (control == "RESTART")
            {
                openRestartMenu();
            }
            else if (control == "EXITGAME")
            {
                // TOTALA-EXE.md S:63, mode 2: "Surrender this battle and exit to Windows?"
                openConfirmDialog(
                    "Surrender this battle and exit to Windows?",
                    [this]() { sceneContext.sceneManager->requestExit(); },
                    [this]() { openGameExitMenu(); });
            }
            else if (control == "MAINMENU")
            {
                // TOTALA-EXE.md S:63, mode 0: "Surrender this battle and return to main menu?"
                openConfirmDialog(
                    "Surrender this battle and return to main menu?",
                    [this]() { exitToMainMenu(); },
                    [this]() { openGameExitMenu(); });
            }
        }
        else if (topic == "RESTART")
        {
            if (control == "CANCEL")
            {
                openGameExitMenu();
            }
            else if (control == "RESTART")
            {
                restartGame();
            }
        }
        else if (topic == "YESORNO")
        {
            if (control == "CHOICE1")
            {
                auto action = pendingConfirmAction;
                pendingConfirmAction = nullptr;
                pendingConfirmCancel = nullptr;
                if (action)
                {
                    action();
                }
            }
            else if (control == "CHOICE2")
            {
                auto cancel = pendingConfirmCancel;
                pendingConfirmAction = nullptr;
                pendingConfirmCancel = nullptr;
                if (cancel)
                {
                    cancel();
                }
                else
                {
                    openGameMenuRoot();
                }
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
                shadingMode = nextStage(shadingMode);
            }
            else if (control == "ANTI")
            {
                antiAliasEnabled = !antiAliasEnabled;
                recreateWorldRenderTextures();
            }
            else if (control == "HALO")
            {
                buildingHaloEnabled = !buildingHaloEnabled;
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
            toggle->setStage(static_cast<unsigned int>(shadingMode));
        }
        if (auto toggle = findInGameMenu<UiStagedButton>("ANTI"))
        {
            toggle->setStage(antiAliasEnabled ? 1 : 0);
        }

        if (auto toggle = findInGameMenu<UiStagedButton>("HALO"))
        {
            toggle->setStage(buildingHaloEnabled ? 1 : 0);
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
            if (!replayPlayback)
            {
                return vis;
            }

            // Watching a recording with the fog on: show what BOTH sides can
            // see, which is neither player's own grid. One side's fog would
            // hide half of what there is to watch, and a lit map hides the
            // thing the fog is interesting for -- who had eyes where, and
            // when. Rebuilt when the tick moves on, because visibility does.
            if (!combinedVisibility
                || combinedVisibilityTick != simulation.gameTime.value
                || combinedVisibility->explored.getWidth() != vis.explored.getWidth()
                || combinedVisibility->explored.getHeight() != vis.explored.getHeight())
            {
                PlayerVisibility combined(vis.explored.getWidth(), vis.explored.getHeight());
                auto& explored = combined.explored.getVector();
                auto& visible = combined.visible.getVector();
                std::fill(explored.begin(), explored.end(), static_cast<unsigned char>(0));
                std::fill(visible.begin(), visible.end(), static_cast<unsigned char>(0));
                for (const auto& other : simulation.playerVisibility)
                {
                    const auto& otherExplored = other.explored.getVector();
                    const auto& otherVisible = other.visible.getVector();
                    for (std::size_t i = 0; i < explored.size() && i < otherExplored.size(); ++i)
                    {
                        explored[i] = explored[i] || otherExplored[i];
                        visible[i] = visible[i] || otherVisible[i];
                    }
                }
                combinedVisibility = std::move(combined);
                combinedVisibilityTick = simulation.gameTime.value;
            }
            return *combinedVisibility;
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

        if (spectatorMode)
        {
            // A spectator is not a player and there is nobody for a unit to
            // be hidden from. Switching the fog off is not enough on its own:
            // a cloaked enemy stays hidden with the fog off, because hiding
            // it was never a fog rule.
            return true;
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
        buildingMask = sceneContext.graphics->createEmptyTexture(width, height);
        // Attached as a second colour target of the world framebuffer, so the
        // passes that draw the world fill it as they go. See unitTexture.frag.
        sceneContext.graphics->attachFrameBufferMaskBuffer(worldFrameBuffer.frameBuffer.get(), buildingMask.get());
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
