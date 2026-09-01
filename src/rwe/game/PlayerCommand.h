#pragma once

#include <rwe/game/SceneTime.h>
#include <rwe/sim/UnitFireOrders.h>
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

        struct Stop
        {
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

        using Command = std::variant<IssueOrder, ModifyBuildQueue, Stop, SetFireOrders, SetOnOff, SetCloak, SelfDestruct, CancelBuildOrder>;

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
