#pragma once

#include <functional>
#include <memory>
#include <rwe/SceneContext.h>
#include <rwe/UiRenderService.h>
#include <rwe/io/smk/SmkDecoder.h>
#include <rwe/render/Sprite.h>
#include <rwe/scene/Scene.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * Plays a Smacker movie over a black screen, placed as the original places it, and hands
     * control back when it ends or the player presses anything. The audio is
     * decoded in one pass up front and plays as a single stream; the video
     * decodes as it goes, paced against the wall clock.
     */
    class MovieScene : public Scene
    {
    private:
        SceneContext sceneContext;
        UiRenderService uiRenderService;
        SmkDecoder decoder;
        std::function<void()> onFinish;

        unsigned long long elapsedMicroseconds{0};
        unsigned long long framesPresented{0};
        std::shared_ptr<Sprite> frameSprite;
        bool finished{false};

    public:
        MovieScene(const SceneContext& sceneContext, std::vector<char>&& movieData, std::function<void()>&& onFinish);

        void init() override;
        void update(int millisecondsElapsed) override;
        void render() override;

        void onKeyDown(const SDL_KeyboardEvent& key) override;
        void onMouseDown(MouseButtonEvent event) override;

    private:
        void uploadFrame();
        void finish();
    };

    /**
     * Where a film is drawn on the 640 by 480 screen (BT-157, 0x47C441): left
     * at 0, top at (480 - the film's height) shifted right by one, destination
     * height the film's own and no stretching requested. A film that the
     * library draws height-doubled (its Y-scale or Y-interlace flag, which is
     * films 1 to 4 at 640 by 240) counts as twice its stored height, so it
     * fills the screen from the top; film 5 (640 by 304, no flag) sits in its
     * own pixels at y = 88 with bars above and below instead of being
     * stretched to 4:3. Whether the library reports the doubled or the stored
     * height for films 1 to 4 is the ruling's medium-confidence part.
     */
    struct MoviePlacement
    {
        int x;
        int y;
        int width;
        int height;
    };

    MoviePlacement moviePlacement(unsigned int filmWidth, unsigned int filmHeight, unsigned int headerFlags);

    /**
     * A film scene over the given file, or nothing if the file is empty or
     * is not a film the decoder can open. The original skips a zero-length
     * film like a missing one; a damaged one is skipped here too, with a line
     * in the log, where the constructor would have thrown out of a click.
     */
    std::shared_ptr<MovieScene> tryMakeMovieScene(const SceneContext& sceneContext, std::vector<char>&& movieData, std::function<void()>&& onFinish);

    /**
     * The films at `vfsPaths` one after another, passing over any that is
     * missing as the original's player does (0x4267B3), and then the scene
     * `then` makes.
     */
    std::shared_ptr<Scene> filmsThen(const SceneContext& sceneContext, std::vector<std::string> vfsPaths, std::function<std::shared_ptr<Scene>()> then);
}
