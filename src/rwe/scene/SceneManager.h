#pragma once

#include <array>
#include <memory>
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
