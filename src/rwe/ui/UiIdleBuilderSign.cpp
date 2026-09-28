#include "UiIdleBuilderSign.h"

#include <algorithm>
#include <cmath>
#include <rwe/ColorPalette.h>
#include <string>

namespace rwe
{
    namespace
    {
        /**
         * The plate behind the caption. Translucent, so the sign reads over
         * whatever the side panel's own background is doing underneath it
         * rather than depending on it.
         */
        const Color SignPlate(0, 0, 0, 160);

        /**
         * The green the resource bar already draws its production figure in
         * (Color(83, 223, 79) throughout GameScene_render), so a builder
         * standing idle says the same thing as metal coming in: this is
         * happening, and it is not happening by itself.
         */
        const Color SignActive(83, 223, 79);

        /** Nothing to jump to: the sign stays where it is and says so quietly. */
        const Color SignIdle(90, 100, 90);
    }

    std::string UiIdleBuilderSign::caption() const
    {
        // The number is always drawn, zero included. A sign that only appeared
        // when there was something to say would be a thing the player had to
        // already know about to look for, which is the fault this is here to
        // fix.
        return "IDLE BUILDERS " + std::to_string(idleBuilderCount);
    }

    UiIdleBuilderSign::UiIdleBuilderSign(
        int posX,
        int posY,
        unsigned int sizeX,
        unsigned int sizeY,
        const std::shared_ptr<SpriteSeries>& font)
        : UiComponent(posX, posY, sizeX, sizeY),
          font(font)
    {
    }

    void UiIdleBuilderSign::render(UiRenderService& context) const
    {
        const auto active = idleBuilderCount > 0;

        context.fillColor(static_cast<float>(posX), static_cast<float>(posY), static_cast<float>(sizeX), static_cast<float>(sizeY), SignPlate);

        // The edge, not the caption, is what tells the pointer is over
        // something clickable: a caption that changed colour under the cursor
        // would be a second thing to read, and the edge is already saying
        // "this is a plate".
        const auto& edge = active ? SignActive : SignIdle;
        context.drawBoxOutline(
            static_cast<float>(posX),
            static_cast<float>(posY),
            static_cast<float>(sizeX),
            static_cast<float>(sizeY),
            (hovered || armed) ? SignActive : edge,
            1.0f);

        const auto text = caption();
        // Centred the way UiStagedButton centres a caption: measure, then round
        // once, so the text lands on the same pixel column every frame instead
        // of shimmering as the count changes width. The 1px down-and-right is
        // that button's pressed shift, so a press looks pressed here too.
        const auto textX = std::round(
            static_cast<float>(posX) + (sizeX / 2.0f) - (context.getTextWidth(text, *font) / 2.0f)) + (armed ? 1.0f : 0.0f);
        const auto textY = static_cast<float>(posY) + 1.0f + (armed ? 1.0f : 0.0f);
        context.drawText(textX, textY, text, *font, edge);
    }

    void UiIdleBuilderSign::mouseDown(MouseButtonEvent event)
    {
        // Left only. There is no second action behind the sign to put on the
        // right button, and arming on a right click would leave the sign
        // looking held for as long as the player held the button.
        if (idleBuilderCount == 0 || event.button != MouseButtonEvent::MouseButton::Left)
        {
            return;
        }

        // The press is armed here and only committed in mouseUp, which is what
        // makes a drag off the sign and a release elsewhere not fire. The
        // panel routes mouseUp to whichever child has the focus rather than
        // to the one under the pointer, so this flag is the whole of the
        // hit test on the way up.
        armed = true;
    }

    void UiIdleBuilderSign::mouseUp(MouseButtonEvent event)
    {
        if (!armed)
        {
            return;
        }
        armed = false;

        if (idleBuilderCount == 0 || event.button != MouseButtonEvent::MouseButton::Left)
        {
            return;
        }

        // The route every gadget out of a gui file takes: UiPanel::appendChild
        // subscribed to this subject and republishes it as a GroupMessage, and
        // the scene's existing handler on the panel's group messages picks it
        // up by name. The panel is the subscriber and it owns this subject, so
        // it is destroyed after the sign rather than before it.
        messagesSubject.next(ActivateMessage{ActivateMessage::Type::Primary});
    }

    void UiIdleBuilderSign::mouseEnter()
    {
        hovered = true;
    }

    void UiIdleBuilderSign::mouseLeave()
    {
        hovered = false;
    }

    void UiIdleBuilderSign::unfocus()
    {
        // The press died with the focus: the panel clears it when a rebuild
        // destroys the child that had it, and a sign that came back still held
        // would fire on the next release anywhere on the panel.
        armed = false;
    }

    void UiIdleBuilderSign::setIdleBuilderCount(int count)
    {
        idleBuilderCount = std::max(0, count);
        if (idleBuilderCount == 0)
        {
            // The last builder picked up work while the pointer was down on the
            // sign. There is nothing left to jump to, so the press does not
            // become a click.
            armed = false;
        }
    }
}
