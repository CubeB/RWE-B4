// Headless probe for the options screens: builds the real panels from the
// real game data, dumps every gadget's hitbox, and simulates clicks the way
// the scenes deliver them, printing which gadget takes each event and what
// message comes out. Diagnoses layout/dispatch faults without launching the
// game.
#include <cstdio>
#include "antialias_probe.h"
#include <glad/gl.h>
#include <iostream>
#include <rwe/AudioService.h>
#include <rwe/ColorPalette.h>
#include <rwe/CursorService.h>
#include <algorithm>
#include <chrono>
#include <rwe/GlobalConfig.h>
#include <rwe/MainMenuScene.h>
#include <rwe/MainMenuScene_multiplayer.h>
#include <rwe/net/InviteCode.h>
#include <rwe/net/stun_test_util.h>
#include <rwe/SceneContext.h>
#include <rwe/scene/SceneManager.h>
#include <set>
#include <map>
#include <thread>
#include <rwe/PathMapping.h>
#include <rwe/ShaderService.h>
#include <rwe/TextureService.h>
#include <rwe/UiRenderService.h>
#include <rwe/Viewport.h>
#include <rwe/events.h>
#include <rwe/io/gui/gui.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/render/OpenGlVersion.h>
#include <rwe/rwe_time.h>
#include <rwe/sdl/SdlContextManager.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/ui/UiLabel.h>
#include <rwe/ui/UiListBox.h>
#include <rwe/ui/UiPanel.h>
#include <rwe/ui/UiStagedButton.h>
#include <rwe/ui/UiTextBox.h>
#include <rwe/util/Result.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

using namespace rwe;

namespace rwe
{
    /**
     * What the flow check reads off a menu scene: the panel a person is
     * looking at, the dialog over it, and the model behind it. MainMenuScene
     * names this as its friend and nothing else may see what is in it.
     */
    struct MenuProbe
    {
        static UiPanel* panel(MainMenuScene& scene)
        {
            return scene.panelStack.empty() ? nullptr : scene.panelStack.back().get();
        }

        static UiPanel* dialog(MainMenuScene& scene)
        {
            return scene.dialogStack.empty() ? nullptr : scene.dialogStack.back().get();
        }

        static MainMenuModel& model(MainMenuScene& scene)
        {
            return scene.model;
        }

        // MAP-LAYOUT-01/02: exercise the real dialog, including subscriptions.
        static void openMaps(MainMenuScene& scene) { scene.openMapSelectionDialog(); }
        static void selectMap(MainMenuScene& scene, const std::string& name) { scene.setCandidateSelectedMap(name); }
        static auto mapNames(MainMenuScene& scene) { return scene.getMapNames(); }

        /**
         * Points one scene's network at things of its own, so that two of
         * them in one process can be a host and a guest across "the
         * internet": a discovery port that is not the shared one, a game
         * port that is not the other's, and a STUN server on loopback.
         */
        static void useLocalNetwork(MainMenuScene& scene, unsigned short discoveryPort, const std::string& gamePort, const std::string& stunServer)
        {
            scene.discoveryPortOverride = discoveryPort;
            scene.model.localNetworkPort.next(gamePort);
            scene.stunServerOverride = {stunServer};
        }

        static MultiplayerFlow* flow(MainMenuScene& scene)
        {
            return scene.multiplayerFlow.get();
        }
    };
}

namespace
{
    rwe::Result<rwe::SdlContext::GlContextUniquePtr, const char*> makeGl(rwe::SdlContext* sdlContext, SDL_Window* window, const rwe::OpenGlVersionInfo& v)
    {
        using namespace rwe;
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v.version.majorVersion))
            return Err(SDL_GetError());
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v.version.minorVersion))
            return Err(SDL_GetError());
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, getSdlProfileMask(v.profile)))
            return Err(SDL_GetError());
        auto glContext = sdlContext->glCreateContext(window);
        if (glContext == nullptr)
            return Err(SDL_GetError());
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


namespace
{
    /** Prints each activation a panel's gadgets send, the way the scenes receive them. */
    void printActivations(UiPanel& panel)
    {
        panel.groupMessages().subscribe([](const auto& msg) {
            if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
            {
                std::cout << "      -> MSG topic=" << msg.topic << " control=" << msg.controlName << "\n";
            }
        });
    }

    std::pair<int, int> centreOf(UiPanel& panel, const std::string& name)
    {
        std::pair<int, int> result{-1, -1};
        panel.forAll<UiComponent>([&](UiComponent& c) {
            if (c.getName() == name)
            {
                result = {panel.getX() + c.getX() + static_cast<int>(c.getWidth()) / 2,
                    panel.getY() + c.getY() + static_cast<int>(c.getHeight()) / 2};
            }
        });
        return result;
    }

    void clickNamed(UiPanel& panel, const std::string& name)
    {
        auto pos = centreOf(panel, name);
        std::cout << "  click '" << name << "' at (" << pos.first << "," << pos.second << ")\n";
        if (pos.first < 0)
        {
            std::cout << "      (no such gadget)\n";
            return;
        }
        panel.mouseDown(MouseButtonEvent(pos.first, pos.second, MouseButtonEvent::MouseButton::Left));
        panel.mouseUp(MouseButtonEvent(pos.first, pos.second, MouseButtonEvent::MouseButton::Left));
    }

    void pressKey(UiPanel& panel, const char* label, int key)
    {
        std::cout << "  key " << label << "\n";
        panel.keyDown(KeyEvent(key));
    }

    void expectGadgets(UiPanel& panel, const std::vector<std::string>& wanted, const std::vector<std::string>& unwanted)
    {
        for (const auto& n : wanted)
        {
            bool found = false;
            panel.forAll<UiComponent>([&](UiComponent& c) { found = found || c.getName() == n; });
            std::cout << "  gadget " << n << (found ? ": present\n" : ": MISSING\n");
        }
        for (const auto& n : unwanted)
        {
            bool found = false;
            panel.forAll<UiComponent>([&](UiComponent& c) { found = found || c.getName() == n; });
            std::cout << "  gadget " << n << (found ? ": STILL THERE\n" : ": removed\n");
        }
    }

    /**
     * Every real button of a panel -- the 16-pixel lamps are not -- has a
     * face of its own and not the blank default sprite. NEWMULTI's Next and
     * Previous were 119x21 and 119x19 and had none, which the baked plate
     * under them hid until the button was pressed. Returns how many are blank.
     */
    int expectButtonFaces(UiPanel& panel, TextureService& textures, const std::string& panelName)
    {
        const auto blank = textures.getDefaultSprite();
        int blanks = 0;
        panel.forAll<UiStagedButton>([&](UiStagedButton& b) {
            if (b.getWidth() < 32 || b.getHeight() < 12 || b.getStageCount() == 0)
            {
                return;
            }
            const auto face = b.getStageSprite(0);
            if (!face || face == blank)
            {
                std::cout << "  face " << panelName << "/" << b.getName() << " (" << b.getWidth() << "x" << b.getHeight() << "): BLANK\n";
                ++blanks;
            }
        });
        std::cout << "  faces " << panelName << ": " << (blanks == 0 ? "all buttons have art" : std::to_string(blanks) + " blank") << "\n";
        return blanks;
    }

    void printSelections(UiPanel& panel)
    {
        std::cout << "  selection by column:";
        for (const auto& name : gameListColumnNames())
        {
            auto box = panel.find<UiListBox>(name);
            if (!box)
            {
                std::cout << " " << name << "=MISSING";
                continue;
            }
            auto sel = box->get().getSelectedIndex();
            std::cout << " " << name << "=" << (sel ? std::to_string(*sel) : "none");
        }
        std::cout << "\n";
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
        if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)))
        {
            throw std::runtime_error("Failed to initialize OpenGL function loader");
        }

        // Everything that is not `--panel NAME` is a path to add to the VFS,
        // which is what this took before the option existed.
        CompositeVirtualFileSystem vfs;
        std::vector<std::string> requestedPanels;
        bool multiplayerScreens = false;
        bool multiplayerFlow = false;
        bool renderRequested = false;
        bool mapLayout = false;
        bool antialias = false;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
            if (arg == "--antialias")
            {
                antialias = true;
                continue;
            }
            if (arg == "--map-layout")
            {
                mapLayout = true;
                continue;
            }
            if (arg == "--multiplayer")
            {
                multiplayerScreens = true;
                continue;
            }
            if (arg == "--multiplayer-flow")
            {
                multiplayerFlow = true;
                continue;
            }
            if (arg == "--render")
            {
                renderRequested = true;
                continue;
            }
            if (arg == "--panel" && i + 1 < argc)
            {
                requestedPanels.emplace_back(argv[++i]);
                continue;
            }
            if (arg == "--help" || arg == "-h")
            {
                std::cout << "usage: ui_probe [--panel NAME]... <data path>...\n"
                          << "  --antialias checks production AA/shading shaders on a hidden GL context.\n"
                          << "  --map-layout checks the original map selector font, rows and artwork.\n"
                          << "  With no --panel, walks the options screens the probe was written for.\n"
                          << "  With one or more, dumps and clicks just those guis and stops.\n"
                          << "  --multiplayer builds the four multiplayer screens (SELPROV, TCP, SELGAME,\n"
                          << "  NEWMULTI) the way the main menu does, drives them, and writes probe_mp_*.ppm.\n"
                          << "  --render, with --panel, also draws each panel and writes probe_panel_<NAME>.ppm.\n"
                          << "  --multiplayer-flow runs three real MainMenuScenes (a host and two guests)\n"
                          << "  through the whole flow, the battleroom included, over LAN discovery on this\n"
                          << "  machine; it checks what each sees, writes probe_flow_*.ppm, and exits non-zero\n"
                          << "  if a check fails.\n";
                return 0;
            }
            addToVfs(vfs, arg);
        }

        auto paletteBytes = vfs.readFile("palettes/PALETTE.PAL");
        auto palette = readPalette(*paletteBytes);

        GraphicsContext graphics;
        TextureService textureService(&graphics, &vfs, &*palette);
        if (antialias) return runAntialiasProbe(graphics, textureService, vfs);
        AudioService audioService(sdlContext, sdlManager.getSdlMixerContext(), &vfs);
        auto pathMapping = std::make_unique<PathMapping>();
        pathMapping->guis = "guis";
        pathMapping->bitmaps = "bitmaps";
        TdfBlock emptySoundLookup;

        UiFactory factory(&textureService, &audioService, &emptySoundLookup, &vfs, pathMapping.get(), 640, 480);

        if (mapLayout)
        {
            ShaderService shaders = ShaderService::createShaderService(graphics);
            Viewport viewport(0, 0, 640, 480);
            GlobalConfig config;
            TimeService clock(getTimestamp());
            CursorService cursors(sdlContext, &clock, Cursors{});
            SceneManager manager(sdlContext, window.get(), &graphics, &clock, nullptr, &cursors,
                &config, UiRenderService(&graphics, &shaders, &viewport), &viewport);
            std::unordered_map<std::string, SideData> sides;
            SceneContext context(sdlContext, &viewport, &graphics, &textureService, &audioService,
                &cursors, &shaders, &vfs, &*palette, &*palette, &manager, &sides, &clock, pathMapping.get(), &config);
            MainMenuScene menu(context, &emptySoundLookup, 640, 480);
            menu.init();
            MenuProbe::openMaps(menu);
            auto* panel = MenuProbe::dialog(menu);
            if (!panel) { throw std::runtime_error("Map selector did not open"); }
            auto& description = panel->find<UiLabel>("DESCRIPTION")->get();
            auto& size = panel->find<UiLabel>("SIZE")->get();
            const auto names = MenuProbe::mapNames(menu);
            std::string longestName;
            unsigned int longestHeight = 0;
            // MAP-FONT-01/02: check the real font and unchanged GUI, not a
            // larger box that can conceal wrong typography.
            auto labelFont = textureService.getGafEntry("anims/hattfont11.gaf", "Haettenschweiler (120)");
            if (panel->getX() != 84 || panel->getY() != 12 || panel->getWidth() != 494 || panel->getHeight() != 420
                || description.getX() != 60 || description.getY() != 300 || description.getWidth() != 235 || description.getHeight() != 31
                || size.getX() != 60 || size.getY() != 330 || size.getWidth() != 235 || size.getHeight() != 18
                || labelFont->textBaseline != 10 || labelFont->textLineAdvance != 14 || !labelFont->advanceByFrameWidth)
            {
                throw std::runtime_error("Original map layout/font metrics changed");
            }
            const std::map<std::string, std::pair<int,int>> expectedRows{
                {"Mounds of Mars",{235,133}}, {"Ror Shock",{219,225}}, {"Steel Jungle",{224,160}},
                {"Temblorian Mist",{207,182}}, {"The Barrier Reef",{222,226}}};
            unsigned int checkedRows = 0;
            unsigned int clippedInkOverhangs = 0;
            for (const auto& name : names)
            {
                MenuProbe::selectMap(menu, name);
                if (description.getTextHeight() > longestHeight)
                {
                    longestHeight = description.getTextHeight();
                    longestName = name;
                }
                const auto area = Rectangle2f::fromTopLeft(60,300,235,31);
                auto glyphs = layoutWrappedText(area,description.getText(),*labelFont);
                auto full = layoutWrappedText(Rectangle2f::fromTopLeft(60,300,235,100000),description.getText(),*labelFont);
                if (glyphs.size() != full.size()) throw std::runtime_error("Clipped map description: " + name);
                for (const auto& glyph : glyphs)
                {
                    const auto& bounds = labelFont->sprites[glyph.glyph]->bounds;
                    // MAP-FONT-01: pen width and ink bearing are distinct.
                    // A final period may overhang by one pixel; the original
                    // label surface, and RWE's scissor, clip that ink.
                    if (glyph.x+bounds.right() > 295) ++clippedInkOverhangs;
                    if (glyph.y+bounds.top() < 300 || glyph.y+bounds.bottom() >= 330 || glyph.x+labelFont->glyphAdvance(glyph.glyph) > 295)
                        throw std::runtime_error("Map glyph outside original description area: " + name + " glyph "
                            + std::to_string(glyph.glyph) + " x/y " + std::to_string(glyph.x) + "/" + std::to_string(glyph.y)
                            + " top/bottom/right " + std::to_string(glyph.y+bounds.top()) + "/" + std::to_string(glyph.y+bounds.bottom())
                            + "/" + std::to_string(glyph.x+bounds.right()) + ": " + description.getText());
                }
                if (description.getTextHeight() > description.getHeight())
                {
                    throw std::runtime_error("Map does not fit original description box: " + name);
                }
                if (auto expected = expectedRows.find(name); expected != expectedRows.end())
                {
                    int first = 0, second = 0;
                    for (const auto& glyph : glyphs)
                    {
                        const auto right = static_cast<int>(glyph.x + labelFont->glyphAdvance(glyph.glyph) - 60);
                        if (glyph.y == 310) first = std::max(first,right);
                        else if (glyph.y == 324) second = std::max(second,right);
                        else throw std::runtime_error("Unexpected description row: " + name);
                    }
                    std::cout << "Original rows: " << name << " " << first << '/' << second << " px\n";
                    if (std::make_pair(first,second) != expected->second) throw std::runtime_error("Original row break differs: " + name);
                    ++checkedRows;
                }
            }
            if (checkedRows != expectedRows.size()) throw std::runtime_error("Missing original map fixtures");
            std::cout << "Original label-edge ink overhangs clipped: " << clippedInkOverhangs << '\n';
            MenuProbe::selectMap(menu, longestName);
            UiRenderService ui(&graphics, &shaders, &viewport);
            graphics.setViewport(0, 0, 640, 480);
            graphics.disableDepthBuffer();
            graphics.enableBlending();
            graphics.clearColor();
            panel->render(ui);
            glFinish();
            screenshot("probe_map_layout.ppm");
            for (const auto& fixture : expectedRows)
            {
                MenuProbe::selectMap(menu,fixture.first);
                graphics.clearColor();
                panel->render(ui);
                glFinish();
                auto filename = "probe_map_font_" + fixture.first + ".ppm";
                std::replace(filename.begin(),filename.end(),' ','_');
                screenshot(filename.c_str());
            }
            std::cout << "Map layout: " << names.size() << " real maps, full descriptions visible; longest: "
                      << longestName << " (" << longestHeight << " px)\n";
            dumpPanel(*panel, "original map selector");
            // Activation exercises the factory's callbacks after construction;
            // a transferred child can otherwise retain a dead parent callback.
            bool cancelSent = false;
            panel->groupMessages().subscribe([&cancelSent](const auto& message) {
                if (message.controlName == "PREVMENU" && std::holds_alternative<ActivateMessage>(message.message))
                {
                    cancelSent = true;
                }
            });
            // Keep the panel alive through dispatch, just as the scene does.
            auto& cancel = panel->find<UiComponent>("PREVMENU")->get();
            panel->mouseDown(MouseButtonEvent(panel->getX() + cancel.getX() + 4,
                panel->getY() + cancel.getY() + 4, MouseButtonEvent::MouseButton::Left));
            panel->mouseUp(MouseButtonEvent(panel->getX() + cancel.getX() + 4,
                panel->getY() + cancel.getY() + 4, MouseButtonEvent::MouseButton::Left));
            if (!cancelSent) { throw std::runtime_error("Expanded map selector Cancel did not activate"); }
            return 0;
        }



        // ---- two real menu scenes, a host and a guest, over real discovery ----
        if (multiplayerFlow)
        {
            ShaderService shaders = ShaderService::createShaderService(graphics);
            Viewport viewport(0, 0, 640, 480);
            UiRenderService ui(&graphics, &shaders, &viewport);
            graphics.setViewport(0, 0, 640, 480);
            graphics.disableDepthBuffer();
            graphics.enableBlending();

            GlobalConfig globalConfig;
            TimeService timeService(getTimestamp());
            // Cursors the scene manager only hands a frame density to; nothing here draws one.
            CursorService cursorService(sdlContext, &timeService, Cursors{});
            SceneManager sceneManager(sdlContext, window.get(), &graphics, &timeService, nullptr, &cursorService, &globalConfig, UiRenderService(&graphics, &shaders, &viewport), &viewport);
            std::unordered_map<std::string, SideData> sideData;

            SceneContext context(
                sdlContext, &viewport, &graphics, &textureService, &audioService, &cursorService, &shaders, &vfs,
                &*palette, &*palette, &sceneManager, &sideData, &timeService, pathMapping.get(), &globalConfig);

            MainMenuScene host(context, &emptySoundLookup, 640, 480);
            MainMenuScene guest(context, &emptySoundLookup, 640, 480);
            MainMenuScene guest2(context, &emptySoundLookup, 640, 480);
            host.init();
            guest.init();
            guest2.init();

            // Somebody who only listens, to say what is on the wire.
            LanDiscovery observer;
            observer.start(LanDiscovery::defaultPort);
            std::cout << "observer listening: " << observer.isRunning() << "\n";

            auto describe = [&](const char* when) {
                std::cout << "  [wire @ " << when << "]\n";
                for (const auto& g : observer.games())
                {
                    std::cout << "    session=" << g.session << " label='" << g.label << "' host='" << g.hostName << "' map='" << g.map
                              << "' port=" << g.port << " pw=" << (g.passwordHash.empty() ? "no" : "yes") << " launching=" << g.launching
                              << " from=" << g.sourceAddress << "\n";
                    if (g.slots.empty() || isClaimSession(g.session))
                    {
                        for (const auto& s : g.slots)
                        {
                            std::cout << "      claims seat " << s.index << " as '" << s.name << "'\n";
                        }
                        continue;
                    }
                    std::cout << "      seats:";
                    for (const auto& s : g.slots)
                    {
                        if (s.controller == "open")
                        {
                            continue;
                        }
                        std::cout << " " << s.index << "=" << s.controller << (s.ready ? "+ready" : "");
                        if (!s.name.empty())
                        {
                            std::cout << "('" << s.name << "'";
                            if (!s.address.empty())
                            {
                                std::cout << " @" << s.address;
                            }
                            if (!s.personality.empty() && s.controller == "network")
                            {
                                std::cout << " token " << s.personality;
                            }
                            std::cout << ")";
                        }
                    }
                    std::cout << "\n      options:";
                    for (const auto& [n, v] : g.options)
                    {
                        std::cout << " " << n << "=" << v;
                    }
                    std::cout << "\n";
                    for (const auto& line : g.chat)
                    {
                        std::cout << "      chat " << line.sequence << " <" << line.name << "> " << line.text << "\n";
                    }
                }
            };

            auto run = [&](double seconds) {
                auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
                while (std::chrono::steady_clock::now() < end)
                {
                    host.update(16);
                    guest.update(16);
                    guest2.update(16);
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                }
            };

            auto snap = [&](MainMenuScene& scene, const char* file) {
                graphics.clearColor();
                scene.render();
                glFinish();
                screenshot(file);
                std::cout << "  wrote " << file << "\n";
            };

            auto press = [&](MainMenuScene& scene, const char* who, const char* topic, const char* message) {
                std::cout << "  " << who << " presses " << topic << "/" << message << "\n";
                scene.messageNow(topic, message, ActivateMessage{});
            };

            auto click = [&](MainMenuScene& scene, int x, int y) {
                scene.onMouseDown(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
                scene.onMouseUp(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
            };

            auto key = [&](MainMenuScene& scene, SDL_Keycode k) {
                SDL_KeyboardEvent e{};
                e.type = SDL_EVENT_KEY_DOWN;
                e.key = k;
                scene.onKeyDown(e);
            };

            std::cout << "---- both: MULTI, Select, blank address (scan)\n";
            for (auto* scene : {&host, &guest, &guest2})
            {
                scene->messageNow("MAINMENU", "MULTI", ActivateMessage{});
                scene->messageNow("SELPROV", "SELECT", ActivateMessage{});
                scene->messageNow("TCP", "OK", ActivateMessage{});
            }
            run(1.0);
            snap(host, "probe_flow_01_empty_list.ppm");
            describe("nothing hosted yet");

            std::cout << "---- host: New, fills the form (game name, name, password), Next\n";
            press(host, "host", "SELGAME", "STARTNEW");
            click(host, 260, 140);
            host.onTextInput("Bring snacks");
            click(host, 260, 200);
            host.onTextInput("Hector");
            click(host, 260, 260);
            host.onTextInput("sesame");
            snap(host, "probe_flow_02_new_game_form.ppm");
            press(host, "host", "NEWMULTI", "OK");
            run(2.5);
            snap(host, "probe_flow_03_host_setup.ppm");
            describe("host on the setup screen");

            std::cout << "---- guest: the list shows the game; Join with no name, then the wrong password, then right\n";
            snap(guest, "probe_flow_04_guest_list.ppm");
            click(guest, 40, 128);
            run(0.1);
            press(guest, "guest", "SELGAME", "JOINGAME");
            run(0.1);
            snap(guest, "probe_flow_05_no_name.ppm");
            press(guest, "guest", "MSGBOX", "OK");
            run(0.1);

            click(guest, 250, 355);
            guest.onTextInput("Guest");
            click(guest, 250, 425);
            guest.onTextInput("wrong");
            press(guest, "guest", "SELGAME", "JOINGAME");
            run(0.1);
            snap(guest, "probe_flow_06_wrong_password.ppm");
            press(guest, "guest", "MSGBOX", "OK");
            run(0.1);

            click(guest, 250, 425);
            for (int i = 0; i < 5; ++i)
            {
                key(guest, SDLK_BACKSPACE);
            }
            guest.onTextInput("sesame");
            press(guest, "guest", "SELGAME", "JOINGAME");
            run(3.0);
            snap(guest, "probe_flow_07_guest_setup.ppm");
            snap(host, "probe_flow_08_host_with_guest.ppm");
            describe("guest joined");

            std::cout << "---- a second guest joins the same game: it must get the next seat, and the first guest must see it\n";
            run(1.0);
            click(guest2, 250, 355);
            guest2.onTextInput("Third");
            click(guest2, 250, 425);
            guest2.onTextInput("sesame");
            click(guest2, 40, 128);
            press(guest2, "guest2", "SELGAME", "JOINGAME");
            run(3.0);
            snap(guest, "probe_flow_07b_first_guest_sees_third.ppm");
            snap(guest2, "probe_flow_07c_third_player.ppm");
            describe("two guests joined");

            // ---- the battleroom, as each person sees it ----
            //
            // What is asserted is read off the real panels and the real model
            // of each scene (MenuProbe), and off the wire where the question
            // is what another machine would hear. A check that fails is
            // counted, printed, and makes the probe exit non-zero.
            int failures = 0;
            auto check = [&](bool ok, const std::string& what) {
                std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n";
                if (!ok)
                {
                    ++failures;
                }
            };

            auto panelName = [](MainMenuScene& s) -> std::string {
                auto* p = MenuProbe::panel(s);
                return p ? p->getName() : std::string("-");
            };
            auto dialogText = [](MainMenuScene& s) -> std::string {
                std::string out;
                if (auto* d = MenuProbe::dialog(s))
                {
                    d->forAll<UiLabel>([&out](UiLabel& l) {
                        if (out.empty() && !l.getText().empty())
                        {
                            out = l.getText();
                        }
                    });
                }
                return out;
            };
            auto transcript = [](MainMenuScene& s) -> std::vector<std::string> {
                if (auto* p = MenuProbe::panel(s))
                {
                    if (auto box = p->find<UiListBox>("OUTPUT"))
                    {
                        return box->get().getItems();
                    }
                }
                return {};
            };
            auto helpLine = [](MainMenuScene& s) -> std::string {
                if (auto* p = MenuProbe::panel(s))
                {
                    if (auto l = p->find<UiLabel>("HELPTEXT"))
                    {
                        return l->get().getText();
                    }
                }
                return {};
            };
            auto hasButton = [](MainMenuScene& s, const std::string& name) {
                auto* p = MenuProbe::panel(s);
                return p != nullptr && p->find<UiStagedButton>(name).has_value();
            };
            auto buttonEnabled = [](MainMenuScene& s, const std::string& name) {
                auto* p = MenuProbe::panel(s);
                auto b = p ? p->find<UiStagedButton>(name) : std::nullopt;
                return b && b->get().isEnabled();
            };
            auto buttonVisible = [](MainMenuScene& s, const std::string& name) {
                auto* p = MenuProbe::panel(s);
                auto b = p ? p->find<UiStagedButton>(name) : std::nullopt;
                return b && b->get().isVisible();
            };
            auto buttonLabel = [](MainMenuScene& s, const std::string& name) -> std::string {
                auto* p = MenuProbe::panel(s);
                auto b = p ? p->find<UiStagedButton>(name) : std::nullopt;
                return b ? b->get().getStageLabel(b->get().getStage()) : std::string("<no such button>");
            };
            auto contains = [](const std::vector<std::string>& lines, const std::string& text) {
                return std::find(lines.begin(), lines.end(), text) != lines.end();
            };
            auto indexOf = [](const std::vector<std::string>& lines, const std::string& text) -> int {
                auto it = std::find(lines.begin(), lines.end(), text);
                return it == lines.end() ? -1 : static_cast<int>(it - lines.begin());
            };
            auto hostLobby = [&]() -> std::optional<DiscoveredGame> {
                for (const auto& g : observer.games())
                {
                    if (!isClaimSession(g.session) && g.label == "Bring snacks")
                    {
                        return g;
                    }
                }
                return std::nullopt;
            };

            // A gadget is pressed where the panel has it, by name: the
            // battleroom's layout is the data set's, and which of two it is
            // depends on whether the 3.1 patch is loaded.
            auto clickGadget = [&](MainMenuScene& s, const std::string& name) {
                int cx = -1;
                int cy = -1;
                if (auto* p = MenuProbe::panel(s))
                {
                    p->forAll<UiComponent>([&](UiComponent& c) {
                        if (cx < 0 && c.getName() == name && c.isVisible())
                        {
                            cx = p->getX() + c.getX() + static_cast<int>(c.getWidth()) / 2;
                            cy = p->getY() + c.getY() + static_cast<int>(c.getHeight()) / 2;
                        }
                    });
                }
                if (cx < 0)
                {
                    std::cout << "  (no visible gadget " << name << " to press)\n";
                    return;
                }
                click(s, cx, cy);
            };
            auto clickReady = [&](MainMenuScene& s, int seat) {
                clickGadget(s, "PLAYER" + std::to_string(seat) + "_ready");
                run(1.5);
            };
            auto say = [&](MainMenuScene& s, const std::string& text) {
                clickGadget(s, "MESSAGE");
                s.onTextInput(text);
                key(s, SDLK_RETURN);
            };
            auto seatOf = [&](MainMenuScene& s, const std::string& caption) -> int {
                for (int i = 0; i < 10; ++i)
                {
                    if (buttonLabel(s, "PLAYER" + std::to_string(i)) == caption)
                    {
                        return i;
                    }
                }
                return -1;
            };
            auto optionLabel = [](const std::string& option, const std::string& value) -> std::string {
                if (option == "commander")
                {
                    return value == "0" ? "Game ends" : "Continues";
                }
                if (option == "startloc")
                {
                    return value == "0" ? "Fixed" : "Random";
                }
                if (option == "mapping")
                {
                    return value == "0" ? "Unmapped" : "Mapped";
                }
                return value == "0" ? "Permanent" : (value == "1" ? "True" : "Circular");
            };

            std::cout << "---- the battleroom\n";
            check(panelName(host) == "LOUNGE2", "the host is in the battleroom");
            check(panelName(guest) == "LOUNGE2", "a guest is in the battleroom");
            check(panelName(guest2) == "LOUNGE2", "the other guest is in the battleroom");
            check(hasButton(host, "START"), "the host has a Start button");
            check(!hasButton(guest, "START"), "a guest has no Start button");
            check(!hasButton(guest2, "START"), "the other guest has no Start button");
            check(!buttonEnabled(host, "START"), "Start is disabled while the guests are not ready");
            check(helpLine(host) == "Waiting for Guest to be ready", "the help line says who is waited for: '" + helpLine(host) + "'");
            check(buttonLabel(host, "PLAYER0") == "Hector", "seat 1 reads Hector on the host");
            check(buttonLabel(guest, "PLAYER1") == "Guest", "seat 2 reads Guest on a guest");
            check(buttonLabel(host, "PLAYER2") == "Third", "seat 3 reads Third on the host");
            check(buttonLabel(host, "PLAYER3") == "Open", "seat 4 reads Open");
            check(!buttonVisible(host, "PLAYER3_side"), "an open seat shows no side");
            check(buttonVisible(host, "PLAYER1_side"), "a taken seat shows its side");
            check(!buttonEnabled(host, "PLAYER0"), "the host's own name is not clickable");
            check(buttonEnabled(host, "PLAYER1"), "a guest's seat can be clicked by the host");
            check(!buttonEnabled(guest, "PLAYER1"), "a guest's own name is not clickable");
            check(!buttonEnabled(guest, "PLAYER2"), "a guest cannot click another's seat");
            check(buttonEnabled(guest, "PLAYER1_side") && !buttonEnabled(guest, "PLAYER2_side") && !buttonEnabled(guest, "PLAYER0_side"),
                "a guest changes its own side and nobody else's");
            check(!buttonEnabled(host, "PLAYER1_side") && buttonEnabled(host, "PLAYER0_side"),
                "the host changes its own side and not a guest's");
            check(!buttonEnabled(guest, "COMMANDER") && buttonEnabled(host, "COMMANDER"), "only the host changes the game's options");
            check(!buttonEnabled(guest, "MAP") && buttonEnabled(host, "MAP"), "only the host selects the map");
            {
                auto joined = transcript(host);
                check(contains(joined, "Guest has joined") && contains(joined, "Third has joined"), "the room says who joined");
                check(transcript(guest) == joined && transcript(guest2) == joined, "the guests have the same room as the host");
            }

            std::cout << "---- chat, every way\n";
            say(guest, "hello from Guest");
            run(1.5);
            say(guest2, "hi Guest, this is Third");
            run(1.5);
            say(host, "welcome both");
            run(1.5);
            {
                const auto t = transcript(host);
                const auto a = indexOf(t, "Guest: hello from Guest");
                const auto b = indexOf(t, "Third: hi Guest, this is Third");
                const auto c = indexOf(t, "Hector: welcome both");
                check(a >= 0 && b >= 0 && c >= 0, "all three lines are in the host's transcript");
                check(a < b && b < c, "in the order they were said");
                check(transcript(guest) == t, "a guest sees the same transcript, in the same order");
                check(transcript(guest2) == t, "the other guest sees the same transcript, in the same order");
            }
            {
                const auto before = transcript(host).size();
                say(guest, "     ");
                run(1.5);
                check(transcript(host).size() == before, "a line of spaces is not sent");

                // Seventy characters typed, sixty-three kept.
                clickGadget(guest, "MESSAGE");
                guest.onTextInput(std::string(70, 'x'));
                auto* gp = MenuProbe::panel(guest);
                auto box = gp ? gp->find<UiTextBox>("MESSAGE") : std::nullopt;
                check(box && box->get().getText().size() == 63, "no more than 63 characters can be typed");
                key(guest, SDLK_RETURN);
                run(1.5);
                check(box && box->get().getText().empty(), "the box is empty once a line is sent");
            }

            std::cout << "---- ready gates Start\n";
            clickReady(guest, 1);
            {
                auto lobby = hostLobby();
                check(lobby && lobby->slots.size() > 1 && lobby->slots[1].ready, "the host announces that Guest is ready");
                check(lobby && lobby->slots.size() > 2 && !lobby->slots[2].ready, "and that Third is not");
            }
            check(!buttonEnabled(host, "START"), "Start is still disabled with one guest not ready");
            check(helpLine(host) == "Waiting for Third to be ready", "the help line now names the other: '" + helpLine(host) + "'");
            check(buttonEnabled(guest, "PLAYER1_ready"), "a guest's own ready box can be pressed");
            check(!buttonEnabled(guest, "PLAYER2_ready"), "and another's cannot");
            clickReady(guest2, 2);
            check(buttonEnabled(host, "START"), "Start is enabled when every guest is ready");
            clickReady(guest, 1);
            check(!buttonEnabled(host, "START"), "Start is disabled again when a guest unreadies");
            clickReady(guest, 1);
            check(buttonEnabled(host, "START"), "and enabled when it readies again");
            clickReady(guest2, 2);
            check(!buttonEnabled(host, "START"), "the other guest unreadying closes it too");

            std::cout << "---- a guest guesses at Start, and at the host's controls\n";
            press(guest, "guest", "LOUNGE2", "START");
            run(1.0);
            {
                auto lobby = hostLobby();
                check(lobby && !lobby->launching, "a Start from a guest does nothing");
            }

            std::cout << "---- side, colour and team: a guest's own, and the host's\n";
            auto& hm = MenuProbe::model(host);
            auto& g2m = MenuProbe::model(guest2);
            {
                const auto before = hm.players[1].side.getValue();
                clickGadget(guest, "PLAYER" + std::to_string(1) + "_side");
                run(1.5);
                check(hm.players[1].side.getValue() != before, "the guest's side change arrives on the host");
                check(g2m.players[1].side.getValue() == hm.players[1].side.getValue(), "and on the other guest");
                auto lobby = hostLobby();
                check(lobby && lobby->slots[1].side == (hm.players[1].side.getValue() == MainMenuModel::PlayerSettings::Side::Core ? "CORE" : "ARM"),
                    "and in the host's announcement");
            }
            {
                const auto before = hm.players[1].colorIndex.getValue();
                clickGadget(guest, "PLAYER" + std::to_string(1) + "_color");
                run(1.5);
                check(hm.players[1].colorIndex.getValue() != before, "the guest's colour change arrives on the host");
                check(g2m.players[1].colorIndex.getValue() == hm.players[1].colorIndex.getValue(), "and on the other guest");
                std::set<unsigned int> used;
                bool distinct = true;
                for (const auto& p : hm.players)
                {
                    if (p.type.getValue() != MainMenuModel::PlayerSettings::Type::Open)
                    {
                        distinct = used.insert(p.colorIndex.getValue().value).second && distinct;
                    }
                }
                check(distinct, "no two seats in the game share a colour");
            }
            {
                clickGadget(guest, "PLAYER" + std::to_string(1) + "_team");
                run(1.5);
                check(hm.players[1].teamIndex.getValue() == std::optional<int>(0), "the guest's team change arrives on the host");
                check(g2m.players[1].teamIndex.getValue() == std::optional<int>(0), "and on the other guest");
            }
            {
                // The host takes a colour in the very instant a guest asks
                // for it. The host's word stands, and the guest's screen
                // goes back to what it had.
                const auto old = hm.players[2].colorIndex.getValue();
                clickGadget(guest2, "PLAYER" + std::to_string(2) + "_color");
                guest2.update(16);
                const auto wanted = g2m.players[2].colorIndex.getValue();
                check(wanted != old, "the guest asked for another colour (shown at once)");
                hm.players[0].colorIndex.next(wanted);
                run(3.5);
                check(hm.players[2].colorIndex.getValue() == old, "the host refuses a colour another seat holds");
                check(hm.players[0].colorIndex.getValue() == wanted, "and keeps its own");
                check(g2m.players[2].colorIndex.getValue() == old, "the guest's screen goes back to the colour it has");
            }

            std::cout << "---- who may touch what\n";
            {
                const auto hostSide = hm.players[0].side.getValue();
                clickGadget(guest, "PLAYER" + std::to_string(0) + "_side");
                clickGadget(guest, "PLAYER" + std::to_string(0) + "_color");
                clickGadget(guest, "PLAYER" + std::to_string(5));
                clickGadget(guest, "PLAYER" + std::to_string(2) + "_side");
                run(1.5);
                check(hm.players[0].side.getValue() == hostSide, "a guest cannot change the host's side");
                check(hm.players[5].type.getValue() == MainMenuModel::PlayerSettings::Type::Open, "a guest cannot add a computer");

                clickGadget(host, "PLAYER" + std::to_string(1) + "_side");
                run(0.5);
                check(hm.players[1].side.getValue() == g2m.players[1].side.getValue(), "the host cannot change a guest's side");

                clickGadget(host, "PLAYER" + std::to_string(5));
                run(1.5);
                check(hm.players[5].type.getValue() == MainMenuModel::PlayerSettings::Type::Computer, "the host adds a computer to an open seat");
                check(MenuProbe::model(guest).players[5].type.getValue() == MainMenuModel::PlayerSettings::Type::Computer, "and the guest sees it");
                // A computer seat is the original's by default, then RWE's own
                // (ADR-0005), then open again; the seat button's face names the
                // engine and the host's choice reaches every guest.
                check(hm.players[5].personality.getValue() == "Original TA", "a new computer is Original TA");
                check(MenuProbe::model(guest).players[5].personality.getValue() == "Original TA", "and the guest sees which engine");
                check(MenuProbe::model(guest2).players[5].personality.getValue() == "Original TA", "and so does the other guest");
                check(buttonLabel(host, "PLAYER5") == "Original TA", "the seat reads Original TA");
                check(buttonVisible(host, "PLAYER5_ready") && buttonVisible(host, "PLAYER5_side"), "a computer shows its choices");
                check(!buttonVisible(host, "PLAYER5_personality"), "there is no AI personality picker on a computer's row");

                // A guest's click on a computer seat changes neither the
                // engine nor the seat.
                clickGadget(guest, "PLAYER" + std::to_string(5));
                run(1.5);
                check(hm.players[5].type.getValue() == MainMenuModel::PlayerSettings::Type::Computer, "a guest cannot remove the host's computer");
                check(hm.players[5].personality.getValue() == "Original TA", "and cannot change its engine");

                clickGadget(host, "PLAYER" + std::to_string(5));
                run(1.5);
                check(hm.players[5].type.getValue() == MainMenuModel::PlayerSettings::Type::Computer, "the next click keeps it a computer");
                check(hm.players[5].personality.getValue() == "Balanced", "and makes it RWE AI");
                check(buttonLabel(host, "PLAYER5") == "RWE AI", "the seat reads RWE AI");
                check(MenuProbe::model(guest).players[5].personality.getValue() == "Balanced", "the guest sees the change");
                check(buttonLabel(guest, "PLAYER5") == "RWE AI", "and its button reads RWE AI too");
                check(MenuProbe::model(guest2).players[5].personality.getValue() == "Balanced", "and so does the other guest");

                clickGadget(host, "PLAYER" + std::to_string(5));
                run(1.0);
                check(hm.players[5].type.getValue() == MainMenuModel::PlayerSettings::Type::Open, "and a third takes it away again");
                check(buttonLabel(host, "PLAYER5") == "Open", "the seat reads Open");
                clickGadget(host, "PLAYER" + std::to_string(5));
                run(1.0);
                check(hm.players[5].personality.getValue() == "Original TA", "opened again, it starts as Original TA");
            }

            std::cout << "---- the game's options: the button's face says what the model holds\n";
            {
                struct Row
                {
                    const char* button;
                    const char* option;
                };
                const Row rows[] = {{"COMMANDER", "commander"}, {"FIXEDLOC", "startloc"}, {"MAPPING", "mapping"}, {"LOSTYPE", "los"}};
                auto value = [&](const char* option) {
                    for (const auto& [n, v] : optionsFromModel(hm))
                    {
                        if (n == option)
                        {
                            return v;
                        }
                    }
                    return std::string("?");
                };
                for (const auto& r : rows)
                {
                    const auto first = value(r.option);
                    check(buttonLabel(host, r.button) == optionLabel(r.option, first),
                        std::string(r.button) + " shows '" + buttonLabel(host, r.button) + "' for " + r.option + "=" + first);
                    clickGadget(host, r.button);
                    run(1.2);
                    const auto second = value(r.option);
                    check(second != first, std::string("a click changes ") + r.option + " from " + first + " to " + second);
                    check(buttonLabel(host, r.button) == optionLabel(r.option, second),
                        std::string(r.button) + " now shows '" + buttonLabel(host, r.button) + "'");
                    check(buttonLabel(guest, r.button) == optionLabel(r.option, second), std::string("the guest's ") + r.button + " follows");
                    check(buttonLabel(guest2, r.button) == buttonLabel(guest, r.button), std::string("and so does the other guest's"));
                }
                // A guest's click changes nothing.
                const auto commander = value("commander");
                clickGadget(guest, "COMMANDER");
                run(1.0);
                check(value("commander") == commander, "a guest cannot change the commander option");
            }

            snap(host, "probe_flow_11_battleroom_host.ppm");
            snap(guest, "probe_flow_12_battleroom_guest.ppm");
            snap(guest2, "probe_flow_13_battleroom_guest2.ppm");
            describe("the battleroom, rendered");

            std::cout << "---- the host removes a guest\n";
            {
                const auto before = hm.players[2].type.getValue();
                check(before == MainMenuModel::PlayerSettings::Type::Network, "Third is in seat 3");
                clickGadget(host, "PLAYER" + std::to_string(2));
                run(3.0);
                check(hm.players[2].type.getValue() == MainMenuModel::PlayerSettings::Type::Open, "the seat is open");
                check(panelName(guest2) == "SELGAME", "the removed guest is back on the game list");
                check(dialogText(guest2) == "You were removed from the game", "and is told so: '" + dialogText(guest2) + "'");
                check(contains(transcript(host), "Third was removed"), "the room says who was removed");
                check(panelName(guest) == "LOUNGE2", "the other guest is still in the room");
                check(contains(transcript(guest), "Third was removed"), "and heard it");
                run(2.5);
                check(hm.players[2].type.getValue() == MainMenuModel::PlayerSettings::Type::Open, "the removed guest is not seated again by its own claim");
                press(guest2, "guest2", "MSGBOX", "OK");
                run(0.2);
            }

            std::cout << "---- the host picks a map the guest does not have\n";
            {
                const auto original = hm.selectedMap.getValue();
                hm.selectedMap.next(MainMenuModel::SelectedMapInfo("No Such Map Anywhere", "", "", nullptr));
                run(2.5);
                check(panelName(guest) == "SELGAME", "the guest is back on the game list");
                check(dialogText(guest) == "You do not have the map No Such Map Anywhere", "and is told which map: '" + dialogText(guest) + "'");
                hm.selectedMap.next(original);
                press(guest, "guest", "MSGBOX", "OK");
                run(0.5);
            }

            std::cout << "---- both guests come back, ready, and the host starts\n";
            for (auto* scene : {&guest, &guest2})
            {
                click(*scene, 40, 128);
                run(0.2);
                press(*scene, "guest", "SELGAME", "JOINGAME");
                run(2.5);
            }
            check(panelName(guest) == "LOUNGE2" && panelName(guest2) == "LOUNGE2", "both are back in the battleroom");
            {
                const auto gs = seatOf(guest, "Guest");
                const auto ts = seatOf(host, "Third");
                check(gs >= 1 && ts >= 1, "each has a seat again");
                if (gs >= 1 && ts >= 1)
                {
                    clickReady(guest, gs);
                    clickReady(guest2, ts);
                }
                check(buttonEnabled(host, "START"), "Start is enabled");
            }
            press(host, "host", "LOUNGE2", "START");
            run(1.0);
            {
                auto lobby = hostLobby();
                check(lobby && lobby->launching, "the host's announcement says it is launching");
            }
            describe("host launching");
            run(3.0);
            describe("after both started");

            // ---- the internet path: invite code, reply code, punch ----
            //
            // A host and a guest that are not on each other's LAN: each has
            // a discovery port and a game port of its own and a STUN server
            // that is on this machine, there is no broadcast between them,
            // and the only way they find each other is the codes. Nothing
            // here is a router -- loopback has no NAT in it -- so what this
            // settles is the lobby's half of the work: the code is the
            // host's discovery address, the reply is the guest's, the game
            // addresses that go into the seats are the STUN ones, and the
            // game ports are opened toward each other. The punching itself
            // is settled by NatTraversal's tests and the two transports'.
            std::cout << "---- the internet path\n";
            {
                FakeStunServer stunServer;

                MainMenuScene invHost(context, &emptySoundLookup, 640, 480);
                MainMenuScene invGuest(context, &emptySoundLookup, 640, 480);
                invHost.init();
                invGuest.init();
                MenuProbe::useLocalNetwork(invHost, 1361, "1371", stunServer.address());
                MenuProbe::useLocalNetwork(invGuest, 1362, "1372", stunServer.address());

                auto runInternet = [&](double seconds) {
                    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
                    while (std::chrono::steady_clock::now() < end)
                    {
                        invHost.update(16);
                        invGuest.update(16);
                        std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    }
                };

                // The first "RWE-XXXX-XXXX-XXXXX" in a piece of text that is a code.
                auto codeIn = [](const std::string& text) -> std::string {
                    for (std::size_t at = text.find("RWE-"); at != std::string::npos; at = text.find("RWE-", at + 1))
                    {
                        const auto candidate = text.substr(at, 19);
                        if (InviteCode::isInviteCode(candidate))
                        {
                            return candidate;
                        }
                    }
                    return {};
                };

                for (auto* scene : {&invHost, &invGuest})
                {
                    scene->messageNow("MAINMENU", "MULTI", ActivateMessage{});
                    scene->messageNow("SELPROV", "SELECT", ActivateMessage{});
                    scene->messageNow("TCP", "OK", ActivateMessage{});
                }
                runInternet(0.5);

                press(invHost, "host", "SELGAME", "STARTNEW");
                click(invHost, 260, 140);
                invHost.onTextInput("Across the sea");
                click(invHost, 260, 200);
                invHost.onTextInput("Hector");
                press(invHost, "host", "NEWMULTI", "OK");
                runInternet(1.5);
                check(panelName(invHost) == "LOUNGE2", "the internet host is in the battleroom");

                // -- the host asks for an invite --
                say(invHost, "/invite");
                runInternet(2.0);
                std::string inviteCode;
                for (const auto& line : transcript(invHost))
                {
                    if (auto c = codeIn(line); !c.empty())
                    {
                        inviteCode = c;
                    }
                }
                check(!inviteCode.empty(), "the host is given an invite code: '" + inviteCode + "'");
                check(!contains(transcript(invHost), "/invite"), "the command is not said to the room as chat");
                if (auto endpoint = InviteCode::decode(inviteCode))
                {
                    check(*endpoint == asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.1"), 1361),
                        "the code is the host's discovery socket as the STUN server saw it: " + InviteCode::toAddressString(*endpoint));
                }

                // -- the guest joins by it --
                invGuest.messageNow("SELPROV", "SELECT", ActivateMessage{});
                invGuest.onTextInput(inviteCode);
                std::cout << "  guest enters the invite code\n";
                invGuest.messageNow("TCP", "OK", ActivateMessage{});
                runInternet(2.0);
                check(panelName(invGuest) == "SELGAME", "the guest is on the game list");
                const auto replyCode = MenuProbe::flow(invGuest) != nullptr ? MenuProbe::flow(invGuest)->replyCode : std::string();
                check(!replyCode.empty(), "the guest has a reply code: '" + replyCode + "'");
                check(helpLine(invGuest).find("Join") != std::string::npos || codeIn(helpLine(invGuest)) == replyCode, "the guest is offered Join or the fallback reply code");
                check(replyCode != inviteCode, "the reply is the guest's own address, not the invite again");

                // The host's lobby reaches the guest, which lists it.
                runInternet(3.0);
                snap(invGuest, "probe_flow_internet_list.ppm");
                std::cout << "  guest help line: '" << helpLine(invGuest) << "'\n";
                check(helpLine(invGuest).find("Join") != std::string::npos,
                    "the guest's list shows the host's game");

                click(invGuest, 250, 355);
                invGuest.onTextInput("Net");
                click(invGuest, 40, 128);
                runInternet(0.2);
                press(invGuest, "guest", "SELGAME", "JOINGAME");
                runInternet(9.5);
                check(panelName(invGuest) == "LOUNGE2", "the guest can wait for approval beyond eight seconds");
                check(!MenuProbe::flow(invGuest)->accepted, "the guest is not accepted automatically");
                check(MenuProbe::model(invGuest).players[1].type.getValue() == MainMenuModel::PlayerSettings::Type::Open,
                    "a waiting guest does not display an unacknowledged local seat");
                check(MenuProbe::model(invHost).players[1].type.getValue() == MainMenuModel::PlayerSettings::Type::Open,
                    "the requested seat stays open until the host accepts");
                check(contains(transcript(invHost), "Net wants to join. Type /accept to allow."), "the join request includes the player's name");
                check(MenuProbe::flow(invHost)->pendingJoins.size() == 1, "repeated claims make one pending request");
                const auto netToken = MenuProbe::flow(invGuest)->claimToken;
                check(MenuProbe::flow(invHost)->invitedGuests.empty(), "no reply code was needed for the direct request");
                say(invHost, "/accept Nobody");
                runInternet(0.3);
                check(!MenuProbe::flow(invGuest)->accepted, "an unknown name does not admit a different request");

                // A second client deliberately omits the approval flag: the
                // host's own policy must still keep it outside the room.
                LanDiscovery requester;
                requester.start(1363);
                requester.setAnnounceTargets({"127.0.0.1:1361"});
                // Reuse the real guest's discovery: probes are rate limited
                // by IP, and both test clients share loopback's address.
                const auto requestGames = MenuProbe::flow(invGuest)->discovery.games();
                const auto requestHost = std::find_if(requestGames.begin(), requestGames.end(),
                    [](const auto& game) { return !isClaimSession(game.session) && game.label == "Across the sea"; });
                check(requestHost != requestGames.end(), "the test requester has the discovered invitation lobby");
                if (requestHost != requestGames.end())
                {
                    auto request = claimAnnouncement(*requestHost, 1, "cafe1234", "Other Player", 1373);
                    request.approvalRequired = false;
                    requester.advertise(request);
                    runInternet(1.5);
                    check(MenuProbe::flow(invHost)->pendingJoins.size() == 2, "the host enforces its approval policy");
                    check(MenuProbe::model(invHost).players[1].type.getValue() == MainMenuModel::PlayerSettings::Type::Open,
                        "a request cannot bypass approval by dropping its flag");
                    requester.stopAdvertising();
                    runInternet(4.5);
                    check(MenuProbe::flow(invHost)->pendingJoins.size() == 1, "a cancelled request expires");
                    requester.advertise(request);
                    runInternet(1.5);
                    say(invHost, "/accept oTHER pLAYER");
                    runInternet(1.5);
                    check(MenuProbe::model(invHost).players[1].name.getValue() == "Other Player", "accept by name selects that requester");
                    check(!MenuProbe::flow(invGuest)->accepted, "accept by name leaves the other guest waiting");
                    check(MenuProbe::flow(invGuest)->claimToken == netToken, "a waiting request keeps its approval token when its seat is taken");
                    check(MenuProbe::flow(invGuest)->mySeat != 1, "the waiting guest asks for a different open seat");
                }
                say(invHost, "/accept");
                runInternet(2.0);
                check(MenuProbe::flow(invGuest)->accepted, "bare accept admits the oldest pending guest");
                check(contains(transcript(invHost), "Net has joined"), "the host sees the accepted guest join");
                check(!contains(transcript(invHost), "/accept Nobody"), "accept commands are not sent as chat");
                requester.stop();
                runInternet(4.5);
                check(MenuProbe::flow(invHost)->pendingJoins.empty(), "accepted and cancelled requests leave no pending entries");
                say(invHost, "/accept");
                runInternet(0.3);
                check(contains(transcript(invHost), "No join requests are waiting."), "accept reports an empty request queue");
                snap(invHost, "probe_flow_internet_host.ppm");
                snap(invGuest, "probe_flow_internet_guest.ppm");
                const auto heardGuests = MenuProbe::flow(invHost)->nat.heardFrom();
                check(std::any_of(heardGuests.begin(), heardGuests.end(), [](const auto& endpoint) {
                    return endpoint.address().to_string() == "127.0.0.1" && endpoint.port() == 1372;
                }), "the guest's game port is reachable after approval");

                // -- the host takes the reply --
                // A new unapproved claim at the reply endpoint is admitted
                // by the pasted code, without any /accept command. Sharing
                // an IP with Net must not approve a different port earlier.
                LanDiscovery fallback;
                fallback.start(1363);
                fallback.setAnnounceTargets({"127.0.0.1:1361"});
                LanDiscovery unapproved;
                unapproved.start(1364);
                unapproved.setAnnounceTargets({"127.0.0.1:1361"});
                auto fallbackGames = MenuProbe::flow(invGuest)->discovery.games();
                auto fallbackHost = std::find_if(fallbackGames.begin(), fallbackGames.end(),
                    [](const auto& game) { return !isClaimSession(game.session) && game.label == "Across the sea"; });
                check(fallbackHost != fallbackGames.end(), "the fallback requester has the host's session");
                if (fallbackHost != fallbackGames.end())
                {
                    auto fallbackRequest = claimAnnouncement(*fallbackHost, 1, "face1234", "Fallback", 1373);
                    // The STUN mapping deliberately differs from the real
                    // source address and port; no external router is needed.
                    fallbackRequest.discoveryAddress = "127.0.0.2:2463";
                    fallback.advertise(fallbackRequest);
                    auto otherRequest = claimAnnouncement(*fallbackHost, 3, "face5678", "Unapproved", 1374);
                    otherRequest.discoveryAddress = "127.0.0.2:2464";
                    unapproved.advertise(otherRequest);
                    runInternet(1.5);
                    check(MenuProbe::flow(invHost)->pendingJoins.size() == 2, "both fallback requesters initially wait for approval");
                    const auto fallbackCode = InviteCode::encode(asio::ip::udp::endpoint(asio::ip::make_address_v4("127.0.0.2"), 2463));
                    check(fallbackCode.has_value(), "the fallback requester has a reply code");
                    if (fallbackCode)
                    {
                        say(invHost, *fallbackCode);
                        runInternet(1.5);
                        check(MenuProbe::model(invHost).players[1].name.getValue() == "Fallback", "pasting a reply code admits its requester without accept");
                        check(MenuProbe::flow(invHost)->pendingJoins.size() == 1, "reply-code approval clears only its matching pending request");
                        check(MenuProbe::model(invHost).players[3].type.getValue() == MainMenuModel::PlayerSettings::Type::Open,
                            "a different reply port on the same IP stays unapproved");
                    }
                }
                fallback.stop();
                unapproved.stop();
                runInternet(4.5);
                check(MenuProbe::flow(invHost)->pendingJoins.empty(), "cancelled unmatched requests expire after reply-code admission");
                say(invHost, replyCode);
                runInternet(1.0);
                {
                    const auto lines = transcript(invHost);
                    // Both ends are 127.0.0.1 here, which is exactly "the same
                    // router", so that is the sentence the host is given.
                    check(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("That code is on this router") != std::string::npos; }),
                        "the host is told when the reply is from its own router");
                    check(!contains(lines, replyCode), "the reply code is not said to the room as chat");
                }

                // The addresses the game would be started with.
                {
                    auto& hostModel = MenuProbe::model(invHost);
                    auto& guestModel = MenuProbe::model(invGuest);
                    std::string seenByHost;
                    for (const auto& p : hostModel.players)
                    {
                        if (p.type.getValue() == rwe::MainMenuModel::PlayerSettings::Type::Network)
                        {
                            seenByHost = p.networkAddress.getValue();
                        }
                    }
                    std::string seenByGuest;
                    for (const auto& p : guestModel.players)
                    {
                        if (p.type.getValue() == rwe::MainMenuModel::PlayerSettings::Type::Network)
                        {
                            seenByGuest = p.networkAddress.getValue();
                        }
                    }
                    std::cout << "  the host will send to " << seenByHost << ", the guest to " << seenByGuest << "\n";
                    check(seenByHost == "127.0.0.1:1372", "the host's seat for the guest is the guest's game port as STUN saw it");
                    check(seenByGuest == "127.0.0.1:1371", "the guest's seat for the host is the host's game port as STUN saw it");
                }

                // Each side's game port has been sent to the other's, and heard.
                runInternet(1.5);
                {
                    auto* hostFlow = MenuProbe::flow(invHost);
                    auto* guestFlow = MenuProbe::flow(invGuest);
                    check(hostFlow != nullptr && !hostFlow->nat.heardFrom().empty(), "the host's game port has heard the guest's");
                    check(guestFlow != nullptr && !guestFlow->nat.heardFrom().empty(), "the guest's game port has heard the host's");
                }

            // ---- another build ----
            //
            // The host from the section above is still in its battleroom. A
            // guest that is a different build is told so, on the list and
            // when it presses Join, and is not seated; and a claim from a
            // build that did not check, which is what an older one is, is
            // refused by the host and said to the room.
            std::cout << "---- a guest on another build\n";
            {
                MainMenuScene staleGuest(context, &emptySoundLookup, 640, 480);
                staleGuest.init();
                MenuProbe::useLocalNetwork(staleGuest, 1363, "1373", "127.0.0.1:1");

                auto runBoth = [&](double seconds) {
                    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(seconds * 1000));
                    while (std::chrono::steady_clock::now() < end)
                    {
                        invHost.update(16);
                        staleGuest.update(16);
                        std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    }
                };

                staleGuest.messageNow("MAINMENU", "MULTI", ActivateMessage{});
                staleGuest.messageNow("SELPROV", "SELECT", ActivateMessage{});
                staleGuest.onTextInput("127.0.0.1:1361");
                staleGuest.messageNow("TCP", "OK", ActivateMessage{});
                runBoth(2.0);

                MenuProbe::flow(staleGuest)->discovery.setBuildIdentity("abcdef0123456789abcdef0123456789abcdef01");

                click(staleGuest, 250, 355);
                staleGuest.onTextInput("Stale");
                click(staleGuest, 40, 128);
                runBoth(0.3);
                std::cout << "  help line: '" << helpLine(staleGuest) << "'\n";
                check(helpLine(staleGuest).find("cannot join") != std::string::npos, "the list says the game is another version");

                press(staleGuest, "stale guest", "SELGAME", "JOINGAME");
                runBoth(0.3);
                std::cout << "  dialog: '" << dialogText(staleGuest) << "'\n";
                check(dialogText(staleGuest).find("version") != std::string::npos, "pressing Join says the version is not the host's");
                check(panelName(staleGuest) == "SELGAME", "the guest has not left the list");
                press(staleGuest, "stale guest", "MSGBOX", "OK");
                runBoth(1.0);
                check(!contains(transcript(invHost), "Stale has joined"), "the host has not seated it");

                // What an older build does: claims without having looked. A
                // claim naming another build, made by hand.
                LanDiscovery older;
                older.start(1364);
                older.setAnnounceTargets({"127.0.0.1:1361"});
                older.probe("127.0.0.1:1361");
                runBoth(1.0);
                const auto heardGames = older.games();
                const auto hostGame = std::find_if(heardGames.begin(), heardGames.end(), [](const auto& g) { return !isClaimSession(g.session) && g.label == "Across the sea"; });
                check(hostGame != heardGames.end(), "the hand-made guest can see the host's game");
                if (hostGame != heardGames.end())
                {
                    auto claim = claimAnnouncement(*hostGame, 4, std::string(claimTokenLength, 'a'), "Antique", 1374);
                    claim.build = "0123456789abcdef0123456789abcdef01234567";
                    older.advertise(claim);
                    runBoth(2.0);
                    const auto lines = transcript(invHost);
                    check(std::any_of(lines.begin(), lines.end(), [](const std::string& l) { return l.find("Antique cannot join") != std::string::npos; }),
                        "the host says the other build was turned away");
                    check(!contains(lines, "Antique has joined"), "and did not seat it");
                }
                older.stop();
            }
            }

            observer.stop();
            std::cout << "flow done: " << failures << " check(s) failed\n";
            return failures == 0 ? 0 : 1;
        }

        // ---- the multiplayer screens, built by the same functions the scene uses ----
        if (multiplayerScreens)
        {
            ShaderService shaders = ShaderService::createShaderService(graphics);
            Viewport uiViewport(0, 0, 640, 480);
            UiRenderService ui(&graphics, &shaders, &uiViewport);
            graphics.setViewport(0, 0, 640, 480);
            graphics.disableDepthBuffer();
            graphics.enableBlending();

            int blankFaces = 0;

            // ---- SELPROV ----
            {
                auto panel = makeSelectConnectionPanel(factory);
                printActivations(*panel);
                dumpPanel(*panel, "SELPROV");
                expectGadgets(*panel, {"DPLAY", "SLIDER", "SELECT", "PREVMENU"}, {"SETTINGS", "SERVICEX"});
                blankFaces += expectButtonFaces(*panel, textureService, "SELPROV");
                auto list = panel->find<UiListBox>("DPLAY");
                std::cout << "  DPLAY rows=" << (list ? list->get().getItems().size() : 0)
                          << " first='" << (list && !list->get().getItems().empty() ? list->get().getItems()[0] : "") << "'"
                          << " selected=" << (list && list->get().getSelectedIndex() ? std::to_string(*list->get().getSelectedIndex()) : "none") << "\n";

                bool activated = false;
                if (list)
                {
                    list->get().itemActivated().subscribe([&activated](unsigned int) { activated = true; });
                    auto pos = std::make_pair(panel->getX() + list->get().getX() + 40, panel->getY() + list->get().getY() + 6);
                    panel->mouseDown(MouseButtonEvent(pos.first, pos.second, MouseButtonEvent::MouseButton::Left));
                    panel->update(0.1f);
                    panel->mouseDown(MouseButtonEvent(pos.first, pos.second, MouseButtonEvent::MouseButton::Left));
                    std::cout << "  double-click on the connection: " << (activated ? "item activated" : "NOT activated") << "\n";
                }
                clickNamed(*panel, "SELECT");
                clickNamed(*panel, "PREVMENU");
                pressKey(*panel, "Enter (no crdefault in the file: Select)", SDLK_RETURN);
                pressKey(*panel, "Escape (escdefault=PREVMENU)", SDLK_ESCAPE);

                graphics.clearColor();
                panel->render(ui);
                glFinish();
                screenshot("probe_mp_selprov.ppm");

                // ---- TCP, as a dialog over it ----
                auto tcp = makeTcpAddressPanel(factory);
                printActivations(*tcp);
                dumpPanel(*tcp, "TCP");
                expectGadgets(*tcp, {"TEXT", "ADDRESS", "OK", "PREV"}, {});
                blankFaces += expectButtonFaces(*tcp, textureService, "TCP");
                auto address = tcp->find<UiTextBox>("ADDRESS");
                if (address)
                {
                    std::cout << "  ADDRESS maxLength=" << address->get().getMaxLength()
                              << " masked=" << address->get().isMasked() << "\n";
                    tcp->textInput("192.168.2.120");
                    std::cout << "  typed into the focused box: '" << address->get().getText() << "'"
                              << " valid=" << LanDiscovery::isProbeAddress(address->get().getText()) << "\n";
                    for (const char* sample : {"", "example.com", "192.168.2.120:1350", "[::1]:1338", "999.1.1.1"})
                    {
                        std::cout << "  isProbeAddress('" << sample << "')=" << LanDiscovery::isProbeAddress(sample) << "\n";
                    }
                }
                pressKey(*tcp, "Enter (crdefault -> OK)", SDLK_RETURN);
                pressKey(*tcp, "Escape (escdefault -> PREV)", SDLK_ESCAPE);
                clickNamed(*tcp, "OK");
                clickNamed(*tcp, "PREV");

                graphics.clearColor();
                panel->render(ui);
                ui.fillScreen(Color(0, 0, 0, 63));
                tcp->render(ui);
                glFinish();
                screenshot("probe_mp_tcp.ppm");
            }

            // ---- SELGAME ----
            {
                auto panel = makeSelectGamePanel(factory);
                printActivations(*panel);
                dumpPanel(*panel, "SELGAME");
                expectGadgets(*panel, {"GAMENAME", "MAPNAME", "COMMANDER", "PING", "LOS", "FULLMAP", "METAL", "ENERGY", "PLAYERS", "STATUS", "SLIDER", "NICKNAME", "PASSWORD", "STARTNEW", "UPDATE", "JOINGAME", "PREVMENU", "HELPTEXT"}, {"WATCH"});
                blankFaces += expectButtonFaces(*panel, textureService, "SELGAME");
                if (auto help = panel->find<UiLabel>("HELPTEXT"))
                {
                    std::cout << "  HELPTEXT is " << help->get().getWidth() << " wide, to x=" << (help->get().getX() + static_cast<int>(help->get().getWidth())) << " of 640\n";
                    if (help->get().getX() + static_cast<int>(help->get().getWidth()) < 600)
                    {
                        std::cout << "  FAIL  the status line does not reach the right of the screen\n";
                        ++blankFaces;
                    }
                }

                for (const char* name : {"NICKNAME", "PASSWORD"})
                {
                    if (auto box = panel->find<UiTextBox>(name))
                    {
                        std::cout << "  " << name << " maxLength=" << box->get().getMaxLength() << " masked=" << box->get().isMasked() << "\n";
                    }
                }

                // Three games, as the list would be given them.
                std::vector<DiscoveredGame> games(3);
                const char* names[] = {"Bring snacks", "Big Metal Rush", "A very long game name indeed that cannot fit"};
                const char* maps[] = {"Coast To Coast", "Hundred Isles", "Lava Lake"};
                for (std::size_t i = 0; i < games.size(); ++i)
                {
                    auto& g = games[i];
                    g.session = std::string(16, static_cast<char>('a' + i));
                    g.map = maps[i];
                    g.label = names[i];
                    g.hostName = "Host" + std::to_string(i);
                    g.options = {{"commander", i == 1 ? "1" : "0"}, {"los", std::to_string(i)}, {"mapping", i == 2 ? "1" : "0"}};
                    for (int s = 0; s < 10; ++s)
                    {
                        DiscoveredSlot slot;
                        slot.index = s;
                        slot.controller = s == 0 ? "human" : (s == 1 && i == 2 ? "network" : (static_cast<std::size_t>(s) <= i + 1 ? "computer" : "open"));
                        slot.metal = i == 1 ? 10000 : 1000;
                        slot.energy = i == 1 ? 10000 : 1000;
                        g.slots.push_back(slot);
                    }
                    g.launching = (i == 2);
                }
                std::vector<GameListRow> rows;
                for (const auto& g : games)
                {
                    rows.push_back(gameListRow(g));
                }
                fillGameList(*panel, rows);
                std::cout << "  filled " << rows.size() << " rows; first row players=" << rows[0].players << " status=" << rows[0].status
                          << ", third status=" << rows[2].status << "\n";
                printSelections(*panel);

                // Click the second row of the MAPNAME column: rows are 12px
                // from the box's top.
                if (auto map = panel->find<UiListBox>("MAPNAME"))
                {
                    auto x = panel->getX() + map->get().getX() + 20;
                    auto y = panel->getY() + map->get().getY() + 12;
                    std::cout << "  click second row of MAPNAME at (" << x << "," << y << ")\n";
                    panel->mouseDown(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
                    panel->mouseUp(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
                    printSelections(*panel);
                    std::cout << "  selectedGameRow=" << (selectedGameRow(*panel) ? std::to_string(*selectedGameRow(*panel)) : "none") << "\n";
                }
                if (auto status = panel->find<UiListBox>("STATUS"))
                {
                    auto x = panel->getX() + status->get().getX() + 5;
                    auto y = panel->getY() + status->get().getY() + 24;
                    std::cout << "  click third row of STATUS at (" << x << "," << y << ")\n";
                    panel->mouseDown(MouseButtonEvent(x, y, MouseButtonEvent::MouseButton::Left));
                    printSelections(*panel);
                }

                if (auto nickname = panel->find<UiTextBox>("NICKNAME"))
                {
                    panel->setFocusByName("NICKNAME");
                    panel->textInput("Hector the Great and Terrible, Lord of Many Names");
                    std::cout << "  typed a 49 character nickname -> '" << nickname->get().getText() << "' (" << nickname->get().getText().size() << ")\n";
                }
                if (auto password = panel->find<UiTextBox>("PASSWORD"))
                {
                    panel->setFocusByName("PASSWORD");
                    panel->textInput("sesame");
                    std::cout << "  typed a password -> getText='" << password->get().getText() << "' masked=" << password->get().isMasked() << "\n";
                }

                clickNamed(*panel, "STARTNEW");
                clickNamed(*panel, "UPDATE");
                clickNamed(*panel, "JOINGAME");
                clickNamed(*panel, "PREVMENU");
                pressKey(*panel, "Enter (crdefault=STARTNEW)", SDLK_RETURN);
                pressKey(*panel, "Escape (escdefault=PREVMENU)", SDLK_ESCAPE);

                if (auto help = panel->find<UiLabel>("HELPTEXT"))
                {
                    help->get().setText("Hosted by Host1. Enter its password to join.");
                }

                panel->setFocusByName("STARTNEW");
                graphics.clearColor();
                panel->render(ui);
                glFinish();
                screenshot("probe_mp_selgame.ppm");
            }

            // ---- NEWMULTI ----
            {
                auto panel = makeNewGamePanel(factory);
                printActivations(*panel);
                dumpPanel(*panel, "NEWMULTI");
                expectGadgets(*panel, {"GAMENAME", "NICKNAME", "PASSWORD", "OK", "CANCEL"}, {});
                blankFaces += expectButtonFaces(*panel, textureService, "NEWMULTI");

                for (const char* name : {"GAMENAME", "NICKNAME", "PASSWORD"})
                {
                    if (auto box = panel->find<UiTextBox>(name))
                    {
                        panel->setFocusByName(name);
                        panel->textInput("abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789");
                        std::cout << "  " << name << " maxLength=" << box->get().getMaxLength() << " masked=" << box->get().isMasked()
                                  << " accepted " << box->get().getText().size() << " of 72 typed characters\n";
                    }
                }
                if (auto box = panel->find<UiTextBox>("GAMENAME"))
                {
                    box->get().setText("Bring snacks");
                }
                if (auto box = panel->find<UiTextBox>("NICKNAME"))
                {
                    box->get().setText("Hector");
                }
                if (auto box = panel->find<UiTextBox>("PASSWORD"))
                {
                    box->get().setText("secret");
                }

                clickNamed(*panel, "OK");
                clickNamed(*panel, "CANCEL");
                panel->setFocusByName("GAMENAME");
                pressKey(*panel, "Enter with a text box focused (crdefault=OK)", SDLK_RETURN);
                pressKey(*panel, "Escape (escdefault=CANCEL)", SDLK_ESCAPE);

                panel->setFocusByName("GAMENAME");
                graphics.clearColor();
                panel->render(ui);
                glFinish();
                screenshot("probe_mp_newgame.ppm");
            }

            std::cout << "probe done\n";
            if (blankFaces != 0)
            {
                std::cout << blankFaces << " problem(s) with button faces or the status line\n";
                return 1;
            }
            return 0;
        }

        // ---- any gui the caller named, and nothing else ----
        if (!requestedPanels.empty())
        {
            ShaderService shaders = ShaderService::createShaderService(graphics);
            Viewport uiViewport(0, 0, 640, 480);
            UiRenderService ui(&graphics, &shaders, &uiViewport);
            graphics.setViewport(0, 0, 640, 480);
            graphics.disableDepthBuffer();
            graphics.enableBlending();

            for (const auto& panelName : requestedPanels)
            {
                auto p = factory.panelFromGuiFile(panelName);
                if (renderRequested)
                {
                    graphics.clearColor();
                    p->render(ui);
                    glFinish();
                    const auto file = "probe_panel_" + panelName + ".ppm";
                    screenshot(file.c_str());
                    std::cout << "  wrote " << file << "\n";
                }
                p->groupMessages().subscribe([](const auto& msg) {
                    if (std::get_if<ActivateMessage>(&msg.message) != nullptr)
                    {
                        std::cout << "[MSG topic=" << msg.topic << " control=" << msg.controlName << "]";
                    }
                });
                dumpPanel(*p, panelName);
                clickAll(*p, panelName);
            }
            std::cout << "probe done\n";
            return 0;
        }

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

        // lookup probe
        std::cout << "IGOPT=" << (textureService.getGuiTexture("X", "IGOPT").has_value() ? "HIT" : "MISS") << std::endl;
        std::cout << "SOUNDSRT=" << (textureService.getGuiTexture("X", "SOUNDSRT").has_value() ? "HIT" : "MISS") << std::endl;
        std::cout << "MUSICRT=" << (textureService.getGuiTexture("X", "MUSICRT").has_value() ? "HIT" : "MISS") << std::endl;
        std::cout << "VISUALSRT=" << (textureService.getGuiTexture("X", "VISUALSRT").has_value() ? "HIT" : "MISS") << std::endl;
        std::cout << "SPEEDSRT=" << (textureService.getGuiTexture("X", "SPEEDSRT").has_value() ? "HIT" : "MISS") << std::endl;
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

        // every in-game page, plus the save/load dialogs
        graphics.clearColor();
        for (const char* pageName : {"VISUALRT"})
        {
            auto p2 = factory.panelFromGuiFile(pageName);
            std::cout << pageName << " panel pos=(" << p2->getX() << "," << p2->getY() << ")" << std::endl;
            dumpPanel(*p2, pageName);
            p2->render(ui);
        }
        glFinish();
        screenshot("probe_pages.ppm");

        graphics.clearColor();
        auto dlgEntries = parseGuiFromBytes(*vfs.readFile("guis/LOADGAME.GUI"));
        auto saveDlg = factory.panelFromGuiFile("SAVEGAME", "DSavegame2", *dlgEntries);
        auto loadDlg = factory.panelFromGuiFile("LOADGAME");
        saveDlg->render(ui);
        glFinish();
        screenshot("probe_save.ppm");
        graphics.clearColor();
        loadDlg->render(ui);
        glFinish();
        screenshot("probe_load.ppm");

        std::cout << "probe done\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "probe error: " << e.what() << "\n";
        return 1;
    }
}
