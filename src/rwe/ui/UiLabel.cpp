#include "UiLabel.h"

#include <rwe/util/rwe_string.h>

namespace rwe
{

    void UiLabel::render(UiRenderService& context) const
    {
        switch (alignment)
        {
            case Alignment::Left:
                context.drawTextWrapped(Rectangle2f::fromTopLeft(posX, posY + 12.0f, sizeX, sizeY), text, *font);
                break;
            case Alignment::Center:
                context.drawTextCentered(posX, posY, text, *font);
                break;
        }
    }

    UiLabel::UiLabel(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::string& text, const std::shared_ptr<SpriteSeries>& font) : UiComponent(posX, posY, sizeX, sizeY), text(ensureUtf8(text)), font(font)
    {
    }

    void UiLabel::setText(const std::string& newText)
    {
        // Here rather than in the renderer: this is called when something
        // changes and the renderer is called every frame. See ensureUtf8 for
        // what arrives that is not UTF-8 and why it must not throw.
        text = ensureUtf8(newText);
    }

    void UiLabel::setAlignment(Alignment newAlignment)
    {
        alignment = newAlignment;
    }
}
