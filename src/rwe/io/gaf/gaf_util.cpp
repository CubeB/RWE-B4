#include "gaf_util.h"

#include <memory>
#include <rwe/io/io_util.h>

namespace rwe
{
    GafException::GafException(const char* message) : runtime_error(message) {}

    void decompressRow(std::istream& stream, char* buffer, std::size_t rowLength, char transparencyIndex)
    {
        auto compressedRowLength = readGafRaw<uint16_t>(stream);

        std::size_t readPos = 0;
        std::size_t writePos = 0;

        while (readPos < compressedRowLength && writePos < rowLength)
        {
            auto mask = readGafRaw<uint8_t>(stream);
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
                auto val = readGafRaw<uint8_t>(stream);
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
                if (static_cast<std::size_t>(stream.gcount()) != count)
                {
                    throw GafException("truncated GAF");
                }
                readPos += count;
                writePos += count;
            }
        }

        // A row that stops short is transparent from the first pixel it did
        // not write (this filled from the row's first pixel, over what it had
        // just decoded), and bytes the prefix claims beyond what a full row
        // used are skipped, so the next row's length is read from where it is.
        std::fill_n(buffer + writePos, rowLength - writePos, transparencyIndex);
        if (readPos < compressedRowLength)
        {
            stream.ignore(static_cast<std::streamsize>(compressedRowLength - readPos));
            if (stream.fail())
            {
                throw GafException("truncated GAF");
            }
        }
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
        /**
         * An uncompressed frame is its pixels and nothing else; a file that
         * ends first is refused rather than left with an unwritten tail.
         */
        void readRawFrame(std::istream& stream, char* buffer, std::size_t bytes)
        {
            stream.read(buffer, static_cast<std::streamsize>(bytes));
            if (static_cast<std::size_t>(stream.gcount()) != bytes)
            {
                throw GafException("truncated GAF");
            }
        }

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
            auto frameHeader = readGafRaw<GafFrameData>(*_stream);
            checkFrameSize(frameHeader);
            adapter.beginFrame(entry, frameHeader);

            _stream->seekg(frameHeader.frameDataOffset);
            if (frameHeader.subframesCount == 0)
            {
                auto buffer = std::make_unique<char[]>(frameBytes(frameHeader));
                if (frameHeader.compressed == 0)
                {
                    readRawFrame(*_stream, buffer.get(), frameBytes(frameHeader));
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
                    auto subframeOffset = readGafRaw<uint32_t>(*_stream);
                    auto pos = _stream->tellg();
                    _stream->seekg(subframeOffset);
                    auto subframeHeader = readGafRaw<GafFrameData>(*_stream);
                    checkFrameSize(subframeHeader);
                    auto buffer = std::make_unique<char[]>(frameBytes(subframeHeader));

                    _stream->seekg(subframeHeader.frameDataOffset);
                    if (subframeHeader.compressed == 0)
                    {
                        readRawFrame(*_stream, buffer.get(), frameBytes(subframeHeader));
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
