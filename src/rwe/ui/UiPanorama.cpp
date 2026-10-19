#include "UiPanorama.h"
#include <numeric>

namespace rwe
{
    void PanoramaScrollState::advance(std::uint64_t now, std::uint64_t deadlineNow, unsigned int width)
    {
        // A strict deadline and one step, even after a long pause (BT-267).
        if (width && now > deadline)
        {
            ++offset;
            if (offset >= width)
            {
                offset = 0;
            }
            deadline = deadlineNow + 2;
        }
    }

    std::vector<int> panoramaTileOffsets(const std::vector<unsigned int>& widths, unsigned int offset)
    {
        std::vector<int> positions;
        if (widths.empty())
        {
            return positions;
        }
        int x = -static_cast<int>(offset);
        for (auto width : widths)
        {
            positions.push_back(x);
            x += static_cast<int>(width);
        }
        positions.push_back(x); // Exactly one extra frame zero, as in BT-267.
        return positions;
    }

    UiPanorama::UiPanorama(int x, int y, unsigned int width, unsigned int height,
        std::shared_ptr<SpriteSeries> frames, std::shared_ptr<SpriteSeries> masks,
        unsigned int side, TimeService* clock, std::shared_ptr<PanoramaScrollState> scroll)
        : UiComponent(x, y, width, height), frames(std::move(frames)), masks(std::move(masks)),
          side(side), clock(clock), scroll(std::move(scroll))
    {
        if (this->frames)
        {
            for (const auto& sprite : this->frames->sprites)
            {
                widths.push_back(static_cast<unsigned int>(sprite->bounds.width()));
            }
        }
        totalWidth = std::accumulate(widths.begin(), widths.end(), 0u);
    }

    void UiPanorama::update(float)
    {
        const auto now = static_cast<std::uint64_t>(clock->getTicks()) * 30 / 1000;
        if (now > scroll->deadline)
        {
            const auto deadlineNow = static_cast<std::uint64_t>(clock->getTicks()) * 30 / 1000;
            scroll->advance(now, deadlineNow, totalWidth);
        }
    }

    void UiPanorama::render(UiRenderService& context) const
    {
        if (widths.empty())
        {
            return;
        }
        // Original full briefing surface, not a clip to the gadget (BT-267).
        const auto surface = Rectangle2f::fromTopLeft(0, 0, 640, 480);
        const auto positions = panoramaTileOffsets(widths, scroll->offset);
        for (std::size_t i = 0; i < positions.size(); ++i)
        {
            const auto frame = i == widths.size() ? 0 : i;
            context.drawSpriteAbsClipped(posX + positions[i], posY, *frames->sprites[frame], surface);
        }
        if (masks && side < masks->sprites.size())
        {
            context.drawSpriteAbsClipped(0, 0, *masks->sprites[side], surface);
        }
    }
}
