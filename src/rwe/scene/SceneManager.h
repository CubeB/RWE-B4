#pragma once

#include <array>
#include <memory>
#include <optional>
#include <rwe/CursorService.h>
#include <rwe/GlobalConfig.h>
#include <rwe/ImGuiContext.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/rwe_time.h>
#include <rwe/scene/Scene.h>
#include <rwe/sdl/SdlContext.h>

namespace rwe
{
    class SceneManager
    {
    private:
        std::shared_ptr<Scene> currentScene;
        std::shared_ptr<Scene> nextScene;
        SdlContext* sdl;
        SDL_Window* window;
        GraphicsContext* graphics;
        TimeService* timeService;
        ImGuiContext* imGuiContext;
        CursorService* cursorService;
        GlobalConfig* globalConfig;
        UiRenderService uiRenderService;
        Viewport* const viewport;
        bool requestedExit;
        /**
         * Draws nothing and stops asking the clock what time it is: every
         * iteration of the loop advances the scene by exactly one simulation
         * tick instead. Used by the AI arena, where the game is being
         * measured rather than watched, and where waiting for a display to
         * refresh would make a batch of twenty games take all afternoon.
         */
        bool headless{false};
        bool showDebugWindow{false};
        bool showDemoWindow{false};
        /**
         * Set by Ctrl+F9 and cleared once the picture is taken, after the
         * scene has drawn the frame (TOTALA-EXE.md S:77).
         */
        bool screenshotRequested{false};

        /**
         * GlobalConfig::screenScale, and what it needs: the window's own
         * size in pixels, which the viewport the scenes read no longer
         * holds at a scale above 1, and the buffer the frame is drawn into
         * before it is blown up onto the window.
         */
        unsigned int screenScale{1};
        int windowWidth{0};
        int windowHeight{0};
        std::optional<FrameBufferInfo> presentationBuffer;
        int presentationBufferWidth{0};
        int presentationBufferHeight{0};

        unsigned int lastFrameStartTime{0};

        std::array<float, 500> frameTimes;
        int frameTimesOffset{0};

    public:
        explicit SceneManager(SdlContext* sdl, SDL_Window* window, GraphicsContext* graphics, TimeService* timeService, ImGuiContext* imGuiContext, CursorService* cursorService, GlobalConfig* globalConfig, UiRenderService&& uiRenderService, Viewport* viewport);
        void setNextScene(std::shared_ptr<Scene> scene);

        void execute();

        void requestExit();

        /** See the headless member. Set before execute(). */
        void setHeadless(bool value) { headless = value; }

        /**
         * Switches the live window between windowed, borderless and
         * fullscreen, so the options screen's choice takes effect where the
         * player made it rather than at the next launch.
         */
        void setWindowMode(const std::string& mode);

    private:
        void renderDebugWindow();
    };
}
