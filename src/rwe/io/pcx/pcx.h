#pragma once

#include <algorithm>
#include <cstdint>
#include <rwe/ColorPalette.h>
#include <stdexcept>
#include <vector>

namespace rwe
{
    class PcxException : public std::runtime_error
    {
    public:
        explicit PcxException(const char* message);
    };

#pragma pack(1)

    struct PcxWindow
    {
        uint16_t xMin;
        uint16_t yMin;
        uint16_t xMax;
        uint16_t yMax;
    };

    struct PcxHeader
    {
        uint8_t manufacturer;
        uint8_t version;

        uint8_t encoding;
        uint8_t bitsPerPixel;
        PcxWindow window;
        uint16_t horizontalDpi;
        uint16_t verticalDpi;
        uint8_t colorMap[48];
        uint8_t reserved;
        uint8_t numberOfPlanes;
        uint16_t bytesPerLine;
        uint16_t paletteInfo;
        uint16_t horizontalScreenSize;
        uint16_t verticalScreenSize;

        uint8_t filler[54];
    };

    struct PcxPaletteColor
    {
        uint8_t red;
        uint8_t green;
        uint8_t blue;

        Color toColor() const { return Color(red, green, blue); }
    };

#pragma pack()

    template <typename It>
    class PcxDecoder
    {
    private:
        It begin;
        It end;
        const PcxHeader* header;

    public:
        /**
         * The largest image, in bytes of decoded rows, this will decode: an
         * eight-thousand-pixel square. TA's own are unit pictures and
         * screens, a few hundred pixels across. Issue #75.
         */
        static constexpr std::size_t MaxDecodedBytes = std::size_t(8192) * 8192;

        /**
         * Everything here reads bytes a mod supplies. The header, the row
         * lengths and the palette used to be taken on trust, and a run could
         * write past the end of the image. Each is checked now, and a file
         * that fails any check throws PcxException. Issue #75.
         */
        PcxDecoder(It begin, It end) : begin(begin), end(end)
        {
            if (end - begin < static_cast<std::ptrdiff_t>(sizeof(PcxHeader)))
            {
                throw PcxException("file too short for a header");
            }
            header = reinterpret_cast<const PcxHeader*>(&*begin);
            if (header->window.xMax < header->window.xMin || header->window.yMax < header->window.yMin)
            {
                throw PcxException("image window is inside out");
            }
        }

        unsigned int getWidth()
        {
            return (header->window.xMax - header->window.xMin) + 1u;
        }

        unsigned int getHeight()
        {
            return (header->window.yMax - header->window.yMin) + 1u;
        }

        /**
         * The image as width * height palette indices, which is what every
         * texture made from one expects: rows cropped to the width, and the
         * format held to the eight-bit, one-plane kind a palette applies to.
         * decodeImage gives rows as stored, padding and all.
         */
        std::vector<char> decodePalettedImage()
        {
            if (header->numberOfPlanes != 1 || header->bitsPerPixel != 8)
            {
                throw PcxException("not an eight-bit paletted image");
            }
            auto width = static_cast<std::size_t>(getWidth());
            auto height = static_cast<std::size_t>(getHeight());
            auto stride = static_cast<std::size_t>(header->bytesPerLine);
            if (stride < width)
            {
                throw PcxException("rows are shorter than the image is wide");
            }
            auto rows = decodeImage();
            std::vector<char> out(width * height);
            for (std::size_t y = 0; y < height; ++y)
            {
                std::copy_n(rows.data() + (y * stride), width, out.data() + (y * width));
            }
            return out;
        }

        std::vector<char> decodeImage()
        {
            auto ySize = static_cast<std::size_t>(getHeight());

            auto totalBytesPerRow = static_cast<std::size_t>(header->numberOfPlanes) * header->bytesPerLine;
            if (totalBytesPerRow == 0 || totalBytesPerRow * ySize > MaxDecodedBytes)
            {
                throw PcxException("image dimensions out of range");
            }
            auto totalBytes = totalBytesPerRow * ySize;

            std::vector<char> vec(totalBytes);
            auto buf = vec.data();

            auto it = begin + sizeof(PcxHeader);

            for (std::size_t y = 0; y < ySize; ++y)
            {
                auto rowBuf = buf + (y * totalBytesPerRow);

                std::size_t bytesWritten = 0;
                while (bytesWritten < totalBytesPerRow)
                {
                    if (it == end)
                    {
                        throw PcxException("reached end of input before row could be finished");
                    }
                    auto byte = *(it++);
                    if ((byte & 0b11000000) == 0b11000000)
                    {
                        // A run is no longer than what is left of the row. The
                        // last row's used to run on past the image.
                        auto count = std::min(static_cast<std::size_t>(byte & 0b00111111), totalBytesPerRow - bytesWritten);
                        if (it == end)
                        {
                            throw PcxException("malformed row");
                        }
                        auto data = *(it++);
                        std::fill_n(rowBuf + bytesWritten, count, data);
                        bytesWritten += count;
                    }
                    else
                    {
                        rowBuf[bytesWritten++] = byte;
                    }
                }
            }

            return vec;
        }

        std::vector<PcxPaletteColor> decodePalette()
        {
            const auto paletteBytes = static_cast<std::ptrdiff_t>((sizeof(PcxPaletteColor) * 256) + 1);
            if (end - begin < static_cast<std::ptrdiff_t>(sizeof(PcxHeader)) + paletteBytes)
            {
                throw PcxException("file too short for a palette");
            }
            auto it = end - paletteBytes;
            if (*(it++) != 12)
            {
                throw PcxException("invalid palette");
            }

            auto colorIt = reinterpret_cast<const PcxPaletteColor*>(&*it);

            std::vector<PcxPaletteColor> colors(256);

            std::copy(colorIt, colorIt + 256, colors.data());

            return colors;
        }
    };

    /**
     * Encodes a true-colour image as a 24-bit PCX: version 5, run-length
     * encoded, three eight-bit planes (red, green, blue), each plane of each
     * scanline encoded on its own so no run crosses a plane or a line. rgb is
     * width * height * 3 bytes, top row first, RGBRGB... within a row.
     *
     * The original writes 8-bit palettized PCX from its 8-bit screen
     * (TOTALA-EXE.md S:77). RWE draws in true colour, so it keeps the name,
     * the folder and the format family and widens the pixels.
     */
    std::vector<uint8_t> encodePcx24(unsigned int width, unsigned int height, const std::vector<uint8_t>& rgb);
}
