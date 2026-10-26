#include "UiLabel.h"

#include <rwe/util/rwe_string.h>
#include <cmath>
#include <limits>

namespace rwe
{

    void UiLabel::render(UiRenderService& context) const
    {
        switch (alignment)
        {
            case Alignment::Left:
                if (singleLine)
                {
                    context.drawText(static_cast<float>(posX), posY + font->textBaseline, fittedToWidth(context), *font);
                }
                else
                {
                    context.drawTextWrapped(Rectangle2f::fromTopLeft(posX, posY, sizeX, sizeY), text, *font);
                }
                break;
            case Alignment::Center:
                if (singleLine)
                {
                    context.drawTextCentered(posX + (static_cast<float>(sizeX) / 2.0f), posY, fittedToWidth(context), *font);
                }
                else
                {
                    context.drawTextWrappedCentered(Rectangle2f::fromTopLeft(posX, posY, sizeX, sizeY), text, *font);
                }
                break;
            case Alignment::Right:
                if (singleLine)
                {
                    context.drawTextAlignRight(posX + sizeX, posY + font->textBaseline, fittedToWidth(context), *font);
                }
                else
                {
                    context.drawTextWrappedRight(Rectangle2f::fromTopLeft(posX, posY, sizeX, sizeY), text, *font);
                }
                break;
        }
    }

    UiLabel::Alignment UiLabel::alignmentFromAttributes(int attribs)
    {
        switch (attribs & 0x7)
        {
            case 2:
                return Alignment::Center;
            case 4:
                return Alignment::Right;
            default:
                return Alignment::Left;
        }
    }

    std::string UiLabel::fittedToWidth(UiRenderService& context) const
    {
        const auto limit = static_cast<float>(sizeX);
        if (context.getTextWidth(text, *font) <= limit)
        {
            return text;
        }

        // Drop whole characters from the end until the rest and an ellipsis
        // fit; a UTF-8 continuation byte is never left standing alone.
        std::string shown = text;
        while (!shown.empty())
        {
            do
            {
                shown.pop_back();
            } while (!shown.empty() && (static_cast<unsigned char>(shown.back()) & 0xC0) == 0x80);

            if (context.getTextWidth(shown + "...", *font) <= limit)
            {
                break;
            }
        }
        return shown + "...";
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

    void UiLabel::setSingleLine(bool value)
    {
        singleLine = value;
    }

    unsigned int UiLabel::getTextHeight() const
    {
        const auto glyphs = layoutWrappedText(
            Rectangle2f::fromTopLeft(0.0f, 0.0f, sizeX, std::numeric_limits<float>::max()), text, *font);
        float bottom = 0.0f;
        for (const auto& glyph : glyphs)
        {
            bottom = std::max(bottom, std::max(glyph.y + font->verticalOffset,
                glyph.y + font->sprites[glyph.glyph]->bounds.bottom()));
        }
        return static_cast<unsigned int>(std::ceil(bottom)) + 3;
    }

}
