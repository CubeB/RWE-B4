#include "WireframeScan.h"

#include <algorithm>
#include <cmath>

namespace rwe
{
    namespace
    {
        struct RowEnd
        {
            bool set{false};
            float x{0.0f};
            std::size_t from{0};
            std::size_t to{0};
            float t{0.0f};
        };

        int firstPixelAtOrPast(float coordinate)
        {
            return static_cast<int>(std::ceil(coordinate - 0.5f));
        }

        /**
         * Walks one chain of edges from the top corner to the bottom one,
         * stepping round the corners by step, and records where it crosses
         * each row. As in the original, an edge that does not go down the
         * screen is passed over.
         */
        void walkChain(const std::vector<Vector2f>& corners, std::size_t top, std::size_t bottom, std::size_t step, int firstRow, std::vector<RowEnd>& rows)
        {
            auto count = corners.size();
            auto endRow = firstRow + static_cast<int>(rows.size());
            auto a = top;
            while (a != bottom)
            {
                auto b = (a + step) % count;
                const auto& pa = corners[a];
                const auto& pb = corners[b];
                if (pb.y > pa.y)
                {
                    auto rowA = std::max(firstPixelAtOrPast(pa.y), firstRow);
                    auto rowB = std::min(firstPixelAtOrPast(pb.y), endRow);
                    for (auto row = rowA; row < rowB; ++row)
                    {
                        auto t = ((static_cast<float>(row) + 0.5f) - pa.y) / (pb.y - pa.y);
                        rows[row - firstRow] = RowEnd{true, pa.x + (t * (pb.x - pa.x)), a, b, t};
                    }
                }
                a = b;
            }
        }
    }

    void scanWireframePolygon(const std::vector<Vector2f>& corners, std::vector<WireframePixel>& out)
    {
        auto count = corners.size();
        if (count < 3)
        {
            return;
        }

        // The first corner with the least y and the first with the greatest,
        // as 0x4C0820 picks them.
        std::size_t top = 0;
        std::size_t bottom = 0;
        for (std::size_t i = 1; i < count; ++i)
        {
            if (corners[i].y < corners[top].y)
            {
                top = i;
            }
            if (corners[i].y > corners[bottom].y)
            {
                bottom = i;
            }
        }

        auto firstRow = firstPixelAtOrPast(corners[top].y);
        auto endRow = firstPixelAtOrPast(corners[bottom].y);
        if (endRow <= firstRow)
        {
            return;
        }

        std::vector<RowEnd> left(static_cast<std::size_t>(endRow - firstRow));
        std::vector<RowEnd> right(left.size());
        walkChain(corners, top, bottom, count - 1, firstRow, left);
        walkChain(corners, top, bottom, 1, firstRow, right);

        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto& l = left[i];
            const auto& r = right[i];
            if (!l.set || !r.set)
            {
                continue;
            }
            auto x0 = firstPixelAtOrPast(l.x);
            auto x1 = firstPixelAtOrPast(r.x);
            if (x1 - x0 <= 0)
            {
                continue;
            }
            auto row = firstRow + static_cast<int>(i);
            out.push_back(WireframePixel{x0, row, l.from, l.to, l.t, l.x});
            out.push_back(WireframePixel{x1, row, r.from, r.to, r.t, r.x});
        }
    }
}
