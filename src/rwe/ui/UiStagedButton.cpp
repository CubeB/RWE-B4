#include "UiStagedButton.h"

#include <cmath>

namespace rwe
{
    ButtonClickEvent::Source mouseButtonToSource(MouseButtonEvent::MouseButton btn)
    {
        switch (btn)
        {
            case MouseButtonEvent::MouseButton::Left:
                return ButtonClickEvent::Source::LeftMouseButton;
            case MouseButtonEvent::MouseButton::Right:
                return ButtonClickEvent::Source::RightMouseButton;
            case MouseButtonEvent::MouseButton::Middle:
                return ButtonClickEvent::Source::MiddleMouseButton;
                break;
            default:
                throw std::logic_error("Unknown mouse button value");
        }
    }

    void UiStagedButton::render(UiRenderService& graphics) const
    {
        const auto& sprite = !enabled && disabledSprite
            ? *disabledSprite
            : (pressed || toggledOn ? *pressedSprite : *stages[currentStage].sprite);

        graphics.drawSpriteAbs(posX, posY, sprite);

        if (textAlign == TextAlign::Hidden)
        {
            return;
        }

        const auto& label = stages[currentStage].label;

        // Where the caption's own origin lands, settled once so that the drop
        // shadow, the caption and the quick-key underline all follow the same
        // alignment. The two centred cases work out here what
        // drawTextCentered and drawTextCenteredX would work out internally,
        // rounding included -- and the pressed shift stays exact through
        // that, round(a + 1) being round(a) + 1 for any a.
        float textX;
        float textY;
        switch (textAlign)
        {
            case TextAlign::Left:
                textX = posX + 6.0f;
                textY = posY + (sizeY / 2.0f) + 6.0f;
                break;
            case TextAlign::Center:
                textX = std::round((posX + (sizeX / 2.0f)) - (graphics.getTextWidth(label, *labelFont) / 2.0f));
                textY = std::round(posY + (sizeY / 2.0f) + 5.0f);
                break;
            case TextAlign::BottomCenter:
                textX = std::round((posX + (sizeX / 2.0f)) - (graphics.getTextWidth(label, *labelFont) / 2.0f));
                textY = posY + sizeY - 7;
                break;
            default:
                throw std::logic_error("Invalid TextAlign value");
        }

        if (pressed)
        {
            textX += 1.0f;
            textY += 1.0f;
        }

        // S:99: the drop shadow, at (x+1, y+3) in interface colour 0, is
        // drawn only for a gadget whose attribs carry bit 3 -- 0x4A59A4
        // tests it before 0x4A59A9-0x4A59E3 draw anything. The shipped menu
        // buttons do not set it, so the original shows them without one.
        // Interface colour 0 is GUIPAL's black; RWE has no runtime
        // interface-colour table, so it is a literal with the slot named, as
        // the minimap rings are in GameScene_render.
        if (captionShadow)
        {
            graphics.drawText(textX + 1.0f, textY + 3.0f, label, *labelFont, Color(0, 0, 0));
        }
        graphics.drawText(textX, textY, label, *labelFont);

        // S:99, 0x4A5B2C-0x4A5CFC: where the gadget's quickkey character
        // appears in its caption, the original draws a line under it with its
        // line routine 0x4BE950, in interface colour 2 ([cfg+0x8B4]) -- GUIPAL
        // green, palette index 2 on screen, (0, 128, 0) -- not in the
        // caption's colour. The line runs the character's advance, and sits
        // one row below the height of the font's 'I': 0x4A5CB7 measures that
        // glyph and draws at y + height + 1. The glyph's bottom edge is its
        // top plus its height, so here that is textY + bottom + 1.
        if (quickKey)
        {
            if (auto span = findCharacterInText(label, *quickKey, *labelFont))
            {
                auto lineY = textY + 1.0f;
                if (static_cast<std::size_t>('I') < labelFont->sprites.size())
                {
                    lineY = textY + labelFont->sprites['I']->bounds.bottom() + 1.0f;
                }
                graphics.fillColor(textX + span->x, lineY, span->width, 1.0f, Color(0, 128, 0));
            }
        }
    }

    UiStagedButton::UiStagedButton(
        int posX,
        int posY,
        unsigned int sizeX,
        unsigned int sizeY,
        std::vector<StageInfo> stages,
        std::shared_ptr<Sprite> pressedSprite,
        std::shared_ptr<SpriteSeries> labelFont)
        : UiComponent(posX, posY, sizeX, sizeY),
          stages(std::move(stages)),
          pressedSprite(std::move(pressedSprite)),
          labelFont(std::move(labelFont))
    {
        if (this->stages.empty())
        {
            throw std::logic_error("No stages provided for staged button");
        }
    }

    void UiStagedButton::mouseDown(MouseButtonEvent event)
    {
        if (!enabled)
        {
            return;
        }

        switch (behaviorMode)
        {
            case BehaviorMode::Radio:
            case BehaviorMode::Cycle:
                activateButton({(mouseButtonToSource(event.button))});
                break;
            case BehaviorMode::Toggle:
            case BehaviorMode::Button:
            case BehaviorMode::Staged:
                armed = true;
                pressed = true;
                break;
            default:
                throw std::logic_error("Invalid BehaviorMode");
        }
    }

    void UiStagedButton::mouseUp(MouseButtonEvent event)
    {
        switch (behaviorMode)
        {
            case BehaviorMode::Radio:
            case BehaviorMode::Cycle:
                break;
            case BehaviorMode::Button:
            case BehaviorMode::Toggle:
            case BehaviorMode::Staged:
                armed = false;
                if (pressed)
                {
                    pressed = false;
                    activateButton({(mouseButtonToSource(event.button))});
                }
                break;
            default:
                throw std::logic_error("Invalid BehaviorMode");
        }
    }

    void UiStagedButton::mouseEnter()
    {
        hoverSubject.next(true);

        switch (behaviorMode)
        {
            case BehaviorMode::Button:
            case BehaviorMode::Toggle:
            case BehaviorMode::Staged:
                if (armed)
                {
                    pressed = true;
                }
                break;
            default:
                break;
        }
    }

    void UiStagedButton::mouseLeave()
    {
        hoverSubject.next(false);

        switch (behaviorMode)
        {
            case BehaviorMode::Button:
            case BehaviorMode::Toggle:
            case BehaviorMode::Staged:
                pressed = false;
                break;
            default:
                break;
        }
    }

    void UiStagedButton::unfocus()
    {
        switch (behaviorMode)
        {
            case BehaviorMode::Button:
            case BehaviorMode::Toggle:
            case BehaviorMode::Staged:
                armed = false;
                pressed = false;
                break;
            default:
                break;
        }
    }

    Observable<ButtonClickEvent>& UiStagedButton::onClick()
    {
        return clickSubject;
    }

    Observable<bool>& UiStagedButton::onHover()
    {
        return hoverSubject;
    }

    void UiStagedButton::keyDown(KeyEvent event)
    {
        if (!enabled)
        {
            return;
        }

        if (event.keyCode == SDLK_SPACE || (quickKey && event.keyCode == quickKey))
        {
            activateButton({ButtonClickEvent::Source::Keyboard});
        }
    }

    void UiStagedButton::activateButton(const ButtonClickEvent& event)
    {
        if (!enabled)
        {
            return;
        }

        switch (behaviorMode)
        {
            case BehaviorMode::Radio:
                toggledOn = true;
                break;
            case BehaviorMode::Cycle:
                nextStage();
                break;
            case BehaviorMode::Toggle:
                toggledOn = !toggledOn;
                break;
            case BehaviorMode::Button:
            case BehaviorMode::Staged:
                break;
            default:
                throw std::logic_error("Invalid BehaviorMode");
        }

        clickSubject.next(event);
        messagesSubject.next(ActivateMessage{sourceToType(event.source)});
    }

    void UiStagedButton::setStage(unsigned int newStage)
    {
        if (newStage >= stages.size())
        {
            throw std::logic_error("New stage is not in range");
        }
        currentStage = newStage;
    }

    void UiStagedButton::setTextAlign(UiStagedButton::TextAlign align)
    {
        textAlign = align;
    }

    void UiStagedButton::setLabel(const std::string& label)
    {
        stages[currentStage].label = label;
    }

    void UiStagedButton::setNormalSprite(const std::shared_ptr<Sprite>& sprite)
    {
        stages[currentStage].sprite = sprite;
    }

    void UiStagedButton::setPressedSprite(const std::shared_ptr<Sprite>& sprite)
    {
        pressedSprite = sprite;
    }

    void UiStagedButton::setToggledOn(bool _toggledOn)
    {
        toggledOn = _toggledOn;
    }

    void UiStagedButton::setBehaviorMode(BehaviorMode mode)
    {
        behaviorMode = mode;
    }

    void UiStagedButton::setQuickKey(int quickKey)
    {
        this->quickKey = quickKey;
    }

    void UiStagedButton::setCaptionShadow(bool shadow)
    {
        captionShadow = shadow;
    }

    void UiStagedButton::nextStage()
    {
        auto stageCount = stages.size();
        currentStage = (currentStage + 1) % stageCount;
    }

    void UiStagedButton::setDisabledSprite(const std::shared_ptr<Sprite>& sprite)
    {
        disabledSprite = sprite;
    }

    void UiStagedButton::setEnabled(bool newEnabled)
    {
        enabled = newEnabled;
    }

    bool UiStagedButton::isEnabled() const
    {
        return enabled;
    }
}
