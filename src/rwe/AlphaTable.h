#pragma once

#include <array>
#include <optional>
#include <rwe/ColorPalette.h>
#include <rwe/grid/Grid.h>
#include <vector>

namespace rwe
{
    static const unsigned int AlphaTableEntries = 256;

    /**
     * palettes/PALETTE.ALP: a 256x256 table of palette indices, ALP[a * 256 +
     * b] being the entry nearest the 50/50 average of palette entries a and b.
     * The original installs it at [display+0xC0] as the anti-alias blend table
     * (TOTALA-EXE-SHADING.md S:09) and the cloak composite reads it at
     * 0x4B8500 -> 0x4CBF2C.
     *
     * Measured properties of the shipped file, all pinned by AlphaTable.test:
     * it is exactly 65536 bytes, it is symmetric (ALP[a][b] == ALP[b][a] for
     * all 65536 pairs), and its diagonal is the identity. So there is no
     * source-versus-destination question to get wrong -- a fact worth having
     * written down, because the obvious way to be wrong about a blend table is
     * to transpose it and never notice.
     */
    using AlphaTable = std::array<unsigned char, AlphaTableEntries * AlphaTableEntries>;

    /** Reads the shipped table. Fails unless the file is exactly 65536 bytes. */
    std::optional<AlphaTable> readAlphaTable(const std::vector<char>& bytes);

    /**
     * Rebuilds the table from the palette alone, for a data set missing it.
     *
     * UNLIKE generateShadeTable this is an APPROXIMATION and not a
     * transcription. The routine that built the shipped file was not found in
     * the binary, so this averages each pair and takes the least-squared-
     * distance nearest entry, which is the obvious reconstruction and is not
     * quite what the original did -- AlphaTable.test measures how far off it
     * is against the shipped file and pins the figure, so a change that makes
     * it worse is caught. Prefer the file wherever there is one.
     */
    AlphaTable generateAlphaTable(const ColorPalette& palette);

    /**
     * The table as a 256x256 image for the shader, carrying both of the things
     * a lookup needs: rgb is the colour of the blended entry, and **alpha is
     * the blended entry's own index**, so a result can be fed back in as the
     * operand of a further lookup. The original's anti-aliasing does exactly
     * that -- two lookups over the pixel pairs and then a third over their two
     * results -- so the chaining is not a convenience, it is the mechanism.
     */
    Grid<Color> alphaTableToImage(const AlphaTable& table, const ColorPalette& palette);
}
