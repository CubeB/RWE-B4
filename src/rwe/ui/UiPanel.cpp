#include "UiPanel.h"
#include <algorithm>
#include <cmath>
#include <rwe/util/rwe_string.h>

namespace rwe
{
    UiPanel::UiPanel(int posX, int posY, unsigned int sizeX, unsigned int sizeY)
        : UiComponent(posX, posY, sizeX, sizeY)
    {
    }

    UiPanel::UiPanel(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::shared_ptr<Sprite> background)
        : UiComponent(posX, posY, sizeX, sizeY),
          background(std::move(background))
    {
    }

    UiPanel::UiPanel(int posX, int posY, unsigned int sizeX, unsigned int sizeY, std::optional<std::shared_ptr<Sprite>> background)
        : UiComponent(posX, posY, sizeX, sizeY),
          background(background)
    {
    }

    UiPanel::UiPanel(UiPanel&& panel) noexcept
        : UiComponent(panel.posX, panel.posY, panel.sizeX, panel.sizeY),
          background(std::move(panel.background)),
          children(std::move(panel.children)),
          focusedChild(std::move(panel.focusedChild)),
          drawSolidPlate(panel.drawSolidPlate),
          plateTile(std::move(panel.plateTile))
    {
    }

    UiPanel& UiPanel::operator=(UiPanel&& panel) noexcept
    {
        posX = panel.posX;
        posY = panel.posY;
        sizeX = panel.sizeX;
        sizeY = panel.sizeY;
        background = panel.background;
        children = std::move(panel.children);
        focusedChild = std::move(panel.focusedChild);
        drawSolidPlate = panel.drawSolidPlate;
        plateTile = panel.plateTile;

        return *this;
    }

    void UiPanel::render(UiRenderService& graphics) const
    {
        if (background)
        {
            graphics.drawSpriteAbs(posX, posY, **background);
        }
        else if (drawSolidPlate)
        {
            // A dialog whose gui declares no art of its own (YESORNO and
            // EXITMENU say panel=NULL, and carry no picture box or bitmap
            // either) still has to be seen. The original fills it with
            // BackTile, so tile that where it is to be had and keep the flat
            // fill underneath for the part of the last row and column that
            // the tile overhangs, and for a data set that has no tile.
            graphics.fillColor(posX, posY, sizeX, sizeY, Color(28, 34, 30));

            if (plateTile)
            {
                const auto& tile = **plateTile;
                auto naturalWidth = tile.bounds.width();
                auto naturalHeight = tile.bounds.height();
                if (naturalWidth >= 1.0f && naturalHeight >= 1.0f)
                {
                    // The tile is stretched a little so that a whole number
                    // of them covers the panel exactly. Drawing them at their
                    // own size instead would leave a part-tile at the right
                    // and bottom, and drawSpriteAbs scales rather than clips,
                    // so that part-tile would be the same texture squashed
                    // into a quarter of its width -- a smeared strip down the
                    // edge of every dialog. Spread over every tile the same
                    // squash is a few percent and invisible on a texture that
                    // is mostly noise. A 400x100 dialog takes 7 across and 2
                    // down.
                    auto columns = std::max(1.0f, std::ceil(static_cast<float>(sizeX) / naturalWidth));
                    auto rows = std::max(1.0f, std::ceil(static_cast<float>(sizeY) / naturalHeight));
                    auto tileWidth = static_cast<float>(sizeX) / columns;
                    auto tileHeight = static_cast<float>(sizeY) / rows;

                    for (int row = 0; row < static_cast<int>(rows); ++row)
                    {
                        for (int column = 0; column < static_cast<int>(columns); ++column)
                        {
                            graphics.drawSpriteAbs(
                                Rectangle2f::fromTopLeft(
                                    static_cast<float>(posX) + (static_cast<float>(column) * tileWidth),
                                    static_cast<float>(posY) + (static_cast<float>(row) * tileHeight),
                                    tileWidth,
                                    tileHeight),
                                tile);
                        }
                    }
                }
            }

            graphics.drawBoxOutline(posX, posY, sizeX, sizeY, Color(110, 124, 110), 2.0f);
        }

        graphics.pushMatrix();
        graphics.multiplyMatrix(Matrix4f::translation(Vector3f(posX, posY, 0.0f)));

        for (const auto& i : children)
        {
            i->render(graphics);
        }

        graphics.popMatrix();
    }

    void UiPanel::mouseDown(MouseButtonEvent event)
    {
        event.x -= posX;
        event.y -= posY;

        auto size = children.size();
        for (std::size_t i = 0; i < size; ++i)
        {
            auto& c = children[i];
            if (c->contains(event.x, event.y))
            {
                setFocus(i);
                c->mouseDown(event);
                return;
            }
        }
    }

    void UiPanel::mouseUp(MouseButtonEvent event)
    {
        if (!focusedChild)
        {
            return;
        }

        event.x -= posX;
        event.y -= posY;

        (*focusedChild)->mouseUp(event);
    }

    void UiPanel::keyDown(KeyEvent event)
    {
        for (auto& e : children)
        {
            e->keyDown(event);
        }
    }

    void UiPanel::keyUp(KeyEvent event)
    {
        for (auto& e : children)
        {
            e->keyUp(event);
        }
    }

    void UiPanel::mouseMove(MouseMoveEvent event)
    {
        UiComponent::mouseMove(event);

        event.x -= posX;
        event.y -= posY;

        for (auto& e : children)
        {
            e->mouseMove(event);
        }
    }

    void UiPanel::focus()
    {
        // The test used to be inverted, so a panel with no focused child
        // dereferenced an empty optional and a panel that had one never
        // passed the focus down. Nothing caught it because a panel inside a
        // panel is rare, and the two callers that exist go through
        // setFocus/clearFocus instead.
        if (focusedChild)
        {
            (*focusedChild)->focus();
        }
    }

    void UiPanel::unfocus()
    {
        if (focusedChild)
        {
            (*focusedChild)->unfocus();
        }
    }

    void UiPanel::appendChild(std::unique_ptr<UiComponent>&& c)
    {
        auto& component = children.emplace_back(std::move(c));
        component->messages().subscribe([this, &cc = *component](const auto& message) {
            GroupMessage groupMessage(this->name, cc.getGroup(), cc.getName(), message);
            this->uiMessage(groupMessage);
            this->groupMessagesSubject.next(groupMessage);
        });
    }

    void UiPanel::setFocus(std::size_t controlIndex)
    {
        assert(controlIndex < children.size());

        if (focusedChild)
        {
            (*focusedChild)->unfocus();
        }

        focusedChild = children[controlIndex].get();
        (*focusedChild)->focus();
    }

    void UiPanel::clearFocus()
    {
        if (focusedChild)
        {
            (*focusedChild)->unfocus();
        }

        focusedChild = std::nullopt;
    }

    void UiPanel::update(float dt)
    {
        for (auto& c : children)
        {
            c->update(dt);
        }
    }

    void UiPanel::mouseWheel(MouseWheelEvent event)
    {
        if (!focusedChild)
        {
            return;
        }

        (*focusedChild)->mouseWheel(event);
    }

    void UiPanel::uiMessage(const GroupMessage& message)
    {
        for (auto& c : children)
        {
            c->uiMessage(message);
        }
    }

    std::vector<std::unique_ptr<UiComponent>>& UiPanel::getChildren()
    {
        return children;
    }

    void UiPanel::removeChildrenWithPrefix(const std::string& prefix)
    {
        // Don't leave the focus pointing at a child we are about to destroy.
        if (focusedChild && startsWith((*focusedChild)->getName(), prefix))
        {
            focusedChild = std::nullopt;
        }

        children.erase(
            std::remove_if(
                children.begin(),
                children.end(),
                [&prefix](const auto& e) { return startsWith(e->getName(), prefix); }),
            children.end());
    }

    void UiPanel::removeChildrenNamed(const std::string& name)
    {
        // Don't leave the focus pointing at a child we are about to destroy.
        if (focusedChild && (*focusedChild)->getName() == name)
        {
            focusedChild = std::nullopt;
        }

        children.erase(
            std::remove_if(
                children.begin(),
                children.end(),
                [&name](const auto& e) { return e->getName() == name; }),
            children.end());
    }

    void UiPanel::setFocusByName(const std::string& name)
    {
        auto it = std::find_if(children.begin(), children.end(), [&name](const auto& c) { return c->getName() == name; });
        if (it != children.end())
        {
            setFocus(it - children.begin());
        }
    }

    Observable<GroupMessage>& UiPanel::groupMessages()
    {
        return groupMessagesSubject;
    }

    const Observable<GroupMessage>& UiPanel::groupMessages() const
    {
        return groupMessagesSubject;
    }
}
