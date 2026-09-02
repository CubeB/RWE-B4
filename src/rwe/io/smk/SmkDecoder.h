#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rwe
{
    /**
     * A decoder for Smacker version 2 video, the format Total Annihilation's
     * movies are in. The GOG release ships them as Data/<n>.ZRB -- the
     * extension is a disguise, the files begin with the ordinary "SMK2"
     * magic. Decoding was checked frame-for-frame and sample-for-sample
     * against FFmpeg's decoder over the shipped movies.
     *
     * The decoder is sequential: call decodeNextFrame() repeatedly and read
     * the video indices, the palette, and the frame's slice of audio after
     * each call. Frames depend on their predecessors, so there is no seeking,
     * only rewind().
     */
    class SmkDecoder
    {
    public:
        struct AudioInfo
        {
            unsigned int sampleRate;
            bool is16Bit;
            bool stereo;
        };

    private:
        /** A Huffman tree flattened into an array of nodes. */
        struct HuffTree
        {
            struct Node
            {
                // Leaf when left == -1.
                int left{-1};
                int right{-1};
                uint16_t value{0};
                // For the 16-bit trees: >= 0 marks a leaf that reads the
                // moving cache slot instead of holding a literal.
                int cacheSlot{-1};
            };
            std::vector<Node> nodes;
            int root{-1};
            std::array<uint16_t, 3> cache{{0, 0, 0}};

            bool empty() const { return root == -1; }
            void resetCache()
            {
                cache = {0, 0, 0};
            }
        };

        struct BitReader
        {
            const uint8_t* data{nullptr};
            std::size_t sizeBits{0};
            std::size_t pos{0};

            BitReader() = default;
            BitReader(const uint8_t* data, std::size_t sizeBytes) : data(data), sizeBits(sizeBytes * 8) {}

            bool readBit();
            unsigned int readBits(unsigned int count);
            bool exhausted() const { return pos >= sizeBits; }
        };

    public:
        explicit SmkDecoder(std::vector<char>&& fileData);

        unsigned int getWidth() const { return width; }
        unsigned int getHeight() const { return height; }
        unsigned int getFrameCount() const { return frameCount; }
        /** Duration of one frame, in microseconds. */
        unsigned int getMicrosecondsPerFrame() const { return microsecondsPerFrame; }
        std::optional<AudioInfo> getAudioInfo() const { return audioInfo; }

        /**
         * Decodes the next frame in sequence. Returns false when the movie is
         * over. After a successful call the palette, the video indices and
         * the frame's decoded audio samples are current.
         */
        bool decodeNextFrame();

        /** One palette index per pixel, row-major, width*height of them. */
        const std::vector<uint8_t>& getVideoIndices() const { return videoIndices; }

        /** The current palette as RGB byte triples. */
        const std::array<std::array<uint8_t, 3>, 256>& getPalette() const { return palette; }

        /**
         * The audio decoded from the frame just decoded: interleaved signed
         * 16-bit samples (8-bit source samples are widened on the way out).
         */
        const std::vector<int16_t>& getFrameAudio() const { return frameAudio; }

        void rewind();

        /**
         * When set, frames decode palette and audio but not video -- the
         * audio chunks are independent of the video stream, so a fast
         * audio-only pass over the file is legal. Rewind before turning
         * video back on: the video stream is differential.
         */
        void setSkipVideo(bool skip) { skipVideo = skip; }

    private:
        std::vector<char> fileData;

        unsigned int width{0};
        unsigned int height{0};
        unsigned int frameCount{0};
        unsigned int microsecondsPerFrame{0};
        std::optional<AudioInfo> audioInfo;
        uint32_t audioTrackFlags[7] = {};

        std::vector<uint32_t> frameSizes;
        std::vector<uint8_t> frameTypes;
        std::size_t firstFrameOffset{0};

        HuffTree mmapTree;
        HuffTree mclrTree;
        HuffTree fullTree;
        HuffTree typeTree;

        unsigned int currentFrame{0};
        std::size_t currentOffset{0};
        bool skipVideo{false};

        std::array<std::array<uint8_t, 3>, 256> palette{};
        std::vector<uint8_t> videoIndices;
        std::vector<int16_t> frameAudio;

        void parseHeader();
        HuffTree buildTree8(BitReader& reader);
        int buildTree8Recursive(BitReader& reader, HuffTree& tree);
        HuffTree buildTree16(BitReader& reader);
        int buildTree16Recursive(BitReader& reader, HuffTree& tree, HuffTree& low, HuffTree& high);
        static uint16_t decodeTree(BitReader& reader, HuffTree& tree);

        void decodePalette(const uint8_t* data, std::size_t size);
        void decodeAudioChunk(const uint8_t* data, std::size_t size, unsigned int track);
        void decodeVideo(const uint8_t* data, std::size_t size);
    };
}
