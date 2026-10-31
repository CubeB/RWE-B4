#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The size of a `+MakePoster` capture, in output pixels. The original's
     * poster is a world image rather than a viewport grab
     * (BT-GAME-CONSOLE-COMMANDS-PARITY.md section 6), so one output pixel is
     * one map pixel (one world unit) here; the camera's zoom is not folded in.
     */
    struct PosterSize
    {
        unsigned int width{0};
        unsigned int height{0};
    };

    /**
     * The largest poster this build will attempt. The original's map pixel
     * size is a map dimension in world units, and a 63x63-tile map is 32256
     * world units across, which would be a one-billion-pixel image. The cap
     * keeps a capture bounded in time and file size; a request over it is
     * refused with a message rather than started.
     *
     * The PCX format's own limit is 65535 a side, which this is well under.
     * Tiles are streamed, so video memory does not grow with the poster.
     */
    constexpr unsigned int MaxPosterDimension = 16384;
    constexpr unsigned long long MaxPosterPixels = 16384ull * 16384ull;

    /** The side of one off-screen capture tile. Constant, so memory is bounded by width x tile. */
    constexpr unsigned int PosterTileSize = 512;

    /**
     * The smallest poster dimension the original allows: the visible world
     * span rounded up to whole sixteen-world-unit tiles. A zoomed-out view
     * therefore raises the minimum; a zoomed-in one lowers it.
     */
    unsigned int posterMinimumDimension(float visibleWorldSpan);

    /**
     * Raises each requested side to the minimum and lowers it to the map's
     * own pixel size, per side. A map smaller than the minimum wins, because
     * there is nothing beyond it to draw.
     */
    PosterSize clampPosterSize(
        unsigned int requestedWidth,
        unsigned int requestedHeight,
        unsigned int mapWidth,
        unsigned int mapHeight,
        unsigned int minimumWidth,
        unsigned int minimumHeight);

    /** Whether a size is within this build's hard dimension and pixel caps. */
    bool posterFitsLimits(PosterSize size);

    /**
     * Where the poster's top-left world corner lands. The rectangle is
     * centred on the view and then moved inside the map; a rectangle as large
     * as the map starts at the map's own corner.
     */
    float clampPosterAxis(float centre, unsigned int size, float mapMinimum, float mapMaximum);

    /** The name the original writes: a `BIGSHOT` bitmap. RWE keeps the stem and its own PCX format. */
    std::string posterFileName();

    /**
     * A streaming 24-bit PCX writer, so a large poster never has to be held
     * in memory as one image. Rows are written top down; each is three
     * planes, run-length encoded with the same rules as encodePcx24.
     *
     * The constructor creates the parent directory and writes the header. If
     * that fails, isOpen() is false. finish() reports whether every row and
     * the final flush succeeded; a failed writer can be discarded with
     * removePartial(), which closes it and deletes the partial file.
     */
    class PosterPcxWriter
    {
    public:
        PosterPcxWriter(std::filesystem::path path, unsigned int width, unsigned int height);
        ~PosterPcxWriter();

        PosterPcxWriter(const PosterPcxWriter&) = delete;
        PosterPcxWriter& operator=(const PosterPcxWriter&) = delete;

        bool isOpen() const { return out.is_open(); }

        /** `rgb` is width * 3 bytes, top-down within the row. */
        bool writeRow(const std::uint8_t* rgb);

        bool finish();

        void removePartial();

        const std::filesystem::path& path() const { return destination; }

    private:
        std::filesystem::path destination;
        std::ofstream out;
        unsigned int width;
        unsigned int bytesPerLine;
        std::vector<std::uint8_t> plane;
        /** True once this writer opened (and truncated) `destination`. */
        bool ownsFile{false};
        bool finished{false};
    };
}
