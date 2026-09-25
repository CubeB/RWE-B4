#include "ColorPalette.h"
#include <cassert>

namespace rwe
{
    const Color Color::Black = Color(0, 0, 0, 255);
    const Color Color::Transparent = Color(0, 0, 0, 0);

    Color::Color(unsigned char r, unsigned char g, unsigned char b) noexcept : Color(r, g, b, 255) {}

    Color::Color(unsigned char r, unsigned char g, unsigned char b, unsigned char a) noexcept : r(r), g(g), b(b), a(a) {}

    std::optional<ColorPalette> readPalette(std::vector<char>& vector)
    {
        // A mod can supply a palette, and a short one was read past its end
        // with only an assert in the way. It already returns an optional,
        // and every caller treats nothing as an error. Issue #75.
        if (vector.size() < (4 * 256))
        {
            return std::nullopt;
        }

        std::vector<Color> colors(256);

        for (unsigned int i = 0; i < 256; ++i)
        {
            colors[i].r = static_cast<unsigned char>(vector[(4 * i)]);
            colors[i].g = static_cast<unsigned char>(vector[(4 * i) + 1]);
            colors[i].b = static_cast<unsigned char>(vector[(4 * i) + 2]);
            colors[i].a = 255;
        }

        return colors;
    }
}
