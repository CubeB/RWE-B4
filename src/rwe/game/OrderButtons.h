#pragma once

#include <optional>
#include <rwe/sim/UnitDefinition.h>
#include <string>
#include <vector>

namespace rwe
{
    /**
     * The buttons on the general orders panel that the original gates on a
     * capability flag. ARMGEN.GUI and CORGEN.GUI carry every button there is;
     * the game takes the ones the selection cannot use away rather than
     * building a panel per unit type.
     *
     * BUILD and ORDERS are not here. They swap between the two halves of a
     * single unit's panel, are offered only when exactly one unit is selected,
     * and are gated on that unit having a build page at all -- a different
     * question from what it can be ordered to do.
     */
    enum class OrderButton
    {
        Attack,
        Move,
        Defend,
        Patrol,
        Stop,
        Reclaim,
        Repair,
        Capture,
        Load,
        Unload,
        Blast,
        Cloak,
        OnOff,
        FireOrders,
        MoveOrders,
    };

    /**
     * One selected unit, as the panel needs to see it: its definition, and
     * whether it is actually carrying a command-fire weapon. The second is not
     * a definition flag, because the D-gun button needs a weapon to fire as
     * well as the flag that offers it.
     */
    struct OrderButtonUnit
    {
        const UnitDefinition* definition;
        bool hasCommandFireWeapon;
    };

    /**
     * The button a GUI element's name refers to, with the side prefix already
     * stripped. Nothing for a name this does not gate, which is left alone.
     */
    std::optional<OrderButton> orderButtonFromName(const std::string& name);

    /**
     * Whether one unit offers a button. The original reads this straight off
     * the capability flags in `def+0x245`, one bit per button, in the
     * selection walk at 0x41B3F6-0x41B524.
     */
    bool unitOffersOrderButton(const OrderButtonUnit& unit, OrderButton button);

    /**
     * Whether a selection offers a button. The original's accumulator loop
     * ORs the capability bits together -- 0x41B4A1 and its nine neighbours all
     * store a plain 1 into their slot and nothing ever clears one -- so a
     * button appears when *any* selected unit can use it, not when all of them
     * can. The four stateful toggles (fire orders, move orders, on/off, cloak)
     * are gathered by a different accumulator that skips a unit which does not
     * name the flag, but the effect on whether the button is offered at all is
     * the same: it appears if anything in the selection named it.
     */
    bool selectionOffersOrderButton(const std::vector<OrderButtonUnit>& selection, OrderButton button);
}
