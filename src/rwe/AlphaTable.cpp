#include "AlphaTable.h"
#include <limits>
#include <stdexcept>

namespace rwe
{
    std::optional<AlphaTable> readAlphaTable(const std::vector<char>& bytes)
    {
        if (bytes.size() != AlphaTableEntries * AlphaTableEntries)
        {
            return std::nullopt;
        }

        AlphaTable table{};
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            table[i] = static_cast<unsigned char>(bytes[i]);
        }

        return table;
    }

    AlphaTable generateAlphaTable(const ColorPalette& palette)
    {
        if (palette.size() < 256)
        {
            throw std::runtime_error("alpha table needs a 256 entry palette");
        }

        AlphaTable table{};

        for (unsigned int a = 0; a < AlphaTableEntries; ++a)
        {
            for (unsigned int b = a; b < AlphaTableEntries; ++b)
            {
                // The average, rounded the way an integer halving rounds:
                // (x + y) / 2 truncated, which is what a period shift of a
                // channel sum gives.
                auto mixR = (static_cast<int>(palette[a].r) + static_cast<int>(palette[b].r)) / 2;
                auto mixG = (static_cast<int>(palette[a].g) + static_cast<int>(palette[b].g)) / 2;
                auto mixB = (static_cast<int>(palette[a].b) + static_cast<int>(palette[b].b)) / 2;

                unsigned char best = 0;
                auto bestDistance = std::numeric_limits<int>::max();
                for (unsigned int i = 0; i < 256; ++i)
                {
                    auto dr = static_cast<int>(palette[i].r) - mixR;
                    auto dg = static_cast<int>(palette[i].g) - mixG;
                    auto db = static_cast<int>(palette[i].b) - mixB;
                    auto distance = (dr * dr) + (dg * dg) + (db * db);
                    // First strict minimum wins, as elsewhere in the palette
                    // work -- see generateShadeTable.
                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        best = static_cast<unsigned char>(i);
                    }
                }

                // Symmetric by construction, which is also true of the shipped
                // file and is asserted against it in the tests.
                table[(a * AlphaTableEntries) + b] = best;
                table[(b * AlphaTableEntries) + a] = best;
            }
        }

        return table;
    }

    Grid<Color> alphaTableToImage(const AlphaTable& table, const ColorPalette& palette)
    {
        if (palette.size() < 256)
        {
            throw std::runtime_error("alpha table image needs a 256 entry palette");
        }

        Grid<Color> image(static_cast<int>(AlphaTableEntries), static_cast<int>(AlphaTableEntries));

        for (unsigned int b = 0; b < AlphaTableEntries; ++b)
        {
            for (unsigned int a = 0; a < AlphaTableEntries; ++a)
            {
                auto blended = table[(b * AlphaTableEntries) + a];
                const auto& c = palette[blended];
                // rgb is what to draw, alpha is what to look up next.
                image.set(static_cast<int>(a), static_cast<int>(b), Color(c.r, c.g, c.b, blended));
            }
        }

        return image;
    }
}
