#include "ShadeTable.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace rwe
{
    std::optional<ShadeTable> readShadeTable(const std::vector<char>& bytes)
    {
        if (bytes.size() != ShadeTableRows * ShadeTableEntriesPerRow)
        {
            return std::nullopt;
        }

        ShadeTable table{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            table[i] = static_cast<unsigned char>(bytes[i]);
        }

        return table;
    }

    PaletteMatcher::PaletteMatcher(const ColorPalette& palette) : palette(palette)
    {
        if (palette.size() < 256)
        {
            throw std::runtime_error("the palette matcher needs a 256 entry palette");
        }

        // 0x4BA920: the sum of each entry's channels, then an exchange sort
        // ascending that carries an index array along with it.
        for (int i = 0; i < 256; ++i)
        {
            const auto& c = palette[i];
            sums[i] = static_cast<int>(c.r) + static_cast<int>(c.g) + static_cast<int>(c.b);
            order[i] = i;
        }
        for (int i = 0; i < 256; ++i)
        {
            for (int j = i + 1; j < 256; ++j)
            {
                if (sums[i] > sums[j])
                {
                    std::swap(sums[i], sums[j]);
                    std::swap(order[i], order[j]);
                }
            }
        }
    }

    int PaletteMatcher::nearest(int r, int g, int b) const
    {
        // 0x4BA9D0: search a window of +-40 on the channel sum in that sorted
        // order and take the least squared RGB distance, the first strict
        // minimum winning. The window is why the sort exists, and it is why
        // this is not simply the nearest entry in the palette.
        auto total = r + g + b;
        auto low = total - 40;
        auto high = total + 40;
        auto best = std::numeric_limits<int>::max();
        int bestPosition = 0;
        for (int p = 0; p < 256; ++p)
        {
            if (sums[p] < low)
            {
                continue;
            }
            if (sums[p] > high)
            {
                break;
            }
            const auto& c = palette[order[p]];
            auto dr = static_cast<int>(c.r) - r;
            auto dg = static_cast<int>(c.g) - g;
            auto db = static_cast<int>(c.b) - b;
            auto d = (dr * dr) + (dg * dg) + (db * db);
            if (d < best)
            {
                best = d;
                bestPosition = p;
            }
        }
        return order[bestPosition];
    }

    ShadeTable generateShadeTable(const ColorPalette& palette)
    {
        if (palette.size() < 256)
        {
            throw std::runtime_error("shade table needs a 256 entry palette");
        }

        // Row k remaps every palette entry to the nearest palette colour to
        // that entry scaled by 0.06875k. Because that is a nearest match in a
        // fixed 256 entry palette and not a scale, brightening saturates
        // towards white at the top of each of the palette's ramps rather than
        // continuing to scale -- which is the behaviour no curve applied to
        // the colour can reproduce, and the reason the table has to be a
        // table.

        // 0x4BA920 and 0x4BA9D0, the band matcher every one of the palette
        // tables is built with.
        PaletteMatcher matcher(palette);
        auto nearest = [&](int r, int g, int b) { return matcher.nearest(r, g, b); };

        ShadeTable table{};
        auto scale = 0.0;
        for (unsigned int row = 0; row < ShadeTableRows; ++row)
        {
            for (unsigned int i = 0; i < ShadeTableEntriesPerRow; ++i)
            {
                const auto& c = palette[i];
                const int source[3] = {c.r, c.g, c.b};
                int scaled[3];
                for (int k = 0; k < 3; ++k)
                {
                    // _ftol truncates toward zero, and the store keeps a byte
                    // with a saturating test on the low word of the result.
                    auto v = static_cast<int>(source[k] * scale);
                    scaled[k] = ((v & 0xffff) > 255) ? 255 : (v & 0xff);
                }
                table[(row * ShadeTableEntriesPerRow) + i] = static_cast<unsigned char>(nearest(scaled[0], scaled[1], scaled[2]));
            }
            scale += 0.06875;
        }
        return table;
    }

    PaletteRemap generateBlueTable(const ColorPalette& palette)
    {
        // 0x4BAF30: red halved, green halved, blue halved plus 50, each entry
        // matched. (An overflow test on blue half plus 60 never fires, since
        // half a byte is at most 127.)
        PaletteMatcher matcher(palette);
        PaletteRemap table{};
        for (int i = 0; i < 256; ++i)
        {
            const auto& c = palette[i];
            table[i] = static_cast<unsigned char>(matcher.nearest(c.r >> 1, c.g >> 1, (c.b >> 1) + 50));
        }
        return table;
    }

    PaletteRemap generateGrayTable(const ColorPalette& palette)
    {
        // 0x4BAD30: every channel becomes the truncated mean of the three,
        // matched. A desaturation, not a darkening.
        PaletteMatcher matcher(palette);
        PaletteRemap table{};
        for (int i = 0; i < 256; ++i)
        {
            const auto& c = palette[i];
            auto mean = (static_cast<int>(c.r) + static_cast<int>(c.g) + static_cast<int>(c.b)) / 3;
            table[i] = static_cast<unsigned char>(matcher.nearest(mean, mean, mean));
        }
        return table;
    }

    Grid<Color> shadeAndBlueTablesToImage(const ShadeTable& table, const PaletteRemap& blue, const ColorPalette& palette)
    {
        Grid<Color> image(static_cast<int>(ShadeTableEntriesPerRow), static_cast<int>(BlueUnshadedRow + 1));
        for (unsigned int r = 0; r < ShadeTableRows; ++r)
        {
            for (unsigned int t = 0; t < ShadeTableEntriesPerRow; ++t)
            {
                auto shaded = table[(r * ShadeTableEntriesPerRow) + t];
                image.set(static_cast<int>(t), static_cast<int>(r), palette[shaded]);
                image.set(static_cast<int>(t), static_cast<int>(BlueShadeRowOffset + r), palette[blue[shaded]]);
            }
        }
        for (unsigned int t = 0; t < ShadeTableEntriesPerRow; ++t)
        {
            image.set(static_cast<int>(t), static_cast<int>(BlueUnshadedRow), palette[blue[t]]);
        }

        return image;
    }

    Grid<Color> shadeTableToImage(const ShadeTable& table, const ColorPalette& palette)
    {
        Grid<Color> image(static_cast<int>(ShadeTableEntriesPerRow), static_cast<int>(ShadeTableRows));
        for (unsigned int r = 0; r < ShadeTableRows; ++r)
        {
            for (unsigned int t = 0; t < ShadeTableEntriesPerRow; ++t)
            {
                image.set(static_cast<int>(t), static_cast<int>(r), palette[table[(r * ShadeTableEntriesPerRow) + t]]);
            }
        }

        return image;
    }
}
