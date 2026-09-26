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
     * Plays a Smacker movie over a black screen, scaled to fit, and hands
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
     * The films at `vfsPaths` one after another, passing over any that is
     * missing as the original's player does (0x4267B3), and then the scene
     * `then` makes.
     */
    std::shared_ptr<Scene> filmsThen(const SceneContext& sceneContext, std::vector<std::string> vfsPaths, std::function<std::shared_ptr<Scene>()> then);
}
