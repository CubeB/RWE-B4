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

    namespace
    {
        struct ShiftPair
        {
            int keyCode;
            char unshifted;
            char shifted;
        };

        // A US layout mapping, done by hand keycode-by-keycode rather than
        // through SDL's text-input events. The project's key events are
        // broadcast to every child of a panel rather than routed to a
        // focused one (see the comment in
        // GameScene_commands.cpp::openSaveDialog), so there is no
        // SDL_TEXTINPUT plumbing to hang this off; wiring that up is a
        // separate job. This covers the digits and the punctuation row a
        // save name is likely to use, and no more.
        constexpr ShiftPair shiftTable[] = {
            {SDLK_1, '1', '!'},
            {SDLK_2, '2', '@'},
            {SDLK_3, '3', '#'},
            {SDLK_4, '4', '$'},
            {SDLK_5, '5', '%'},
            {SDLK_6, '6', '^'},
            {SDLK_7, '7', '&'},
            {SDLK_8, '8', '*'},
            {SDLK_9, '9', '('},
            {SDLK_0, '0', ')'},
            {SDLK_MINUS, '-', '_'},
            {SDLK_EQUALS, '=', '+'},
            {SDLK_LEFTBRACKET, '[', '{'},
            {SDLK_RIGHTBRACKET, ']', '}'},
            {SDLK_BACKSLASH, '\\', '|'},
            {SDLK_SEMICOLON, ';', ':'},
            {SDLK_APOSTROPHE, '\'', '"'},
            {SDLK_COMMA, ',', '<'},
            {SDLK_PERIOD, '.', '>'},
            {SDLK_SLASH, '/', '?'},
            {SDLK_GRAVE, '`', '~'},
            {SDLK_SPACE, ' ', ' '},
        };
    }

    std::optional<char> textBoxCharacterFor(int keyCode, unsigned short modState)
    {
        bool shift = (modState & SDL_KMOD_SHIFT) != 0;

        if (keyCode >= SDLK_A && keyCode <= SDLK_Z)
        {
            // Caps lock inverts the shift result for letters only -- it does
            // not touch the digit/punctuation row, the way a real text field
            // behaves.
            bool caps = (modState & SDL_KMOD_CAPS) != 0;
            bool upper = shift != caps;
            char base = static_cast<char>('a' + (keyCode - SDLK_A));
            return upper ? static_cast<char>(base - 'a' + 'A') : base;
        }

        for (const auto& pair : shiftTable)
        {
            if (pair.keyCode == keyCode)
            {
                return shift ? pair.shifted : pair.unshifted;
            }
        }

        return std::nullopt;
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

        if (auto c = textBoxCharacterFor(key, SDL_GetModState()))
        {
            text.push_back(*c);
        }
    }

    void UiTextBox::setText(const std::string& newText)
    {
        text = newText;
    }
}
