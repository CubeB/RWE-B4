#pragma once

#include <optional>
#include <rwe/ui/UiComponent.h>

namespace rwe
{
    /**
     * The gui files' gadget type 3: a single line of editable text. The
     * original uses it for the save-game name, the multiplayer screens' name,
     * game name, password and TCP address; this one accepts printable ASCII
     * from the keyboard, which is all any of them needs.
     */
    class UiTextBox : public UiComponent
    {
    public:
        /**
         * What the gui files let a box be typed to when they say nothing
         * (`maxchars=` is read where there is one) and the most any of them
         * says: SAVEGAME.GUI asks for 99, which is a filename's worth of
         * trouble and more than a line of this font fits.
         */
        static constexpr unsigned int defaultMaxLength = 24;
        static constexpr unsigned int largestMaxLength = 63;

    private:
        std::string text;
        std::shared_ptr<SpriteSeries> font;
        bool focused{false};
        unsigned int maxLength{defaultMaxLength};

        /** Draws a `*` for each character; getText still returns what was typed. */
        bool masked{false};

        /** A read-only box shows its text and takes no typing. */
        bool editable{true};

        /**
         * Whether the box draws a field of its own. It does by default; a
         * screen whose bitmap already has the field painted into it -- the
         * multiplayer screens do -- turns it off, and the box is then only
         * the text and the caret over the picture's plate.
         */
        bool drawPlate{true};

    public:
        UiTextBox(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::string& text, const std::shared_ptr<SpriteSeries>& font);

        void render(UiRenderService& context) const override;

        /**
         * The glyph-drawer Y for the line: the original's pen (`posY + 3`)
         * plus the "I" frame's height. Height-independent, because a text
         * input is not a centred button (#903,
         * BT-GUI-TEXTBOX-BASELINE-FINDINGS.md).
         */
        float textLineBaseline() const;

        /** The caret's first row, which is the pen. */
        float caretTop() const;

        /** The caret's painted height in rows: the "I" height plus three. */
        float caretHeight() const;

        void keyDown(KeyEvent event) override;

        void textInput(const std::string& newText) override;

        /** The box is typed into, so a focused one takes every key its panel gets. */
        bool wantsTextInput() const override { return true; }

        void focus() override;

        void unfocus() override;

        const std::string& getText() const { return text; }

        void setText(const std::string& newText);

        unsigned int getMaxLength() const { return maxLength; }

        /** Caps typing, and trims what is already there to fit. */
        void setMaxLength(unsigned int newMaxLength);

        bool isMasked() const { return masked; }
        void setMasked(bool newMasked) { masked = newMasked; }

        bool isEditable() const { return editable; }
        void setEditable(bool newEditable) { editable = newEditable; }

        void setDrawPlate(bool newDrawPlate) { drawPlate = newDrawPlate; }

    private:
        /** What is drawn: the text, or a star each if masked. */
        std::string displayText() const;
    };
}
