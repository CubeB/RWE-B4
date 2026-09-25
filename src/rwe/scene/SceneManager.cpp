#include "SceneManager.h"
#include <algorithm>
#include <rwe/render/render_prof.h>
#include <rwe/scene/Screenshot.h>
#include <rwe/sim/SimTicksPerSecond.h>
#include <rwe/util.h>
#include <rwe/util/CrashHandler.h>
#include <rwe/util/SimpleLogger.h>

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
    float frameDensityFor(float displayScale, unsigned int pixelSize)
    {
        auto clampedPixelSize = std::max(1u, pixelSize);
        return displayScale / static_cast<float>(clampedPixelSize);
    }

    void SceneManager::refreshWindowMetrics()
    {
        windowDisplayScale = sdl->getWindowDisplayScale(window);
        if (!(windowDisplayScale > 0.0f))
        {
            windowDisplayScale = 1.0f;
        }
        sdl->getWindowSize(window, &logicalWidth, &logicalHeight);
        sdl->getWindowSizeInPixels(window, &outputWidth, &outputHeight);
        viewport->setDimensions(outputWidth / static_cast<int>(pixelSize), outputHeight / static_cast<int>(pixelSize));
        cursorService->setFrameDensity(frameDensity());
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
        // At a pixel size above 1 the scenes see a viewport of the output
        // pixels divided by it, draw into a buffer of that size, and the
        // buffer is blown up onto the window at the end of the frame. The
        // mouse is mapped back through the same factor on the way in.
        pixelSize = std::clamp(globalConfig->pixelSize, 1u, 4u);
        refreshWindowMetrics();
    }

    void SceneManager::setNextScene(std::shared_ptr<Scene> scene)
    {
        nextScene = std::move(scene);
    }

    void dispatchToScene(const SDL_Event& event, Scene& currentScene, float frameDensity)
    {
        switch (event.type)
        {
            case SDL_EVENT_KEY_DOWN:
                currentScene.onKeyDown(event.key);
                break;
            case SDL_EVENT_KEY_UP:
                currentScene.onKeyUp(event.key);
                break;
            case SDL_EVENT_TEXT_INPUT:
                // What the layout actually composed, which is the only thing
                // a text box should be written from.
                currentScene.onTextInput(event.text.text);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            {
                auto button = convertSdlMouseButton(event.button.button);
                if (!button)
                {
                    break;
                }

                MouseButtonEvent e(static_cast<int>(event.button.x * frameDensity), static_cast<int>(event.button.y * frameDensity), *button);
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

                MouseButtonEvent e(static_cast<int>(event.button.x * frameDensity), static_cast<int>(event.button.y * frameDensity), *button);
                currentScene.onMouseUp(e);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
            {
                MouseMoveEvent e(static_cast<int>(event.motion.x * frameDensity), static_cast<int>(event.motion.y * frameDensity));
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

                // Ctrl+F9 is the original's screenshot key. It is handled a
                // layer above the screens, so it works on every one of them
                // and none of them ever sees it (TOTALA-EXE.md S:77). The
                // picture itself is taken below, once the frame is drawn.
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F9 && (event.key.mod & SDL_KMOD_CTRL) != 0)
                {
                    screenshotRequested = true;
                    continue;
                }

                if (event.type == SDL_EVENT_QUIT)
                {
                    return;
                }

                if ((event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED) && event.window.windowID == sdl->getWindowId(window))
                {
                    refreshWindowMetrics();
                    // The GL viewport does not follow the window by itself,
                    // and only the game scene ever sets it per frame -- the
                    // menu and the movie player draw through the default one,
                    // so without this a resized window kept rendering into a
                    // corner sized like the old window.
                    graphics->setViewport(0, 0, viewport->width(), viewport->height());
                    continue;
                }

                // A move between monitors of different density changes the
                // display scale without necessarily changing the pixel size,
                // so the density and the frame have to be re-derived here.
                if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED && event.window.windowID == sdl->getWindowId(window))
                {
                    refreshWindowMetrics();
                    continue;
                }

                dispatchToScene(event, *currentScene, frameDensity());
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
                // ImGui is deliberately not scaled with the UI: it already
                // follows the display's framebuffer scale and is a debug tool.
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
            if (pixelSize > 1)
            {
                // The frame goes into a buffer of the scaled size. Scenes
                // that bind buffers of their own unbind back to it rather
                // than to the window, which is what the presentation target
                // on the graphics context is for.
                auto wantedWidth = static_cast<int>(viewport->width());
                auto wantedHeight = static_cast<int>(viewport->height());
                if (!presentationBuffer || presentationBufferWidth != wantedWidth || presentationBufferHeight != wantedHeight)
                {
                    presentationBuffer = graphics->createFrameBuffer(wantedWidth, wantedHeight);
                    presentationBufferWidth = wantedWidth;
                    presentationBufferHeight = wantedHeight;
                }
                graphics->setPresentationFrameBuffer(presentationBuffer->frameBuffer.get());
                graphics->bindFrameBuffer(presentationBuffer->frameBuffer.get());
            }
            graphics->clear();
            // The game scene points the GL viewport at its own buffers as
            // it works (the supersampled world among them), and the menu and
            // movie scenes draw through whatever is current: without this
            // reset, leaving a game left the front end rendering into a
            // corner sized like the old window.
            graphics->setViewport(0, 0, viewport->width(), viewport->height());
            currentScene->render();

            if (!imGuiContext->io->WantCaptureMouse)
            {
                sdl->hideCursor();
                cursorService->render(uiRenderService);
            }

            if (pixelSize > 1)
            {
                // Nearest-neighbour, so at a whole-number scale every game
                // pixel is a square block of screen pixels. The world's own
                // supersample and the building halo were resolved inside
                // the frame, before this, as the original's own pixels
                // would have been before a monitor stretched them.
                graphics->setPresentationFrameBuffer(std::nullopt);
                graphics->blitFrameBufferToWindow(presentationBuffer->frameBuffer.get(), presentationBufferWidth, presentationBufferHeight, outputWidth, outputHeight);
                graphics->setViewport(0, 0, outputWidth, outputHeight);
            }

            // Taken here, after the scene and the cursor and before the debug
            // windows, so the picture is the game with nothing of RWE's own on
            // top of it. It reads the window's back buffer, which at a scale
            // above 1 has the frame on it only once it has been blown up. The
            // cursor is in it now, where it was not before the scale work; the
            // original's own screenshots are not decoded on that point.
            if (screenshotRequested)
            {
                screenshotRequested = false;
                if (auto dataPath = getLocalDataPath())
                {
                    auto written = saveScreenshot(*dataPath / "screenshots", static_cast<unsigned int>(outputWidth), static_cast<unsigned int>(outputHeight));
                    if (written)
                    {
                        LOG_INFO << "Screenshot saved to " << written->string();
                    }
                    else
                    {
                        LOG_ERROR << "Screenshot could not be written";
                    }
                }
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
