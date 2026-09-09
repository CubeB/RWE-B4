#pragma once

#include <optional>
#include <rwe/ui/UiComponent.h>

namespace rwe
{
    /**
     * What keyDown types into the box for a given keycode and modifier
     * state (an SDL_Keymod bitmask), split out from keyDown so it can be
     * tested without a live SDL modifier state to read. Returns nullopt for
     * anything the box does not accept.
     */
    std::optional<char> textBoxCharacterFor(int keyCode, unsigned short modState);

    /**
     * The gui files' gadget type 3: a single line of editable text. The
     * original uses it for the save-game name; this one accepts letters,
     * digits and a few separators from the keyboard, which is all a save
     * name needs.
     */
    class UiTextBox : public UiComponent
    {
    private:
        std::string text;
        std::shared_ptr<SpriteSeries> font;
        bool focused{false};
        unsigned int maxLength{24};

    public:
        UiTextBox(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::string& text, const std::shared_ptr<SpriteSeries>& font);

        void render(UiRenderService& context) const override;

        void keyDown(KeyEvent event) override;

        void focus() override;

        void unfocus() override;

        const std::string& getText() const { return text; }

        void setText(const std::string& newText);
    };
}
