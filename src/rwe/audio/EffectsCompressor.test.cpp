#include "EffectsCompressor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <vector>

namespace rwe
{
    namespace
    {
        constexpr int SampleRate = 44100;

        std::vector<float> constantMono(float value, int count)
        {
            return std::vector<float>(static_cast<std::size_t>(count), value);
        }
    }

    TEST_CASE("compressorGainForLevel leaves quiet input alone", "[audio]")
    {
        REQUIRE(compressorGainForLevel(0.0f, 0.5f, 4.0f) == 1.0f);
        REQUIRE(compressorGainForLevel(0.1f, 0.5f, 4.0f) == 1.0f);
        REQUIRE(compressorGainForLevel(0.5f, 0.5f, 4.0f) == 1.0f);
    }

    TEST_CASE("compressorGainForLevel follows the ratio above threshold", "[audio]")
    {
        REQUIRE_THAT(compressorGainForLevel(1.0f, 0.5f, 4.0f), Catch::Matchers::WithinAbs(0.625f, 1e-6));
        REQUIRE_THAT(compressorGainForLevel(2.0f, 0.5f, 4.0f), Catch::Matchers::WithinAbs(0.4375f, 1e-6));
        REQUIRE_THAT(compressorGainForLevel(2.0f, 0.5f, 0.5f), Catch::Matchers::WithinAbs(1.0f, 1e-6));
    }

    TEST_CASE("a quiet signal passes through untouched", "[audio]")
    {
        EffectsCompressor compressor;
        auto buffer = constantMono(0.25f, 4410);

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 1, SampleRate);

        for (auto sample : buffer)
        {
            REQUIRE(sample == 0.25f);
        }
        REQUIRE(compressor.currentGain() == 1.0f);
    }

    TEST_CASE("a loud steady signal settles at the curve's gain", "[audio]")
    {
        EffectsCompressor compressor;
        auto buffer = constantMono(1.0f, 44100);

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 1, SampleRate);

        REQUIRE_THAT(buffer.back(), Catch::Matchers::WithinAbs(0.625f, 1e-3));
        REQUIRE_THAT(compressor.currentGain(), Catch::Matchers::WithinAbs(0.625f, 1e-3));
    }

    TEST_CASE("no output sample exceeds the ceiling, even on the first sample of a transient", "[audio]")
    {
        EffectsCompressor compressor;
        auto buffer = constantMono(4.0f, 4410);

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 1, SampleRate);

        for (auto sample : buffer)
        {
            REQUIRE(std::fabs(sample) <= 0.98f + 1e-6f);
        }
    }

    TEST_CASE("both channels of a frame get the same gain", "[audio]")
    {
        EffectsCompressor compressor;
        const int frameCount = 44100;
        std::vector<float> buffer(static_cast<std::size_t>(frameCount) * 2);
        for (int i = 0; i < frameCount; ++i)
        {
            buffer[static_cast<std::size_t>(i) * 2] = 1.0f;
            buffer[static_cast<std::size_t>(i) * 2 + 1] = 0.5f;
        }

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 2, SampleRate);

        auto left = buffer[buffer.size() - 2];
        auto right = buffer[buffer.size() - 1];
        REQUIRE_THAT(right / left, Catch::Matchers::WithinAbs(0.5f, 1e-5));
    }

    TEST_CASE("the gain recovers after the loud part ends", "[audio]")
    {
        EffectsCompressor compressor;
        auto loud = constantMono(1.0f, 22050);
        compressor.process(loud.data(), static_cast<int>(loud.size()), 1, SampleRate);

        auto quiet = constantMono(0.1f, 44100);
        compressor.process(quiet.data(), static_cast<int>(quiet.size()), 1, SampleRate);

        REQUIRE_THAT(compressor.currentGain(), Catch::Matchers::WithinAbs(1.0f, 1e-3));
    }

    TEST_CASE("reset forgets the envelope", "[audio]")
    {
        EffectsCompressor compressor;
        auto loud = constantMono(1.0f, 4410);
        compressor.process(loud.data(), static_cast<int>(loud.size()), 1, SampleRate);
        REQUIRE(compressor.currentGain() < 0.9f);

        compressor.reset();
        REQUIRE(compressor.currentGain() == 1.0f);

        auto quiet = constantMono(0.1f, 1);
        compressor.process(quiet.data(), static_cast<int>(quiet.size()), 1, SampleRate);
        REQUIRE(quiet[0] == 0.1f);
    }

    TEST_CASE("bad arguments do nothing", "[audio]")
    {
        EffectsCompressor compressor;

        compressor.process(nullptr, 4, 1, SampleRate);

        auto buffer = constantMono(0.3f, 4);

        compressor.process(buffer.data(), 0, 1, SampleRate);
        REQUIRE(buffer == constantMono(0.3f, 4));

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 0, SampleRate);
        REQUIRE(buffer == constantMono(0.3f, 4));

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 1, 0);
        REQUIRE(buffer == constantMono(0.3f, 4));
    }

    TEST_CASE("a trailing partial frame is left alone", "[audio]")
    {
        EffectsCompressor compressor;
        std::vector<float> buffer(5, 4.0f);

        compressor.process(buffer.data(), static_cast<int>(buffer.size()), 2, SampleRate);

        for (int i = 0; i < 4; ++i)
        {
            REQUIRE(std::fabs(buffer[static_cast<std::size_t>(i)]) <= 0.98f + 1e-6f);
        }
        REQUIRE(buffer[4] == 4.0f);
    }
}
