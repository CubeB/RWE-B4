#pragma once

#include <array>
#include <optional>
#include <rwe/grid/Grid.h>
#include <rwe/vfs/AbstractVirtualFileSystem.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * One frame of anims/fog.gaf.
     *
     * The frames are pure stencils: every painted pixel is palette index 0 and
     * everything else is the transparency key, so all we keep is a coverage
     * mask. The frame sits at (offsetX, offsetY) within its 32x32 overlay tile,
     * taken from the GAF frame's hotspot. Hand-drawn raggedness means a frame
     * can be a pixel or two larger than the quadrant it nominally fills, and
     * may spill outside the tile, so callers must clip rather than assume.
     */
    struct FogTileFrame
    {
        int width{0};
        int height{0};
        int offsetX{0};
        int offsetY{0};
        /** One byte per pixel: non-zero where the frame paints. */
        std::vector<char> mask;
    };

    /**
     * The 14 frames of one fog.gaf sequence, indexed by marching-squares corner
     * code minus one. Bit 1 of the code is the tile's top-left quadrant, 2 the
     * top-right, 4 the bottom-left and 8 the bottom-right; code 0 draws nothing
     * and code 15 is a solid fill, so neither has a frame.
     */
    using FogTileVariant = std::array<FogTileFrame, 14>;

    /**
     * TA's fog artwork: four interchangeable ragged versions of each shape for
     * each of the two layers. Black* covers ground that has never been seen,
     * Gray* ground that is remembered but not currently in sight.
     */
    struct FogTileSet
    {
        static constexpr int VariantCount = 4;

        std::array<FogTileVariant, VariantCount> unexplored;
        std::array<FogTileVariant, VariantCount> unseen;

        /**
         * Bounding box of every frame relative to its tile's top-left corner,
         * as [minX, maxX) x [minY, maxY). Wider than 32x32 where the artwork
         * spills over the tile edge.
         */
        int minX{0};
        int minY{0};
        int maxX{0};
        int maxY{0};
    };

    /** Reads the eight Black1..4 / Gray1..4 sequences out of a fog GAF. */
    std::optional<FogTileSet> loadFogTileSet(AbstractVirtualFileSystem& vfs, const std::string& gafName);

    /**
     * Square-edged stand-ins for the fog tiles, used when the data set has no
     * anims/fog.gaf. The fog then looks like RWE's old blocky overlay rather
     * than TA's, but nothing else changes.
     */
    FogTileSet makeSquareFogTileSet();

    /**
     * Draws the fog of war as TA does: a marching-squares overlay tile per
     * corner of the vision grid, rasterised into a single-channel image at one
     * texel per world unit.
     *
     * The image is indexed in the same space as the vision grid, which is the
     * space the terrain graphics are drawn in, so the terrain shader can sample
     * it with a plain world-position lookup.
     *
     * Only a window around the camera is kept. TA's maps run to 20,000 world
     * units across, and at the artwork's own scale a whole-map image would be
     * hundreds of megabytes and beyond what a texture may be. The window is
     * held a little larger than the view so that scrolling does not rebuild it
     * every frame, and while it stays put only the tiles whose corner codes
     * moved are redrawn.
     */
    class FogRasterizer
    {
    public:
        /**
         * Texels per vision cell. A vision cell is 32 world units across and
         * TA's overlay tiles are 32x32 screen pixels, so this is one texel per
         * world unit and the artwork lands at its authored scale.
         */
        static constexpr int TexelsPerCell = 32;

        /** Slack kept around the view, in tiles, before a scroll moves the window. */
        static constexpr int MarginInTiles = 4;

        /** Written where the ground has been explored but is not currently seen. */
        static constexpr unsigned char UnseenValue = 128;

        /** Written where the ground has never been seen. */
        static constexpr unsigned char UnexploredValue = 255;

        int getWidth() const { return tilesWide * TexelsPerCell; }
        int getHeight() const { return tilesHigh * TexelsPerCell; }
        const unsigned char* getData() const { return pixels.data(); }

        /**
         * Where the image sits, as the texel offset of its top-left corner from
         * the map's top-left corner, in world units. Negative by half a tile
         * where the window reaches the edge of the map, since the overlay tiles
         * straddle the vision grid's corners.
         */
        int getOffsetX() const { return (originX * TexelsPerCell) - (TexelsPerCell / 2); }
        int getOffsetY() const { return (originY * TexelsPerCell) - (TexelsPerCell / 2); }

        struct Update
        {
            /** The window moved or resized, so the whole image is new. */
            bool windowChanged;

            /** Otherwise, the part of the image that changed. */
            GridRegion dirty;
        };

        /**
         * True when the window already covers those cells, so an update can be
         * put off without the edge of the texture coming into view.
         */
        bool covers(const GridRegion& cellsInView) const;

        /**
         * Rebuilds the overlay from a player's vision grids, covering at least
         * cellsInView. Returns nothing at all when the fog is exactly as it was
         * and the window has not had to move.
         */
        std::optional<Update> update(
            const FogTileSet& tiles,
            const Grid<unsigned char>& visible,
            const Grid<unsigned char>& explored,
            const GridRegion& cellsInView);

    private:
        int cellsWide{0};
        int cellsHigh{0};

        /**
         * The overlay entry whose tile's top-left texel is the image's, and how
         * many tiles the image is across. An entry is a corner of the vision
         * grid, so entry (ex, ey) exists for ex in [0, cellsWide].
         */
        int originX{0};
        int originY{0};
        int tilesWide{0};
        int tilesHigh{0};

        /** One byte per texel: 0, UnseenValue or UnexploredValue. */
        std::vector<unsigned char> pixels;

        /**
         * Corner codes of what is currently drawn, with the unexplored layer in
         * the low nibble and the unseen layer in the high one. Covers the
         * window with a one entry ring around it, because artwork on a
         * neighbouring tile can spill over the window's edge.
         */
        std::vector<unsigned char> codes;

        std::vector<unsigned char> computeCodes(const Grid<unsigned char>& visible, const Grid<unsigned char>& explored) const;

        /** Draws a rectangle of overlay entries, which may reach outside the grid. */
        void drawEntries(const FogTileSet& tiles, int entryX, int entryY, int entriesWide, int entriesHigh);
    };
}
