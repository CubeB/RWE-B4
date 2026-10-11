#include "NinePatch.h"
#include <algorithm>

namespace rwe
{
    std::vector<NinePatchTile> layoutNinePatch(int width, int height, int stepX, int stepY)
    {
        std::vector<NinePatchTile> tiles;
        if (width <= 0 || height <= 0 || stepX <= 0 || stepY <= 0)
        {
            return tiles;
        }

        const auto columns = std::max(1, (width + stepX - 1) / stepX);
        const auto rows = std::max(1, (height + stepY - 1) / stepY);

        for (int row = 0; row < rows; ++row)
        {
            const bool lastRow = row == rows - 1 && rows > 1;
            const int rowKind = row == 0 ? 0 : (lastRow ? 2 : 1);
            const int y = lastRow ? height - stepY : row * stepY;
            for (int column = 0; column < columns; ++column)
            {
                const bool lastColumn = column == columns - 1 && columns > 1;
                const int columnKind = column == 0 ? 0 : (lastColumn ? 2 : 1);
                const int x = lastColumn ? width - stepX : column * stepX;
                tiles.push_back(NinePatchTile{x, y, rowKind * 3 + columnKind});
            }
        }
        return tiles;
    }
}
