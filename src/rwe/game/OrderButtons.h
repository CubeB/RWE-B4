#pragma once

#include <optional>
#include <rwe/sim/UnitDefinition.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitMovementOrders.h>
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
     * Whether a GUI element is the button that orders a round for a stockpiled
     * weapon. The original does not compare the name: it runs strstr for
     * "MAKENUKE" and then for "MAKEANTI" over whatever the gadget happens to be
     * called (0x419B3C and 0x419B4E, with 0x4E49B0 being strstr) and treats a
     * hit on either as the same command. It has to be a substring test, because
     * the six launchers in the shipped data do not agree on a prefix: ARMSILO1
     * names its button ARMMAKENUKE and CORSILO1 CORMAKENUKE, but ARMEMP1 says
     * EMPMAKENUKE and CORTRON1 TRONMAKENUKE.
     */
    bool isStockpileButtonName(const std::string& name);

    /**
     * The readout the original prints on that button (0x419A2B): the rounds in
     * the magazine, then the outstanding order count as " +M". Either being
     * zero leaves it off, so an empty launcher with three on order reads " +3",
     * leading space and all -- the original prints the suffix at the end of a
     * string it has just truncated to nothing, and does not go back for the
     * space.
     */
    std::string stockpileButtonLabel(int stockedRounds, int queuedRounds);

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

    /**
     * The state one of the four stateful toggles (fire orders, move orders,
     * on/off, cloak) shows for a selection. The original gathers it in the
     * selection walk at 0x41B403-0x41B449 into a sentinel accumulator: a unit
     * that does not name the flag is skipped, the first offerer's value is
     * taken, and a later offerer that disagrees collapses the state to
     * "mixed". A click reads this shared state, not each unit's own.
     */
    template <typename T>
    struct GatheredToggle
    {
        /** No unit in the selection offers the button. The original greys it. */
        bool offered{false};

        /** The offerers disagree. `value` is the first offerer's, and means nothing on its own. */
        bool mixed{false};

        T value{};

        bool operator==(const GatheredToggle&) const = default;
    };

    /**
     * Gathers a toggle over a selection. One entry per selected unit: the
     * unit's own state when its definition offers the button, nothing when it
     * does not, so the accumulator skips it.
     */
    template <typename T>
    GatheredToggle<T> gatherToggle(const std::vector<std::optional<T>>& states)
    {
        GatheredToggle<T> result;
        for (const auto& state : states)
        {
            if (!state)
            {
                continue;
            }
            if (!result.offered)
            {
                result.offered = true;
                result.value = *state;
            }
            else if (*state != result.value)
            {
                result.mixed = true;
            }
        }
        return result;
    }

    /**
     * The frame a toggle button draws for a gathered state: one per state
     * value, then one more for a disagreeing selection. The shipped art is
     * built that way. ARMFIREORD and ARMMOVEORD (commongui.gaf) carry HOLD
     * FIRE / RETURN FIRE / FIRE AT WILL and then FIRE ORDERS with all three
     * lights lit, frames 0 to 3, before the pressed and greyed frames;
     * ARMONOFF carries OFF / ON / OFF/ON ORDERS. `stateCount` is how many
     * values the toggle has, which is the frame the mixed face sits on.
     */
    template <typename T>
    unsigned int toggleFace(const GatheredToggle<T>& shown, unsigned int stateCount)
    {
        return shown.mixed ? stateCount : static_cast<unsigned int>(shown.value);
    }

    /**
     * What a click on FIREORD sets every offerer to. The handler at 0x41A5EF
     * jumps through a four-entry table at 0x41A910 on the gathered state:
     * hold fire -> return fire -> fire at will -> hold fire, and a mixed
     * selection goes to hold fire as well. One order goes to the whole
     * selection, so a mixed selection converges rather than each unit
     * stepping on from wherever it was.
     */
    UnitFireOrders fireOrdersAfterClick(const GatheredToggle<UnitFireOrders>& shown);

    /**
     * What a click on MOVEORD sets every offerer to: 0x41A4F0 and the table
     * at 0x41A900, the same shape as FIREORD. Hold position -> maneuver ->
     * roam -> hold position, and mixed -> hold position.
     */
    UnitMovementOrders moveOrdersAfterClick(const GatheredToggle<UnitMovementOrders>& shown);

    /**
     * What a click on ONOFF sets every offerer to. 0x41A7DB: all off ->
     * ACTIVATE, all on -> DEACTIVATE, and a mixed selection is switched ON.
     */
    bool onOffAfterClick(const GatheredToggle<bool>& shown);

    /**
     * What a click on CLOAK sets every offerer to. 0x41A743 tests the two
     * state bits together: only a selection with every cloak off gets
     * CLOAK_ON; all on, or mixed, gets CLOAK_OFF. Note the asymmetry with
     * ONOFF, which is the original's and is kept.
     */
    bool cloakAfterClick(const GatheredToggle<bool>& shown);
}
