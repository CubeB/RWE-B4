#pragma once

#include <memory>
#include <rwe/render/Sprite.h>
#include <vector>

namespace rwe
{
    struct SpriteSeries
    {
        std::vector<std::shared_ptr<Sprite>> sprites;

        /**
         * Rows the text is drawn above the Y it is asked for. A bitmap font
         * file carries a signed vertical offset that the original's glyph
         * blitter subtracts from Y (1 to 3 in every shipped font); the sprite
         * bounds are left alone so that advances and measures do not change.
         */
        float verticalOffset{0.0f};
    };
}
