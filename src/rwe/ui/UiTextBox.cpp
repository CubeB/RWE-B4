#include "UiTextBox.h"

#include <SDL3/SDL.h>

namespace rwe
{
    UiTextBox::UiTextBox(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::string& text, const std::shared_ptr<SpriteSeries>& font)
        : UiComponent(posX, posY, sizeX, sizeY), text(text), font(font)
    {
    }

    void UiTextBox::render(UiRenderService& context) const
    {
        // A visible field to type into -- sunken plate and outline -- then
        // the line with a plain underscore caret, in the panel font.
        context.fillColor(posX, posY, sizeX, sizeY, Color(8, 10, 8, 170));
        context.drawBoxOutline(posX, posY, sizeX, sizeY, Color(150, 162, 150, 255), 1.0f);
        context.drawText(posX + 4, posY + 12.0f, text + "_", *font);
    }

    void UiTextBox::keyDown(KeyEvent event)
    {
        auto key = event.keyCode;
        if (key == SDLK_BACKSPACE)
        {
            if (!text.empty())
            {
                text.pop_back();
            }
            return;
        }

        if (text.size() >= maxLength)
        {
            return;
        }

        // Letters, digits and a few separators, which is everything a save
        // name needs. There is no text-input event plumbing to draw on, so
        // shifted characters are out; names come out lowercase.
        if (key >= SDLK_A && key <= SDLK_Z)
        {
            text.push_back(static_cast<char>('a' + (key - SDLK_A)));
        }
        else if (key >= SDLK_0 && key <= SDLK_9)
        {
            text.push_back(static_cast<char>('0' + (key - SDLK_0)));
        }
        else if (key == SDLK_SPACE || key == SDLK_MINUS || key == SDLK_PERIOD)
        {
            text.push_back(key == SDLK_SPACE ? ' ' : static_cast<char>(key));
        }
    }

    void UiTextBox::setText(const std::string& newText)
    {
        text = newText;
    }
}
