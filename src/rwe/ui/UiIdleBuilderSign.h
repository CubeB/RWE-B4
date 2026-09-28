#pragma once

#include <memory>
#include <rwe/ui/UiComponent.h>

namespace rwe
{
    /**
     * The side panel's idle-builder sign: how many of the player's own
     * construction units have nothing to do, and a click that goes to the next
     * one.
     *
     * This exists because nothing on screen said idle builders were there. A
     * player found out by pressing Ctrl+B and hoping, and the count is the
     * thing they were having to hope for. RWE's own gadget rather than
     * anything the original has: TA's side panel says nothing about idle
     * construction units, and the sign is a plate and a line of the panel's
     * own font laid out against the panel's box, because there is no gui entry
     * and no art for it.
     *
     * The count is *pushed in* by the scene once a frame, from a read of the
     * simulation's own state (see game/idle_builders.h). It is deliberately not
     * a callback or a reference to the simulation: this class cannot reach the
     * simulation at all, so it cannot write to it however it is wired up, and
     * it holds nothing that is not either geometry or the number it was last
     * handed.
     *
     * A click leaves as an ActivateMessage on the inherited messagesSubject,
     * which is what UiPanel::appendChild already forwards into the panel's own
     * group messages -- the same route every gadget out of a gui file takes, so
     * the scene needs no subscription of its own and cannot be left holding one
     * to a widget that has been destroyed by a panel rebuild.
     *
     * No destructor is declared, and none is needed. The sign subscribes to
     * nothing and owns no Subject of its own beyond the inherited
     * messagesSubject, which is a UiComponent member and so is still alive
     * while ~UiComponent runs. Should a subject ever be added here, the
     * destructor has to call releaseSubscriptions() itself -- a base destructor
     * runs after the derived class's members are gone. See
     * UiComponent::releaseSubscriptions and UiComponent.test.cpp.
     */
    class UiIdleBuilderSign : public UiComponent
    {
    private:
        std::shared_ptr<SpriteSeries> font;

        /** How many construction units have nothing to do, as handed in this frame. */
        int idleBuilderCount{0};

        /** The pointer went down on the sign and has not come up yet. */
        bool armed{false};

        /** The pointer is over the sign. */
        bool hovered{false};

        std::string caption() const;

    public:
        UiIdleBuilderSign(int posX, int posY, unsigned int sizeX, unsigned int sizeY, const std::shared_ptr<SpriteSeries>& font);

        void render(UiRenderService& context) const override;

        void mouseDown(MouseButtonEvent event) override;

        void mouseUp(MouseButtonEvent event) override;

        void mouseEnter() override;

        void mouseLeave() override;

        void unfocus() override;

        /**
         * The count for this frame. Zero is not an error, it is the ordinary
         * answer: it greys the sign and stops it answering, because a click
         * with nothing to jump to has nothing to do.
         */
        void setIdleBuilderCount(int count);

        int getIdleBuilderCount() const { return idleBuilderCount; }
    };
}
