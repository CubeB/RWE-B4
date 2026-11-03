#include "GameScene.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <rwe/game/PlayerCommand.h>
#include <rwe/game/GameScene_util.h>
#include <rwe/presentation/PresentationRuntime.h>
#include <rwe/presentation/PresentationScript.h>
#include <rwe/presentation/PresentationView.h>
#include <rwe/sim/GamePlayerInfo.h>
#include <rwe/sim/UnitOrder.h>
#include <rwe/util/SimpleLogger.h>

/**
 * MOD-10, docs/MODDING-SDK-FINDINGS.md MOD-SDK-013.
 *
 * The scene's half of the presentation SDK: building the runtime, handing it
 * a filtered view once a frame, drawing the list it produced, and turning the
 * orders it asked for into ordinary local player commands.
 *
 * Kept in its own translation unit because GameScene.cpp is close to the COFF
 * section budget (see CLAUDE.md); this file includes only the presentation
 * surface and the command types, not the renderer or the menu.
 */
namespace rwe
{
    void PresentationRuntimeDeleter::operator()(PresentationRuntime* runtime) const
    {
        delete runtime;
    }

    namespace
    {
        /**
         * A mod may submit orders for as many units as it likes in one frame;
         * the buffer one frame may push is bounded so a runaway script cannot
         * make one machine spend minutes applying a single frame's work.
         */
        constexpr std::size_t PresentationCommandLimitPerFrame = 1024;

    }

    PlayerView GameScene::makePresentationView()
    {
        // The view is a stack object for the duration of one call. It borrows
        // both the simulation and the visibility picture; nothing it returns
        // outlives either. A spectator's remembered ground is every group's,
        // as localExploredGrid is.
        return PlayerView(
            simulation,
            localPlayerId,
            localPlayerVisibility(),
            isPlayback() || eliminationWatching);
    }

    void GameScene::initPresentation()
    {
        if (presentationRuntime || !hasPresentationPackages(gameParameters.modContent.get()))
        {
            return;
        }

        try
        {
            auto packages = resolvedPresentationPackages(gameParameters.modContent.get());
            if (packages.empty())
            {
                return;
            }

            presentationRuntime.reset(new PresentationRuntime(std::move(packages)));
            for (const auto& line : presentationRuntime->log())
            {
                LOG_INFO << "[presentation] " << line;
            }
            for (const auto& package : presentationRuntime->disabledPackages())
            {
                LOG_WARN << "[presentation] package disabled at load: " << package;
            }
        }
        catch (const std::exception& e)
        {
            // Presentation is not simulation state and must not refuse a
            // launch. A broken HUD leaves the game playable and says why.
            LOG_WARN << "[presentation] not loaded: " << e.what();
        }
    }

    bool GameScene::reloadPresentationMods()
    {
        if (!presentationRuntime)
        {
            return false;
        }

        try
        {
            auto packages = resolvedPresentationPackages(gameParameters.modContent.get());
            if (packages.empty())
            {
                return false;
            }

            const auto reloaded = presentationRuntime->reload(std::move(packages));
            for (const auto& line : presentationRuntime->log())
            {
                LOG_INFO << "[presentation] " << line;
            }
            return reloaded;
        }
        catch (const std::exception& e)
        {
            LOG_WARN << "[presentation] reload failed: " << e.what();
            return false;
        }
    }

    void GameScene::updatePresentation(int millisecondsElapsed)
    {
        if (!presentationRuntime)
        {
            return;
        }

        auto input = makePresentationFrameInput();
        input.deltaSeconds = static_cast<float>(millisecondsElapsed) / 1000.0f;

        auto view = makePresentationView();
        presentationRuntime->beginFrame(view, input);

        routePresentationCommands();
    }

    void GameScene::renderPresentation()
    {
        if (!presentationRuntime)
        {
            return;
        }

        const auto& list = presentationRuntime->drawList();
        if (list.empty())
        {
            return;
        }

        for (const auto& command : list.commands)
        {
            switch (command.kind)
            {
            case PresentationDrawCommand::Kind::Rect:
                chromeUiRenderService.fillColor(command.x, command.y, command.width, command.height, command.color);
                break;

            case PresentationDrawCommand::Kind::Line:
                // UiRenderService draws lines one pixel wide; `thickness` is
                // carried for a future batch API and deliberately not faked
                // with several passes here.
                chromeUiRenderService.drawLines({Vector2f(command.x, command.y), Vector2f(command.x2, command.y2)}, command.color);
                break;

            case PresentationDrawCommand::Kind::Text:
                if (guiFont)
                {
                    chromeUiRenderService.drawText(command.x, command.y, command.text, *guiFont, command.color);
                }
                break;

            case PresentationDrawCommand::Kind::Bar:
                chromeUiRenderService.fillColor(command.x, command.y, command.width * command.fraction, command.height, command.color);
                chromeUiRenderService.drawBoxOutline(command.x, command.y, command.width, command.height, command.color);
                break;
            }
        }
    }

    PresentationFrameInput GameScene::makePresentationFrameInput() const
    {
        PresentationFrameInput input;

        const auto scale = effectiveUiScale();
        input.uiWidth = static_cast<float>(sceneContext.viewport->width()) / scale;
        input.uiHeight = static_cast<float>(sceneContext.viewport->height()) / scale;

        auto frame = getMousePosition();
        auto ui = toUiCoordinates(frame.x, frame.y);
        input.mouseX = static_cast<float>(ui.x);
        input.mouseY = static_cast<float>(ui.y);
        input.mouseInsideWorld = isCursorOverWorld();

        input.cameraX = worldCameraState.position.x;
        input.cameraY = worldCameraState.position.y;
        input.cameraZ = worldCameraState.position.z;
        input.cameraZoom = worldCameraState.zoom;

        input.selectedUnits.assign(selectedUnits.begin(), selectedUnits.end());
        input.hoveredUnit = hoveredUnit;
        input.hoveredFeature = hoveredFeature;

        return input;
    }

    bool GameScene::presentationConsumesMouseDown(const MouseButtonEvent& event)
    {
        if (!presentationRuntime)
        {
            return false;
        }

        auto input = makePresentationFrameInput();
        auto ui = toUiCoordinates(event.x, event.y);
        input.mouseX = static_cast<float>(ui.x);
        input.mouseY = static_cast<float>(ui.y);

        auto view = makePresentationView();
        const auto consumed = presentationRuntime->mouseDown(view, input, static_cast<int>(event.button));
        routePresentationCommands();
        return consumed;
    }

    bool GameScene::presentationConsumesMouseUp(const MouseButtonEvent& event)
    {
        if (!presentationRuntime)
        {
            return false;
        }

        auto input = makePresentationFrameInput();
        auto ui = toUiCoordinates(event.x, event.y);
        input.mouseX = static_cast<float>(ui.x);
        input.mouseY = static_cast<float>(ui.y);

        auto view = makePresentationView();
        const auto consumed = presentationRuntime->mouseUp(view, input, static_cast<int>(event.button));
        routePresentationCommands();
        return consumed;
    }

    void GameScene::presentationMouseMove(const MouseMoveEvent& event)
    {
        if (!presentationRuntime)
        {
            return;
        }

        auto input = makePresentationFrameInput();
        auto ui = toUiCoordinates(event.x, event.y);
        input.mouseX = static_cast<float>(ui.x);
        input.mouseY = static_cast<float>(ui.y);

        auto view = makePresentationView();
        presentationRuntime->mouseMove(view, input);
        routePresentationCommands();
    }

    bool GameScene::presentationConsumesKeyDown(const SDL_KeyboardEvent& event)
    {
        if (!presentationRuntime)
        {
            return false;
        }

        auto input = makePresentationFrameInput();
        auto view = makePresentationView();
        const auto consumed = presentationRuntime->keyDown(view, input, event.key);
        routePresentationCommands();
        return consumed;
    }

    void GameScene::routePresentationCommands()
    {
        if (!presentationRuntime)
        {
            return;
        }

        auto commands = presentationRuntime->takeCommands();
        if (commands.empty())
        {
            return;
        }

        // A presentation mod acts as the local human and nobody else. It has
        // nothing to submit while watching, and nothing to say as a computer
        // or remote player.
        if (isPlayback() || simulation.getPlayer(localPlayerId).type != GamePlayerType::Human)
        {
            return;
        }

        auto view = makePresentationView();
        std::size_t issued = 0;

        for (const auto& command : commands)
        {
            for (auto unitId : command.units)
            {
                if (issued >= PresentationCommandLimitPerFrame)
                {
                    LOG_WARN << "[presentation] command limit reached this frame";
                    return;
                }

                // Re-checked here as well as in the runtime: several frames
                // may have passed since the script collected its ids, and a
                // unit that died, changed hands or was never the viewer's is
                // not something a mod may order.
                const auto unit = view.unit(unitId);
                if (!unit || !unit->owned)
                {
                    continue;
                }

                switch (command.kind)
                {
                case PresentationCommand::Kind::Move:
                {
                    auto destination = command.position;
                    destination.y = simulation.terrain.getHeightAt(destination.x, destination.z);
                    localPlayerIssueUnitOrder(unitId, MoveOrder(destination));
                    break;
                }

                case PresentationCommand::Kind::Stop:
                    localPlayerStopUnit(unitId);
                    break;

                case PresentationCommand::Kind::Attack:
                {
                    // The target must be one the viewer can see. A radar
                    // contact is not a unit view, so a mod cannot use this to
                    // order an attack on something the player could not click.
                    if (!view.unit(command.target))
                    {
                        continue;
                    }
                    localPlayerIssueUnitOrder(unitId, AttackOrder(command.target));
                    break;
                }
                }

                ++issued;
            }
        }
    }
}
