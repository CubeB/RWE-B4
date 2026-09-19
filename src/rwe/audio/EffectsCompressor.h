#pragma once

namespace rwe
{
    /** Settings for EffectsCompressor. All levels are linear amplitude, not decibels. */
    struct EffectsCompressorSettings
    {
        /** Level above which gain reduction starts. */
        float threshold{0.5f};
        /** Input-over-threshold to output-over-threshold. 4 means 4:1. Values below 1 are treated as 1 (no compression). */
        float ratio{4.0f};
        /** Hard ceiling applied after compression; no output sample's magnitude may exceed it. */
        float ceiling{0.98f};
        /** Time for the envelope to rise most of the way to a louder level, in milliseconds. */
        float attackMs{2.0f};
        /** Time for the envelope to fall back, in milliseconds. */
        float releaseMs{150.0f};
    };

    /**
     * Downward compressor for the mixed float PCM of an effects channel,
     * so that many simultaneous weapon and explosion sounds summing above
     * the nominal -1..1 range are brought back down smoothly instead of
     * clipping into audible crackle. Runs on the audio thread: process()
     * must not throw, allocate or lock.
     */
    class EffectsCompressor
    {
    public:
        explicit EffectsCompressor(const EffectsCompressorSettings& settings = EffectsCompressorSettings());

        /**
         * Compresses interleaved float PCM in place.
         * `sampleCount` is the total number of floats (frames x channels).
         * `channels` must be at least 1; `sampleRate` in Hz must be positive.
         * If either is not, or pcm is null, or sampleCount <= 0, the call does nothing.
         */
        void process(float* pcm, int sampleCount, int channels, int sampleRate);

        /** The gain applied to the most recent frame, 1.0 when nothing is being reduced. For tests and for a debug readout. */
        float currentGain() const;

        /** Forgets the envelope, as if no sound had ever played. */
        void reset();

    private:
        EffectsCompressorSettings settings;
        float envelope{0.0f};
        float lastGain{1.0f};
        int coefficientSampleRate{0};
        float attackCoefficient{0.0f};
        float releaseCoefficient{0.0f};
    };

    /**
     * The static gain curve on its own, as a free function so it can be tested without a buffer:
     * the gain (0..1] a steady input of magnitude `level` should be given.
     * At or below threshold: 1. Above: the output level is threshold + (level - threshold) / ratio,
     * so the gain is that divided by level. ratio below 1 is treated as 1. level <= 0 returns 1.
     */
    float compressorGainForLevel(float level, float threshold, float ratio);
}
