#include "OrderButtons.h"
#include <algorithm>

namespace rwe
{
    std::optional<OrderButton> orderButtonFromName(const std::string& name)
    {
        if (name == "ATTACK")
        {
            return OrderButton::Attack;
        }
        if (name == "MOVE")
        {
            return OrderButton::Move;
        }
        if (name == "DEFEND")
        {
            return OrderButton::Defend;
        }
        if (name == "PATROL")
        {
            return OrderButton::Patrol;
        }
        if (name == "STOP")
        {
            return OrderButton::Stop;
        }
        if (name == "RECLAIM")
        {
            return OrderButton::Reclaim;
        }
        if (name == "REPAIR")
        {
            return OrderButton::Repair;
        }
        if (name == "CAPTURE")
        {
            return OrderButton::Capture;
        }
        if (name == "LOAD")
        {
            return OrderButton::Load;
        }
        if (name == "UNLOAD")
        {
            return OrderButton::Unload;
        }
        if (name == "BLAST")
        {
            return OrderButton::Blast;
        }
        if (name == "CLOAK")
        {
            return OrderButton::Cloak;
        }
        if (name == "ONOFF")
        {
            return OrderButton::OnOff;
        }
        if (name == "FIREORD")
        {
            return OrderButton::FireOrders;
        }
        if (name == "MOVEORD")
        {
            return OrderButton::MoveOrders;
        }
        return std::nullopt;
    }

    bool unitOffersOrderButton(const OrderButtonUnit& unit, OrderButton button)
    {
        const auto& definition = *unit.definition;
        switch (button)
        {
            case OrderButton::Attack:
                return definition.canAttack;
            case OrderButton::Move:
                return definition.canMove;
            case OrderButton::Defend:
                return definition.canGuard;
            case OrderButton::Patrol:
                return definition.canPatrol;
            case OrderButton::Stop:
                return definition.canStop;
            case OrderButton::Reclaim:
                return definition.canReclamate;
            case OrderButton::Repair:
                // There is no CanRepair key. The parser copies canreclamate
                // into a second bit of its own (0x42CA3D) and the repair
                // predicate 0x4899CC reads that one, so the two buttons come
                // and go together: everything that can reclaim can repair.
                return definition.canReclamate;
            case OrderButton::Capture:
                return definition.canCapture;
            case OrderButton::Load:
            case OrderButton::Unload:
                return definition.canLoad;
            case OrderButton::Blast:
                // Both halves are needed. candgun is what offers the button,
                // but the D-gun is an ordinary command-fire weapon and a
                // commander with none has nothing to fire.
                return definition.canDgun && unit.hasCommandFireWeapon;
            case OrderButton::Cloak:
                return definition.cloakable;
            case OrderButton::OnOff:
                return definition.onOffable;
            case OrderButton::FireOrders:
                return definition.fireStandOrders;
            case OrderButton::MoveOrders:
                return definition.mobileStandOrders;
        }

        return true;
    }

    bool selectionOffersOrderButton(const std::vector<OrderButtonUnit>& selection, OrderButton button)
    {
        auto anyOffers = [&](OrderButton b) {
            return std::any_of(selection.begin(), selection.end(), [&](const auto& unit) { return unitOffersOrderButton(unit, b); });
        };

        // LOAD and BLAST share one slot on the panel -- in ARMGEN.GUI they are
        // both at 64,317 and the same size -- and LOAD wins it. 0x41A471 takes
        // BLAST away outright as soon as anything in the selection can load,
        // so picking up a transport alongside the commander costs you the
        // D-gun button until you drop it again.
        if (button == OrderButton::Blast && anyOffers(OrderButton::Load))
        {
            return false;
        }

        return anyOffers(button);
    }
}
