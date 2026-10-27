#include "GameScene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <rwe/CroppedViewport.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/Mesh.h>
#include <rwe/ai/AiPlayerController.h>
#include <rwe/camera_util.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/game/InterfaceRules.h>
#include <rwe/game/OrderButtons.h>
#include <rwe/game/OrderDispatch.h>
#include <rwe/game/SaveFile.h>
#include <rwe/game/chat_util.h>
#include <rwe/game/dump_util.h>
#include <rwe/game/matrix_util.h>
#include <rwe/game/save_util.h>
#include <rwe/io/gui/gui.h>
#include <rwe/render/render_prof.h>
#include <rwe/resource_io.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/sim/UnitBehaviorService.h>
#include <rwe/sim/UnitBehaviorService_util.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/Index.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/util/match.h>
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
        // The shared pause key remains available while options or chat owns
        // other keys. Holding it must not generate repeated toggles.
        if (keysym.key == SDLK_PAUSE)
        {
            if (!keysym.repeat)
            {
                togglePause();
            }
            return;
        }
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

        // The picture and the chart each take a key (BT-199, 0x4C1AB0): any key
        // dismisses the picture once its one-second lock is up, and a key fills
        // the chart's bars at once. Ahead of Enter's message bar below, which
        // must not open over either; on the chart Escape keeps leaving.
        if (gameOver && keyDismissesEndScreen(keysym.key))
        {
            if (endGamePhase == EndGamePhase::Glamour)
            {
                glamourClicked();
                return;
            }
            if (endGameChartVisible() && keysym.key != SDLK_ESCAPE)
            {
                finishEndGameBars();
                return;
            }
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
        // Auto-repeat is kept from it too: B is held as a selection-box
        // filter, and every repeat would press BUILD again, sound and all.
        if (!isCtrlDown() && keysym.key != SDLK_SPACE && !keysym.repeat)
        {
            currentPanel->keyDown(KeyEvent(keysym.key));
        }

        // Held as filters for the selection box, whatever else the key
        // does: B also opens a builder's BUILD tab through the panel above.
        if (keysym.key == SDLK_W)
        {
            bandboxFilter.armedMobile = true;
        }
        else if (keysym.key == SDLK_B)
        {
            bandboxFilter.constructors = true;
        }
        else if (keysym.key == SDLK_Y)
        {
            bandboxFilter.factories = true;
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
            // persist -- a new game starts without the list out. It latches the
            // score panel's player list and nothing else (BT-183, 0x4963A3).
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
            // original's default paging keys. They page with the toggle
            // argument set (BT-104, 0x41BDE0 and 0x41BF10): stepping past
            // the last page closes the menu, and stepping from closed opens
            // the first page going forward or the last going back. The
            // panel's NEXT and PREV buttons pass it clear and wrap. The
            // sound plays whether or not there is a unit to page.
            if (sounds.nextBuildMenu)
            {
                sceneContext.audioService->playSound(*sounds.nextBuildMenu);
            }
            if (auto selectedUnit = getSingleSelectedUnit())
            {
                const auto& unit = getUnit(*selectedUnit);
                auto pages = getBuildPageCount(builderGuisDatabase, unit.unitType);
                if (pages > 0)
                {
                    auto& guiInfo = getGuiInfo(*selectedUnit);
                    auto next = stepBuildMenu(
                        BuildMenuPosition{guiInfo.section == UnitGuiInfo::Section::Build, guiInfo.currentBuildPage},
                        pages,
                        keysym.key == SDLK_PERIOD,
                        true);
                    guiInfo.currentBuildPage = next.page;
                    guiInfo.section = next.open ? UnitGuiInfo::Section::Build : UnitGuiInfo::Section::Orders;
                    if (!next.open)
                    {
                        setNextPanel(createOrdersPanel());
                    }
                    else if (auto buildPanelDefinition = getBuilderGui(builderGuisDatabase, unit.unitType, guiInfo.currentBuildPage))
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
                if (sounds.selectSquad)
                {
                    playUiSound(*sounds.selectSquad);
                }
            }
            else if (cameraBookmarks[slot])
            {
                // BT-CATEGORY-CAMERA-KEY-FINDINGS: stored bookmark recall
                // cancels following; F3/n and the general setter do not.
                if (std::holds_alternative<CameraControlStateTrackingUnit>(cameraControlState))
                {
                    cameraControlState = CameraControlStateFree();
                }
                trackedUnitId.reset();
                setCameraPosition(*cameraBookmarks[slot]);
                if (sounds.selectSquad)
                {
                    playUiSound(*sounds.selectSquad);
                }
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
            selectNextOwnUnit(nextUnitCursor, [](const UnitState&, const UnitDefinition&) { return true; });
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
        else if (keysym.key == SDLK_A && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+A selects every unit the player owns, anywhere on the map
            // -- the original's special case, not merely what is on screen.
            // Every selectable, finished, uncarried one, buildings and
            // factories included: "idle" and "movers" are not what the
            // service (0x48BD50) tests (BT-180). It also lets go of the
            // camera if it was following a unit.
            selectAllWhere([](const UnitState& u, const UnitDefinition& d) { return sweepSelectsUnit(u.isBeingBuilt(d), u.carriedBy.has_value()); });
            startTrackInternal({});
        }
        else if (keysym.key == SDLK_S && isCtrlDown() && !isShiftDown())
        {
            // The 3.9.01 patch's Ctrl+S, armed units only, where the
            // original takes everything on screen (TOTALA-EXE.md S:88).
            // Stop lives on the plain S quickkey through the orders panel.
            selectAllOnScreen();
        }
        else if (keysym.key == SDLK_D && isCtrlDown() && !isShiftDown())
        {
            // Ctrl+D: self-destruct selected units (TA behaviour).
            // Routes through the deterministic command queue so the
            // explosion happens at the same game tick on all peers.
            //
            // A selection where some units are already counting down cancels
            // those and arms none of the others; only when none is counting
            // does the key arm the lot (BT-181, 0x49641D). The command is the
            // same toggle either way.
            std::vector<UnitId> candidates;
            for (const auto& unitId : selectedUnits)
            {
                const auto& unit = tryGetUnit(unitId);
                if (unit && unit->get().isAlive() && unit->get().isOwnedBy(localPlayerId))
                {
                    candidates.push_back(unitId);
                }
            }
            std::sort(candidates.begin(), candidates.end());
            std::vector<bool> counting;
            counting.reserve(candidates.size());
            for (const auto& unitId : candidates)
            {
                counting.push_back(getUnit(unitId).selfDestructTime.has_value());
            }
            auto recipients = selfDestructRecipients(counting);
            for (auto index : recipients)
            {
                localPlayerSelfDestructUnit(candidates[index]);
            }
            LOG_INFO << "Self-destruct toggled for " << recipients.size() << " selected unit(s)";
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
        else if (keysym.key == SDLK_F && isCtrlDown() && !isShiftDown())
        {
            // The 3.9.01 patch's walk through idle factories, where the
            // original selects every CTRL_F unit (TOTALA-EXE.md S:88).
            selectNextOwnUnit(idleFactoryCursor, [](const UnitState& u, const UnitDefinition& d) {
                return isIdleFactory(d.isMobile, d.builder, u.buildQueue.empty());
            });
        }
        else if (keysym.key == SDLK_B && isCtrlDown() && !isShiftDown())
        {
            // The 3.9.01 patch's walk through idle builders, where the
            // original selects every CTRL_B unit (TOTALA-EXE.md S:88).
            selectNextOwnUnit(idleBuilderCursor, [](const UnitState& u, const UnitDefinition& d) {
                return isIdleBuilder(d.isMobile, d.builder, u.orders.empty());
            });
        }
        else if (isCtrlDown() && !isShiftDown()
            && (keysym.key == SDLK_W || keysym.key == SDLK_P
                || keysym.key == SDLK_V || keysym.key == SDLK_R))
        {
            // The original's Ctrl+letter selections are data-driven: it
            // formats "CTRL_%c" and matches the FBI Category tokens, and the
            // shipped data defines CTRL_W (armed mobile units), CTRL_V
            // (vtols), CTRL_R (radars and jammers) and CTRL_P (armed
            // aircraft). CTRL_F and CTRL_B are in the data too, but their
            // keys are the 3.9.01 patch's idle walks above.
            char letter = static_cast<char>(std::toupper(keysym.key));
            selectAllByCategoryToken(std::string("CTRL_") + letter);
        }
        else
        {
            // Control groups: keys 1-0 map to groups 0-9.
            // Ctrl+digit  → bind current selection to group (replace).
            // Shift+digit → recall group, adding it to the current selection
            //               (the squad recall's second argument is the Shift
            //               state, BT-182: 0x48D9A0).
            // Digit alone → recall group (replace current selection).
            // Ctrl+Shift+digit adds the current selection to the group; it is
            // RWE's own, the original having no such binding.
            // Assigning plays CreateSquad and recalling SelectSquad.
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
                if (isCtrlDown())
                {
                    if (isShiftDown())
                    {
                        // Ctrl+Shift+digit: add selection to control group.
                        for (const auto& unitId : selectedUnits)
                        {
                            controlGroups[idx].insert(unitId);
                        }
                    }
                    else
                    {
                        // Ctrl+digit: bind (replace) control group with current selection.
                        controlGroups[idx] = selectedUnits;
                    }
                    if (sounds.createSquad)
                    {
                        playUiSound(*sounds.createSquad);
                    }
                }
                else
                {
                    // Digit alone: recall control group.
                    // Filter out any units that are now dead or no longer owned
                    // by the local player so stale IDs do not pollute the set.
                    // The test is the select-everything sweep's (BT-180).
                    std::unordered_set<UnitId> liveUnits;
                    for (const auto& unitId : controlGroups[idx])
                    {
                        auto unitRef = tryGetUnit(unitId);
                        if (unitRef && unitRef->get().isAlive() && unitRef->get().isOwnedBy(localPlayerId) && !unitRef->get().heldByMission
                            && sweepSelectsUnit(unitRef->get().isBeingBuilt(simulation.unitDefinitions.at(unitRef->get().unitType)), unitRef->get().carriedBy.has_value()))
                        {
                            liveUnits.insert(unitId);
                        }
                    }
                    // Prune the stored group to remove dead entries.
                    controlGroups[idx] = liveUnits;
                    if (sounds.selectSquad)
                    {
                        playUiSound(*sounds.selectSquad);
                    }
                    if (isShiftDown())
                    {
                        // Shift adds the squad to what is selected already.
                        auto combined = selectedUnits;
                        combined.insert(liveUnits.begin(), liveUnits.end());
                        replaceUnitSelection(combined);
                    }
                    else
                    {
                        replaceUnitSelection(liveUnits);
                    }
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
        else if (keysym.key == SDLK_W)
        {
            bandboxFilter.armedMobile = false;
        }
        else if (keysym.key == SDLK_B)
        {
            bandboxFilter.constructors = false;
        }
        else if (keysym.key == SDLK_Y)
        {
            bandboxFilter.factories = false;
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
            // The picture goes at the release of either button (BT-199): the
            // original refreshes its pointer record on the left- and
            // right-button-up messages and dismisses on that record.
            if (endGamePhase == EndGamePhase::Glamour)
            {
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
                            // The original's attack arm can answer "no order"
                            // (a ground gun against a submerged submarine) and
                            // then the unit keeps its queue. BT-63.
                            auto orderer = simulation.tryGetUnitState(selectedUnit);
                            auto victim = simulation.tryGetUnitState(*hoveredUnit);
                            if (orderer && victim
                                && !attackOrderIsAccepted(simulation, simulation.unitDefinitions.at(orderer->get().unitType), orderer->get(), victim->get()))
                            {
                                continue;
                            }

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
                            // And a first weapon that is anti-air makes no
                            // order against a place either (BT-63).
                            auto groundOrderer = simulation.tryGetUnitState(selectedUnit);
                            if (coord && groundOrderer && !attackGroundOrderIsAccepted(simulation, groundOrderer->get()))
                            {
                                continue;
                            }
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
                        // The armed button asks only that the unit can capture
                        // and that the target belongs to someone else: an ally's
                        // unit is accepted (BT-107, 0x43F6D1 and 0x43E797).
                        auto orderer = simulation.tryGetUnitState(selectedUnit);
                        auto victim = hoveredUnit ? simulation.tryGetUnitState(*hoveredUnit) : std::nullopt;
                        if (orderer && victim
                            && armedCaptureAccepts(simulation.unitDefinitions.at(orderer->get().unitType).canCapture, orderer->get().owner, victim->get().owner))
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
                                localPlayerEnqueueUnitOrder(selectedUnit, mouseFormationOrder(selectedUnit, PatrolOrder(*coord)));
                            }
                            else
                            {
                                // A move the unit is walking becomes the route's
                                // first waypoint, otherwise its own position
                                // does. See issueOrder.
                                localPlayerIssueUnitOrder(selectedUnit, mouseFormationOrder(selectedUnit, PatrolOrder(*coord)));
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
                    // Every selected builder gets the site (BT-108, 0x419670):
                    // the original walks the whole selection and gives each
                    // builder its own order, with no lead-builder rule and no
                    // limit of one. The armed cursor is not disarmed by a
                    // change of selection, so it follows whoever is selected
                    // when the click lands.
                    std::vector<UnitId> builders;
                    for (const auto& selectedUnit : selectedUnits)
                    {
                        if (auto unit = simulation.tryGetUnitState(selectedUnit); unit && !unit->get().isDead())
                        {
                            const auto& definition = simulation.unitDefinitions.at(unit->get().unitType);
                            if (buildClickTakesUnit(definition.builder, definition.isMobile))
                            {
                                builders.push_back(selectedUnit);
                            }
                        }
                    }
                    // The set has no order of its own; the commands should not
                    // depend on how it happens to hash.
                    std::sort(builders.begin(), builders.end());

                    if (!builders.empty() && hoverBuildInfo)
                    {
                        if (hoverBuildInfo->isValid)
                        {
                            auto topLeftWorld = simulation.terrain.heightmapIndexToWorldCorner(hoverBuildInfo->rect.x,
                                hoverBuildInfo->rect.y);
                            auto x = topLeftWorld.x + ((SimScalar(hoverBuildInfo->rect.width) * MapTerrain::HeightTileWidthInWorldUnits) / 2_ss);
                            auto z = topLeftWorld.z + ((SimScalar(hoverBuildInfo->rect.height) * MapTerrain::HeightTileHeightInWorldUnits) / 2_ss);
                            // BUILD-GRID-001: the accepted order retains the
                            // preview's placement plane, including water yards.
                            const auto& definition = simulation.unitDefinitions.at(buildCursor.unitType);
                            auto y = simulation.computeBuildHeight(definition, hoverBuildInfo->rect);
                            SimVector buildPos(x, y, z);
                            if (isShiftDown())
                            {
                                for (const auto& builder : builders)
                                {
                                    // Shift-clicking a building already in the plan takes it out again.
                                    if (auto planned = plannedBuildOrderAt(builder, buildPos))
                                    {
                                        localPlayerCancelBuildOrder(builder, *planned);
                                    }
                                    else
                                    {
                                        localPlayerEnqueueUnitOrder(builder, BuildOrder(buildCursor.unitType, buildPos));
                                    }
                                }
                            }
                            else
                            {
                                for (const auto& builder : builders)
                                {
                                    localPlayerIssueUnitOrder(builder, BuildOrder(buildCursor.unitType, buildPos));
                                }
                                cursorMode.next(NormalCursorMode());
                            }
                        }
                        else if (isShiftDown() && getMouseTerrainCoordinate())
                        {
                            // The spot is blocked by our own plan: shift-click removes that plan.
                            auto cancelled = false;
                            for (const auto& builder : builders)
                            {
                                if (auto planned = plannedBuildOrderAt(builder, *getMouseTerrainCoordinate()))
                                {
                                    localPlayerCancelBuildOrder(builder, *planned);
                                    cancelled = true;
                                }
                            }
                            if (!cancelled && sounds.notOkToBuild)
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
                        cursorMode.next(NormalCursorMode{NormalCursorMode::SelectingState(sceneTime, worldViewportToCameraPlane(worldViewportPos), sceneContext.timeService->getTicks())});
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
            if (endGamePhase == EndGamePhase::Glamour
                && (event.button == MouseButtonEvent::MouseButton::Left || event.button == MouseButtonEvent::MouseButton::Right))
            {
                glamourClicked();
                return;
            }

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

                            // A click or a drag is decided on the wall clock and on
                            // the map, not on simulation ticks and screen pixels:
                            // fewer than 25 of the clock's thirtieths of a second
                            // and fewer than 32 world units on each axis
                            // (BT-102, 0x4995AE).
                            auto releasePosition = worldViewportToCameraPlane(worldViewportPos);
                            if (pressIsClick(state.startMillis, sceneContext.timeService->getTicks(), state.startPosition.x - releasePosition.x, state.startPosition.y - releasePosition.y))
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

    UnitOrder GameScene::mouseFormationOrder(UnitId receiver, const UnitOrder& order) const
    {
        const auto& unit = simulation.getUnitState(receiver);
        if (!simulation.unitDefinitions.at(unit.unitType).isMobile
            || (!std::holds_alternative<MoveOrder>(order) && !std::holds_alternative<PatrolOrder>(order)))
        {
            return order;
        }
        std::vector<SimVector> positions;
        for (auto id : selectedUnits)
        {
            if (hoveredUnit && id == *hoveredUnit)
            {
                continue;
            }
            if (auto selected = simulation.tryGetUnitState(id); selected && !selected->get().isDead())
            {
                positions.push_back(selected->get().position);
            }
        }
        if (auto move = std::get_if<MoveOrder>(&order))
        {
            return MoveOrder(formationDestination(positions, unit.position, move->destination));
        }
        return PatrolOrder(formationDestination(positions, unit.position, std::get<PatrolOrder>(order).destination));
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
            localPlayerEnqueueUnitOrder(selectedUnit, mouseFormationOrder(selectedUnit, *order));
        }
        else
        {
            localPlayerIssueUnitOrder(selectedUnit, mouseFormationOrder(selectedUnit, *order));
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
