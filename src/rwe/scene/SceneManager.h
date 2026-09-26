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
    /** A per-output-pixel scale turned into a per-frame-pixel one; pixelSize is clamped to at least 1. */
    float frameDensityFor(float scale, unsigned int pixelSize);

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
        float windowPixelDensity{1.0f};
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
         * Whether this run advances exactly one tick per loop iteration. The
         * scenario driver refuses to run otherwise: a tick-keyed step is only
         * meaningful when one frame means one tick.
         */
        bool isHeadless() const { return headless; }

        /**
         * Switches the live window between windowed, borderless and
         * fullscreen, so the options screen's choice takes effect where the
         * player made it rather than at the next launch.
         */
        void setWindowMode(const std::string& mode);

        /**
         * Frame pixels per window coordinate, which is what SDL reports the
         * mouse in. For mapping input only: on Windows and X11 it is 1 at
         * any desktop scale, so it says nothing about how big things look.
         */
        float frameDensity() const { return frameDensityFor(windowPixelDensity, pixelSize); }

        /**
         * How many frame pixels the desktop wants drawn for each pixel of an
         * unscaled display. The world projection divides by it so a sharper
         * display shows the same battlefield rather than more of it, and the
         * UI's Auto scale follows it so the interface keeps its physical
         * size. It is frameDensity only where the window is sized in points.
         */
        float contentScale() const { return frameDensityFor(windowDisplayScale, pixelSize); }

    private:
        void renderDebugWindow();

        /** Re-derives the display scale, both window sizes, the frame viewport and the cursor density. */
        void refreshWindowMetrics();
    };
}
