#include <GL/glew.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <rwe/AudioService.h>
#include <rwe/ColorPalette.h>
#include <rwe/GlobalConfig.h>
#include <rwe/LoadingScene.h>
#include <rwe/MainMenuScene.h>
#include <rwe/MovieScene.h>
#include <rwe/PathMapping.h>
#include <rwe/game/SaveFile.h>
#include <rwe/SceneContext.h>
#include <rwe/ShaderService.h>
#include <rwe/Viewport.h>
#include <rwe/config.h>
#include <rwe/game/PlayerColorIndex.h>
#include <rwe/io/gui/gui.h>
#include <rwe/io/tdf/tdf.h>
#include <rwe/ip_util.h>
#include <rwe/render/GraphicsContext.h>
#include <rwe/render/OpenGlVersion.h>
#include <rwe/rwe_time.h>
#include <rwe/scene/SceneManager.h>
#include <rwe/sdl/SdlContext.h>
#include <rwe/sdl/SdlContextManager.h>
#include <rwe/sim/Energy.h>
#include <rwe/sim/Metal.h>
#include <rwe/ui/UiFactory.h>
#include <rwe/util.h>
#include <rwe/util/OpaqueArgs.h>
#include <rwe/util/Result.h>
#include <rwe/util/SimpleLogger.h>
#include <rwe/vfs/CompositeVirtualFileSystem.h>

#include <rwe/GameLaunch.h>

namespace fs = std::filesystem;

namespace rwe
{
    OpenGlVersion getOpenGlContextVersion()
    {
        int major;
        int minor;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        return OpenGlVersion(major, minor);
    }

    void doGlewInit()
    {
        // Must set glewExperimental to true, otherwise in glew <= 1.13.0 glewInit() will fail
        // and set glError to GL_INVALID_ENUM if called in an OpenGL core context.
        // GL_INVALID_ENUM may *still* be emitted anyway, but isn't critical.
        // Ubuntu 16.04 and earlier uses glew 1.13.0, Ubuntu 18.04 uses glew 2.0.0.
        // See: https://www.khronos.org/opengl/wiki/OpenGL_Loading_Library
        glewExperimental = GL_TRUE;
        if (auto result = glewInit(); result != GLEW_OK)
        {
            throw std::runtime_error(reinterpret_cast<const char*>(glewGetErrorString(result)));
        }

        if (auto error = glGetError(); error == GL_NO_ERROR)
        {
            // all good, continue on
        }
        else if (error == GL_INVALID_ENUM)
        {
            // Ignore, expected from glew even with glewExperimental = true.
            // It should have still worked anyway.

            // Check if there are any more errors.
            if (auto nextError = glGetError(); nextError != GL_NO_ERROR)
            {
                throw OpenGlException(error);
            }
        }
        else
        {
            throw OpenGlException(error);
        }
    }

    Result<SdlContext::GlContextUniquePtr, const char*> createOpenGlContext(SdlContext* sdlContext, SDL_Window* window, const OpenGlVersionInfo& requiredVersion)
    {
        LOG_INFO << "Requesting OpenGL version "
                 << requiredVersion.version.majorVersion << "."
                 << requiredVersion.version.minorVersion << ", "
                 << getOpenGlProfileName(requiredVersion.profile) << " profile";

        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, requiredVersion.version.majorVersion))
        {
            return Err(SDL_GetError());
        }
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, requiredVersion.version.minorVersion))
        {
            return Err(SDL_GetError());
        }
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, getSdlProfileMask(requiredVersion.profile)))
        {
            return Err(SDL_GetError());
        }
        if (!sdlContext->glSetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG))
        {
            return Err(SDL_GetError());
        }

        auto glContext = sdlContext->glCreateContext(window);
        if (glContext == nullptr)
        {
            return Err(SDL_GetError());
        }

        auto contextVersion = getOpenGlContextVersion();
        if (contextVersion < requiredVersion.version)
        {
            static const char* errMessage = "Created OpenGL context did not meet version requirements";
            return Err(errMessage);
        }

        return Ok(std::move(glContext));
    };


    int run(const std::vector<fs::path>& searchPath, const PathMapping& pathMapping, const std::optional<GameParameters>& gameParameters, unsigned int desiredWindowWidth, unsigned int desiredWindowHeight, WindowMode windowMode, const std::string& imGuiIniPath, GlobalConfig& globalConfig)
    {
        LOG_INFO << ProjectNameVersion;
        LOG_INFO << "Current directory: " << fs::current_path().string();

        TimeService timeService(getTimestamp());

        LOG_INFO << "Initializing SDL";
        SdlContextManager sdlManager;

        auto sdlContext = sdlManager.getSdlContext();

        // require a stencil buffer of some kind
        if (!sdlContext->glSetAttribute(SDL_GL_STENCIL_SIZE, 1))
        {
            throw std::runtime_error(SDL_GetError());
        }

        Uint32 windowFlags = SDL_WINDOW_OPENGL;
        switch (windowMode)
        {
            case WindowMode::Bordered:
                windowFlags |= SDL_WINDOW_RESIZABLE;
                break;
            case WindowMode::Borderless:
                // In SDL3 a fullscreen window with no exclusive display mode
                // set is a borderless window over the desktop, which is
                // exactly what is wanted here.
                windowFlags |= SDL_WINDOW_FULLSCREEN;
                break;
            case WindowMode::Fullscreen:
                windowFlags |= SDL_WINDOW_FULLSCREEN;
                break;
        }

        auto window = sdlContext->createWindow(
            "RWE",
            desiredWindowWidth,
            desiredWindowHeight,
            windowFlags);
        if (window == nullptr)
        {
            throw std::runtime_error(SDL_GetError());
        }

        if (windowMode == WindowMode::Fullscreen)
        {
            SDL_DisplayMode targetMode;
            auto displayID = sdlContext->getWindowDisplayIndex(window.get());
            if (displayID == 0)
            {
                throw std::runtime_error(SDL_GetError());
            }

            if (!sdlContext->getClosestDisplayMode(displayID, desiredWindowWidth, desiredWindowHeight, 0.0f, &targetMode))
            {
                throw std::runtime_error(SDL_GetError());
            }

            if (!sdlContext->setWindowDisplayMode(window.get(), &targetMode))
            {
                throw std::runtime_error(SDL_GetError());
            }
            sdlContext->setWindowSize(window.get(), desiredWindowWidth, desiredWindowHeight);
        }

        // Prevent the mouse from leaving the window.
        // We rely on nudging the edges of the screen to pan the camera,
        // so this is necessary for the game to work -- except in a bordered
        // window, where trapping the cursor would make the title bar
        // unreachable and the window impossible to drag. There the edges
        // still pan, just without the fence.
        if (windowMode != WindowMode::Bordered)
        {
            sdlContext->setWindowGrab(window.get(), true);
        }

        int windowWidth;
        int windowHeight;
        sdlContext->getWindowSize(window.get(), &windowWidth, &windowHeight);
        Viewport viewport(0, 0, windowWidth, windowHeight);

        LOG_INFO << "Initializing OpenGL context";

        auto glContextResult = createOpenGlContext(sdlContext, window.get(), OpenGlVersionInfo(3, 2, OpenGlProfile::Core));
        if (!glContextResult)
        {
            LOG_ERROR << "Failed to create preferred OpenGL context: " << glContextResult.getErr();
            glContextResult = createOpenGlContext(sdlContext, window.get(), OpenGlVersionInfo(3, 0, OpenGlProfile::Compatibility));
            if (!glContextResult)
            {
                throw std::runtime_error(glContextResult.getErr());
            }
        }

        auto glContext = std::move(*glContextResult);
        if (glContext == nullptr)
        {
            throw std::runtime_error(SDL_GetError());
        }

        doGlewInit();

        // log opengl context info
        LOG_INFO << "OpenGL version: " << glGetString(GL_VERSION);
        LOG_INFO << "OpenGL vendor: " << glGetString(GL_VENDOR);
        LOG_INFO << "OpenGL renderer: " << glGetString(GL_RENDERER);
        LOG_INFO << "OpenGL shading language version: " << glGetString(GL_SHADING_LANGUAGE_VERSION);
        LOG_DEBUG << "OpenGL extensions:";
        int openGlExtensionCount;
        glGetIntegerv(GL_NUM_EXTENSIONS, &openGlExtensionCount);
        for (int i = 0; i < openGlExtensionCount; ++i)
        {
            LOG_DEBUG << "  " << glGetStringi(GL_EXTENSIONS, i);
        }

        LOG_INFO << "Initializing Dear ImGui";
        ImGuiContext imGuiContext(imGuiIniPath, window.get(), glContext.get());

        LOG_INFO << "Initializing virtual file system";
        CompositeVirtualFileSystem vfs;
        for (const auto& path : searchPath)
        {
            addToVfs(vfs, path.string());
        }

        LOG_INFO << "Loading palette";
        auto paletteBytes = vfs.readFile("palettes/PALETTE.PAL");
        if (!paletteBytes)
        {
            throw std::runtime_error("Couldn't find palette");
        }

        auto palette = readPalette(*paletteBytes);
        if (!palette)
        {
            throw std::runtime_error("Couldn't read palette");
        }

        LOG_INFO << "Loading GUI palette";
        auto guiPaletteBytes = vfs.readFile("palettes/GUIPAL.PAL");
        if (!guiPaletteBytes)
        {
            throw std::runtime_error("Couldn't find palette");
        }

        auto guiPalette = readPalette(*guiPaletteBytes);
        if (!guiPalette)
        {
            throw std::runtime_error("Couldn't read GUI palette");
        }

        LOG_INFO << "Initializing services";
        GraphicsContext graphics;
        graphics.enableCulling();
        graphics.enableBlending();

        ShaderService shaders = ShaderService::createShaderService(graphics);

        TextureService textureService(&graphics, &vfs, &*palette);

        AudioService audioService(sdlContext, sdlManager.getSdlMixerContext(), &vfs);
        audioService.setSoundVolume(static_cast<float>(globalConfig.soundVolume) / 100.0f);
        audioService.setMusicVolume(static_cast<float>(globalConfig.musicVolume) / 100.0f);
        audioService.setMusicEnabled(globalConfig.musicEnabled);
        audioService.setSoundEnabled(globalConfig.soundMode != 0);
        // Allocate a pool of tracks for sound playback
        audioService.allocateTracks(256);

        // load sound definitions
        LOG_INFO << "Loading global sound definitions";
        auto allSoundBytes = vfs.readFile("gamedata/ALLSOUND.TDF");
        if (!allSoundBytes)
        {
            throw std::runtime_error("Couldn't read ALLSOUND.TDF");
        }

        std::string allSoundString(allSoundBytes->data(), allSoundBytes->size());
        auto allSoundTdf = parseTdfFromString(allSoundString);

        LOG_INFO << "Loading cursors";
        Cursors cursors;
        cursors[*CursorType::Normal] = textureService.getGafEntry("anims/CURSORS.GAF", "cursornormal");
        cursors[*CursorType::Select] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorselect");
        cursors[*CursorType::Attack] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorattack");
        cursors[*CursorType::Move] = textureService.getGafEntry("anims/CURSORS.GAF", "cursormove");
        cursors[*CursorType::Guard] = textureService.getGafEntry("anims/CURSORS.GAF", "cursordefend");
        cursors[*CursorType::Repair] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorrepair");
        cursors[*CursorType::Reclaim] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorreclamate");
        cursors[*CursorType::Patrol] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorpatrol");
        cursors[*CursorType::Capture] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorcapture");
        cursors[*CursorType::Load] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorload");
        cursors[*CursorType::Unload] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorunload");
        cursors[*CursorType::Red] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorred");
        cursors[*CursorType::Green] = textureService.getGafEntry("anims/CURSORS.GAF", "cursorgrn");
        cursors[*CursorType::PathIcon] = textureService.getGafEntry("anims/CURSORS.GAF", "pathicon");
        CursorService cursor(sdlContext, &timeService, cursors);

        sdlContext->hideCursor();

        SceneManager sceneManager(sdlContext, window.get(), &graphics, &timeService, &imGuiContext, &cursor, &globalConfig, UiRenderService(&graphics, &shaders, &viewport), &viewport);

        LOG_INFO << "Loading side data";
        auto sideDataBytes = vfs.readFile("gamedata/SIDEDATA.TDF");
        if (!sideDataBytes)
        {
            throw std::runtime_error("Missing side data");
        }
        std::string sideDataString(sideDataBytes->data(), sideDataBytes->size());
        std::unordered_map<std::string, SideData> sideDataMap;
        {
            auto sideData = parseSidesFromSideData(parseTdfFromString(sideDataString));
            for (auto& side : sideData)
            {
                std::string name = side.name;
                sideDataMap.insert({std::move(name), std::move(side)});
            }
        }

        SceneContext sceneContext(
            sdlContext,
            &viewport,
            &graphics,
            &textureService,
            &audioService,
            &cursor,
            &shaders,
            &vfs,
            &*palette,
            &*guiPalette,
            &sceneManager,
            &sideDataMap,
            &timeService,
            &pathMapping,
            &globalConfig);

        if (gameParameters)
        {
            LOG_INFO << "Launching into game on map: " << gameParameters->mapName;
            auto scene = std::make_unique<LoadingScene>(
                sceneContext,
                &allSoundTdf,
                AudioService::LoopToken(),
                *gameParameters);
            sceneManager.setNextScene(std::shared_ptr<Scene>(std::move(scene)));
        }
        else
        {
            LOG_INFO << "Launching into the main menu";

            // The startup logo. 0x4271FE is what separates the films: 1.zrb
            // is the one the original plays on the way to the menu, before
            // anything else is on screen, where 2.zrb -- the intro -- is what
            // the menu's own button plays. Any key or click skips it, which
            // MovieScene already does for every film.
            //
            // Only on the way to the menu. Launching straight into a game
            // (the --map path every harness takes) never reaches this branch,
            // and neither does quitting a game back to the menu, which builds
            // its MainMenuScene itself.
            auto logoBytes = vfs.readFile("movies/1.zrb");
            if (logoBytes)
            {
                auto context = sceneContext;
                auto* soundTdf = &allSoundTdf;
                auto* viewportPtr = &viewport;
                auto logo = std::make_shared<MovieScene>(
                    sceneContext,
                    std::move(*logoBytes),
                    [context, soundTdf, viewportPtr]() {
                        auto menu = std::make_shared<MainMenuScene>(
                            context,
                            soundTdf,
                            viewportPtr->width(),
                            viewportPtr->height());
                        context.sceneManager->setNextScene(menu);
                    });
                sceneManager.setNextScene(logo);
            }
            else
            {
                auto scene = std::make_unique<MainMenuScene>(
                    sceneContext,
                    &allSoundTdf,
                    viewport.width(),
                    viewport.height());
                sceneManager.setNextScene(std::shared_ptr<Scene>(std::move(scene)));
            }
        }

        LOG_INFO << "Entering main loop";
        // An arena run is headless by definition: nobody is watching, and
        // waiting for a display to refresh would make a batch of twenty games
        // take all afternoon.
        if (gameParameters && gameParameters->aiArenaSeconds)
        {
            sceneManager.setHeadless(true);
        }

        sceneManager.execute();

        LOG_INFO << "Finished main loop, exiting";

        return 0;
    }

    PlayerControllerType parseControllerFromString(const std::string& controllerString)
    {
        auto components = utf8Split(controllerString, ',');
        if (components.empty())
        {
            throw std::runtime_error("controller string was empty?");
        }

        if (components[0] == "Human")
        {
            return PlayerControllerTypeHuman();
        }

        if (components[0] == "Computer")
        {
            return PlayerControllerTypeComputer();
        }

        if (components[0] == "Network")
        {
            if (components.size() != 2)
            {
                throw std::runtime_error("Invalid network player string format");
            }
            auto hostAndPort = getHostAndPort(components[1]);
            if (!hostAndPort)
            {
                throw std::runtime_error("Invalid network player address format");
            }
            return PlayerControllerTypeNetwork{hostAndPort->first, hostAndPort->second};
        }

        throw std::runtime_error("Unknown controller string");
    }

    std::string parseSideFromString(const std::string& side)
    {
        if (side == "ARM" || side == "CORE")
        {
            return side;
        }

        throw std::runtime_error("Unknown side string");
    }

    PlayerColorIndex parseColorFromString(const std::string& colorString)
    {
        std::stringstream s(colorString);
        unsigned int i;
        s >> i;
        if (s.fail())
        {
            throw std::runtime_error("Invalid player colour string");
        }
        if (i > 9)
        {
            throw std::runtime_error("Invalid player colour string");
        }
        return PlayerColorIndex(i);
    }

    std::optional<PlayerInfo> parsePlayerInfoFromArg(const std::string& playerString)
    {
        if (playerString == "empty")
        {
            return std::nullopt;
        }

        auto components = rwe::utf8Split(playerString, ';');
        if (components.size() != 4)
        {
            throw std::runtime_error("Invalid player arg");
        }

        auto name = components[0];
        auto controller = parseControllerFromString(components[1]);
        auto side = parseSideFromString(components[2]);
        auto color = parseColorFromString(components[3]);

        return PlayerInfo{name, controller, side, color, Metal(1000), Energy(1000)};
    }
}