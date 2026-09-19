#include "EffectsCompressor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace rwe
{
    float compressorGainForLevel(float level, float threshold, float ratio)
    {
        if (level <= 0.0f)
        {
            return 1.0f;
        }

        if (level <= threshold)
        {
            return 1.0f;
        }

        auto effectiveRatio = ratio < 1.0f ? 1.0f : ratio;

        auto outputLevel = threshold + (level - threshold) / effectiveRatio;

        return outputLevel / level;
    }

    EffectsCompressor::EffectsCompressor(const EffectsCompressorSettings& settings) : settings(settings)
    {
    }

    void EffectsCompressor::process(float* pcm, int sampleCount, int channels, int sampleRate)
    {
        if (pcm == nullptr || sampleCount <= 0 || channels < 1 || sampleRate <= 0)
        {
            return;
        }

        // The coefficients only depend on the sample rate and the (fixed for
        // the object's lifetime) attack/release settings, so they are cached
        // and only recomputed when the rate actually changes -- this runs on
        // the audio thread and exp() is not free.
        if (sampleRate != coefficientSampleRate)
        {
            auto attackSeconds = std::max(settings.attackMs, 0.01f) * 0.001f;
            auto releaseSeconds = std::max(settings.releaseMs, 0.01f) * 0.001f;

            attackCoefficient = std::exp(-1.0f / (attackSeconds * static_cast<float>(sampleRate)));
            releaseCoefficient = std::exp(-1.0f / (releaseSeconds * static_cast<float>(sampleRate)));

            coefficientSampleRate = sampleRate;
        }

        auto frameCount = sampleCount / channels;

        for (int frame = 0; frame < frameCount; ++frame)
        {
            auto frameStart = pcm + (static_cast<std::size_t>(frame) * channels);

            auto peak = 0.0f;
            for (int c = 0; c < channels; ++c)
            {
                peak = std::max(peak, std::fabs(frameStart[c]));
            }

            // An asymmetric one-pole follower: it climbs on a transient at the
            // attack rate and falls back at the (normally much slower) release
            // rate, so a single loud burst does not immediately hand the level
            // back once it passes.
            if (peak > envelope)
            {
                envelope = attackCoefficient * envelope + (1.0f - attackCoefficient) * peak;
            }
            else
            {
                envelope = releaseCoefficient * envelope + (1.0f - releaseCoefficient) * peak;
            }

            auto gain = compressorGainForLevel(envelope, settings.threshold, settings.ratio);

            for (int c = 0; c < channels; ++c)
            {
                frameStart[c] *= gain;
            }

            // The envelope follower lags behind a sudden transient by design,
            // so for the first few milliseconds of one the gain above is not
            // yet enough -- this hard clamp is the safety net for that gap.
            for (int c = 0; c < channels; ++c)
            {
                frameStart[c] = std::clamp(frameStart[c], -settings.ceiling, settings.ceiling);
            }

            lastGain = gain;
        }

        // Guard against the envelope drifting into NaN (should not happen
        // given the inputs above, but this runs indefinitely on live audio)
        // or lingering as a denormal, which is slow on some FPUs.
        if (!std::isfinite(envelope))
        {
            envelope = 0.0f;
        }
        else if (envelope < 1e-9f)
        {
            envelope = 0.0f;
        }
    }

    float EffectsCompressor::currentGain() const
    {
        return lastGain;
    }

    void EffectsCompressor::reset()
    {
        envelope = 0.0f;
        lastGain = 1.0f;
    }
}
