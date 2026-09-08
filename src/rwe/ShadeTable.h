#pragma once

#include <array>
#include <optional>
#include <rwe/ColorPalette.h>
#include <rwe/grid/Grid.h>
#include <vector>

namespace rwe
{
    static const unsigned int ShadeTableRows = 32;
    static const unsigned int ShadeTableEntriesPerRow = 256;

    /**
     * palettes/PALETTE.SHD: 32 rows of 256 palette indices. The original's
     * span fillers shade a pixel by reading SHD[row * 256 + texel], where the
     * texel is the raw palette index out of the texture and the row is the
     * interpolated shade level -- one lookup, no arithmetic either side of it.
     */
    using ShadeTable = std::array<unsigned char, ShadeTableRows * ShadeTableEntriesPerRow>;

    /** Reads the shipped table. Fails unless the file is exactly 8192 bytes. */
    std::optional<ShadeTable> readShadeTable(const std::vector<char>& bytes);

    /**
     * Rebuilds the table from the palette alone, for a data set that is
     * missing it. This is 0x4BADF0 transcribed, and it reproduces the shipped
     * palettes/PALETTE.SHD byte for byte from the shipped palettes/PALETTE.PAL.
     */
    ShadeTable generateShadeTable(const ColorPalette& palette);

    /**
     * The table as a 256x32 image -- column t, row r being the colour of
     * SHD[r * 256 + t] -- so a shader can fetch it directly.
     */
    Grid<Color> shadeTableToImage(const ShadeTable& table, const ColorPalette& palette);
}
