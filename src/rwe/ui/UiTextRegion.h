#pragma once

#include <functional>
#include <memory>
#include <rwe/render/SpriteSeries.h>
#include <rwe/ui/UiComponent.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * A block of text in a gadget's rectangle, wrapped to its width at word
     * boundaries and shown a page at a time: the campaign briefing's
     * TextRegion, which a click turns to the next page (0x476EF0) and which
     * goes back to the first after the last.
     */
    class UiTextRegion : public UiComponent
    {
    private:
        /** The text with its markup taken out, and the colour each byte of it is drawn in. */
        std::string text;
        std::vector<unsigned char> colours;
        std::shared_ptr<SpriteSeries> font;
        unsigned int page{0};
        /** How many pages the last drawing came to; a click needs it and only drawing can measure. */
        mutable unsigned int pageCount{1};

    public:
        /** The height of a line, as UiRenderService::drawTextWrapped steps it. */
        static constexpr float LineHeight = 15.0f;

        UiTextRegion(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::shared_ptr<SpriteSeries>& font);

        ~UiTextRegion() override { releaseSubscriptions(); }

        void setText(const std::string& newText);

        void setFont(const std::shared_ptr<SpriteSeries>& newFont);

        void nextPage();

        unsigned int getPage() const { return page; }

        void render(UiRenderService& context) const override;

        void mouseDown(MouseButtonEvent event) override;

        /**
         * The text broken into lines no wider than `width`: at each line
         * break of its own (CR, LF or both) and otherwise between words, a
         * word too long for a line standing on a line by itself.
         */
        static std::vector<std::pair<std::size_t, std::size_t>> wrap(const std::string& text, float width, const std::function<float(const std::string&)>& measure);

        /**
         * The briefings' markup: `&Y`, `&R` and `&G` start yellow, red and
         * green text and a bare `&` goes back to white. The `*` the shipped
         * briefings put round a heading is taken out, its meaning not
         * decoded. Returns the plain text and a colour index (0 white, 1
         * yellow, 2 red, 3 green) for each byte of it.
         */
        static std::pair<std::string, std::vector<unsigned char>> parseMarkup(const std::string& text);
    };

    /**
     * A GAF sequence played in a loop in a gadget's rectangle: the briefing's
     * rotating planet. Its frames carry a whole-screen origin, so each is
     * drawn by its own top-left corner at the gadget's, at its own size or
     * fitted to the gadget.
     */
    class UiAnimation : public UiComponent
    {
    private:
        std::shared_ptr<SpriteSeries> frames;
        float framesPerSecond;
        bool fitToGadget;
        float elapsed{0.0f};

    public:
        UiAnimation(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<SpriteSeries> frames, float framesPerSecond, bool fitToGadget = false);

        ~UiAnimation() override { releaseSubscriptions(); }

        void update(float dt) override;

        void render(UiRenderService& context) const override;
    };
}
