#include "TntArchive.h"
#include <algorithm>

#include <array>
#include <cassert>
#include <rwe/io/io_util.h>

namespace rwe
{
    TntException::TntException(const std::string& __arg) : runtime_error(__arg)
    {
    }

    TntException::TntException(const char* string) : runtime_error(string)
    {
    }

    TntArchive::TntArchive(std::istream* _stream) : stream(_stream)
    {
        header = readRaw<TntHeader>(*stream);
        if (header.magicNumber != TntMagicNumber)
        {
            throw TntException("Invalid TNT version number");
        }

        // A map is a download, and every size below sizes an allocation or a
        // loop. A product that wrapped used to give buffers far smaller than
        // the reads into them. The largest shipped or community map is a few
        // thousand cells across; these are well past any. Issue #75.
        constexpr uint32_t MaxDimension = 8192;
        constexpr uint32_t MaxCount = 65536;
        if (header.width == 0 || header.height == 0 || header.width > MaxDimension || header.height > MaxDimension)
        {
            throw TntException("Map size out of range");
        }
        if (header.numberOfTiles > MaxCount || header.numberOfFeatures > MaxCount)
        {
            throw TntException("Map tile or feature count out of range");
        }
    }

    void TntArchive::readTiles(std::function<void(const char*)> tileCallback)
    {
        stream->seekg(header.tileGraphicsOffset);

        std::array<char, 32 * 32> buffer{};

        for (unsigned int i = 0; i < header.numberOfTiles; ++i)
        {
            stream->read(buffer.data(), 32 * 32);
            tileCallback(buffer.data());
        }
    }

    void TntArchive::readFeatures(std::function<void(const std::string&)> featureCallback)
    {
        stream->seekg(header.featuresOffset);

        std::string str;
        str.reserve(128);

        for (unsigned int i = 0; i < header.numberOfFeatures; ++i)
        {
            auto feature = readRaw<TntFeature>(*stream);
            auto nullIt = std::find(feature.name, feature.name + 128, '\0');
            str.erase();
            str.append(feature.name, nullIt);
            featureCallback(str);
        }
    }

    const TntHeader& TntArchive::getHeader() const
    {
        return header;
    }

    void TntArchive::readMapData(uint16_t* outputBuffer)
    {
        stream->seekg(header.mapDataOffset);
        stream->read(reinterpret_cast<char*>(outputBuffer), (header.width / 2) * (header.height / 2) * sizeof(uint16_t));
    }

    void TntArchive::readMapAttributes(TntTileAttributes* outputBuffer)
    {
        stream->seekg(header.mapAttributesOffset);
        stream->read(reinterpret_cast<char*>(outputBuffer), header.width * header.height * sizeof(TntTileAttributes));
    }

    struct MinimapSize
    {
        unsigned int width;
        unsigned int height;
    };

    MinimapSize getMinimapActualSize(const std::vector<char>& data, unsigned int width, unsigned int height)
    {
        unsigned int realWidth = width;
        unsigned int realHeight = height;

        while (realWidth > 0 && data[realWidth - 1] == TntMinimapVoidByte)
        {
            --realWidth;
        }

        while (realHeight > 0 && data[((realHeight - 1) * width)] == TntMinimapVoidByte)
        {
            --realHeight;
        }

        return MinimapSize{realWidth, realHeight};
    }

    // height is read by an assert alone, so a release build has no use for it.
    std::vector<char> trimMinimapBytes(const std::vector<char>& data, unsigned int width, [[maybe_unused]] unsigned int height, unsigned int newWidth, unsigned int newHeight)
    {
        assert(newWidth <= width);
        assert(newHeight <= height);

        std::vector<char> newData(newWidth * newHeight);
        for (unsigned int y = 0; y < newHeight; ++y)
        {
            for (unsigned int x = 0; x < newWidth; ++x)
            {
                newData[(y * newWidth) + x] = data[(y * width) + x];
            }
        }

        return newData;
    }

    TntMinimapInfo TntArchive::readMinimap()
    {
        stream->seekg(header.minimapOffset);
        auto minimapHeader = readRaw<TntMinimapHeader>(*stream);

        // TA's minimaps are at most 252 pixels a side. The product was
        // taken in 32 bits, and one that wrapped sized a buffer the scan for
        // the void border then read far past. Issue #75.
        constexpr uint32_t MaxMinimapDimension = 1024;
        if (minimapHeader.width > MaxMinimapDimension || minimapHeader.height > MaxMinimapDimension)
        {
            throw TntException("Minimap size out of range");
        }
        if (minimapHeader.width == 0 || minimapHeader.height == 0)
        {
            return TntMinimapInfo{0, 0, std::vector<char>()};
        }

        auto minimapBytes = static_cast<std::size_t>(minimapHeader.width) * minimapHeader.height;
        std::vector<char> buffer(minimapBytes);
        stream->read(buffer.data(), static_cast<std::streamsize>(minimapBytes));

        auto realSize = getMinimapActualSize(buffer, minimapHeader.width, minimapHeader.height);

        auto resizedBuffer = trimMinimapBytes(buffer, minimapHeader.width, minimapHeader.height, realSize.width, realSize.height);

        return TntMinimapInfo{realSize.width, realSize.height, std::move(resizedBuffer)};
    }
}
