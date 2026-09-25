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
    /**
     * Frame pixels per logical point: the display's output pixels per point
     * divided by the retro pixel size. pixelSize is clamped to at least 1.
     * Mouse coordinates from SDL are in logical points, so scene/frame
     * coordinates are the point times this.
     */
    float frameDensityFor(float displayScale, unsigned int pixelSize);

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
         * GlobalConfig::pixelSize, the display's own output scale, and what
         * they need: the window in logical points and in output pixels, the
         * frame the scenes render at (output pixels / pixelSize), and the
         * buffer that frame is drawn into before it is blown up onto the
         * window.
         */
        unsigned int pixelSize{1};
        float windowDisplayScale{1.0f};
        int outputWidth{0};
        int outputHeight{0};
        int logicalWidth{0};
        int logicalHeight{0};
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

        /**
         * Frame pixels per logical point. Scenes fold this into the world
         * projection so a sharper display shows the same battlefield rather
         * than more of it, and use it to map mouse points into the frame.
         */
        float frameDensity() const { return frameDensityFor(windowDisplayScale, pixelSize); }

        /**
         * Output pixels per logical point, as the window's display reports
         * it. The UI's Auto scale follows this so the interface keeps its
         * physical size across displays of different density.
         */
        float displayScale() const { return windowDisplayScale; }

    private:
        void renderDebugWindow();

        /** Re-derives the display scale, both window sizes, the frame viewport and the cursor density. */
        void refreshWindowMetrics();
    };
}
