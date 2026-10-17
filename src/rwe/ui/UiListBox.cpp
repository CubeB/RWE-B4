#include "UiListBox.h"
#include <algorithm>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    UiListBox::UiListBox(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<SpriteSeries> font)
        : UiComponent(posX, posY, sizeX, sizeY), font(std::move(font))
    {
        scrollPosition().subscribe([this](const auto& /*scrollPos*/) {
            ScrollPositionMessage m{getViewportPercent(), getScrollPercent()};
            messagesSubject.next(m);
        });
        itemsChanged().subscribe([this](const auto& /*nothing*/) {
            ScrollPositionMessage m{getViewportPercent(), getScrollPercent()};
            messagesSubject.next(m);
        });
    }

    void UiListBox::render(UiRenderService& context) const
    {
        auto lines = std::min<unsigned int>(numberOfLines(), items.size());

        for (unsigned int i = 0; i < lines; ++i)
        {
            float y = 12.0f + (i * 12.0f);
            auto itemIndex = scrollPositionSubject.getValue() + i;

            // The scroll position is kept at or below maxScrollPosition, so
            // this holds; it is the read that must never leave the items,
            // whatever put the position where it is.
            if (itemIndex >= items.size())
            {
                break;
            }

            // A row wider than the box is cut at its edge. The multiplayer
            // game list sets nine of these side by side with no gap between,
            // and a long game name would otherwise run over its neighbour.
            std::string cut;
            const std::string* shown = &items[itemIndex];
            if (context.getTextWidth(*shown, *font) > static_cast<float>(sizeX))
            {
                cut = *shown;
                while (!cut.empty() && context.getTextWidth(cut, *font) > static_cast<float>(sizeX))
                {
                    cut.pop_back();
                }
                shown = &cut;
            }

            context.drawText(posX, posY + y, *shown, *font);

            // The selected row is brightened, and the pass runs after the
            // text so the text is brightened with it (0x4A1FAE-0x4A1FC9,
            // reached only when the row index matches the box's selected
            // index at gadget+0xBA). The original does it by running the
            // row's rectangle through PALETTE.LHT at level 30 of 32 -- a
            // palette-index lookup, which RWE has no equivalent of. Measured
            // against the shipped table that level lifts luminance by a
            // median of 1.84x, with the lift largest on dark pixels and
            // nothing at all on white; blending white over the row has the
            // same shape, and this alpha reproduces the median on a
            // mid-tone. RWE was washing the row with 12% white, which at
            // 1.17x was barely visible.
            const auto& selectedIndexValue = selectedIndexSubject.getValue();
            if (selectedIndexValue && itemIndex == *selectedIndexValue)
            {
                context.fillColor(posX, posY + y - 11.0f, sizeX, 12.0f, Color(255, 255, 255, 115));
            }
        }
    }

    void UiListBox::appendItem(std::string item)
    {
        // A list box is what the map names go in, and a map name is the name
        // of a file in an archive: bytes somebody else chose. See ensureUtf8.
        items.push_back(ensureUtf8(item));
        itemsChangedSubject.next(true);
    }

    void UiListBox::clearItems()
    {
        items.clear();
        lastClickedRow.reset();
        selectedIndexSubject.next(std::nullopt);
        scrollPositionSubject.next(0);
        itemsChangedSubject.next(true);
    }

    std::optional<unsigned int> UiListBox::getSelectedIndex() const
    {
        return selectedIndexSubject.getValue();
    }

    void UiListBox::mouseDown(MouseButtonEvent event)
    {
        auto line = pixelToLine(event.y);
        if (!line)
        {
            return;
        }

        auto index = *line + scrollPositionSubject.getValue();
        if (index < items.size())
        {
            selectedIndexSubject.next(index);

            // A second click on the same row within half a second.
            if (lastClickedRow && *lastClickedRow == index && secondsSinceClick < 0.5f)
            {
                lastClickedRow.reset();
                itemActivatedSubject.next(index);
            }
            else
            {
                lastClickedRow = index;
                secondsSinceClick = 0.0f;
            }
        }
    }

    void UiListBox::update(float dt)
    {
        secondsSinceClick += dt;
    }

    void UiListBox::setSelectedIndex(const std::optional<unsigned int>& index)
    {
        if (index && *index >= items.size())
        {
            selectedIndexSubject.next(std::nullopt);
            return;
        }
        selectedIndexSubject.next(index);
    }

    Observable<unsigned int>& UiListBox::itemActivated()
    {
        return itemActivatedSubject;
    }

    void UiListBox::setSelectedItem(const std::string& item)
    {
        auto it = std::find(items.begin(), items.end(), item);
        if (it != items.end())
        {
            auto index = static_cast<unsigned int>(it - items.begin());
            selectedIndexSubject.next(index);
            setScrollPositionCentered(index);
        }
    }

    void UiListBox::clearSelectedItem()
    {
        selectedIndexSubject.next(std::nullopt);
    }

    void UiListBox::scrollToEnd()
    {
        setScrollPosition(maxScrollPosition());
    }

    Observable<std::optional<unsigned int>>& UiListBox::selectedIndex()
    {
        return selectedIndexSubject;
    }

    const Observable<std::optional<unsigned int>>& UiListBox::selectedIndex() const
    {
        return selectedIndexSubject;
    }

    Observable<bool>& UiListBox::itemsChanged()
    {
        return itemsChangedSubject;
    }

    const Observable<bool>& UiListBox::itemsChanged() const
    {
        return itemsChangedSubject;
    }

    const std::vector<std::string>& UiListBox::getItems()
    {
        return items;
    }

    void UiListBox::mouseWheel(MouseWheelEvent event)
    {
        if (event.y < 0)
        {
            scrollDown();
        }
        else if (event.y > 0)
        {
            scrollUp();
        }
    }

    void UiListBox::keyDown(KeyEvent event)
    {
        switch (event.keyCode)
        {
            case SDLK_DOWN:
                selectNextListItem();
                break;

            case SDLK_UP:
                selectPreviousListItem();
                break;
        }
    }

    unsigned int UiListBox::numberOfLines() const
    {
        auto lines = static_cast<unsigned int>((sizeY) / 12.0f);

        // listboxes defined in TA guis tend to be too long,
        // so shorten our number of lines by 1 to compensate.
        // A box under one row tall has no lines; the subtraction used to wrap
        // to about 4.29 billion (#668).
        return lines > 0 ? lines - 1 : 0;
    }

    std::optional<unsigned int> UiListBox::pixelToLine(int y) const
    {
        auto floatIndex = (y - posY) / 12.0f;
        if (floatIndex < 0.0f)
        {
            return std::nullopt;
        }

        auto index = static_cast<unsigned int>(floatIndex);
        if (index >= numberOfLines())
        {
            return std::nullopt;
        }

        return index;
    }

    void UiListBox::setScrollPositionCentered(unsigned int newPosition)
    {
        auto halfLines = numberOfLines() / 2;
        if (newPosition > halfLines)
        {
            setScrollPosition(newPosition - halfLines);
        }
        else
        {
            setScrollPosition(0);
        }
    }

    void UiListBox::setScrollPositionLast(unsigned int newPosition)
    {
        auto lines = numberOfLines();
        auto linesToSubtract = lines > 0 ? lines - 1 : 0;
        if (newPosition > linesToSubtract)
        {
            setScrollPosition(newPosition - linesToSubtract);
        }
        else
        {
            setScrollPosition(0);
        }
    }

    void UiListBox::setScrollPosition(unsigned int newPosition)
    {
        auto maxScroll = maxScrollPosition();
        if (newPosition > maxScroll)
        {
            scrollPositionSubject.next(maxScroll);
        }
        else
        {
            scrollPositionSubject.next(newPosition);
        }
    }

    unsigned int UiListBox::maxScrollPosition() const
    {
        auto listSize = items.size();
        auto displayLines = numberOfLines();
        if (listSize > displayLines)
        {
            return static_cast<unsigned int>(listSize - displayLines);
        }
        else
        {
            return 0;
        }
    }

    Observable<unsigned int>& UiListBox::scrollPosition()
    {
        return scrollPositionSubject;
    }

    const Observable<unsigned int>& UiListBox::scrollPosition() const
    {
        return scrollPositionSubject;
    }

    float UiListBox::getScrollPercent() const
    {
        auto scrollIndex = scrollPositionSubject.getValue();
        auto maxScrollIndex = maxScrollPosition();
        if (maxScrollIndex == 0)
        {
            return 0.0f;
        }

        return static_cast<float>(scrollIndex) / static_cast<float>(maxScrollIndex);
    }

    float UiListBox::getViewportPercent() const
    {
        auto itemCount = items.size();
        auto linesCount = numberOfLines();

        if (linesCount >= itemCount)
        {
            return 1.0f;
        }

        return static_cast<float>(linesCount) / static_cast<float>(itemCount);
    }

    void UiListBox::uiMessage(const GroupMessage& message)
    {
        if (message.controlName == name)
        {
            return;
        }

        if (message.group != group)
        {
            return;
        }

        std::visit(ListBoxUiMessageVisitor(this), message.message);
    }

    void UiListBox::scrollUp()
    {
        if (scrollPositionSubject.getValue() > 3)
        {
            scrollPositionSubject.next(scrollPositionSubject.getValue() - 3);
        }
        else
        {
            scrollPositionSubject.next(0);
        }
    }

    void UiListBox::scrollDown()
    {
        // maxScroll - 3 wrapped for a list that overflows by fewer than three
        // rows (and for one that fits), sending the position to three, past
        // the end of the items (#667). Add and compare instead.
        auto maxScroll = maxScrollPosition();
        auto position = std::min(scrollPositionSubject.getValue(), maxScroll);
        scrollPositionSubject.next(maxScroll - position > 3 ? position + 3 : maxScroll);
    }

    void UiListBox::selectNextListItem()
    {
        auto index = selectedIndexSubject.getValue();
        if (!index || *index >= items.size() - 1)
        {
            return;
        }
        auto newIndex = *index + 1;
        selectedIndexSubject.next(newIndex);
        ensureIndexInView(newIndex);
    }

    void UiListBox::selectPreviousListItem()
    {
        auto index = selectedIndexSubject.getValue();
        if (!index || *index < 1)
        {
            return;
        }
        auto newIndex = *index - 1;
        selectedIndexSubject.next(newIndex);
        ensureIndexInView(newIndex);
    }

    void UiListBox::ensureIndexInView(unsigned int newIndex)
    {
        auto firstIndexInView = scrollPositionSubject.getValue();
        auto lines = numberOfLines();
        if (lines == 0)
        {
            // Nothing is in view in a box with no rows; keep the item first.
            setScrollPosition(newIndex);
            return;
        }
        auto lastIndexInView = firstIndexInView + lines - 1;

        if (newIndex < firstIndexInView)
        {
            setScrollPosition(newIndex);
        }
        else if (newIndex > lastIndexInView)
        {
            setScrollPositionLast(newIndex);
        }
    }
}
