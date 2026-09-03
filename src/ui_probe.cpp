// Headless probe for the options screens: builds the real panels from the
// real game data, dumps every gadget's hitbox, and simulates clicks the way
// the scenes deliver them, printing which gadget takes each event and what
// message comes out. Diagnoses layout/dispatch faults without launching the
// game.
#include <GL/glew.h>
#include <iostream>
#include <rwe/render/GraphicsContext.h>
#include <rwe/render/OpenGlVersion.h>
#include <rwe/util/Result.h>
#include <rwe/events.h>
#include <rwe/io/gui/gui.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/rwe_time.h>
#include <rwe/sdl/SdlContextManager.h>
#include <rwe/TextureService.h>
#include <rwe/AudioService.h>
#include <rwe/PathMapping.h>
#include <rwe/ShaderService.h>
#include <rwe/UiRenderService.h>
#include <rwe/Viewport.h>
#include <cstdio>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>
#include <rwe/ColorPalette.h>

using namespace rwe;

namespace
{
    rwe::Result<rwe::SdlContext::GlContextUniquePtr, const char*> makeGl(rwe::SdlContext* sdlContext, SDL_Window* window, const rwe::OpenGlVersionInfo& v)
    {
        using namespace rwe;
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.version.majorVersion)) return Err(SDL_GetError());
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.version.minorVersion)) return Err(SDL_GetError());
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, getSdlProfileMask(v.profile))) return Err(SDL_GetError());
        auto glContext = sdlContext->glCreateContext(window);
        if (glContext == nullptr) return Err(SDL_GetError());
        return Ok(std::move(glContext));
    }
}


namespace
{
    void screenshot(const char* path)
    {
        std::vector<unsigned char> pixels(640 * 480 * 3);
        glReadPixels(0, 0, 640, 480, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        FILE* f = std::fopen(path, "wb");
        std::fprintf(f, "P6 640 480 255\n");
        for (int y = 479; y >= 0; --y)
        {
            std::fwrite(pixels.data() + (y * 640 * 3), 1, 640 * 3, f);
        }
        std::fclose(f);
    }
}

namespace
{
    void dumpPanel(UiPanel& panel, const std::string& title)
    {
        std::cout << "==== " << title << " panel pos=(" << panel.getX() << "," << panel.getY()
                  << ") size=" << panel.getWidth() << "x" << panel.getHeight() << "\n";
        panel.forAll<UiComponent>([&](UiComponent& c) {
            std::cout << "  child '" << c.getName() << "' type=" << typeid(c).name()
                      << " rel=(" << c.getX() << "," << c.getY() << ") size=" << c.getWidth() << "x" << c.getHeight();
            std::cout << "\n";
        });
    }

    void clickAll(UiPanel& panel, const std::string& title)
    {
        std::cout << "---- clicking centre of every child of " << title << "\n";
        std::vector<std::pair<std::string, std::pair<int, int>>> targets;
        panel.forAll<UiComponent>([&](UiComponent& c) {
            targets.emplace_back(c.getName(),
                std::make_pair(panel.getX() + c.getX() + static_cast<int>(c.getWidth()) / 2,
                    panel.getY() + c.getY() + static_cast<int>(c.getHeight()) / 2));
        });
        for (const auto& [name, pos] : targets)
        {
            std::cout << "  click (" << pos.first << "," << pos.second << ") aimed at '" << name << "': ";
            MouseButtonEvent down(pos.first, pos.second, MouseButtonEvent::MouseButton::Left);
            panel.mouseDown(down);
            MouseButtonEvent up(pos.first, pos.second, MouseButtonEvent::MouseButton::Left);
            panel.mouseUp(up);
            std::cout << "\n";
        }
    }
}

int main(int argc, char* argv[])
{
    try
    {
        SdlContextManager sdlManager;
        auto sdlContext = sdlManager.getSdlContext();
        auto window = sdlContext->createWindow("probe", 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window)
        {
            throw std::runtime_error(SDL_GetError());
        }
        auto glContextResult = makeGl(sdlContext, window.get(), OpenGlVersionInfo(3, 2, OpenGlProfile::Core));
        if (!glContextResult)
        {
            glContextResult = makeGl(sdlContext, window.get(), OpenGlVersionInfo(3, 0, OpenGlProfile::Compatibility));
        }
        if (!glContextResult)
        {
            throw std::runtime_error("no GL context");
        }
        auto glContext = std::move(*glContextResult);
        glewExperimental = GL_TRUE;
        glewInit();

        CompositeVirtualFileSystem vfs;
        for (int i = 1; i < argc; ++i)
        {
            addToVfs(vfs, argv[i]);
        }

        auto paletteBytes = vfs.readFile("palettes/PALETTE.PAL");
        auto palette = readPalette(*paletteBytes);

        GraphicsContext graphics;
        TextureService textureService(&graphics, &vfs, &*palette);
        AudioService audioService(sdlContext, sdlManager.getSdlMixerContext(), &vfs);
        auto pathMapping = std::make_unique<PathMapping>();
        pathMapping->guis = "guis";
        pathMapping->bitmaps = "bitmaps";
        TdfBlock emptySoundLookup;

        UiFactory factory(&textureService, &audioService, &emptySoundLookup, &vfs, pathMapping.get(), 640, 480);

        // ---- front end composite, exactly as MainMenuScene builds it ----
        auto startOptRaw = vfs.readFile("guis/STARTOPT.GUI");
        auto entries = parseGuiFromBytes(*startOptRaw);
        auto soundsRaw = vfs.readFile("guis/SOUNDS.GUI");
        auto soundEntries = parseGuiFromBytes(*soundsRaw);
        entries->insert(entries->end(), soundEntries->begin() + 1, soundEntries->end());
        auto frontPanel = factory.panelFromGuiFile("STARTOPT", "OptSound4x", *entries);
        frontPanel->groupMessages().subscribe([](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                std::cout << "[MSG topic=" << msg.topic << " control=" << msg.controlName << "]";
            }
        });
        dumpPanel(*frontPanel, "FRONT STARTOPT+SOUNDS");
        clickAll(*frontPanel, "FRONT");

        // ---- in-game fold-out ----
        auto prefs = factory.panelFromGuiFile("PREFS");
        auto page = factory.panelFromGuiFile("SOUNDSRT");
        for (auto* p : {prefs.get(), page.get()})
        {
            p->groupMessages().subscribe([](const auto& msg) {
                if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
                {
                    std::cout << "[MSG topic=" << msg.topic << " control=" << msg.controlName << "]";
                }
            });
        }
        dumpPanel(*prefs, "INGAME PREFS");
        dumpPanel(*page, "INGAME SOUNDSRT");
        clickAll(*prefs, "PREFS");
        clickAll(*page, "SOUNDSRT");

        // ---- render what the player actually sees ----
        ShaderService shaders = ShaderService::createShaderService(graphics);
        Viewport uiViewport(0, 0, 640, 480);
        UiRenderService ui(&graphics, &shaders, &uiViewport);
        graphics.setViewport(0, 0, 640, 480);
        graphics.disableDepthBuffer();
        graphics.enableBlending();

        graphics.clearColor();
        frontPanel->render(ui);
        glFinish();
        screenshot("probe_front_sounds.ppm");

        auto rootEntries = parseGuiFromBytes(*vfs.readFile("guis/STARTOPT.GUI"));
        auto rootPanel = factory.panelFromGuiFile("STARTOPT", "Options4x", *rootEntries);
        graphics.clearColor();
        rootPanel->render(ui);
        glFinish();
        screenshot("probe_front_root.ppm");

        graphics.clearColor();
        prefs->render(ui);
        page->render(ui);
        glFinish();
        screenshot("probe_ingame.ppm");

        std::cout << "probe done\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "probe error: " << e.what() << "\n";
        return 1;
    }
}
