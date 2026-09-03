#pragma once

#include <memory>
#include <optional>
#include <rwe/ui/UiComponent.h>

namespace rwe
{
    class UiSurface : public UiComponent
    {
    private:
        std::optional<std::shared_ptr<Sprite>> background;

    public:
        UiSurface(int posX, int posY, unsigned int sizeX, unsigned int sizeY);
        UiSurface(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<Sprite> background);

        void render(UiRenderService& context) const override;

        /**
         * A surface is scenery -- the options screens carry their background
         * art as full-panel picture boxes, and a background that answers the
         * hit test swallows every click meant for the buttons drawn over it.
         */
        bool contains(int, int) const override
        {
            return false;
        }

        void setBackground(std::shared_ptr<Sprite> newBackground);
        void clearBackground();
    };
}
