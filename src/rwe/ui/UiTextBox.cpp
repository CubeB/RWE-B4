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
        // the line and, when this is the control with the focus, a caret.
        //
        // The original draws the caret at 0x4A4FF6: a one-pixel vertical
        // line at the right-hand end of the text, from the text's own top
        // down two pixels past the font's height, in interface colour 9
        // (GUIPAL light blue, which nearest-matches to palette 9). It is
        // behind the only focus test in the whole gadget renderer --
        // 0x4A4F14 compares the gadget's index with the panel's focused
        // index -- so an unfocused box shows the text and nothing else.
        context.fillColor(posX, posY, sizeX, sizeY, Color(8, 10, 8, 170));
        context.drawBoxOutline(posX, posY, sizeX, sizeY, Color(150, 162, 150, 255), 1.0f);

        float textX = posX + 4.0f;
        float baseline = posY + 12.0f;
        context.drawText(textX, baseline, text, *font);

        if (focused)
        {
            auto caretX = textX + context.getTextWidth(text, *font);
            context.fillColor(caretX, baseline - 10.0f, 1.0f, 12.0f, Color(84, 84, 252));
        }
    }

    void UiTextBox::focus()
    {
        focused = true;
    }

    void UiTextBox::unfocus()
    {
        focused = false;
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
