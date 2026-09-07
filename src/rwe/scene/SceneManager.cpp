#include "SceneManager.h"
#include <rwe/render/render_prof.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util/CrashHandler.h>

namespace rwe
{
    std::optional<MouseButtonEvent::MouseButton> convertSdlMouseButton(Uint8 button)
    {
        switch (button)
        {
            case SDL_BUTTON_LEFT:
                return MouseButtonEvent::MouseButton::Left;
            case SDL_BUTTON_MIDDLE:
                return MouseButtonEvent::MouseButton::Middle;
            case SDL_BUTTON_RIGHT:
                return MouseButtonEvent::MouseButton::Right;
            default:
                return std::nullopt;
        }
    }
    SceneManager::SceneManager(
        SdlContext* sdl,
        SDL_Window* window,
        GraphicsContext* graphics,
        TimeService* timeService,
        ImGuiContext* imGuiContext,
        CursorService* cursorService,
        GlobalConfig* globalConfig,
        UiRenderService&& uiRenderService,
        Viewport* viewport)
        : currentScene(),
          nextScene(),
          sdl(sdl),
          window(window),
          graphics(graphics),
          timeService(timeService),
          imGuiContext(imGuiContext),
          cursorService(cursorService),
          globalConfig(globalConfig),
          uiRenderService(std::move(uiRenderService)),
          viewport(viewport),
          requestedExit(false)
    {
    }

    void SceneManager::setNextScene(std::shared_ptr<Scene> scene)
    {
        nextScene = std::move(scene);
    }

    void dispatchToScene(const SDL_Event& event, Scene& currentScene)
    {
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
                currentScene.onKeyDown(event.key);
                break;
            case SDL_EVENT_KEY_UP:
                currentScene.onKeyUp(event.key);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                auto button = convertSdlMouseButton(event.button.button);
                if (!button)
                {
                    break;
                }

                MouseButtonEvent e(event.button.x, event.button.y, *button);
                currentScene.onMouseDown(e);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                auto button = convertSdlMouseButton(event.button.button);
                if (!button)
                {
                    break;
                }

                MouseButtonEvent e(event.button.x, event.button.y, *button);
                currentScene.onMouseUp(e);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
            {
                MouseMoveEvent e(event.motion.x, event.motion.y);
                currentScene.onMouseMove(e);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
            {
                MouseWheelEvent e(event.wheel.x, event.wheel.y);
                currentScene.onMouseWheel(e);
                break;
            }

            default:
                // skip unrecognised events
                break;
        }
    }

    void SceneManager::execute()
    {
        while (!requestedExit)
        {
            if (nextScene)
            {
                currentScene = std::move(nextScene);
                currentScene->init();
            }

            auto startTime = timeService->getTicks();
            // Headless does not ask the clock how long the last frame took.
            // It hands the scene exactly one tick's worth every iteration, so
            // the simulation advances at whatever rate the CPU manages and
            // the run is not paced by a display. One tick rather than many
            // because each tick pops one entry from every player's command
            // buffer, and a frame that asked for more ticks than the buffer
            // holds would stall waiting for commands that never come.
            auto timeElapsed = headless
                ? static_cast<unsigned int>(SimMillisecondsPerTick)
                : (lastFrameStartTime == 0 ? 0 : startTime - lastFrameStartTime);

            setCrashPhase(CrashPhase::Input);

            SDL_Event event;
            // The whole frame, from here to the swap: the scene's own
            // breakdown is worth little without the number it has to add up
            // to. Everything after this in the loop body is inside it.
            RWE_RENDERPROF("loop");
            while (sdl->pollEvent(&event))
            {
                if (imGuiContext->processEvent(event))
                {
                    continue;
                }

                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11)
                {
                    showDebugWindow = !showDebugWindow;
                    continue;
                }

                if (event.type == SDL_EVENT_QUIT)
                {
                    return;
                }

                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && event.window.windowID == sdl->getWindowId(window))
                {
                    viewport->setDimensions(event.window.data1, event.window.data2);
                    // The GL viewport does not follow the window by itself,
                    // and only the game scene ever sets it per frame -- the
                    // menu and the movie player draw through the default one,
                    // so without this a resized window kept rendering into a
                    // corner sized like the old window.
                    graphics->setViewport(0, 0, event.window.data1, event.window.data2);
                    continue;
                }

                dispatchToScene(event, *currentScene);
            }

            if (!headless)
            {
                if (imGuiContext->io->WantCaptureMouse)
                {
                    imGuiContext->io->ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
                }
                else
                {
                    imGuiContext->io->ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
                }
                imGuiContext->newFrame(window);
            }
            {
                RWE_RENDERPROF("update");
                setCrashPhase(CrashPhase::Update);
                currentScene->update(timeElapsed);
            }
            if (headless)
            {
                // Nothing below here draws anything worth drawing, and the
                // swap at the end of it would wait for a display refresh.
                lastFrameStartTime = startTime;
                continue;
            }
            if (showDemoWindow)
            {
                ImGui::ShowDemoWindow(&showDemoWindow);
            }
            renderDebugWindow();
            imGuiContext->render();

            setCrashPhase(CrashPhase::Render);
            graphics->clear();
            // The game scene points the GL viewport at its own buffers as
            // it works (the supersampled world among them), and the menu and
            // movie scenes draw through whatever is current: without this
            // reset, leaving a game left the front end rendering into a
            // stale sub-rectangle while the mouse math used the window.
            graphics->setViewport(0, 0, viewport->width(), viewport->height());
            currentScene->render();

            if (!imGuiContext->io->WantCaptureMouse)
            {
                sdl->hideCursor();
                cursorService->render(uiRenderService);
            }

            {
                RWE_RENDERPROF("imgui");
                imGuiContext->renderDrawData();
            }

            {
                RWE_RENDERPROF("swap");
                setCrashPhase(CrashPhase::Swap);
                sdl->glSwapWindow(window);
            }

            auto finishTime = timeService->getTicks();
            auto lastFrameDurationMs = finishTime - startTime;
            lastFrameStartTime = startTime;
            frameTimes[frameTimesOffset] = lastFrameDurationMs;
            frameTimesOffset = (frameTimesOffset + 1) % 500;
        }
    }

    void SceneManager::setWindowMode(const std::string& mode)
    {
        if (mode == "fullscreen" || mode == "borderless")
        {
            // Borderless is a fullscreen window with no exclusive display
            // mode; fullscreen keeps whatever mode was set at startup.
            if (mode == "borderless")
            {
                sdl->setWindowDisplayMode(window, nullptr);
            }
            sdl->setWindowFullscreen(window, true);
            sdl->setWindowGrab(window, true);
        }
        else
        {
            sdl->setWindowFullscreen(window, false);
            sdl->setWindowBordered(window, true);
            sdl->setWindowResizable(window, true);
            // A bordered window must not trap the cursor: the title bar has
            // to stay reachable.
            sdl->setWindowGrab(window, false);
        }
    }

    void SceneManager::requestExit()
    {
        requestedExit = true;
    }

    void SceneManager::renderDebugWindow()
    {
        if (!showDebugWindow)
        {
            return;
        }

        ImGui::Begin("Global Debug", &showDebugWindow);
        ImGui::Text("Last frame time: %.0fms", frameTimes[(frameTimesOffset + 499) % 500]);
        ImGui::PlotLines("Frame Times", frameTimes.data(), frameTimes.size(), frameTimesOffset);
        ImGui::Separator();
        ImGui::Checkbox("Left click interface mode", &globalConfig->leftClickInterfaceMode);
        ImGui::Separator();
        if (ImGui::Button("Grab Mouse"))
        {
            sdl->setWindowGrab(window, true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Ungrab Mouse"))
        {
            sdl->setWindowGrab(window, false);
        }
        ImGui::Separator();
        if (ImGui::Button("Show Demo Window"))
        {
            showDemoWindow = true;
        }
        ImGui::End();
    }
}
