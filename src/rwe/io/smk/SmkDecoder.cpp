#include "SmkDecoder.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace rwe
{
    namespace
    {
        constexpr uint32_t AudioFlagPacked = 0x80000000u;
        constexpr uint32_t AudioFlagPresent = 0x40000000u;
        constexpr uint32_t AudioFlag16Bit = 0x20000000u;
        constexpr uint32_t AudioFlagStereo = 0x10000000u;

        uint32_t readU32(const uint8_t* p)
        {
            return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
        }

        /** Smacker palettes are 6-bit; this is the standard expansion. */
        uint8_t pal6To8(uint8_t v)
        {
            return static_cast<uint8_t>((v << 2) | (v >> 4));
        }

        /** Run lengths for the block-type chains. */
        unsigned int sizeTable(unsigned int index)
        {
            static const unsigned int high[] = {128, 256, 512, 1024, 2048};
            return index < 59 ? index + 1 : high[index - 59];
        }
    }

    bool SmkDecoder::BitReader::readBit()
    {
        if (pos >= sizeBits)
        {
            throw std::runtime_error("SMK bitstream overrun");
        }
        auto bit = (data[pos >> 3] >> (pos & 7)) & 1;
        ++pos;
        return bit != 0;
    }

    unsigned int SmkDecoder::BitReader::readBits(unsigned int count)
    {
        // Least significant bit first, like everything in the format.
        unsigned int value = 0;
        for (unsigned int i = 0; i < count; ++i)
        {
            value |= static_cast<unsigned int>(readBit()) << i;
        }
        return value;
    }

    SmkDecoder::SmkDecoder(std::vector<char>&& data) : fileData(std::move(data))
    {
        parseHeader();
        videoIndices.assign(width * height, 0);
    }

    void SmkDecoder::parseHeader()
    {
        if (fileData.size() < 104)
        {
            throw std::runtime_error("SMK file too small");
        }
        const auto* p = reinterpret_cast<const uint8_t*>(fileData.data());

        if (std::memcmp(p, "SMK2", 4) != 0)
        {
            // SMK4 changes the full-block encoding; nothing in TA uses it.
            throw std::runtime_error("not an SMK2 file");
        }

        width = readU32(p + 4);
        height = readU32(p + 8);
        frameCount = readU32(p + 12);
        auto frameRate = static_cast<int32_t>(readU32(p + 16));
        auto flags = readU32(p + 20);

        // frameRate > 0: milliseconds per frame; < 0: hundredths of a
        // millisecond per frame; 0: ten frames a second.
        if (frameRate > 0)
        {
            microsecondsPerFrame = static_cast<unsigned int>(frameRate) * 1000u;
        }
        else if (frameRate < 0)
        {
            microsecondsPerFrame = static_cast<unsigned int>(-frameRate) * 10u;
        }
        else
        {
            microsecondsPerFrame = 100000u;
        }

        auto treesSize = readU32(p + 52);
        for (int i = 0; i < 7; ++i)
        {
            audioTrackFlags[i] = readU32(p + 72 + (i * 4));
        }
        if (audioTrackFlags[0] & AudioFlagPresent)
        {
            audioInfo = AudioInfo{
                audioTrackFlags[0] & 0x00FFFFFFu,
                (audioTrackFlags[0] & AudioFlag16Bit) != 0,
                (audioTrackFlags[0] & AudioFlagStereo) != 0};
        }

        // A "ring frame" is an extra frame for seamless looping; it has
        // entries in both tables but is not part of the advertised count.
        auto tableFrames = frameCount + ((flags & 1u) ? 1u : 0u);

        std::size_t offset = 104;
        if (fileData.size() < offset + (tableFrames * 5) + treesSize)
        {
            throw std::runtime_error("SMK file truncated");
        }

        frameSizes.reserve(tableFrames);
        for (unsigned int i = 0; i < tableFrames; ++i)
        {
            frameSizes.push_back(readU32(p + offset) & ~3u);
            offset += 4;
        }
        frameTypes.assign(p + offset, p + offset + tableFrames);
        offset += tableFrames;

        BitReader treeReader(p + offset, treesSize);
        mmapTree = buildTree16(treeReader);
        mclrTree = buildTree16(treeReader);
        fullTree = buildTree16(treeReader);
        typeTree = buildTree16(treeReader);
        offset += treesSize;

        firstFrameOffset = offset;
        currentOffset = offset;
    }

    int SmkDecoder::buildTree8Recursive(BitReader& reader, HuffTree& tree)
    {
        auto index = static_cast<int>(tree.nodes.size());
        tree.nodes.emplace_back();
        if (reader.readBit())
        {
            auto left = buildTree8Recursive(reader, tree);
            auto right = buildTree8Recursive(reader, tree);
            tree.nodes[index].left = left;
            tree.nodes[index].right = right;
        }
        else
        {
            tree.nodes[index].value = static_cast<uint16_t>(reader.readBits(8));
        }
        return index;
    }

    SmkDecoder::HuffTree SmkDecoder::buildTree8(BitReader& reader)
    {
        HuffTree tree;
        if (!reader.readBit())
        {
            return tree;
        }
        tree.root = buildTree8Recursive(reader, tree);
        // The tree is closed by a zero bit.
        reader.readBit();
        return tree;
    }

    int SmkDecoder::buildTree16Recursive(BitReader& reader, HuffTree& tree, HuffTree& low, HuffTree& high)
    {
        auto index = static_cast<int>(tree.nodes.size());
        tree.nodes.emplace_back();
        if (reader.readBit())
        {
            auto left = buildTree16Recursive(reader, tree, low, high);
            auto right = buildTree16Recursive(reader, tree, low, high);
            tree.nodes[index].left = left;
            tree.nodes[index].right = right;
        }
        else
        {
            // Leaf values are themselves coded through the two byte trees.
            auto lowByte = decodeTree(reader, low);
            auto highByte = decodeTree(reader, high);
            auto value = static_cast<uint16_t>(lowByte | (highByte << 8));
            for (int i = 0; i < 3; ++i)
            {
                if (value == tree.cache[i])
                {
                    tree.nodes[index].cacheSlot = i;
                }
            }
            if (tree.nodes[index].cacheSlot == -1)
            {
                tree.nodes[index].value = value;
            }
        }
        return index;
    }

    SmkDecoder::HuffTree SmkDecoder::buildTree16(BitReader& reader)
    {
        HuffTree tree;
        if (!reader.readBit())
        {
            return tree;
        }

        auto low = buildTree8(reader);
        auto high = buildTree8(reader);

        // Three escape values; a leaf matching one becomes a reference to
        // the corresponding slot of the moving cache instead of a literal.
        tree.cache[0] = static_cast<uint16_t>(reader.readBits(16));
        tree.cache[1] = static_cast<uint16_t>(reader.readBits(16));
        tree.cache[2] = static_cast<uint16_t>(reader.readBits(16));

        tree.root = buildTree16Recursive(reader, tree, low, high);
        reader.readBit();
        return tree;
    }

    uint16_t SmkDecoder::decodeTree(BitReader& reader, HuffTree& tree)
    {
        if (tree.empty())
        {
            return 0;
        }

        auto index = tree.root;
        while (tree.nodes[index].left != -1)
        {
            index = reader.readBit() ? tree.nodes[index].right : tree.nodes[index].left;
        }

        const auto& node = tree.nodes[index];
        auto value = node.cacheSlot >= 0 ? tree.cache[node.cacheSlot] : node.value;

        // The cache is a three-deep most-recently-used list.
        if (value != tree.cache[0])
        {
            tree.cache[2] = tree.cache[1];
            tree.cache[1] = tree.cache[0];
            tree.cache[0] = value;
        }
        return value;
    }

    void SmkDecoder::decodePalette(const uint8_t* data, std::size_t size)
    {
        auto previous = palette;
        std::size_t pos = 1; // the first byte is the chunk length
        unsigned int index = 0;
        while (index < 256 && pos < size)
        {
            auto b = data[pos++];
            if (b & 0x80)
            {
                // Skip: keep this many entries from the previous palette.
                auto count = static_cast<unsigned int>(b & 0x7F) + 1;
                for (unsigned int i = 0; i < count && index < 256; ++i, ++index)
                {
                    palette[index] = previous[index];
                }
            }
            else if (b & 0x40)
            {
                // Copy a run from an arbitrary place in the previous palette.
                auto count = static_cast<unsigned int>(b & 0x3F) + 1;
                if (pos >= size)
                {
                    break;
                }
                unsigned int source = data[pos++];
                for (unsigned int i = 0; i < count && index < 256 && source < 256; ++i, ++index, ++source)
                {
                    palette[index] = previous[source];
                }
            }
            else
            {
                // A literal 6-bit RGB triple.
                if (pos + 1 >= size)
                {
                    break;
                }
                auto g = data[pos++];
                auto bl = data[pos++];
                palette[index][0] = pal6To8(b & 0x3F);
                palette[index][1] = pal6To8(g & 0x3F);
                palette[index][2] = pal6To8(bl & 0x3F);
                ++index;
            }
        }
    }

    void SmkDecoder::decodeAudioChunk(const uint8_t* data, std::size_t size, unsigned int track)
    {
        if (!(audioTrackFlags[track] & AudioFlagPacked))
        {
            // Raw PCM, unusual but legal.
            if (track != 0)
            {
                return;
            }
            if (audioInfo && audioInfo->is16Bit)
            {
                for (std::size_t i = 0; i + 1 < size; i += 2)
                {
                    frameAudio.push_back(static_cast<int16_t>(data[i] | (data[i + 1] << 8)));
                }
            }
            else
            {
                for (std::size_t i = 0; i < size; ++i)
                {
                    frameAudio.push_back(static_cast<int16_t>((data[i] - 128) << 8));
                }
            }
            return;
        }

        if (size < 4 || track != 0)
        {
            return;
        }
        auto unpackedSize = readU32(data);
        BitReader reader(data + 4, size - 4);

        if (!reader.readBit())
        {
            return;
        }
        bool stereo = reader.readBit();
        bool is16 = reader.readBit();

        auto treeCount = 1u << (static_cast<unsigned int>(stereo) + static_cast<unsigned int>(is16));
        HuffTree trees[4];
        for (unsigned int i = 0; i < treeCount; ++i)
        {
            trees[i] = buildTree8(reader);
        }

        int32_t predictor[2] = {0, 0};
        auto channels = stereo ? 2u : 1u;

        if (is16)
        {
            // Bases arrive right channel first, high byte first within the
            // sixteen bits (the stream is bit-reversed relative to a plain
            // little-endian read).
            for (int ch = static_cast<int>(channels) - 1; ch >= 0; --ch)
            {
                auto hi = reader.readBits(8);
                auto lo = reader.readBits(8);
                predictor[ch] = static_cast<int16_t>(lo | (hi << 8));
            }
            auto sampleCount = unpackedSize / 2;
            frameAudio.reserve(frameAudio.size() + sampleCount);
            for (unsigned int ch = 0; ch < channels; ++ch)
            {
                frameAudio.push_back(static_cast<int16_t>(predictor[ch]));
            }
            for (unsigned int i = channels; i < sampleCount; ++i)
            {
                auto ch = stereo ? (i & 1u) : 0u;
                auto lo = decodeTree(reader, trees[ch * 2]);
                auto hi = decodeTree(reader, trees[(ch * 2) + 1]);
                auto delta = static_cast<int16_t>(lo | (hi << 8));
                predictor[ch] = static_cast<int16_t>(predictor[ch] + delta);
                frameAudio.push_back(static_cast<int16_t>(predictor[ch]));
            }
        }
        else
        {
            for (int ch = static_cast<int>(channels) - 1; ch >= 0; --ch)
            {
                predictor[ch] = static_cast<int32_t>(reader.readBits(8));
            }
            auto sampleCount = unpackedSize;
            frameAudio.reserve(frameAudio.size() + sampleCount);
            for (unsigned int ch = 0; ch < channels; ++ch)
            {
                frameAudio.push_back(static_cast<int16_t>((predictor[ch] - 128) << 8));
            }
            for (unsigned int i = channels; i < sampleCount; ++i)
            {
                auto ch = stereo ? (i & 1u) : 0u;
                auto delta = static_cast<int8_t>(decodeTree(reader, trees[ch]));
                predictor[ch] = static_cast<uint8_t>(predictor[ch] + delta);
                frameAudio.push_back(static_cast<int16_t>((predictor[ch] - 128) << 8));
            }
        }
    }

    void SmkDecoder::decodeVideo(const uint8_t* data, std::size_t size)
    {
        mmapTree.resetCache();
        mclrTree.resetCache();
        fullTree.resetCache();
        typeTree.resetCache();

        BitReader reader(data, size);

        auto blocksX = width / 4;
        auto blocksY = height / 4;
        auto totalBlocks = blocksX * blocksY;

        unsigned int block = 0;
        while (block < totalBlocks)
        {
            auto typeCode = decodeTree(reader, typeTree);
            auto blockType = typeCode & 3u;
            auto run = sizeTable((typeCode >> 2) & 0x3Fu);

            switch (blockType)
            {
                case 0: // two colours through a bitmap
                {
                    for (unsigned int n = 0; n < run && block < totalBlocks; ++n, ++block)
                    {
                        auto colors = decodeTree(reader, mclrTree);
                        auto map = decodeTree(reader, mmapTree);
                        auto setColor = static_cast<uint8_t>(colors >> 8);
                        auto clearColor = static_cast<uint8_t>(colors & 0xFF);
                        auto* out = &videoIndices[((block / blocksX) * 4 * width) + ((block % blocksX) * 4)];
                        for (int row = 0; row < 4; ++row)
                        {
                            for (int col = 0; col < 4; ++col)
                            {
                                out[col] = (map & 1u) ? setColor : clearColor;
                                map >>= 1;
                            }
                            out += width;
                        }
                    }
                    break;
                }
                case 1: // full: every pixel coded, right pair before left
                {
                    for (unsigned int n = 0; n < run && block < totalBlocks; ++n, ++block)
                    {
                        auto* out = &videoIndices[((block / blocksX) * 4 * width) + ((block % blocksX) * 4)];
                        for (int row = 0; row < 4; ++row)
                        {
                            auto rightPair = decodeTree(reader, fullTree);
                            out[2] = static_cast<uint8_t>(rightPair & 0xFF);
                            out[3] = static_cast<uint8_t>(rightPair >> 8);
                            auto leftPair = decodeTree(reader, fullTree);
                            out[0] = static_cast<uint8_t>(leftPair & 0xFF);
                            out[1] = static_cast<uint8_t>(leftPair >> 8);
                            out += width;
                        }
                    }
                    break;
                }
                case 2: // void: the previous frame shows through
                {
                    block += run;
                    break;
                }
                case 3: // solid fill, colour in the high byte
                {
                    auto color = static_cast<uint8_t>(typeCode >> 8);
                    for (unsigned int n = 0; n < run && block < totalBlocks; ++n, ++block)
                    {
                        auto* out = &videoIndices[((block / blocksX) * 4 * width) + ((block % blocksX) * 4)];
                        for (int row = 0; row < 4; ++row)
                        {
                            std::fill(out, out + 4, color);
                            out += width;
                        }
                    }
                    break;
                }
            }
        }
    }

    bool SmkDecoder::decodeNextFrame()
    {
        if (currentFrame >= frameCount)
        {
            return false;
        }

        const auto* base = reinterpret_cast<const uint8_t*>(fileData.data());
        auto frameSize = frameSizes[currentFrame];
        if (currentOffset + frameSize > fileData.size())
        {
            throw std::runtime_error("SMK frame out of bounds");
        }
        const auto* p = base + currentOffset;
        std::size_t remaining = frameSize;
        auto type = frameTypes[currentFrame];

        frameAudio.clear();

        if (type & 1u)
        {
            if (remaining < 1)
            {
                throw std::runtime_error("SMK palette chunk out of bounds");
            }
            std::size_t paletteSize = static_cast<std::size_t>(p[0]) * 4;
            if (paletteSize > remaining)
            {
                throw std::runtime_error("SMK palette chunk out of bounds");
            }
            decodePalette(p, paletteSize);
            p += paletteSize;
            remaining -= paletteSize;
        }

        for (unsigned int track = 0; track < 7; ++track)
        {
            if (!(type & (2u << track)))
            {
                continue;
            }
            if (remaining < 4)
            {
                throw std::runtime_error("SMK audio chunk out of bounds");
            }
            auto chunkSize = readU32(p);
            if (chunkSize < 4 || chunkSize > remaining)
            {
                throw std::runtime_error("SMK audio chunk out of bounds");
            }
            decodeAudioChunk(p + 4, chunkSize - 4, track);
            p += chunkSize;
            remaining -= chunkSize;
        }

        if (!skipVideo)
        {
            decodeVideo(p, remaining);
        }

        currentOffset += frameSize;
        ++currentFrame;
        return true;
    }

    void SmkDecoder::rewind()
    {
        currentFrame = 0;
        currentOffset = firstFrameOffset;
        std::fill(videoIndices.begin(), videoIndices.end(), 0);
        palette = {};
        frameAudio.clear();
    }
}
