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
#include <rwe/game/chat_util.h>
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

// The input half of GameScene: the keyboard and mouse handlers, split out of
// GameScene.cpp for the reason set out at the head of GameScene_render.cpp --
// one translation unit had grown past the 32767 sections a COFF object can
// address, and the assembler was covering for it with the pe-bigobj format
// that the CI toolchain then failed to read back.

namespace rwe
{
    void GameScene::onKeyDown(const SDL_KeyboardEvent& keysym)
    {
        // The message bar owns the keyboard ahead of anything else, the menu
        // included: while it is open, every key on the board is a letter.
        if (isChatBarOpen())
        {
            handleChatBarKey(keysym);
            return;
        }

        // The game menu owns the keyboard while it is up. Tab and F2 toggle
        // it (the original's keys: Tab opens GAME OPTIONS in single player,
        // F2 anywhere), though not once the game has given way to the picture
        // or the chart, whose dialogs are ENDMSN's own; Escape closes it.
        if ((keysym.key == SDLK_TAB || keysym.key == SDLK_F2) && !endGameCoversWorld())
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

        // Between missions Enter is Start (0x41F198).
        if ((keysym.key == SDLK_RETURN || keysym.key == SDLK_KP_ENTER) && endGameChartVisible() && endGameCampaignPanel)
        {
            campaignEndMessage("Start");
            return;
        }

        // Enter opens the message bar, as it does in the original. Ahead of
        // the panel below, which would otherwise take Enter as an activation
        // of whichever button last had focus.
        if (keysym.key == SDLK_RETURN || keysym.key == SDLK_KP_ENTER)
        {
            openChatBar();
            return;
        }

        // Suppress UI panel key activation when Ctrl is held — Ctrl+letter is
        // hotkey territory (Ctrl+A select-all, Ctrl+S stop, Ctrl+D self-destruct,
        // etc.), and the panel's letter-bound buttons (e.g. ATTACK on plain "A")
        // would otherwise also fire.
        // Space is the panel peek in game (76), so it does not reach the side
        // panel's gadgets -- a focused UiStagedButton activates on Space
        // (UiStagedButton.cpp), and the peek would fire whatever button last
        // had focus. The game menu is handled above and has already returned,
        // so this only takes Space away from the in-game panel.
        if (!isCtrlDown() && keysym.key != SDLK_SPACE)
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
        else if (keysym.key == SDLK_F9 && !isCtrlDown())
        {
            networkOverlayVisible = !networkOverlayVisible;
        }
        else if (keysym.key == SDLK_F1)
        {
            helpVisible = !helpVisible;
        }
        else if (keysym.key == SDLK_F4)
        {
            // 76: F4 is the only thing that touches the latch, and it does not
            // persist -- a new game starts with the panel out.
            panelHiddenLatch = !panelHiddenLatch;
        }
        else if (keysym.key == SDLK_SPACE)
        {
            spaceDown = true;
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
            // the current view instead, as the original does. A bookmark
            // holds the position only: recalling one deliberately leaves the
            // current zoom alone.
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
                        if (unitRef && unitRef->get().isAlive() && unitRef->get().isOwnedBy(localPlayerId) && !unitRef->get().heldByMission)
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

    void GameScene::onTextInput(const std::string& text)
    {
        // The message bar first: it is open only while it is being typed into.
        if (isChatBarOpen())
        {
            chatTextAppend(*chatInput, text);
            return;
        }

        // The game menu is the only thing in a battle with a field in it, and
        // only while it is open; the side panel is buttons.
        if (!isGameMenuOpen())
        {
            return;
        }

        for (auto& panel : gameMenuPanels)
        {
            panel->textInput(text);
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
        else if (keysym.key == SDLK_SPACE)
        {
            spaceDown = false;
        }
    }

    void GameScene::onMouseDown(MouseButtonEvent event)
    {
        // Once the game is decided the world stops taking orders. The chart
        // takes two clicks and no more: one anywhere fills every bar at once,
        // which is what a click does during the original's run-up (0x420028),
        // and one on Main Menu leaves.
        if (gameOver)
        {
            if (endGamePhase == EndGamePhase::Glamour && event.button == MouseButtonEvent::MouseButton::Left)
            {
                glamourClicked();
                return;
            }

            if (endGameChartVisible() && isGameMenuOpen())
            {
                // A save or load dialog ENDMSN opened, in the chart's space.
                auto p = endGameScreenPoint(event.x, event.y);
                for (auto& panel : gameMenuPanels)
                {
                    panel->mouseDown(MouseButtonEvent(p.x, p.y, event.button));
                }
                return;
            }

            if (endGameChartVisible() && endGameCampaignPanel && event.button == MouseButtonEvent::MouseButton::Left)
            {
                // ENDMSN's controls find their own gadget; a click anywhere
                // also fills the bars, as on the plain chart.
                auto p = endGameScreenPoint(event.x, event.y);
                endGameCampaignPanel->mouseDown(MouseButtonEvent(p.x, p.y, event.button));
                finishEndGameBars();
                return;
            }

            if (endGameChartVisible() && event.button == MouseButtonEvent::MouseButton::Left)
            {
                // The button is laid out in the chart's own 640x480 space, so
                // the click has to arrive there too -- and only if it landed on
                // the button, because a UiStagedButton does not test that for
                // itself: the panel that normally owns it does, and there is no
                // panel here.
                auto p = endGameScreenPoint(event.x, event.y);
                if (endGameMainMenuButton && endGameMainMenuButton->contains(p.x, p.y))
                {
                    endGameMainMenuButton->mouseDown(MouseButtonEvent(p.x, p.y, event.button));
                    endGameChartButtonArmed = true;
                    return;
                }

                // Anywhere else fills every bar at once, which is what a click
                // does during the original's run-up (0x420028).
                finishEndGameBars();
            }
            return;
        }

        // The UI is drawn through a scaled projection, so a panel takes its
        // events in raw UI coordinates. The world logic below keeps the
        // frame-space event.
        if (isGameMenuOpen())
        {
            auto ui = toUiCoordinates(event.x, event.y);
            MouseButtonEvent uiEvent(ui.x, ui.y, event.button);
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseDown(uiEvent);
            }
            return;
        }

        {
            auto ui = toUiCoordinates(event.x, event.y);
            currentPanel->mouseDown(MouseButtonEvent(ui.x, ui.y, event.button));
        }

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
                    // The MOVE button is not a plain move: command 2 is the
                    // original's full context ladder (0x43F845), and it is
                    // the only armed command that can produce a pickup, a
                    // capture or a landing without its own button. A move
                    // onto one of our own repair pads has always come out of
                    // it as VTOL_LANDING (0x43FB1B), which is the gesture the
                    // FAQ describes: "Select the plane, click on Move and
                    // then click on the repair pad." See TOTALA-EXE.md S:103.
                    auto issued = false;
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        issued = issueDefaultAction(selectedUnit, DefaultActionScheme::MoveButton) || issued;
                    }
                    if (issued && !isShiftDown())
                    {
                        cursorMode.next(NormalCursorMode());
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
                                // A move the unit is walking becomes the route's
                                // first waypoint, otherwise its own position
                                // does. See issueOrder.
                                localPlayerIssueUnitOrder(selectedUnit, PatrolOrder(*coord));
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
                                issueDefaultAction(selectedUnit, DefaultActionScheme::LeftClickDefault);
                            }
                        }
                        else
                        {
                            cursorMode.next(NormalCursorMode{NormalCursorMode::DraggingMinimapState()});
                        }
                    }
                    else if (isCursorOverWorld())
                    {
                        auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, Point(event.x, event.y));
                        cursorMode.next(NormalCursorMode{NormalCursorMode::SelectingState(sceneTime, worldViewportToCameraPlane(worldViewportPos))});
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
                        // "Right Click" mode issues the default action on the
                        // right button (0x4991D5), and it is the longer of
                        // the two ladders -- capture, reclaim, repair, land
                        // on a pad, pick up, guard, and a move at the end of
                        // it. This click used to fall through and issue
                        // nothing at all on a friendly unit. The cursor is
                        // not consulted here, and the original does not
                        // consult it either. See TOTALA-EXE.md S:103.
                        for (const auto& selectedUnit : selectedUnits)
                        {
                            issueDefaultAction(selectedUnit, DefaultActionScheme::RightClickDefault);
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
        if (gameOver)
        {
            if (endGameChartVisible() && isGameMenuOpen())
            {
                auto p = endGameScreenPoint(event.x, event.y);
                for (auto& panel : gameMenuPanels)
                {
                    panel->mouseUp(MouseButtonEvent(p.x, p.y, event.button));
                }
                return;
            }

            if (endGameChartVisible() && endGameCampaignPanel)
            {
                auto p = endGameScreenPoint(event.x, event.y);
                endGameCampaignPanel->mouseUp(MouseButtonEvent(p.x, p.y, event.button));
                return;
            }

            if (endGameChartVisible() && endGameMainMenuButton && endGameChartButtonArmed)
            {
                endGameChartButtonArmed = false;
                auto p = endGameScreenPoint(event.x, event.y);
                endGameMainMenuButton->mouseUp(MouseButtonEvent(p.x, p.y, event.button));
            }
            return;
        }

        if (isGameMenuOpen())
        {
            auto ui = toUiCoordinates(event.x, event.y);
            MouseButtonEvent uiEvent(ui.x, ui.y, event.button);
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseUp(uiEvent);
            }
            return;
        }

        {
            auto ui = toUiCoordinates(event.x, event.y);
            currentPanel->mouseUp(MouseButtonEvent(ui.x, ui.y, event.button));
        }

        if (event.button == MouseButtonEvent::MouseButton::Left)
        {
            match(
                cursorMode.getValue(),
                [&](const NormalCursorMode& normalCursor) {
                    match(
                        normalCursor.state,
                        [&](const NormalCursorMode::SelectingState& state) {
                            auto worldViewportPos = sceneContext.viewport->toOtherViewport(worldViewport, Point(event.x, event.y));
                            auto startViewportPos = cameraPlaneToWorldViewport(state.startPosition);

                            if (sceneTime - state.startTime < SceneTime(30) && startViewportPos.maxSingleDimensionDistance(worldViewportPos) < 32)
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
                                else if (leftClickMode())
                                {
                                    // The shipped scheme's shorter ladder,
                                    // 0x43FE35 for the order and 0x43E512
                                    // for the cursor that gates it. The
                                    // select arm above is the ladder's
                                    // fourth, lifted out because a selection
                                    // is not made per selected unit and
                                    // because shift toggles it rather than
                                    // queueing anything. See TOTALA-EXE.md
                                    // S:103.
                                    for (const auto& selectedUnit : selectedUnits)
                                    {
                                        issueDefaultAction(selectedUnit, DefaultActionScheme::LeftClickDefault);
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
                                selectUnitsInBandbox(DiscreteRect::fromPoints(startViewportPos, worldViewportPos));
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

    bool GameScene::issueDefaultAction(UnitId selectedUnit, DefaultActionScheme scheme)
    {
        auto action = computeDefaultAction(simulation, scheme, selectedUnit, hoveredUnit, hoveredFeature);

        std::optional<UnitOrder> order;
        if (auto o = std::get_if<DefaultActionOrder>(&action.action); o != nullptr)
        {
            order = o->order;
        }
        else if (std::holds_alternative<DefaultActionMove>(action.action))
        {
            // The ladder says "go there" without knowing where there is; the
            // point comes from the same place every other move order's does.
            if (auto coord = getMouseTerrainCoordinate(); coord)
            {
                order = MoveOrder(*coord);
            }
        }

        if (!order)
        {
            return false;
        }

        if (isShiftDown())
        {
            localPlayerEnqueueUnitOrder(selectedUnit, *order);
        }
        else
        {
            localPlayerIssueUnitOrder(selectedUnit, *order);
        }

        return true;
    }

    CursorType GameScene::selectionDefaultCursor(DefaultActionScheme scheme) const
    {
        auto cursor = CursorType::Normal;
        for (const auto& selectedUnit : selectedUnits)
        {
            cursor = preferredCursor(cursor, computeDefaultAction(simulation, scheme, selectedUnit, hoveredUnit, hoveredFeature).cursor);
        }
        return cursor;
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
        if (gameOver)
        {
            if (endGameChartVisible() && isGameMenuOpen())
            {
                auto p = endGameScreenPoint(event.x, event.y);
                for (auto& panel : gameMenuPanels)
                {
                    panel->mouseMove(MouseMoveEvent(p.x, p.y));
                }
            }
            else if (endGameChartVisible() && endGameCampaignPanel)
            {
                auto p = endGameScreenPoint(event.x, event.y);
                endGameCampaignPanel->mouseMove(MouseMoveEvent(p.x, p.y));
            }
            else if (endGameChartVisible() && endGameMainMenuButton)
            {
                auto p = endGameScreenPoint(event.x, event.y);
                endGameMainMenuButton->mouseMove(MouseMoveEvent(p.x, p.y));
            }
            return;
        }

        if (isGameMenuOpen())
        {
            auto ui = toUiCoordinates(event.x, event.y);
            MouseMoveEvent uiEvent(ui.x, ui.y);
            for (auto& panel : gameMenuPanels)
            {
                panel->mouseMove(uiEvent);
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

        {
            auto ui = toUiCoordinates(event.x, event.y);
            currentPanel->mouseMove(MouseMoveEvent(ui.x, ui.y));
        }
    }

    void GameScene::onMouseWheel(MouseWheelEvent event)
    {
        // A wheel event carries scroll amounts rather than a position, so
        // there is nothing to convert.
        if (endGameChartVisible() && endGameCampaignPanel && !isGameMenuOpen())
        {
            endGameCampaignPanel->mouseWheel(event);
            return;
        }

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

}
