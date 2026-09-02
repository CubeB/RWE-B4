#include "MovieScene.h"

#include <cstring>

namespace rwe
{
    namespace
    {
        void appendU32(std::vector<char>& v, uint32_t value)
        {
            v.push_back(static_cast<char>(value & 0xFF));
            v.push_back(static_cast<char>((value >> 8) & 0xFF));
            v.push_back(static_cast<char>((value >> 16) & 0xFF));
            v.push_back(static_cast<char>((value >> 24) & 0xFF));
        }

        void appendU16(std::vector<char>& v, uint16_t value)
        {
            v.push_back(static_cast<char>(value & 0xFF));
            v.push_back(static_cast<char>((value >> 8) & 0xFF));
        }

        /** Wraps interleaved signed 16-bit samples as a WAV in memory. */
        std::vector<char> buildWav(const std::vector<int16_t>& samples, unsigned int sampleRate, unsigned int channels)
        {
            auto dataSize = static_cast<uint32_t>(samples.size() * 2);
            std::vector<char> wav;
            wav.reserve(44 + dataSize);
            wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
            appendU32(wav, 36 + dataSize);
            wav.insert(wav.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
            appendU32(wav, 16);
            appendU16(wav, 1); // PCM
            appendU16(wav, static_cast<uint16_t>(channels));
            appendU32(wav, sampleRate);
            appendU32(wav, sampleRate * channels * 2);
            appendU16(wav, static_cast<uint16_t>(channels * 2));
            appendU16(wav, 16);
            wav.insert(wav.end(), {'d', 'a', 't', 'a'});
            appendU32(wav, dataSize);
            auto offset = wav.size();
            wav.resize(offset + dataSize);
            std::memcpy(wav.data() + offset, samples.data(), dataSize);
            return wav;
        }
    }

    MovieScene::MovieScene(const SceneContext& sceneContext, std::vector<char>&& movieData, std::function<void()>&& onFinish)
        : sceneContext(sceneContext),
          uiRenderService(sceneContext.graphics, sceneContext.shaders, sceneContext.viewport),
          decoder(std::move(movieData)),
          onFinish(std::move(onFinish))
    {
    }

    void MovieScene::init()
    {
        // First pass: pull all the audio out. The audio chunks sit before the
        // video data in every frame and decode independently of it, so the
        // pass can skip the expensive half entirely.
        if (auto audioInfo = decoder.getAudioInfo())
        {
            std::vector<int16_t> samples;
            decoder.setSkipVideo(true);
            while (decoder.decodeNextFrame())
            {
                const auto& frameAudio = decoder.getFrameAudio();
                samples.insert(samples.end(), frameAudio.begin(), frameAudio.end());
            }
            decoder.rewind();
            decoder.setSkipVideo(false);

            if (!samples.empty())
            {
                sceneContext.audioService->playMusicFromMemory(
                    buildWav(samples, audioInfo->sampleRate, audioInfo->stereo ? 2 : 1), false);
            }
        }

        if (decoder.decodeNextFrame())
        {
            framesPresented = 1;
            uploadFrame();
        }
        else
        {
            finish();
        }
    }

    void MovieScene::update(int millisecondsElapsed)
    {
        if (finished)
        {
            return;
        }

        elapsedMicroseconds += static_cast<unsigned long long>(millisecondsElapsed) * 1000ull;
        auto targetFrames = (elapsedMicroseconds / decoder.getMicrosecondsPerFrame()) + 1;

        bool decodedAny = false;
        while (framesPresented < targetFrames)
        {
            if (!decoder.decodeNextFrame())
            {
                finish();
                return;
            }
            ++framesPresented;
            decodedAny = true;
        }
        if (decodedAny)
        {
            uploadFrame();
        }
    }

    void MovieScene::uploadFrame()
    {
        const auto& indices = decoder.getVideoIndices();
        const auto& palette = decoder.getPalette();

        std::vector<Color> pixels;
        pixels.reserve(indices.size());
        for (auto index : indices)
        {
            const auto& entry = palette[index];
            pixels.emplace_back(entry[0], entry[1], entry[2]);
        }

        SharedTextureHandle texture(sceneContext.graphics->createTexture(decoder.getWidth(), decoder.getHeight(), pixels));
        frameSprite = std::make_shared<Sprite>(sceneContext.graphics->createSprite(
            Rectangle2f::fromTopLeft(0.0f, 0.0f, static_cast<float>(decoder.getWidth()), static_cast<float>(decoder.getHeight())),
            Rectangle2f::fromTopLeft(0.0f, 0.0f, 1.0f, 1.0f),
            texture));
    }

    void MovieScene::render()
    {
        uiRenderService.fillScreen(Color(0, 0, 0));

        if (!frameSprite)
        {
            return;
        }

        // The movies are stored anamorphic -- 640x240 shown on a 640x480
        // screen -- so the frame is presented as the largest 4:3 rectangle
        // the window will hold, whatever its own pixel shape.
        auto viewWidth = static_cast<float>(sceneContext.viewport->width());
        auto viewHeight = static_cast<float>(sceneContext.viewport->height());

        auto width = viewWidth;
        auto height = width * 3.0f / 4.0f;
        if (height > viewHeight)
        {
            height = viewHeight;
            width = height * 4.0f / 3.0f;
        }

        auto x = std::floor((viewWidth - width) / 2.0f);
        auto y = std::floor((viewHeight - height) / 2.0f);
        uiRenderService.drawSpriteAbs(x, y, width, height, *frameSprite);
    }

    void MovieScene::onKeyDown(const SDL_KeyboardEvent& /*key*/)
    {
        finish();
    }

    void MovieScene::onMouseDown(MouseButtonEvent /*event*/)
    {
        finish();
    }

    void MovieScene::finish()
    {
        if (finished)
        {
            return;
        }
        finished = true;
        sceneContext.audioService->stopMusic();
        onFinish();
    }
}
