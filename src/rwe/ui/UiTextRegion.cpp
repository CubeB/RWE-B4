#include "UiTextRegion.h"
#include <cctype>
#include <cmath>
#include <functional>
#include <rwe/UiRenderService.h>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    UiTextRegion::UiTextRegion(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::shared_ptr<SpriteSeries>& font)
        : UiComponent(posX, posY, sizeX, sizeY), font(font)
    {
    }

    void UiTextRegion::setText(const std::string& newText)
    {
        // Briefings are files somebody else wrote; see ensureUtf8.
        auto [plain, colourOf] = parseMarkup(ensureUtf8(newText));
        text = std::move(plain);
        colours = std::move(colourOf);
        page = 0;
    }

    void UiTextRegion::setFont(const std::shared_ptr<SpriteSeries>& newFont)
    {
        font = newFont;
    }

    void UiTextRegion::nextPage()
    {
        page = (page + 1) % std::max(1u, pageCount);
    }

    void UiTextRegion::mouseDown(MouseButtonEvent /*event*/)
    {
        nextPage();
    }

    std::pair<std::string, std::vector<unsigned char>> UiTextRegion::parseMarkup(const std::string& text)
    {
        std::string plain;
        std::vector<unsigned char> colourOf;
        unsigned char colour = 0;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            auto c = text[i];
            if (c == '&')
            {
                auto next = i + 1 < text.size() ? std::toupper(static_cast<unsigned char>(text[i + 1])) : 0;
                if (next == 'Y' || next == 'R' || next == 'G')
                {
                    colour = next == 'Y' ? 1 : next == 'R' ? 2 : 3;
                    ++i;
                }
                else
                {
                    colour = 0;
                }
                continue;
            }
            if (c == '*')
            {
                continue;
            }
            plain.push_back(c);
            colourOf.push_back(colour);
        }
        return {plain, colourOf};
    }

    std::vector<std::pair<std::size_t, std::size_t>> UiTextRegion::wrap(const std::string& text, float width, const std::function<float(const std::string&)>& measure)
    {
        // Lines as [begin, end) offsets into the text, so the colours line up.
        std::vector<std::pair<std::size_t, std::size_t>> lines;
        std::size_t paragraphStart = 0;
        auto flushParagraph = [&](std::size_t paragraphEnd) {
            auto lineStart = paragraphStart;
            auto lineEnd = paragraphStart;
            auto at = paragraphStart;
            while (at <= paragraphEnd)
            {
                auto space = text.find(' ', at);
                auto wordEnd = (space == std::string::npos || space > paragraphEnd) ? paragraphEnd : space;
                if (lineEnd > lineStart && measure(text.substr(lineStart, wordEnd - lineStart)) > width)
                {
                    lines.emplace_back(lineStart, lineEnd);
                    lineStart = at;
                }
                lineEnd = wordEnd;
                if (wordEnd == paragraphEnd)
                {
                    break;
                }
                at = wordEnd + 1;
            }
            lines.emplace_back(lineStart, lineEnd);
        };
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            auto c = text[i];
            if (c == '\r' || c == '\n')
            {
                flushParagraph(i);
                // CR LF is one break, and so is either alone.
                if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                {
                    ++i;
                }
                paragraphStart = i + 1;
            }
        }
        if (paragraphStart < text.size())
        {
            flushParagraph(text.size());
        }
        return lines;
    }

    void UiTextRegion::render(UiRenderService& context) const
    {
        if (!font)
        {
            return;
        }
        static const Color palette[4] = {Color(255, 255, 255), Color(255, 255, 0), Color(255, 64, 64), Color(64, 255, 64)};
        auto lines = wrap(text, static_cast<float>(sizeX), [&](const std::string& s) { return context.getTextWidth(s, *font); });
        auto perPage = std::max(1u, static_cast<unsigned int>(static_cast<float>(sizeY) / LineHeight));
        pageCount = std::max(1u, static_cast<unsigned int>((lines.size() + perPage - 1) / perPage));
        auto first = std::min(page, pageCount - 1) * perPage;
        for (unsigned int i = 0; i < perPage && first + i < lines.size(); ++i)
        {
            // The label's baseline offset, so the two line up. Each run of
            // one colour is drawn in turn along the line.
            auto [begin, end] = lines[first + i];
            auto x = static_cast<float>(posX);
            auto y = static_cast<float>(posY) + 12.0f + (static_cast<float>(i) * LineHeight);
            auto runStart = begin;
            for (auto at = begin; at <= end; ++at)
            {
                if (at == end || colours[at] != colours[runStart])
                {
                    auto run = text.substr(runStart, at - runStart);
                    context.drawText(x, y, run, *font, palette[runStart < colours.size() ? colours[runStart] : 0]);
                    x += context.getTextWidth(run, *font);
                    runStart = at;
                }
            }
        }
    }

    UiAnimation::UiAnimation(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<SpriteSeries> frames, float framesPerSecond, bool fitToGadget)
        : UiComponent(posX, posY, sizeX, sizeY), frames(std::move(frames)), framesPerSecond(framesPerSecond), fitToGadget(fitToGadget)
    {
    }

    void UiAnimation::update(float dt)
    {
        elapsed += dt;
    }

    void UiAnimation::render(UiRenderService& context) const
    {
        if (!frames || frames->sprites.empty())
        {
            return;
        }
        auto index = static_cast<std::size_t>(std::floor(elapsed * framesPerSecond)) % frames->sprites.size();
        const auto& sprite = *frames->sprites[index];
        if (fitToGadget)
        {
            context.drawSpriteAbs(static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(sizeX), static_cast<float>(sizeY), sprite);
        }
        else
        {
            context.drawSpriteAbs(static_cast<float>(posX), static_cast<float>(posY), sprite);
        }
    }
}
