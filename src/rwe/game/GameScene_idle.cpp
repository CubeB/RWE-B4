// The idle-builder sign on the side panel, and the jump to the next idle
// construction unit that clicking it makes. Kept out of GameScene_commands.cpp
// for the section budget in CLAUDE.md: that object sat at the ceiling the CI
// check allows, and this is RWE's own HUD code with no reason to share its
// translation unit.
#include "GameScene.h"
#include <memory>
#include <rwe/game/idle_builders.h>
#include <rwe/sim/SimScalar.h>
#include <rwe/ui/UiIdleBuilderSign.h>
#include <rwe/util/SimpleLogger.h>

namespace rwe
{
    void GameScene::attachIdleBuilderSign(UiPanel& panel)
    {
        // RWE's own gadget and not one out of a side's gui file, so there is no
        // gui entry to lay it out from and no art to draw it with: a plate and
        // a line of the panel's own font. What it is for is that nothing on
        // screen said idle construction units existed -- a player found out by
        // pressing Ctrl+B and hoping.
        //
        // A fixed row in the panel's own coordinates, which is how every other
        // control on it is placed. Row 140 of the 352 the side panel is tall,
        // and it is the one band that is empty on both of the panels the scene
        // puts up: <side>MAIN2 carries three labels in its first seventy rows,
        // and <side>GEN's order strip ends at row 135 with the button block
        // starting at 172. Measured with ui_probe rather than guessed.
        constexpr int signHeight = 14;
        constexpr int signRow = 140;
        constexpr int margin = 2;

        const auto width = static_cast<int>(panel.getWidth()) - (margin * 2);
        const auto height = static_cast<int>(panel.getHeight());
        if (width <= 0 || height <= signRow + signHeight + margin)
        {
            LOG_WARN << "Idle-builder sign not added to panel " << panel.getName() << ": it is " << width << "x" << height
                     << " and the sign needs " << (margin * 2 + 1) << "x" << (signRow + signHeight + margin);
            return;
        }

        // And where that row is not empty, there is no sign. A build page is a
        // grid of buttons out of a different gui file -- ARMCOM1's own grid
        // runs from row 27 to 237 with nothing spare -- and RWE has no claim on
        // any part of it, so the sign is left off rather than laid over a
        // shipped button. Said out loud, because a HUD element that is silently
        // absent is worse than one that was never meant to be there.
        for (const auto& child : panel.getChildren())
        {
            const auto childX = child->getX();
            const auto childY = child->getY();
            const auto overlapsX = childX < margin + width && margin < childX + static_cast<int>(child->getWidth());
            const auto overlapsY = childY < signRow + signHeight && signRow < childY + static_cast<int>(child->getHeight());
            if (overlapsX && overlapsY)
            {
                LOG_WARN << "Idle-builder sign not added to panel " << panel.getName()
                         << ": row " << signRow << " is taken by the gadget '" << child->getName() << "'";
                return;
            }
        }

        auto sign = std::make_unique<UiIdleBuilderSign>(margin, signRow, static_cast<unsigned int>(width), static_cast<unsigned int>(signHeight), guiFont);
        // appendChild does not name a control; the gui reader is what does that
        // for everything that comes out of a gui file. Without this the sign
        // has an empty name, and the scene's own once-a-frame lookup and any
        // scenario that clicks it would both be looking for something that is
        // not there.
        sign->setName(IdleBuilderSignName);
        panel.appendChild(std::move(sign));
    }

    void GameScene::updateIdleBuilderSign()
    {
        // Looked up by name rather than held as a pointer: the panel is
        // destroyed and rebuilt every time the selection changes, so a cached
        // pointer would be a dangling one for a frame somewhere. The scan is a
        // handful of children with a dynamic_cast on each, once a frame.
        auto sign = currentPanel->find<UiIdleBuilderSign>(IdleBuilderSignName);
        if (!sign)
        {
            return;
        }

        // A read, once a frame, from state the simulation already hashes. The
        // sign is handed the number and has no way to reach the simulation
        // through any other door, so a frame that draws the HUD cannot change
        // what any unit does.
        auto count = static_cast<int>(playerIdleBuilders(simulation, localPlayerId).size());
        if (!reportedIdleBuilderSignCount)
        {
            // Once a game, and worth having: a player reporting that the sign
            // is not working needs to be told whether the widget was found at
            // all, which is the difference between a layout problem and a
            // counting one.
            reportedIdleBuilderSignCount = true;
            LOG_INFO << "Idle-builder sign found on panel " << currentPanel->getName() << ", showing " << count;
        }
        sign->get().setIdleBuilderCount(count);
    }

    void GameScene::selectNextIdleBuilder()
    {
        const auto builders = playerIdleBuilders(simulation, localPlayerId);
        if (builders.empty())
        {
            return;
        }

        const auto index = nextIdleBuilderIndex(builders, lastIdleBuilderShown);
        lastIdleBuilderShown = builders.at(index);

        // Both of these are the scene's own. Selecting a unit sends no command
        // -- it is not a thing any other peer is told about -- and the camera
        // is nobody else's either, so a click on the sign is as invisible to
        // the simulation as reading the count was.
        replaceUnitSelection(builders.at(index));

        const auto& unit = simulation.getUnitState(builders.at(index));
        auto centre = worldCameraState.position;
        centre.x = simScalarToFloat(unit.position.x);
        centre.z = simScalarToFloat(unit.position.z);
        setCameraPosition(centre);
    }
}
