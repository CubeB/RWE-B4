#pragma once

#include <cstddef>
#include <rwe/math/Vector2f.h>
#include <vector>

namespace rwe
{
    /**
     * One pixel of a construction wireframe: its column and row, and the
     * point on the polygon's outline it was taken from -- the edge's two
     * corners, how far along it, and the edge's exact column on that row --
     * so the caller can put the pixel at the polygon's depth.
     */
    struct WireframePixel
    {
        int x;
        int y;
        std::size_t from;
        std::size_t to;
        float t;
        float edgeX;
    };

    /**
     * The original's construction wireframe for one polygon, whose corners
     * are given in screen pixels, y down, in the order the 3DO lists them
     * (TOTALA-EXE.md S:3; 0x458FA0, 0x4C0820, 0x4C0A90).
     *
     * The polygon is scan-converted, and of each row only two pixels are
     * kept: the first inside its left edge and the first past its right
     * edge. So a steep edge comes out as a solid line one pixel wide, a
     * shallow one as a dotted line, and a horizontal one not at all. The left
     * edge is the chain of corners walked backwards from the topmost corner
     * and the right edge the chain walked forwards, and a row whose right end
     * does not lie to the right of its left end draws nothing -- which is
     * how a polygon facing away from the camera, wound the other way on
     * screen, draws nothing at all.
     *
     * Pixel centres stand in for the original's integer corners: a row is
     * drawn if its centre lies between the top and bottom corners, top
     * inclusive, and each end is the first pixel whose centre is at or past
     * the edge.
     */
    void scanWireframePolygon(const std::vector<Vector2f>& corners, std::vector<WireframePixel>& out);
}
