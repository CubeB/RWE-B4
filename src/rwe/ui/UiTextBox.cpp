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
        // Editing keys only. The characters themselves arrive as text input,
        // which is what makes a box on a French or German layout type what
        // its keys say: this used to be a hand-written US table, and on any
        // other layout it wrote the wrong letters.
        if (event.keyCode == SDLK_BACKSPACE && !text.empty())
        {
            text.pop_back();
        }
    }

    void UiTextBox::textInput(const std::string& newText)
    {
        // SDL hands this over as UTF-8. A save name is a filename, and the
        // font has one glyph to a byte, so anything outside printable ASCII
        // is dropped rather than stored as bytes nothing can draw.
        for (char c : newText)
        {
            if (text.size() >= maxLength)
            {
                return;
            }

            auto byte = static_cast<unsigned char>(c);
            if (byte >= 0x20 && byte < 0x7F)
            {
                text.push_back(c);
            }
        }
    }

    void UiTextBox::setText(const std::string& newText)
    {
        text = newText;
    }
}
