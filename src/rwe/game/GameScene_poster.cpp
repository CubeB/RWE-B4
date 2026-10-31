#include "GameScene.h"

#include <algorithm>
#include <cmath>
#include <glad/gl.h>
#include <rwe/CroppedViewport.h>
#include <rwe/Viewport.h>
#include <rwe/scene/PosterCapture.h>
#include <rwe/util.h>
#include <rwe/util/SimpleLogger.h>

// +MakePoster (#915). BT-GAME-CONSOLE-COMMANDS-PARITY.md section 6 records the
// original's behaviour: default 3200 by 2400, `all` the map's pixel size, each
// side raised to the visible tile span times sixteen and lowered to the map's,
// centred on the view and clamped, a screenshots directory created, a
// `BIGSHOT` bitmap written, and the pacing timestamp reset afterwards.
//
// RWE draws in true colour and has no 8-bit screen to copy, so the picture is
// assembled a tile at a time through the ordinary world renderer. One output
// pixel is one map pixel (one world unit): the map's pixel size is a world
// dimension, and the finding describes the poster in those terms. The camera's
// zoom is therefore not folded into the poster size. This is a documented
// RWE-original choice where the prose does not name a scale.
//
// Nothing here is simulation state. The capture reads the camera, terrain,
// units and features, renders into its own framebuffer, and restores every
// piece of renderer state it touched. No command is queued, no RNG is drawn
// from and no hashed field is written.

namespace rwe
{
    namespace
    {
        struct SavedGlState
        {
            GLint drawFrameBuffer{0};
            GLint readFrameBuffer{0};
            GLint viewport[4]{0, 0, 0, 0};
            GLint packAlignment{4};
            GLint readBuffer{0};
            GLboolean depthTest{GL_FALSE};
            GLboolean depthWrite{GL_TRUE};
            GLboolean blend{GL_FALSE};
            GLboolean cull{GL_FALSE};
            GLboolean scissor{GL_FALSE};
        };

        SavedGlState saveGlState()
        {
            SavedGlState state;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFrameBuffer);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.readFrameBuffer);
            glGetIntegerv(GL_VIEWPORT, state.viewport);
            glGetIntegerv(GL_PACK_ALIGNMENT, &state.packAlignment);
            glGetIntegerv(GL_READ_BUFFER, &state.readBuffer);
            state.depthTest = glIsEnabled(GL_DEPTH_TEST);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWrite);
            state.blend = glIsEnabled(GL_BLEND);
            state.cull = glIsEnabled(GL_CULL_FACE);
            state.scissor = glIsEnabled(GL_SCISSOR_TEST);
            return state;
        }

        void restoreGlState(const SavedGlState& state)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(state.drawFrameBuffer));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(state.readFrameBuffer));
            glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
            glPixelStorei(GL_PACK_ALIGNMENT, state.packAlignment);
            glReadBuffer(static_cast<GLenum>(state.readBuffer));

            if (state.depthTest != GL_FALSE)
            {
                glEnable(GL_DEPTH_TEST);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(state.depthWrite);

            if (state.blend != GL_FALSE)
            {
                glEnable(GL_BLEND);
            }
            else
            {
                glDisable(GL_BLEND);
            }

            if (state.cull != GL_FALSE)
            {
                glEnable(GL_CULL_FACE);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }

            if (state.scissor != GL_FALSE)
            {
                glEnable(GL_SCISSOR_TEST);
            }
            else
            {
                glDisable(GL_SCISSOR_TEST);
            }
        }
    }

    void GameScene::capturePoster(bool all, unsigned int requestedWidth, unsigned int requestedHeight)
    {
        const Color feedbackColor(252, 252, 0);
        const Color successColor(83, 223, 79);

        auto mapWidth = static_cast<unsigned int>(std::lround(simScalarToFloat(simulation.terrain.getWidthInWorldUnits())));
        auto mapHeight = static_cast<unsigned int>(std::lround(simScalarToFloat(simulation.terrain.getHeightInWorldUnits())));
        if (mapWidth == 0 || mapHeight == 0)
        {
            printConsole("Poster: the map has no pixel size.", feedbackColor);
            return;
        }

        auto mapLeft = simScalarToFloat(simulation.terrain.leftInWorldUnits());
        auto mapTop = simScalarToFloat(simulation.terrain.topInWorldUnits());
        auto mapRight = mapLeft + simScalarToFloat(simulation.terrain.getWidthInWorldUnits());
        auto mapBottom = mapTop + simScalarToFloat(simulation.terrain.getHeightInWorldUnits());

        // The lower bound is the world span the view covers, rounded up to
        // whole tiles. It is asked before the camera is moved for the capture.
        auto minimumWidth = posterMinimumDimension(worldCameraState.scaleDimension(static_cast<float>(worldViewport.width())));
        auto minimumHeight = posterMinimumDimension(worldCameraState.scaleDimension(static_cast<float>(worldViewport.height())));

        // No argument at all is the original's 3200 by 2400. The parser
        // leaves both sides zero in that case; an explicit size is always
        // positive.
        if (!all && (requestedWidth == 0 || requestedHeight == 0))
        {
            requestedWidth = 3200;
            requestedHeight = 2400;
        }

        PosterSize requested = all
            ? PosterSize{mapWidth, mapHeight}
            : PosterSize{requestedWidth, requestedHeight};
        auto size = clampPosterSize(requested.width, requested.height, mapWidth, mapHeight, minimumWidth, minimumHeight);
        if (!posterFitsLimits(size))
        {
            printConsole(
                "Poster: " + std::to_string(size.width) + "x" + std::to_string(size.height)
                    + " is too large; the limit is " + std::to_string(MaxPosterDimension) + " a side and "
                    + std::to_string(MaxPosterPixels) + " pixels.",
                feedbackColor);
            return;
        }

        auto left = clampPosterAxis(worldCameraState.position.x, size.width, mapLeft, mapRight);
        auto top = clampPosterAxis(worldCameraState.position.z, size.height, mapTop, mapBottom);

        auto localDataPath = getLocalDataPath();
        if (!localDataPath)
        {
            printConsole("Poster: no local data directory to write into.", feedbackColor);
            return;
        }
        auto path = *localDataPath / "screenshots" / posterFileName();

        // Whole tiles, and even so that a tile's centre stays an integer
        // world coordinate: the camera rounds its position before projecting,
        // and invisible half-pixel seams between tiles are exactly what that
        // would otherwise produce.
        auto maxSide = std::max(size.width, size.height);
        auto tile = std::min(PosterTileSize, std::max(2u, maxSide));
        if ((tile % 2) != 0)
        {
            ++tile;
        }

        PosterPcxWriter writer(path, size.width, size.height);
        if (!writer.isOpen())
        {
            printConsole("Poster: could not create " + path.string(), feedbackColor);
            return;
        }

        auto savedViewport = worldViewport;
        auto savedCamera = worldCameraState;
        auto glState = saveGlState();

        Viewport captureViewport(0, 0, tile, tile);
        auto captureCamera = savedCamera;
        captureCamera.zoom = 1.0f;
        captureCamera.density = 1.0f;

        std::string failure;
        bool wroteEveryRow = true;

        try
        {
            auto captureTarget = sceneContext.graphics->createFrameBuffer(static_cast<int>(tile), static_cast<int>(tile));
            std::vector<std::uint8_t> tilePixels(static_cast<std::size_t>(tile) * tile * 3);
            std::vector<std::uint8_t> strip(static_cast<std::size_t>(size.width) * tile * 3);

            for (unsigned int tileY = 0; tileY < size.height; tileY += tile)
            {
                auto rows = std::min(tile, size.height - tileY);

                for (unsigned int tileX = 0; tileX < size.width; tileX += tile)
                {
                    auto columns = std::min(tile, size.width - tileX);

                    // The tile's whole square is rendered even at the poster's
                    // right and bottom edges; only the part that falls inside
                    // the poster is copied out.
                    captureCamera.position = Vector3f(
                        left + static_cast<float>(tileX) + (static_cast<float>(tile) / 2.0f),
                        savedCamera.position.y,
                        top + static_cast<float>(tileY) + (static_cast<float>(tile) / 2.0f));
                    worldCameraState = captureCamera;
                    worldViewport = CroppedViewport(&captureViewport, 0, 0, 0, 0);
                    posterCaptureActive = true;
                    posterCaptureTarget = captureTarget.frameBuffer.get();

                    renderWorld();

                    glBindFramebuffer(GL_READ_FRAMEBUFFER, captureTarget.frameBuffer.get().value);
                    glReadBuffer(GL_COLOR_ATTACHMENT0);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(
                        0, 0, static_cast<GLsizei>(tile), static_cast<GLsizei>(tile),
                        GL_RGB, GL_UNSIGNED_BYTE, tilePixels.data());

                    // GL's first row is the bottom one; the poster's first row
                    // is the top.
                    for (unsigned int y = 0; y < rows; ++y)
                    {
                        const auto* source = tilePixels.data() + (static_cast<std::size_t>(tile - 1 - y) * tile * 3);
                        auto* destination = strip.data() + ((static_cast<std::size_t>(y) * size.width) + tileX) * 3;
                        std::copy_n(source, static_cast<std::size_t>(columns) * 3, destination);
                    }
                }

                for (unsigned int y = 0; y < rows; ++y)
                {
                    if (!writer.writeRow(strip.data() + (static_cast<std::size_t>(y) * size.width * 3)))
                    {
                        wroteEveryRow = false;
                        failure = "the file stopped accepting rows";
                        break;
                    }
                }
                if (!wroteEveryRow)
                {
                    break;
                }
            }
        }
        catch (const std::exception& e)
        {
            failure = e.what();
        }

        // Put the ordinary renderer and camera back before anything else can
        // look at them. The world textures were made at tile size for the
        // capture and have to follow the window's viewport again.
        posterCaptureActive = false;
        posterCaptureTarget = FrameBufferIdentifier(0);
        worldViewport = savedViewport;
        worldCameraState = savedCamera;
        try
        {
            recreateWorldRenderTextures();
        }
        catch (const std::exception& e)
        {
            failure = failure.empty() ? e.what() : failure;
        }
        restoreGlState(glState);

        // The capture's delay is not a frame of game time. The original resets
        // its pacing timestamp for the same reason
        // (BT-GAME-CONSOLE-COMMANDS-PARITY section 6).
        if (sceneContext.sceneManager != nullptr)
        {
            sceneContext.sceneManager->requestFramePacingReset();
        }

        if (failure.empty() && wroteEveryRow && writer.finish())
        {
            LOG_INFO << "Poster saved to " << path.string() << " (" << size.width << "x" << size.height << ")";
            printConsole(
                "Poster: " + std::to_string(size.width) + "x" + std::to_string(size.height)
                    + " saved to " + path.string(),
                successColor);
            return;
        }

        writer.removePartial();
        LOG_ERROR << "Poster could not be written to " << path.string() << ": " << failure;
        printConsole(
            "Poster: could not write " + path.string() + (failure.empty() ? std::string() : (": " + failure)),
            feedbackColor);
    }
}
