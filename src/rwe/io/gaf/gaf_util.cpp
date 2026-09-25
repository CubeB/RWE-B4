#include "gaf_util.h"

#include <memory>
#include <rwe/io/io_util.h>

namespace rwe
{
    GafException::GafException(const char* message) : runtime_error(message) {}

    void decompressRow(std::istream& stream, char* buffer, std::size_t rowLength, char transparencyIndex)
    {
        auto compressedRowLength = readRaw<uint16_t>(stream);

        std::size_t readPos = 0;
        std::size_t writePos = 0;

        while (readPos < compressedRowLength && writePos < rowLength)
        {
            auto mask = readRaw<uint8_t>(stream);
            ++readPos;

            if ((mask & 1) == 1)
            {
                // skip n pixels (transparency)
                auto count = mask >> 1;
                if (writePos + count > rowLength)
                {
                    throw GafException("malformed row");
                }
                std::fill_n(buffer + writePos, count, transparencyIndex);
                writePos += count;
            }
            else if ((mask & 2) == 2)
            {
                // repeat this byte n times
                auto count = (mask >> 2) + 1u;

                if (readPos + 1 > compressedRowLength)
                {
                    throw GafException("malformed row");
                }
                auto val = readRaw<uint8_t>(stream);
                ++readPos;

                if (writePos + count > rowLength)
                {
                    throw GafException("malformed row");
                }
                std::fill_n(buffer + writePos, count, val);
                writePos += count;
            }
            else
            {
                // by default, copy next n bytes
                auto count = (mask >> 2) + 1u;

                if (readPos + count > compressedRowLength)
                {
                    throw GafException("malformed row");
                }
                if (writePos + count > rowLength)
                {
                    throw GafException("malformed row");
                }
                stream.read(buffer + writePos, count);
                readPos += count;
                writePos += count;
            }
        }

        std::fill_n(buffer, rowLength - writePos, transparencyIndex);
    }

    void decompressFrame(std::istream& stream, char* buffer, std::size_t width, std::size_t height, char transparencyIndex)
    {
        for (std::size_t i = 0; i < height; ++i)
        {
            decompressRow(stream, buffer + (i * width), width, transparencyIndex);
        }
    }

    namespace
    {
        /**
         * A frame's pixel count, in size_t: width and height are sixteen bits
         * each and their product was taken as an int, which overflows at the
         * top of the range. Issue #75.
         */
        std::size_t frameBytes(const GafFrameData& frame)
        {
            return static_cast<std::size_t>(frame.width) * frame.height;
        }

        /**
         * Frames are allocated at their declared size before a pixel is read.
         * TA's largest are screens, 640x480; this is far past any. Issue #75.
         */
        void checkFrameSize(const GafFrameData& frame)
        {
            constexpr uint16_t MaxDimension = 8192;
            if (frame.width > MaxDimension || frame.height > MaxDimension)
            {
                throw GafException("GAF frame size out of range");
            }
        }
    }

    void extractGafEntry(std::istream* _stream, const std::vector<GafFrameEntry>& frameEntries, GafReaderAdapter& adapter)
    {
        for (auto entry : frameEntries)
        {
            _stream->seekg(entry.frameDataOffset);
            auto frameHeader = readRaw<GafFrameData>(*_stream);
            checkFrameSize(frameHeader);
            adapter.beginFrame(entry, frameHeader);

            _stream->seekg(frameHeader.frameDataOffset);
            if (frameHeader.subframesCount == 0)
            {
                auto buffer = std::make_unique<char[]>(frameBytes(frameHeader));
                if (frameHeader.compressed == 0)
                {
                    _stream->read(buffer.get(), static_cast<std::streamsize>(frameBytes(frameHeader)));
                }
                else
                {
                    decompressFrame(*_stream, buffer.get(), frameHeader.width, frameHeader.height, frameHeader.transparencyIndex);
                }

                GafReaderAdapter::LayerData layer{
                    frameHeader.posX,
                    frameHeader.posY,
                    frameHeader.width,
                    frameHeader.height,
                    frameHeader.transparencyIndex,
                    buffer.get(),
                };

                adapter.frameLayer(layer);
            }
            else
            {
                for (std::size_t i = 0; i < frameHeader.subframesCount; ++i)
                {
                    auto subframeOffset = readRaw<uint32_t>(*_stream);
                    auto pos = _stream->tellg();
                    _stream->seekg(subframeOffset);
                    auto subframeHeader = readRaw<GafFrameData>(*_stream);
                    checkFrameSize(subframeHeader);
                    auto buffer = std::make_unique<char[]>(frameBytes(subframeHeader));

                    _stream->seekg(subframeHeader.frameDataOffset);
                    if (subframeHeader.compressed == 0)
                    {
                        _stream->read(buffer.get(), static_cast<std::streamsize>(frameBytes(subframeHeader)));
                    }
                    else
                    {
                        decompressFrame(*_stream, buffer.get(), subframeHeader.width, subframeHeader.height, subframeHeader.transparencyIndex);
                    }

                    GafReaderAdapter::LayerData layer{
                        subframeHeader.posX,
                        subframeHeader.posY,
                        subframeHeader.width,
                        subframeHeader.height,
                        subframeHeader.transparencyIndex,
                        buffer.get(),
                    };

                    adapter.frameLayer(layer);

                    _stream->seekg(pos);
                }
            }

            adapter.endFrame();
        }
    }
}
