#pragma once

#include <optional>
#include <rwe/CursorService.h>
#include <rwe/sim/FeatureId.h>
#include <rwe/sim/GameSimulation.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitOrder.h>
#include <variant>

namespace rwe
{
    /**
     * What a click with no order button armed does, and what the cursor says
     * about it before the click happens.
     *
     * The original works both out in one place. `0x43F0E0` builds the mission
     * for one selected unit from the command id, that unit and whatever is
     * under the cursor, and `0x43E490` runs the same ladder arm for arm to
     * pick the cursor; the click dispatcher `0x498F70` then treats the
     * *displayed cursor* as the decision -- 15 selects, 17 and above do
     * nothing, anything below 17 issues the order. Two ladders written out
     * twice is exactly how RWE's cursor and its click handler came to
     * disagree, so there is one ladder here and both call it.
     *
     * See TOTALA-EXE.md S:103.
     */

    /** The click takes the unit under the cursor as the new selection. */
    struct DefaultActionSelect
    {
    };

    /** The click sends the unit to the point under the cursor. */
    struct DefaultActionMove
    {
    };

    /** The click issues this order to the unit the action was computed for. */
    struct DefaultActionOrder
    {
        UnitOrder order;
        explicit DefaultActionOrder(const UnitOrder& order) : order(order) {}
    };

    /** The click does nothing at all. */
    struct DefaultActionNothing
    {
    };

    using DefaultActionKind = std::variant<DefaultActionNothing, DefaultActionSelect, DefaultActionMove, DefaultActionOrder>;

    struct DefaultAction
    {
        DefaultActionKind action;
        CursorType cursor;
    };

    /**
     * Which ladder to run.
     *
     * The original picks between the first two on the `Interface Type`
     * registry value, which is the LEFTCLICK gadget on the SPEEDS options
     * page: 0 is "Left Click" and is the shipped default. The third is the
     * MOVE button's own command (command 2, `0x43F845`), which is the
     * right-click ladder without the arms that need something other than a
     * unit under the cursor -- a MOVE aimed at bare ground is always a move.
     */
    enum class DefaultActionScheme
    {
        /** Interface Type 0's default action: `0x43FE35` and `0x43E512`. */
        LeftClickDefault,

        /** Interface Type 1's default action: `0x43FA00` and `0x43EB02`. */
        RightClickDefault,

        /** The MOVE button armed: `0x43F845` and `0x43E8BB`. */
        MoveButton,
    };

    /**
     * One selected unit's answer to one hover.
     *
     * `orderer` is always a unit of the local player, because the issue loop
     * at `0x48CF30` walks that player's array; a selected unit that is also
     * the thing under the cursor orders nothing (`0x48D07B`), but still
     * contributes a cursor.
     */
    DefaultAction computeDefaultAction(
        const GameSimulation& sim,
        DefaultActionScheme scheme,
        UnitId orderer,
        std::optional<UnitId> hoveredUnit,
        std::optional<FeatureId> hoveredFeature);

    /**
     * Which of two cursors a mixed selection shows.
     *
     * The chooser's caller starts at `cursornormal` and keeps the *lowest*
     * id any selected unit returned (`0x48D3E9`), which is the cursor's
     * counterpart to S:19's "any, not all" rule for the order buttons: the
     * most specific thing anything in the selection could do is what the
     * player is shown.
     */
    CursorType preferredCursor(CursorType a, CursorType b);

    /**
     * The original's own numbering of the cursors, from the load run at
     * `0x429C9A`. Only the ordering matters to us -- it is what
     * `preferredCursor` folds on -- but the ids are the original's so that
     * the table in S:103 can be read straight across.
     */
    int originalCursorId(CursorType cursor);
}
