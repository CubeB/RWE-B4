// Headless probe for the options screens: builds the real panels from the
// real game data, dumps every gadget's hitbox, and simulates clicks the way
// the scenes deliver them, printing which gadget takes each event and what
// message comes out. Diagnoses layout/dispatch faults without launching the
// game.
#include <cstdio>
#include <glad/gl.h>
#include <iostream>
#include <rwe/AudioService.h>
#include <rwe/ColorPalette.h>
#include <rwe/CursorService.h>
#include <chrono>
#include <rwe/GlobalConfig.h>
#include <rwe/MainMenuScene.h>
#include <rwe/MainMenuScene_multiplayer.h>
#include <rwe/SceneContext.h>
#include <rwe/scene/SceneManager.h>
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
        for (int i = 1; i < argc; ++i)
        {
            std::string arg(argv[i]);
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
            if (arg == "--panel" && i + 1 < argc)
            {
                requestedPanels.emplace_back(argv[++i]);
                continue;
            }
            if (arg == "--help" || arg == "-h")
            {
                std::cout << "usage: ui_probe [--panel NAME]... <data path>...\n"
                          << "  With no --panel, walks the options screens the probe was written for.\n"
                          << "  With one or more, dumps and clicks just those guis and stops.\n"
                          << "  --multiplayer builds the four multiplayer screens (SELPROV, TCP, SELGAME,\n"
                          << "  NEWMULTI) the way the main menu does, drives them, and writes probe_mp_*.ppm.\n"
                          << "  --multiplayer-flow runs two real MainMenuScenes (a host and a guest) through\n"
                          << "  the whole flow over LAN discovery on this machine, and writes probe_flow_*.ppm.\n";
                return 0;
            }
            addToVfs(vfs, arg);
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
                        std::cout << " " << s.index << "=" << s.controller;
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

            std::cout << "---- guest tries to change a shared setting (must be ignored); host changes the map options\n";
            press(guest, "guest", "SKIRMISH", "CommanderDeath");
            press(guest, "guest", "SKIRMISH", "PLAYER2");
            press(host, "host", "SKIRMISH", "LineOfSight");
            press(host, "host", "SKIRMISH", "PLAYER2");
            run(2.5);
            snap(guest, "probe_flow_09_guest_follows.ppm");
            snap(host, "probe_flow_10_host_changed.ppm");
            describe("after the host changed options and seat 3");

            std::cout << "---- host presses Start\n";
            press(host, "host", "SKIRMISH", "Start");
            run(1.0);
            describe("host launching");
            run(3.0);
            describe("after both started");

            observer.stop();
            std::cout << "flow done\n";
            return 0;
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

            // ---- SELPROV ----
            {
                auto panel = makeSelectConnectionPanel(factory);
                printActivations(*panel);
                dumpPanel(*panel, "SELPROV");
                expectGadgets(*panel, {"DPLAY", "SLIDER", "SELECT", "PREVMENU"}, {"SETTINGS", "SERVICEX"});
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
            return 0;
        }

        // ---- any gui the caller named, and nothing else ----
        if (!requestedPanels.empty())
        {
            for (const auto& panelName : requestedPanels)
            {
                auto p = factory.panelFromGuiFile(panelName);
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
