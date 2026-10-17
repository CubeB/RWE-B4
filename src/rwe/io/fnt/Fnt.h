#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <vector>

namespace rwe
{
    struct FntHeader
    {
        uint16_t glyphHeight;
        /**
         * Byte 2, the low byte, is the signed vertical offset the glyph
         * blitter subtracts from the requested Y (1 to 3 in every shipped
         * font); byte 3, the high byte, is the first character the offset
         * table covers (0 in every shipped font).
         */
        uint16_t unknown2;
    };

    /** What FntArchive::extractGlyph found for one character. */
    struct FntGlyph
    {
        /** The glyph's width byte; 0 when the font has no glyph for the character. */
        unsigned int width{0};

        /** Bytes of bitmap actually read into the buffer. */
        unsigned int byteCount{0};
    };

    class FntArchive
    {
    private:
        FntHeader _header;
        std::array<uint16_t, 256> _entries;
        std::istream* _stream;

    public:
        explicit FntArchive(std::istream* stream);

        unsigned int glyphHeight() const;

        /**
         * The signed vertical offset (byte 2 of the file), which the original
         * subtracts from the requested Y so that text is drawn that many rows
         * above where it was asked for (BT-162, 0x4CCF60).
         */
        int verticalOffset() const;

        /**
         * Reads one glyph's bitmap into `buffer`, resizing it to what the
         * glyph needs. The glyph is a width byte followed by one continuous
         * most-significant-bit-first bit stream of width x height bits, not
         * padded per row, so it is ceil(width x height / 8) bytes whatever
         * the height (BT-162): a 17-row font keeps its bottom row.
         *
         * If there is no glyph for the given index (from 0 to 255), or its
         * offset lies outside the file, returns zero width and zero bytes. A
         * glyph cut short by the end of the file returns the bytes there are.
         */
        FntGlyph extractGlyph(unsigned int index, std::vector<char>& buffer);
    };
}
