#include "FogTiles.h"

#include <algorithm>
#include <rwe/io/gaf/GafArchive.h>
#include <rwe/util/SpanStream.h>

namespace rwe
{
    /**
     * The 14 fog shapes, in corner-code order. There are four ragged versions
     * of each layer; TA picks between them with the tile's world position so
     * the choice does not shimmer as the camera scrolls.
     */
    static const std::array<std::string, FogTileSet::VariantCount> unexploredSequenceNames{
        "Black1", "Black2", "Black3", "Black4"};
    static const std::array<std::string, FogTileSet::VariantCount> unseenSequenceNames{
        "Gray1", "Gray2", "Gray3", "Gray4"};

    static constexpr int FramesPerVariant = 14;
    static constexpr int TileSize = FogRasterizer::TexelsPerCell;

    namespace
    {
        /** Collects a GAF sequence as coverage masks rather than textures. */
        class FogFrameAdapter : public GafReaderAdapter
        {
        public:
            std::vector<FogTileFrame> frames;

            void beginFrame(const GafFrameEntry&, const GafFrameData& header) override
            {
                current = FogTileFrame();
                current.width = header.width;
                current.height = header.height;
                // The GAF hotspot is negated to give the frame's top-left
                // corner, the same convention the sprite loader uses.
                posX = header.posX;
                posY = header.posY;
                current.offsetX = -posX;
                current.offsetY = -posY;
                current.mask.assign(static_cast<std::size_t>(header.width) * static_cast<std::size_t>(header.height), 0);
            }

            void frameLayer(const LayerData& data) override
            {
                for (unsigned int y = 0; y < data.height; ++y)
                {
                    for (unsigned int x = 0; x < data.width; ++x)
                    {
                        auto index = static_cast<unsigned char>(data.data[(y * data.width) + x]);
                        if (index == data.transparencyKey)
                        {
                            continue;
                        }

                        auto outX = static_cast<int>(x) - (data.x - posX);
                        auto outY = static_cast<int>(y) - (data.y - posY);
                        if (outX < 0 || outX >= current.width || outY < 0 || outY >= current.height)
                        {
                            continue;
                        }

                        current.mask[(outY * current.width) + outX] = 1;
                    }
                }
            }

            void endFrame() override
            {
                frames.push_back(std::move(current));
            }

        private:
            FogTileFrame current;
            int posX{0};
            int posY{0};
        };

        std::optional<FogTileVariant> loadVariant(GafArchive& archive, const std::string& name)
        {
            auto entry = archive.findEntry(name);
            if (!entry)
            {
                return std::nullopt;
            }

            FogFrameAdapter adapter;
            archive.extract(*entry, adapter);
            if (adapter.frames.size() != FramesPerVariant)
            {
                return std::nullopt;
            }

            FogTileVariant variant;
            for (int i = 0; i < FramesPerVariant; ++i)
            {
                variant[i] = std::move(adapter.frames[i]);
            }
            return variant;
        }

        void growBounds(FogTileSet& tiles, const FogTileVariant& variant)
        {
            for (const auto& frame : variant)
            {
                tiles.minX = std::min(tiles.minX, frame.offsetX);
                tiles.minY = std::min(tiles.minY, frame.offsetY);
                tiles.maxX = std::max(tiles.maxX, frame.offsetX + frame.width);
                tiles.maxY = std::max(tiles.maxY, frame.offsetY + frame.height);
            }
        }

        /** A 32x32 stencil covering the quadrants the corner code names. */
        FogTileFrame squareFrame(int code)
        {
            FogTileFrame frame;
            frame.width = TileSize;
            frame.height = TileSize;
            frame.mask.assign(TileSize * TileSize, 0);
            for (int y = 0; y < TileSize; ++y)
            {
                for (int x = 0; x < TileSize; ++x)
                {
                    auto quadrant = 1 << (((y >= TileSize / 2) ? 2 : 0) + ((x >= TileSize / 2) ? 1 : 0));
                    if (code & quadrant)
                    {
                        frame.mask[(y * TileSize) + x] = 1;
                    }
                }
            }
            return frame;
        }
    }

    std::optional<FogTileSet> loadFogTileSet(AbstractVirtualFileSystem& vfs, const std::string& gafName)
    {
        auto bytes = vfs.readFile(gafName);
        if (!bytes)
        {
            return std::nullopt;
        }

        FogTileSet tiles;
        tiles.minX = TileSize;
        tiles.minY = TileSize;
        tiles.maxX = 0;
        tiles.maxY = 0;

        try
        {
            SpanStream stream(bytes->data(), bytes->size());
            GafArchive archive(&stream);

            for (int i = 0; i < FogTileSet::VariantCount; ++i)
            {
                auto black = loadVariant(archive, unexploredSequenceNames[i]);
                auto gray = loadVariant(archive, unseenSequenceNames[i]);
                if (!black || !gray)
                {
                    return std::nullopt;
                }

                tiles.unexplored[i] = std::move(*black);
                tiles.unseen[i] = std::move(*gray);
                growBounds(tiles, tiles.unexplored[i]);
                growBounds(tiles, tiles.unseen[i]);
            }
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }

        // Code 15 is drawn as a solid tile rather than from the artwork, so the
        // bounds must cover a whole tile even if no frame reaches that far.
        tiles.minX = std::min(tiles.minX, 0);
        tiles.minY = std::min(tiles.minY, 0);
        tiles.maxX = std::max(tiles.maxX, TileSize);
        tiles.maxY = std::max(tiles.maxY, TileSize);

        return tiles;
    }

    FogTileSet makeSquareFogTileSet()
    {
        FogTileSet tiles;
        for (int code = 1; code < 15; ++code)
        {
            auto frame = squareFrame(code);
            for (int v = 0; v < FogTileSet::VariantCount; ++v)
            {
                tiles.unexplored[v][code - 1] = frame;
                tiles.unseen[v][code - 1] = frame;
            }
        }
        tiles.minX = 0;
        tiles.minY = 0;
        tiles.maxX = TileSize;
        tiles.maxY = TileSize;
        return tiles;
    }


    namespace
    {
        /**
         * The corner code of one layer at one overlay entry.
         *
         * Entry (ex, ey) is the corner between cells (ex-1, ey-1), (ex, ey-1),
         * (ex-1, ey) and (ex, ey), which the tile's four quadrants show in that
         * order. Cells off the edge of the map count as clear, so the map's
         * border does not grow a fog fringe of its own.
         */
        template <typename IsDark>
        int cornerCode(int cellsWide, int cellsHigh, int ex, int ey, IsDark isDark)
        {
            auto dark = [&](int cx, int cy) {
                return cx >= 0 && cy >= 0 && cx < cellsWide && cy < cellsHigh && isDark(cx, cy);
            };

            int code = 0;
            code |= dark(ex - 1, ey - 1) ? 1 : 0;
            code |= dark(ex, ey - 1) ? 2 : 0;
            code |= dark(ex - 1, ey) ? 4 : 0;
            code |= dark(ex, ey) ? 8 : 0;
            return code;
        }
    }

    std::vector<unsigned char> FogRasterizer::computeCodes(const Grid<unsigned char>& visible, const Grid<unsigned char>& explored) const
    {
        auto codesWide = tilesWide + 2;
        auto codesHigh = tilesHigh + 2;
        std::vector<unsigned char> result(static_cast<std::size_t>(codesWide) * static_cast<std::size_t>(codesHigh), 0);

        auto unexplored = [&](int x, int y) { return explored.get(x, y) == 0; };
        auto unseen = [&](int x, int y) { return visible.get(x, y) == 0; };

        for (int y = 0; y < codesHigh; ++y)
        {
            auto ey = originY - 1 + y;
            for (int x = 0; x < codesWide; ++x)
            {
                auto ex = originX - 1 + x;
                auto packed = cornerCode(cellsWide, cellsHigh, ex, ey, unexplored)
                    | (cornerCode(cellsWide, cellsHigh, ex, ey, unseen) << 4);
                result[(y * codesWide) + x] = static_cast<unsigned char>(packed);
            }
        }

        return result;
    }

    void FogRasterizer::drawEntries(const FogTileSet& tiles, int entryX, int entryY, int entriesWide, int entriesHigh)
    {
        auto width = getWidth();
        auto height = getHeight();
        auto codesWide = tilesWide + 2;

        auto blit = [&](const FogTileFrame& frame, int tileX, int tileY, unsigned char value) {
            for (int y = 0; y < frame.height; ++y)
            {
                auto py = tileY + frame.offsetY + y;
                if (py < 0 || py >= height)
                {
                    continue;
                }
                const auto* row = frame.mask.data() + (static_cast<std::size_t>(y) * frame.width);
                auto* dest = pixels.data() + (static_cast<std::size_t>(py) * width);
                for (int x = 0; x < frame.width; ++x)
                {
                    if (!row[x])
                    {
                        continue;
                    }
                    auto px = tileX + frame.offsetX + x;
                    if (px < 0 || px >= width)
                    {
                        continue;
                    }
                    // Highest value wins, so the never-seen layer always shows
                    // through the remembered one whichever order they arrive in.
                    dest[px] = std::max(dest[px], value);
                }
            }
        };

        auto fill = [&](int tileX, int tileY, unsigned char value) {
            auto x0 = std::max(0, tileX);
            auto x1 = std::min(width, tileX + TileSize);
            if (x1 <= x0)
            {
                return;
            }
            for (int y = std::max(0, tileY); y < std::min(height, tileY + TileSize); ++y)
            {
                auto* dest = pixels.data() + (static_cast<std::size_t>(y) * width);
                if (value == UnexploredValue)
                {
                    // Nothing can outrank it, so no need to read what is there.
                    std::fill(dest + x0, dest + x1, value);
                    continue;
                }
                for (int x = x0; x < x1; ++x)
                {
                    dest[x] = std::max(dest[x], value);
                }
            }
        };

        for (int ey = entryY; ey < entryY + entriesHigh; ++ey)
        {
            for (int ex = entryX; ex < entryX + entriesWide; ++ex)
            {
                auto packed = codes[((ey - originY + 1) * codesWide) + (ex - originX + 1)];
                if (packed == 0)
                {
                    continue;
                }

                auto tileX = (ex - originX) * TexelsPerCell;
                auto tileY = (ey - originY) * TexelsPerCell;

                // Which of the four ragged versions this tile uses is fixed in
                // world space, so scrolling never reshuffles the fog.
                auto variant = static_cast<unsigned int>((ex + ey) & (FogTileSet::VariantCount - 1));

                auto unexploredCode = packed & 0xf;
                auto unseenCode = (packed >> 4) & 0xf;

                // Ground never seen is a solid black tile that hides whatever
                // the remembered layer would have drawn under it. Skipping that
                // layer is most of the work over unexplored country.
                if (unseenCode != 0 && unexploredCode != 15)
                {
                    if (unseenCode == 15)
                    {
                        fill(tileX, tileY, UnseenValue);
                    }
                    else
                    {
                        blit(tiles.unseen[variant][unseenCode - 1], tileX, tileY, UnseenValue);
                    }
                }

                if (unexploredCode == 15)
                {
                    fill(tileX, tileY, UnexploredValue);
                }
                else if (unexploredCode != 0)
                {
                    blit(tiles.unexplored[variant][unexploredCode - 1], tileX, tileY, UnexploredValue);
                }
            }
        }
    }

    namespace
    {
        /**
         * The entries needed to cover a rectangle of cells. An entry is a
         * corner, so each cell needs the corners on both of its sides: covering
         * cells [x, x + width) takes entries [x, x + width].
         */
        void entriesNeeded(const GridRegion& cells, int cellsWide, int cellsHigh, int& x0, int& y0, int& x1, int& y1)
        {
            x0 = std::clamp(cells.x, 0, cellsWide);
            y0 = std::clamp(cells.y, 0, cellsHigh);
            x1 = std::clamp(cells.x + cells.width, 0, cellsWide);
            y1 = std::clamp(cells.y + cells.height, 0, cellsHigh);
        }
    }

    bool FogRasterizer::covers(const GridRegion& cellsInView) const
    {
        if (pixels.empty())
        {
            return false;
        }

        int x0, y0, x1, y1;
        entriesNeeded(cellsInView, cellsWide, cellsHigh, x0, y0, x1, y1);
        return x0 >= originX
            && y0 >= originY
            && x1 <= originX + tilesWide - 1
            && y1 <= originY + tilesHigh - 1;
    }

    std::optional<FogRasterizer::Update> FogRasterizer::update(
        const FogTileSet& tiles,
        const Grid<unsigned char>& visible,
        const Grid<unsigned char>& explored,
        const GridRegion& cellsInView)
    {
        auto newCellsWide = explored.getWidth();
        auto newCellsHigh = explored.getHeight();
        auto entriesWide = newCellsWide + 1;
        auto entriesHigh = newCellsHigh + 1;

        int needX0, needY0, needX1, needY1;
        entriesNeeded(cellsInView, newCellsWide, newCellsHigh, needX0, needY0, needX1, needY1);

        auto windowChanged = newCellsWide != cellsWide
            || newCellsHigh != cellsHigh
            || pixels.empty()
            || needX0 < originX
            || needY0 < originY
            || needX1 > originX + tilesWide - 1
            || needY1 > originY + tilesHigh - 1;

        if (windowChanged)
        {
            cellsWide = newCellsWide;
            cellsHigh = newCellsHigh;

            // Re-centre on what is needed with slack on every side, so a scroll
            // can carry on for a while before the window has to move again.
            originX = std::max(0, needX0 - MarginInTiles);
            originY = std::max(0, needY0 - MarginInTiles);
            tilesWide = std::min(entriesWide, needX1 + MarginInTiles + 1) - originX;
            tilesHigh = std::min(entriesHigh, needY1 + MarginInTiles + 1) - originY;

            pixels.assign(static_cast<std::size_t>(getWidth()) * static_cast<std::size_t>(getHeight()), 0);
            codes = computeCodes(visible, explored);
            drawEntries(tiles, originX - 1, originY - 1, tilesWide + 2, tilesHigh + 2);
            return Update{true, GridRegion(0, 0, getWidth(), getHeight())};
        }

        auto newCodes = computeCodes(visible, explored);

        // Only the entries whose code changed need redrawing, which while the
        // camera sits still is usually a patch or two of the window.
        auto codesWide = tilesWide + 2;
        auto codesHigh = tilesHigh + 2;
        int minX = codesWide;
        int minY = codesHigh;
        int maxX = -1;
        int maxY = -1;
        for (int y = 0; y < codesHigh; ++y)
        {
            const auto* a = codes.data() + (static_cast<std::size_t>(y) * codesWide);
            const auto* b = newCodes.data() + (static_cast<std::size_t>(y) * codesWide);
            for (int x = 0; x < codesWide; ++x)
            {
                if (a[x] != b[x])
                {
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }

        codes = std::move(newCodes);

        if (maxX < 0)
        {
            return std::nullopt;
        }

        // Everything those entries could have painted, clipped to the image.
        // Code index x is entry originX - 1 + x, whose tile starts at texel
        // (x - 1) * TexelsPerCell.
        auto pixelLeft = std::max(0, ((minX - 1) * TexelsPerCell) + tiles.minX);
        auto pixelTop = std::max(0, ((minY - 1) * TexelsPerCell) + tiles.minY);
        auto pixelRight = std::min(getWidth(), ((maxX - 1) * TexelsPerCell) + tiles.maxX);
        auto pixelBottom = std::min(getHeight(), ((maxY - 1) * TexelsPerCell) + tiles.maxY);
        if (pixelRight <= pixelLeft || pixelBottom <= pixelTop)
        {
            return std::nullopt;
        }

        for (int y = pixelTop; y < pixelBottom; ++y)
        {
            auto* dest = pixels.data() + (static_cast<std::size_t>(y) * getWidth());
            std::fill(dest + pixelLeft, dest + pixelRight, static_cast<unsigned char>(0));
        }

        // Neighbouring tiles' artwork spills into the cleared area, so they are
        // redrawn too. Their own pixels outside it are simply painted again.
        auto redrawX0 = std::max(0, minX - 1);
        auto redrawY0 = std::max(0, minY - 1);
        auto redrawX1 = std::min(codesWide - 1, maxX + 1);
        auto redrawY1 = std::min(codesHigh - 1, maxY + 1);
        drawEntries(
            tiles,
            originX - 1 + redrawX0,
            originY - 1 + redrawY0,
            redrawX1 - redrawX0 + 1,
            redrawY1 - redrawY0 + 1);

        return Update{false, GridRegion(pixelLeft, pixelTop, pixelRight - pixelLeft, pixelBottom - pixelTop)};
    }
}
