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

    /**
     * The band-limited nearest-colour matcher every palette table is built
     * with (0x4BA920 sorts the entries by channel sum with an exchange sort,
     * 0x4BA9D0 searches a window of +-40 on that sum for the least squared RGB
     * distance, the first strict minimum winning). It is why a table cell is
     * not simply the nearest entry in the palette.
     */
    class PaletteMatcher
    {
    public:
        explicit PaletteMatcher(const ColorPalette& palette);

        /** The palette index the band matcher picks for this colour. */
        int nearest(int r, int g, int b) const;

    private:
        const ColorPalette& palette;
        std::array<int, 256> sums{};
        std::array<int, 256> order{};
    };

    /** A one-row palette remap: entry i is the index palette entry i is drawn as. */
    using PaletteRemap = std::array<unsigned char, 256>;

    /**
     * The blue table (0x4BAF30, BT-142): every entry with red and green halved
     * and blue halved plus 50, snapped to a palette index by the band matcher.
     * Black becomes (0, 0, 55) on the shipped palette and white (107, 143, 163).
     */
    PaletteRemap generateBlueTable(const ColorPalette& palette);

    /**
     * The gray table (0x4BAD30, BT-152): every entry with all three channels
     * the truncated mean of the three, snapped by the band matcher.
     */
    PaletteRemap generateGrayTable(const ColorPalette& palette);

    /** Reads the shipped table. Fails unless the file is exactly 8192 bytes. */
    std::optional<ShadeTable> readShadeTable(const std::vector<char>& bytes);

    /**
     * Rebuilds the table from the palette alone, for a data set that is
     * missing it. This is 0x4BADF0 transcribed, and it reproduces the shipped
     * palettes/PALETTE.SHD byte for byte from the shipped palettes/PALETTE.PAL.
     */
    ShadeTable generateShadeTable(const ColorPalette& palette);

    /** Row of the combined image where the blue-remapped shade rows begin. */
    static const unsigned int BlueShadeRowOffset = ShadeTableRows;

    /** Row of the combined image that remaps an unshaded texel through the blue table. */
    static const unsigned int BlueUnshadedRow = ShadeTableRows * 2;

    /**
     * The shade table followed by its blue remap, as one 256 x 65 image
     * (BT-142): rows 0 to 31 are shadeTableToImage; rows 32 to 63 are the same
     * rows with every cell passed through the blue table, which is what the
     * original's tint does to a unit's finished, already shaded pixels; row 64
     * is the blue table alone, for a texel that is not shaded. A shader can
     * fetch any of the three with the texel's palette index.
     */
    Grid<Color> shadeAndBlueTablesToImage(const ShadeTable& table, const PaletteRemap& blue, const ColorPalette& palette);

    /**
     * The table as a 256x32 image -- column t, row r being the colour of
     * SHD[r * 256 + t] -- so a shader can fetch it directly.
     */
    Grid<Color> shadeTableToImage(const ShadeTable& table, const ColorPalette& palette);
}
