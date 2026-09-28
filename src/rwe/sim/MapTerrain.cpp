#include "MapTerrain.h"
#include <algorithm>
#include <cmath>
#include <rwe/geometry/Plane3f.h>
#include <rwe/geometry/Triangle3f.h>
#include <rwe/sim/movement.h>

namespace rwe
{
    namespace
    {
        /**
         * The per-cell slope and water-depth grids, from the heightmap and
         * the sea level alone.
         *
         * Neither is invalidated, and that is not a discipline anyone has to
         * keep: the heightmap is private, is handed out as a const reference
         * and is set once in the constructor, so there is no path by which
         * a cell could change under a cache that had already read it. What
         * the cache is therefore free of is any question of when to rebuild
         * -- and being derived state that provably cannot move the
         * simulation's future, it is neither saved nor hashed, for the same
         * reason MapIntel is.
         *
         * The sea level arrives as a SimScalar off a map header and is
         * cast to an unsigned int, so it can in principle exceed 255 even
         * though no heightmap cell ever will. A depth that large does not
         * fit a byte, so the depth grid is left EMPTY in that case and the
         * queries that want one fall back to reading the heightmap. An
         * empty grid rather than a truncated one: the movement classes
         * compare a depth against MinWaterDepth and MaxWaterDepth, so a
         * depth of 300 wrapped to 44 would be a different answer, not a
         * slower one.
         */
        Grid<unsigned char> buildSlopeMap(const Grid<unsigned char>& heights)
        {
            // One cell narrower on each axis: the 2x2 block at the last row
            // or column would read past the map, which is the same bound
            // getSlope has always had -- MovementClassCollisionService and
            // ReachabilityMap both size their scans off it.
            const int width = std::max(0, heights.getWidth() - 1);
            const int height = std::max(0, heights.getHeight() - 1);
            Grid<unsigned char> slopes(width, height, static_cast<unsigned char>(0));
            if (width <= 0 || height <= 0)
            {
                return slopes;
            }

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    // Written the way movement.cpp's getSlope reads it --
                    // min and max over the same four corners -- rather than
                    // as two separable row passes, because the point is
                    // that this is that function's arithmetic done once
                    // instead of per query.
                    const auto a = static_cast<unsigned int>(heights.get(x, y));
                    const auto b = static_cast<unsigned int>(heights.get(x + 1, y));
                    const auto c = static_cast<unsigned int>(heights.get(x, y + 1));
                    const auto d = static_cast<unsigned int>(heights.get(x + 1, y + 1));
                    const auto lowest = std::min(std::min(a, b), std::min(c, d));
                    const auto highest = std::max(std::max(a, b), std::max(c, d));
                    slopes.set(x, y, static_cast<unsigned char>(highest - lowest));
                }
            }

            return slopes;
        }

        Grid<unsigned char> buildWaterDepthMap(const Grid<unsigned char>& heights, unsigned int waterLevel)
        {
            if (waterLevel > 255u)
            {
                // See the note above: no depth that large can be held, so
                // there is no cache and every query reads the heightmap.
                return Grid<unsigned char>();
            }

            Grid<unsigned char> depths(
                heights.getWidth(),
                heights.getHeight(),
                static_cast<unsigned char>(0));
            for (int y = 0; y < heights.getHeight(); ++y)
            {
                for (int x = 0; x < heights.getWidth(); ++x)
                {
                    const auto height = static_cast<unsigned int>(heights.get(x, y));
                    depths.set(x, y, static_cast<unsigned char>(height < waterLevel ? waterLevel - height : 0u));
                }
            }

            return depths;
        }
    }

    MapTerrain::MapTerrain(
        Grid<unsigned char>&& heights,
        SimScalar seaLevel)
        : heights(std::move(heights)),
          slopeCache(buildSlopeMap(this->heights)),
          waterDepthCache(buildWaterDepthMap(this->heights, simScalarToUInt(seaLevel))),
          seaLevel(seaLevel)
    {
    }

    Point MapTerrain::worldToHeightmapCoordinate(const SimVector& position) const
    {
        auto heightPos = worldToHeightmapSpace(position);
        return Point(static_cast<int>(heightPos.x.value), static_cast<int>(heightPos.z.value));
    }

    Point MapTerrain::worldToHeightmapCoordinateNearest(const SimVector& position) const
    {
        auto heightPos = worldToHeightmapSpace(position);
        return Point(roundToInt(heightPos.x), roundToInt(heightPos.z));
    }

    SimVector MapTerrain::heightmapIndexToWorldCorner(int x, int y) const
    {
        return heightmapToWorldSpace(SimVector(SimScalar(x), 0_ss, SimScalar(y)));
    }

    SimVector MapTerrain::heightmapIndexToWorldCorner(Point p) const
    {
        return heightmapIndexToWorldCorner(p.x, p.y);
    }

    SimVector MapTerrain::heightmapIndexToWorldCenter(int x, int y) const
    {
        return heightmapToWorldSpace(SimVector(SimScalar(x) + 0.5_ssf, 0_ss, SimScalar(y) + 0.5_ssf));
    }

    SimVector MapTerrain::heightmapIndexToWorldCenter(std::size_t x, std::size_t y) const
    {
        return heightmapToWorldSpace(SimVector(SimScalar(x) + 0.5_ssf, 0_ss, SimScalar(y) + 0.5_ssf));
    }

    SimVector MapTerrain::heightmapIndexToWorldCenter(Point p) const
    {
        return heightmapIndexToWorldCenter(p.x, p.y);
    }

    SimVector MapTerrain::worldToHeightmapSpace(const SimVector& v) const
    {
        auto widthInWorldUnits = getWidthInWorldUnits();
        auto heightInWorldUnits = getHeightInWorldUnits();
        auto heightX = (v.x + (widthInWorldUnits / 2_ss)) / HeightTileWidthInWorldUnits;
        auto heightZ = (v.z + (heightInWorldUnits / 2_ss)) / HeightTileHeightInWorldUnits;

        return SimVector(heightX, v.y, heightZ);
    }

    SimVector MapTerrain::heightmapToWorldSpace(const SimVector& v) const
    {
        auto widthInWorldUnits = getWidthInWorldUnits();
        auto heightInWorldUnits = getHeightInWorldUnits();
        auto worldX = (v.x * HeightTileWidthInWorldUnits) - (widthInWorldUnits / 2_ss);
        auto worldZ = (v.z * HeightTileHeightInWorldUnits) - (heightInWorldUnits / 2_ss);

        return SimVector(worldX, v.y, worldZ);
    }

    SimScalar MapTerrain::leftInWorldUnits() const
    {
        return -((intToSimScalar(heights.getWidth()) / 2_ss) * HeightTileWidthInWorldUnits);
    }

    SimScalar MapTerrain::rightCutoffInWorldUnits() const
    {
        auto right = (intToSimScalar(heights.getWidth()) / 2_ss) * HeightTileWidthInWorldUnits;
        return right - (HeightTileWidthInWorldUnits * 2_ss);
    }

    SimScalar MapTerrain::topInWorldUnits() const
    {
        return -((intToSimScalar(heights.getHeight()) / 2_ss) * HeightTileHeightInWorldUnits);
    }

    SimScalar MapTerrain::bottomCutoffInWorldUnits() const
    {
        auto bottom = (intToSimScalar(heights.getHeight()) / 2_ss) * HeightTileHeightInWorldUnits;
        return bottom - (HeightTileHeightInWorldUnits * 8_ss);
    }

    const Grid<unsigned char>& MapTerrain::getHeightMap() const
    {
        return heights;
    }

    const Grid<unsigned char>& MapTerrain::getSlopeMap() const
    {
        return slopeCache;
    }

    const Grid<unsigned char>& MapTerrain::getWaterDepthMap() const
    {
        return waterDepthCache;
    }

    unsigned int MapTerrain::getWaterDepthAt(int x, int y) const
    {
        if (waterDepthCache.getWidth() > 0)
        {
            return waterDepthCache.get(x, y);
        }

        // The sea level is above 255, so there is no cache; read the
        // heightmap and subtract, as every caller did before there was one.
        // movement::getWaterDepth rather than the arithmetic open-coded, so
        // the cached grid and this cannot drift apart.
        return getWaterDepth(heights, simScalarToUInt(seaLevel), static_cast<unsigned int>(x), static_cast<unsigned int>(y));
    }

    SimScalar MapTerrain::getWidthInWorldUnits() const
    {
        return intToSimScalar(heights.getWidth()) * HeightTileWidthInWorldUnits;
    }

    SimScalar MapTerrain::getHeightInWorldUnits() const
    {
        return intToSimScalar(heights.getHeight()) * HeightTileHeightInWorldUnits;
    }

    SimVector MapTerrain::topLeftCoordinateToWorld(const SimVector& pos) const
    {
        return SimVector(
            pos.x - (getWidthInWorldUnits() / 2_ss),
            pos.y,
            pos.z - (getHeightInWorldUnits() / 2_ss));
    }

    bool MapTerrain::isSquareUnderSea(SimScalar x, SimScalar z) const
    {
        // A square is the cell between four heightmap corners, which is what
        // isInHeightMapBounds admits, so the last row and column of corners
        // start no square and read as off the map, as they do to
        // tryGetHeightAt.
        auto cell = worldToHeightmapCoordinate(SimVector(x, 0_ss, z));
        if (!isInHeightMapBounds(cell.x, cell.y))
        {
            return false;
        }

        // The highest of the square's four corners (0x4832D8-0x483329).
        auto high = std::max(
            std::max(heights.get(cell.x, cell.y), heights.get(cell.x + 1, cell.y)),
            std::max(heights.get(cell.x, cell.y + 1), heights.get(cell.x + 1, cell.y + 1)));

        return SimScalar(static_cast<float>(high)) < seaLevel;
    }

    SimScalar MapTerrain::getHeightAt(SimScalar x, SimScalar z) const
    {
        return tryGetHeightAt(x, z).value_or(0_ss);
    }

    std::optional<SimScalar> MapTerrain::tryGetHeightAt(SimScalar x, SimScalar z) const
    {
        auto tilePos = worldToHeightmapCoordinate(SimVector(x, 0_ss, z));
        if (
            tilePos.x < 0
            || tilePos.x >= heights.getWidth() - 1
            || tilePos.y < 0
            || tilePos.y >= heights.getHeight() - 1)
        {
            return 0_ss;
        }

        Line3x<SimScalar> line(SimVector(x, MaxHeight, z), SimVector(x, MinHeight, z));
        auto pos = intersectLine(line);
        if (!pos)
        {
            return std::nullopt;
        }
        return pos->y;
    }

    std::optional<SimVector> MapTerrain::intersectLine(const Line3x<SimScalar>& line) const
    {
        auto heightmapPosition = worldToHeightmapSpace(line.start);
        Point startCell(static_cast<int>(heightmapPosition.x.value), static_cast<int>(heightmapPosition.z.value));

        auto ray = Ray3x<SimScalar>::fromLine(line);

        auto xDirection = ray.direction.x > 0_ss ? 1 : -1;
        auto zDirection = ray.direction.z > 0_ss ? 1 : -1;
        auto xPlaneOffset = (HeightTileWidthInWorldUnits / 2_ss) * intToSimScalar(xDirection);
        auto zPlaneOffset = (HeightTileHeightInWorldUnits / 2_ss) * intToSimScalar(zDirection);

        while (true)
        {
            auto neighbourX = (heightmapPosition.x - intToSimScalar(startCell.x)) > 0.5_ssf ? 1 : -1;
            auto neighbourY = (heightmapPosition.z - intToSimScalar(startCell.y)) > 0.5_ssf ? 1 : -1;

            // Due to floating-point precision issues,
            // when the line passes very close to a cell boundary
            // the "intersectWithHeightmapCell" check may fail,
            // evaluating the line as passing outside the cell,
            // even if the line never leaves the cell according to the values of "startCell".
            // To guard against this, we check collision with the closest four cells,
            // ensuring that the line cannot escape through any "cracks".
            if (isInHeightMapBounds(startCell.x, startCell.y))
            {
                auto cellIntersect = intersectWithHeightmapCell(line, startCell.x, startCell.y);
                if (cellIntersect)
                {
                    return *cellIntersect;
                }
            }

            if (isInHeightMapBounds(startCell.x + neighbourX, startCell.y))
            {
                auto cellIntersect = intersectWithHeightmapCell(line, startCell.x + neighbourX, startCell.y);
                if (cellIntersect)
                {
                    return *cellIntersect;
                }
            }

            if (isInHeightMapBounds(startCell.x, startCell.y + neighbourY))
            {
                auto cellIntersect = intersectWithHeightmapCell(line, startCell.x, startCell.y + neighbourY);
                if (cellIntersect)
                {
                    return *cellIntersect;
                }
            }

            if (isInHeightMapBounds(startCell.x + neighbourX, startCell.y + neighbourY))
            {
                auto cellIntersect = intersectWithHeightmapCell(line, startCell.x + neighbourX, startCell.y + neighbourY);
                if (cellIntersect)
                {
                    return *cellIntersect;
                }
            }

            auto cellPos = heightmapIndexToWorldCenter(startCell);

            Plane3x<SimScalar> xPlane(SimVector(cellPos.x + xPlaneOffset, 0_ss, 0_ss), SimVector(1_ss, 0_ss, 0_ss));
            auto xIntersect = xPlane.intersect(ray);

            Plane3x<SimScalar> zPlane(SimVector(0_ss, 0_ss, cellPos.z + zPlaneOffset), SimVector(0_ss, 0_ss, 1_ss));
            auto zIntersect = zPlane.intersect(ray);

            if (!xIntersect && !zIntersect)
            {
                return std::nullopt;
            }

            if (xIntersect && (!zIntersect || *xIntersect < *zIntersect))
            {
                if (*xIntersect > 1_ss)
                {
                    return std::nullopt;
                }

                startCell.x += xDirection;
                heightmapPosition.x += intToSimScalar(xDirection);
            }
            else
            {
                if (*zIntersect > 1_ss)
                {
                    return std::nullopt;
                }

                startCell.y += zDirection;
                heightmapPosition.z += intToSimScalar(zDirection);
            }
        }
    }

    std::optional<SimVector> MapTerrain::intersectWithHeightmapCell(const Line3x<SimScalar>& line, int x, int y) const
    {
        auto posTopLeft = heightmapIndexToWorldCorner(x, y);
        posTopLeft.y = SimScalar(heights.get(x, y));

        auto posTopRight = heightmapIndexToWorldCorner(x + 1, y);
        posTopRight.y = SimScalar(heights.get(x + 1, y));

        auto posBottomLeft = heightmapIndexToWorldCorner(x, y + 1);
        posBottomLeft.y = SimScalar(heights.get(x, y + 1));

        auto posBottomRight = heightmapIndexToWorldCorner(x + 1, y + 1);
        posBottomRight.y = SimScalar(heights.get(x + 1, y + 1));

        auto midHeight = (posTopLeft.y + posTopRight.y + posBottomLeft.y + posBottomRight.y) / 4_ss;
        auto posMiddle = heightmapIndexToWorldCenter(x, y);
        posMiddle.y = midHeight;

        Triangle3x<SimScalar> left, bottom, right, top;

        // For robust collision testing under floating point arithmetic,
        // we ensure that the direction of any edge shared by two triangles
        // is the same in both triangles.
        // When this is true, the intersectLine intersection test
        // guarantees that a line passing exactly through the edge
        // will intersect at least one of the triangles.
        if (std::abs(y - x) % 2 == 0) // checkerboard pattern
        {
            left = Triangle3x<SimScalar>(posTopLeft, posMiddle, posBottomLeft);
            bottom = Triangle3x<SimScalar>(posBottomLeft, posBottomRight, posMiddle);
            right = Triangle3x<SimScalar>(posBottomRight, posMiddle, posTopRight);
            top = Triangle3x<SimScalar>(posTopRight, posTopLeft, posMiddle);
        }
        else
        {
            left = Triangle3x<SimScalar>(posTopLeft, posBottomLeft, posMiddle);
            bottom = Triangle3x<SimScalar>(posBottomLeft, posMiddle, posBottomRight);
            right = Triangle3x<SimScalar>(posBottomRight, posTopRight, posMiddle);
            top = Triangle3x<SimScalar>(posTopRight, posMiddle, posTopLeft);
        }

        auto result = left.intersectLine(line);
        result = closestTo(line.start, result, bottom.intersectLine(line));
        result = closestTo(line.start, result, right.intersectLine(line));
        result = closestTo(line.start, result, top.intersectLine(line));

        return result;
    }

    SimScalar MapTerrain::getSeaLevel() const
    {
        return seaLevel;
    }

    bool MapTerrain::isInHeightMapBounds(int x, int y) const
    {
        return x >= 0
            && y >= 0
            && x < heights.getWidth() - 1
            && y < heights.getHeight() - 1;
    }
}
