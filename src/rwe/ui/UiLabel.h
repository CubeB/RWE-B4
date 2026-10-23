#pragma once

#include <rwe/ui/UiComponent.h>

namespace rwe
{
    class UiLabel : public UiComponent
    {
    public:
        enum class Alignment
        {
            Left,
            Center
        };

    private:
        /** The text, or as much of it as fits the label's width with "..." after it. */
        std::string fittedToWidth(UiRenderService& context) const;

        std::string text;
        std::shared_ptr<SpriteSeries> font;
        Alignment alignment{Alignment::Left};
        bool singleLine{false};

    public:
        UiLabel(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::string& text, const std::shared_ptr<SpriteSeries>& font);

        void render(UiRenderService& context) const override;

        void setText(const std::string& newText);

        const std::string& getText() const { return text; }

        void setAlignment(Alignment newAlignment);

        /**
         * Draws the text on one line, cut with an ellipsis where it would
         * pass the label's width, instead of wrapping onto the next line. For
         * a status line at the bottom of a screen, where the next line is
         * off the screen: a message must be clipped, never lost.
         */
        void setSingleLine(bool value);

        /** Full wrapped height in the active font, including three pixels of allocation padding. */
        unsigned int getTextHeight() const;
    };
}
