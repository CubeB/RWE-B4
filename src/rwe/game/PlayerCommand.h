#pragma once

#include <rwe/game/SceneTime.h>
#include <rwe/sim/UnitFireOrders.h>
#include <rwe/sim/UnitMovementOrders.h>
#include <rwe/sim/UnitId.h>
#include <rwe/sim/UnitOrder.h>
#include <variant>

namespace rwe
{
    struct PlayerUnitCommand
    {
        struct IssueOrder
        {
            enum IssueKind
            {
                Immediate,
                Queued
            };
            UnitOrder order;
            IssueKind issueKind;

            IssueOrder(const UnitOrder& order, IssueKind issueKind) : order(order), issueKind(issueKind)
            {
            }
        };

        struct ModifyBuildQueue
        {
            int count;
            std::string unitType;
        };

        /**
         * Add to (or take off) the number of rounds a stockpiled weapon is
         * building. The original queues these as ordinary orders on the unit
         * and shows the outstanding count next to the ready one as "N +M"
         * (0x419A2B).
         */
        struct ModifyStockpile
        {
            int count;
        };

        struct Stop
        {
        };

        struct SetMovementOrders
        {
            UnitMovementOrders orders;
        };

        struct SetFireOrders
        {
            UnitFireOrders orders;
        };

        struct SetOnOff
        {
            bool on;
        };

        /**
         * Ask for or drop the cloak. Whether the unit actually cloaks is still
         * settled a second at a time by the energy and by whether an enemy is
         * standing too close.
         */
        struct SetCloak
        {
            bool cloaked;
        };

        struct SelfDestruct
        {
        };

        /** Remove the queued build order whose footprint covers position. */
        struct CancelBuildOrder
        {
            SimVector position;
        };

        using Command = std::variant<IssueOrder, ModifyBuildQueue, ModifyStockpile, Stop, SetFireOrders, SetMovementOrders, SetOnOff, SetCloak, SelfDestruct, CancelBuildOrder>;

        UnitId unit;
        Command command;

        PlayerUnitCommand(const UnitId& unit, const Command& command) : unit(unit), command(command)
        {
        }
    };

    struct PlayerPauseGameCommand
    {
    };

    struct PlayerUnpauseGameCommand
    {
    };

    struct PlayerSetGameSpeedCommand
    {
        int speedIndex;
    };

    using PlayerCommand = std::variant<PlayerUnitCommand, PlayerPauseGameCommand, PlayerUnpauseGameCommand, PlayerSetGameSpeedCommand>;
}
