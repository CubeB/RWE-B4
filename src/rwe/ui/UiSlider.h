#pragma once

#include <rwe/ui/UiScrollBar.h>

namespace rwe
{
    /**
     * A horizontal slider -- the gui files' scrollbar gadget with the
     * horizontal attrib, which the options screens use for volume, scroll
     * speed and the rest. It shares UiScrollBar's value model and messaging
     * so the scenes can treat both alike, but the geometry runs left to
     * right: click or drag anywhere along the groove to set the value.
     */
    class UiSlider : public UiScrollBar
    {
    public:
        UiSlider(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<SpriteSeries> sprites);

        void render(UiRenderService& context) const override;

        void mouseDown(MouseButtonEvent event) override;

        void mouseUp(MouseButtonEvent event) override;

        void mouseMove(MouseMoveEvent event) override;

        void mouseWheel(MouseWheelEvent event) override;

    private:
        void setFromMouse(int mouseX);
    };
}
