#pragma once

#include <vector>

namespace rwe
{
    /** One tile of a nine-patch: where its top-left goes in the area, and which of the nine frames. */
    struct NinePatchTile
    {
        int x;
        int y;
        int frame;
    };

    /**
     * Lays a frame sequence of nine frames over a width x height area, the way
     * TotalA draws a dialog's frame (the frames in rows of three: 0 1 2 / 3 4 5
     * / 6 7 8).
     *
     * The step is the size of one frame. The first row takes 0 1 2, the rows
     * after it 3 4 5, and the last is pinned to the bottom edge with 6 7 8 --
     * so rows overlap when the height is not a multiple of the step. Along a
     * row the first tile is the left frame, the ones after it the middle, and
     * the last is pinned to the right edge as the right frame. An area no
     * bigger than one frame is a single top-left tile. Tiles come out in
     * drawing order, so a pinned tile lands on top of the one it overlaps.
     */
    std::vector<NinePatchTile> layoutNinePatch(int width, int height, int stepX, int stepY);
}
