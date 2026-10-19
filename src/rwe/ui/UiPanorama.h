#pragma once
#include <cstdint>
#include <rwe/rwe_time.h>
#include <rwe/ui/UiComponent.h>
#include <rwe/render/SpriteSeries.h>

namespace rwe
{
    // BT-267; docs/agents/factory-death-and-briefing.md. Presentation only.
    struct PanoramaScrollState
    {
        unsigned int offset{0};
        std::uint64_t deadline{0};
        void advance(std::uint64_t now, std::uint64_t deadlineNow, unsigned int width);
    };

    std::vector<int> panoramaTileOffsets(const std::vector<unsigned int>& widths, unsigned int offset);

    class UiPanorama : public UiComponent
    {
        std::shared_ptr<SpriteSeries> frames;
        std::shared_ptr<SpriteSeries> masks;
        unsigned int side;
        TimeService* clock;
        std::shared_ptr<PanoramaScrollState> scroll;
        std::vector<unsigned int> widths;
        unsigned int totalWidth{0};

    public:
        UiPanorama(int x, int y, unsigned int width, unsigned int height,
            std::shared_ptr<SpriteSeries> frames, std::shared_ptr<SpriteSeries> masks,
            unsigned int side, TimeService* clock, std::shared_ptr<PanoramaScrollState> scroll);
        ~UiPanorama() override { releaseSubscriptions(); }
        void update(float dt) override;
        void render(UiRenderService& context) const override;
    };
}
